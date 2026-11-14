#define COBJMACROS
#include "img.h"
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/json.h"
#include "../base/sbuf.h"
#include "../net/http.h"
#include "../base/crypto.h"

#define NSLOT 96   // 그림 칸 (192px RGBA ≈ 147KB 씩 → 최대 약 14MB)
#define RETRY_MS 30000          // 못 받은 그림은 30초 뒤 다시 (지금 앱 THUMB_RETRY_MS 와 같음)
#define MAX_BYTES (8 * 1024 * 1024)
#define TIMEOUT_MS 10000
#define NTHREAD 4               // 받는 스레드 (11/14: 하나면 한 장이 막힐 때 나머지가 다 기다림)
//  디스크 캐시 (https 그림만 — 에셋 키는 App ID 가 들어가서 안 남긴다). 파일 이름 = 주소의 SHA-1
#define CACHE_MAX_FILES 400
#define CACHE_MAX_BYTES (80ull * 1024 * 1024)
#define CACHE_MAX_AGE_S (30ull * 24 * 3600)

enum { ST_QUEUED = 1, ST_LOADING, ST_OK, ST_FAIL };
typedef struct { char *key; int st; uint32_t *px; DWORD at, used; } slot;

static slot S[NSLOT];
static CRITICAL_SECTION lock;
static HANDLE wake, threads[NTHREAD];   //  wake = 세마포어 (요청 하나마다 하나 올림)
static CRITICAL_SECTION as_lock;        //  에셋 목록 (여러 스레드)
static wchar_t CACHE_DIR[MAX_PATH];
static volatile LONG pruned;
static volatile LONG quit;
static HWND notify_w;
static UINT notify_m;

//  앱 에셋 목록 (App ID 별 — as_lock 안에서만)
typedef struct { char *app; jval *list; DWORD at; } assets_t;
static assets_t AS[8];

static char *dupn(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

// ── 풀기: 가운데를 정사각형으로 잘라 IMG_SIZE 로 줄이고 32bpp PBGRA 로 ──
static uint32_t *decode(IWICImagingFactory *f, const unsigned char *data, size_t len) {
    uint32_t *out = NULL;
    IWICStream *st = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *fr = NULL;
    IWICBitmapClipper *cl = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    UINT w = 0, h = 0;
    if (len > 0xFFFFFFFFu) return NULL;
    if (FAILED(IWICImagingFactory_CreateStream(f, &st))) goto done;
    if (FAILED(IWICStream_InitializeFromMemory(st, (BYTE *)data, (DWORD)len))) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)st, NULL, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &fr))) goto done;
    if (FAILED(IWICBitmapFrameDecode_GetSize(fr, &w, &h)) || !w || !h || w > 16384 || h > 16384) goto done;
    UINT side = w < h ? w : h;
    WICRect r = { (INT)((w - side) / 2), (INT)((h - side) / 2), (INT)side, (INT)side };
    if (FAILED(IWICImagingFactory_CreateBitmapClipper(f, &cl))) goto done;
    if (FAILED(IWICBitmapClipper_Initialize(cl, (IWICBitmapSource *)fr, &r))) goto done;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc))) goto done;
    if (FAILED(IWICBitmapScaler_Initialize(sc, (IWICBitmapSource *)cl, IMG_SIZE, IMG_SIZE, WICBitmapInterpolationModeFant))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)sc, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    out = (uint32_t *)malloc((size_t)IMG_SIZE * IMG_SIZE * 4);
    if (out && FAILED(IWICFormatConverter_CopyPixels(cv, NULL, IMG_SIZE * 4, IMG_SIZE * IMG_SIZE * 4, (BYTE *)out))) { free(out); out = NULL; }
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (cl) IWICBitmapClipper_Release(cl);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (st) IWICStream_Release(st);
    return out;
}

//  마지막 받기 실패 이유 (진단 로그용 — 주소·App ID 는 안 담는다)
static _Thread_local int last_status;
static _Thread_local char last_err[96];
static int fetch(const char *url, sbuf *body) {
    int status = 0;
    char err[96] = "";
    if (strncmp(url, "https://", 8)) return 0;   // https 만
    int ok = http_get(url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) TOKU-RPC\r\n", TIMEOUT_MS, MAX_BYTES, &status, body, err, sizeof err);
    last_status = status;
    snprintf(last_err, sizeof last_err, "%s", ok ? "" : err);
    return ok && status >= 200 && status < 300 && body->len > 0;
}
// ── 디스크 캐시 ──
void img_set_cache_dir(const wchar_t *dir) {
    if (!dir || !*dir) { CACHE_DIR[0] = 0; return; }
    _snwprintf(CACHE_DIR, MAX_PATH - 1, L"%ls", dir);
    CACHE_DIR[MAX_PATH - 1] = 0;
    CreateDirectoryW(CACHE_DIR, NULL);
}
static int cache_path(const char *url, wchar_t *out, const wchar_t *ext) {
    if (!CACHE_DIR[0] || strncmp(url, "https://", 8)) return 0;
    uint8_t h[20];
    sha1(url, strlen(url), h);
    wchar_t hex[41];
    for (int i = 0; i < 20; i++) _snwprintf(hex + i * 2, 3, L"%02x", h[i]);
    hex[40] = 0;
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls%ls", CACHE_DIR, hex, ext);
    out[MAX_PATH - 1] = 0;
    return 1;
}
static ULONGLONG ft_s(FILETIME ft) { return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull; }
static ULONGLONG now_s(void) { FILETIME ft; GetSystemTimeAsFileTime(&ft); return ft_s(ft); }
static int cache_read(const char *url, sbuf *body) {
    wchar_t f[MAX_PATH];
    if (!cache_path(url, f, L".img")) return 0;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(f, GetFileExInfoStandard, &a)) return 0;
    if (now_s() - ft_s(a.ftLastWriteTime) > CACHE_MAX_AGE_S || a.nFileSizeHigh || !a.nFileSizeLow || a.nFileSizeLow > MAX_BYTES) { DeleteFileW(f); return 0; }
    HANDLE h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 0;
    if (sb_reserve(body, a.nFileSizeLow)) {
        DWORD got = 0;
        if (ReadFile(h, body->p, a.nFileSizeLow, &got, NULL) && got == a.nFileSizeLow) { body->len = got; ok = 1; }
    }
    CloseHandle(h);
    return ok;
}
static void cache_drop(const char *url) { wchar_t f[MAX_PATH]; if (cache_path(url, f, L".img")) DeleteFileW(f); }
static void cache_write(const char *url, const sbuf *body) {
    wchar_t f[MAX_PATH], t[MAX_PATH];
    if (!cache_path(url, f, L".img") || !body->len || body->len > MAX_BYTES) return;
    wchar_t ext[24];
    _snwprintf(ext, 23, L".tmp%lu", (unsigned long)GetCurrentThreadId());
    ext[23] = 0;
    if (!cache_path(url, t, ext)) return;
    HANDLE h = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    int ok = WriteFile(h, body->p, (DWORD)body->len, &put, NULL) && put == body->len;
    CloseHandle(h);
    if (!ok || !MoveFileExW(t, f, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(t);
}
//  오래된 것부터 지워 한도 안으로 (켤 때 한 번, 받는 스레드에서)
typedef struct { ULONGLONG at, size; wchar_t name[64]; } centry;
static int cmp_centry(const void *a, const void *b) { ULONGLONG x = ((const centry *)a)->at, y = ((const centry *)b)->at; return x < y ? -1 : x > y; }
static void cache_prune(void) {
    if (!CACHE_DIR[0]) return;
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH - 1, L"%ls\\*", CACHE_DIR);
    pat[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE fh = FindFirstFileW(pat, &fd);
    if (fh == INVALID_HANDLE_VALUE) return;
    centry *l = NULL;
    size_t n = 0, cap = 0;
    ULONGLONG total = 0, now = now_s();
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t nl = wcslen(fd.cFileName);
        int img = nl == 44 && !wcscmp(fd.cFileName + 40, L".img");
        int tmp = nl > 44 && nl < 64 && !wcsncmp(fd.cFileName + 40, L".tmp", 4);
        if (!img && !tmp) continue;   //  이 캐시가 만든 이름만 (40자 SHA-1 + .img / .tmp…)
        ULONGLONG at = ft_s(fd.ftLastWriteTime), sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (tmp || now - at > CACHE_MAX_AGE_S) {
            wchar_t f[MAX_PATH];
            _snwprintf(f, MAX_PATH - 1, L"%ls\\%ls", CACHE_DIR, fd.cFileName);
            f[MAX_PATH - 1] = 0;
            DeleteFileW(f);
            continue;
        }
        if (n >= cap) { size_t nc = cap ? cap * 2 : 256; centry *nl2 = (centry *)realloc(l, nc * sizeof *nl2); if (!nl2) break; l = nl2; cap = nc; }
        l[n].at = at;
        l[n].size = sz;
        wcscpy(l[n].name, fd.cFileName);
        n++;
        total += sz;
    } while (FindNextFileW(fh, &fd));
    FindClose(fh);
    if (l && (n > CACHE_MAX_FILES || total > CACHE_MAX_BYTES)) {
        qsort(l, n, sizeof *l, cmp_centry);
        for (size_t i = 0; i < n && (n - i > CACHE_MAX_FILES || total > CACHE_MAX_BYTES); i++) {
            wchar_t f[MAX_PATH];
            _snwprintf(f, MAX_PATH - 1, L"%ls\\%ls", CACHE_DIR, l[i].name);
            f[MAX_PATH - 1] = 0;
            if (DeleteFileW(f)) total -= l[i].size;
        }
    }
    free(l);
}

//  실패를 창에 알린다: "<어디> <왜>" — https 는 호스트 이름만, 에셋은 'asset' (키에 App ID 가 있어 키는 안 보냄)
static void report_fail(const char *key, const char *why) {
    if (!notify_w) return;
    char host[80] = "asset";
    if (!strncmp(key, "https://", 8)) {
        const char *h = key + 8;
        size_t n = strcspn(h, "/?#:");
        snprintf(host, sizeof host, "%.*s", (int)(n < 70 ? n : 70), h);
    } else if (!strncmp(key, "data:", 5)) snprintf(host, sizeof host, "%s", "data");
    char *m = (char *)malloc(240);
    if (!m) return;
    snprintf(m, 240, "%s %s", host, why);
    if (!PostMessageW(notify_w, notify_m, 1, (LPARAM)m)) free(m);
}

//  "asset:<앱>:<이름>" → cdn 주소 (목록은 앱마다 한 번 받는다, 실패면 30초 뒤 다시)
static int asset_url(const char *key, char *out, size_t n) {
    const char *a = key + 6, *c = strchr(a, ':');
    if (!c || c == a || c - a > 30) return 0;
    for (const char *p = a; p < c; p++) if (*p < '0' || *p > '9') return 0;
    char app[32];
    memcpy(app, a, (size_t)(c - a));
    app[c - a] = 0;
    const char *name = c + 1;
    assets_t *e = NULL;
    for (int i = 0; i < 8; i++) if (AS[i].app && !strcmp(AS[i].app, app)) e = &AS[i];
    if (e && !e->list && GetTickCount() - e->at < RETRY_MS) return 0;
    if (!e || !e->list) {
        if (!e) {
            for (int i = 0; i < 8 && !e; i++) if (!AS[i].app) e = &AS[i];
            if (!e) { e = &AS[0]; free(e->app); json_free(e->list); e->list = NULL; }
            e->app = dupn(app, strlen(app));
        }
        char u[160];
        snprintf(u, sizeof u, "https://discord.com/api/v10/oauth2/applications/%s/assets", app);
        sbuf b; sb_init(&b, 0);
        e->at = GetTickCount();
        if (fetch(u, &b)) {
            jval *j = json_parse(b.p, b.len, NULL);
            if (j && j->t == J_ARR) e->list = j; else json_free(j);
        }
        sb_free(&b);
        if (!e->list) return 0;
    }
    for (size_t i = 0; i < e->list->count; i++) {
        const jval *it = e->list->items[i];
        const char *nm = json_str(json_get(it, "name"), NULL), *id = json_str(json_get(it, "id"), NULL);
        if (nm && id && !strcmp(nm, name)) {
            for (const char *p = id; *p; p++) if (*p < '0' || *p > '9') return 0;
            snprintf(out, n, "https://cdn.discordapp.com/app-assets/%s/%s.png?size=256", app, id);
            return 1;
        }
    }
    return 0;
}

static DWORD WINAPI worker(LPVOID arg) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!InterlockedExchange(&pruned, 1)) cache_prune();
    IWICImagingFactory *f = NULL;
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f);
    while (!quit) {
        WaitForSingleObject(wake, 1000);   //  세마포어 — 요청마다 한 스레드씩 깨어난다
        for (;;) {
            if (quit) break;
            char *key = NULL;
            EnterCriticalSection(&lock);
            for (int i = 0; i < NSLOT && !key; i++)
                if (S[i].key && S[i].st == ST_QUEUED) { S[i].st = ST_LOADING; key = dupn(S[i].key, strlen(S[i].key)); }
            LeaveCriticalSection(&lock);
            if (!key) break;
            uint32_t *px = NULL;
            char url[2100];
            int have = 0;
            if (!strncmp(key, "data:image/", 11)) {
                //  폴더 아이콘 등 — 받지 않고 바로 푼다 (data:image/…;base64,…)
                const char *c = strstr(key, ";base64,");
                if (c && f) {
                    c += 8;
                    size_t bl = strlen(c), o = 0;
                    unsigned char *bin = (unsigned char *)malloc(bl * 3 / 4 + 3);
                    unsigned v = 0;
                    int bits = 0;
                    for (size_t i = 0; bin && i < bl && c[i] != '='; i++) {
                        char ch = c[i];
                        int d = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
                        if (d < 0) continue;
                        v = (v << 6) | (unsigned)d;
                        bits += 6;
                        if (bits >= 8) { bits -= 8; bin[o++] = (unsigned char)(v >> bits); }
                    }
                    if (bin) px = decode(f, bin, o);
                    free(bin);
                }
            }
            else if (!strncmp(key, "asset:", 6)) { EnterCriticalSection(&as_lock); have = asset_url(key, url, sizeof url); LeaveCriticalSection(&as_lock); }
            else if (strlen(key) < sizeof url) { strcpy(url, key); have = 1; }
            int cacheable = have && !strncmp(key, "https://", 8);
            if (cacheable && f) {   //  디스크에 있으면 받지 않는다
                sbuf b; sb_init(&b, 0);
                if (cache_read(url, &b)) { px = decode(f, (const unsigned char *)b.p, b.len); if (px) have = 0; else cache_drop(url); }
                sb_free(&b);
            }
            if (have && f) {
                sbuf b; sb_init(&b, 0);
                int got = fetch(url, &b);
                if (!got && !last_status) { sb_reset(&b); got = fetch(url, &b); }   //  연결·시간 초과면 한 번 더 바로
                if (got) {
                    px = decode(f, (const unsigned char *)b.p, b.len);
                    if (!px) { char w[64]; snprintf(w, sizeof w, "풀기 실패 (%zu바이트)", b.len); report_fail(key, w); }
                    else if (cacheable) cache_write(url, &b);
                } else {
                    char w[160];
                    if (last_status) snprintf(w, sizeof w, "받기 실패 HTTP %d", last_status); else snprintf(w, sizeof w, "받기 실패 %s", last_err);
                    report_fail(key, w);
                }
                sb_free(&b);
            } else if (!f) report_fail(key, "WIC 를 못 만듦");
            else if (!have && !strncmp(key, "asset:", 6)) report_fail(key, "에셋 목록에 없음/목록 받기 실패");
            EnterCriticalSection(&lock);
            int placed = 0;
            for (int i = 0; i < NSLOT; i++)
                if (S[i].key && S[i].st == ST_LOADING && !strcmp(S[i].key, key)) {
                    S[i].px = px;
                    S[i].st = px ? ST_OK : ST_FAIL;
                    S[i].at = GetTickCount();
                    placed = 1;
                    break;
                }
            LeaveCriticalSection(&lock);
            if (!placed) free(px);
            free(key);
            if (notify_w) PostMessageW(notify_w, notify_m, 0, 0);
        }
    }
    if (f) IWICImagingFactory_Release(f);
    CoUninitialize();
    return 0;
}

void img_init(HWND notify, UINT msg) {
    if (threads[0]) return;
    notify_w = notify;
    notify_m = msg;
    InitializeCriticalSection(&lock);
    InitializeCriticalSection(&as_lock);
    wake = CreateSemaphoreW(NULL, 0, 0x7FFFFFFF, NULL);
    for (int i = 0; i < NTHREAD; i++) threads[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

void img_stop(void) {
    if (!threads[0]) return;
    quit = 1;
    ReleaseSemaphore(wake, NTHREAD, NULL);
    int n = 0;
    HANDLE hs[NTHREAD];
    for (int i = 0; i < NTHREAD; i++) if (threads[i]) hs[n++] = threads[i];
    WaitForMultipleObjects((DWORD)n, hs, TRUE, 3000);   // 받는 중이면 시간 초과까지 안 기다린다 (곧 끝나는 프로세스)
}

int img_get(const char *key, const uint32_t **px) {
    *px = NULL;
    int is_data = key && !strncmp(key, "data:image/", 11);
    if (!threads[0] || !key || !*key || strlen(key) > (is_data ? 1024 * 1024 : 2048)) return -1;
    if (!is_data && strncmp(key, "https://", 8) && strncmp(key, "asset:", 6)) return -1;
    DWORD now = GetTickCount();
    int r = 0;
    EnterCriticalSection(&lock);
    slot *s = NULL;
    for (int i = 0; i < NSLOT; i++) if (S[i].key && !strcmp(S[i].key, key)) { s = &S[i]; break; }
    if (s) {
        s->used = now;
        if (s->st == ST_OK) { *px = s->px; r = 1; }
        else if (s->st == ST_FAIL) {
            if (now - s->at >= RETRY_MS) { s->st = ST_QUEUED; ReleaseSemaphore(wake, 1, NULL); r = 0; }
            else r = -1;
        }
    } else {
        //  빈 칸, 없으면 가장 오래 안 쓴 칸 (받는 중인 칸은 빼고) — 비우는 건 이 스레드(창)만 한다
        for (int i = 0; i < NSLOT && !s; i++) if (!S[i].key) s = &S[i];
        if (!s) for (int i = 0; i < NSLOT; i++) if (S[i].st != ST_LOADING && S[i].st != ST_QUEUED && (!s || now - S[i].used > now - s->used)) s = &S[i];
        if (s) {
            free(s->key);
            free(s->px);
            s->key = dupn(key, strlen(key));
            s->px = NULL;
            s->st = s->key ? ST_QUEUED : 0;
            s->used = now;
            if (s->key) ReleaseSemaphore(wake, 1, NULL);
        }
    }
    LeaveCriticalSection(&lock);
    return r;
}

int img_pending(void) {
    if (!threads[0]) return 0;
    int n = 0;
    EnterCriticalSection(&lock);
    for (int i = 0; i < NSLOT; i++) if (S[i].key && (S[i].st == ST_QUEUED || S[i].st == ST_LOADING)) n++;
    LeaveCriticalSection(&lock);
    return n;
}
