#define COBJMACROS
#include "img.h"
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/json.h"
#include "../base/sbuf.h"
#include "../net/http.h"

#define NSLOT 48
#define RETRY_MS 30000          // 못 받은 그림은 30초 뒤 다시 (지금 앱 THUMB_RETRY_MS 와 같음)
#define MAX_BYTES (8 * 1024 * 1024)
#define TIMEOUT_MS 10000

enum { ST_QUEUED = 1, ST_LOADING, ST_OK, ST_FAIL };
typedef struct { char *key; int st; uint32_t *px; DWORD at, used; } slot;

static slot S[NSLOT];
static CRITICAL_SECTION lock;
static HANDLE wake, thread;
static volatile LONG quit;
static HWND notify_w;
static UINT notify_m;

//  앱 에셋 목록 (App ID 별 — 이 스레드만 씀)
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
static int last_status;
static char last_err[96];
static int fetch(const char *url, sbuf *body) {
    int status = 0;
    char err[96] = "";
    if (strncmp(url, "https://", 8)) return 0;   // https 만
    int ok = http_get(url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) TOKU-RPC\r\n", TIMEOUT_MS, MAX_BYTES, &status, body, err, sizeof err);
    last_status = status;
    snprintf(last_err, sizeof last_err, "%s", ok ? "" : err);
    return ok && status >= 200 && status < 300 && body->len > 0;
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
    IWICImagingFactory *f = NULL;
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f);
    while (!quit) {
        WaitForSingleObject(wake, 1000);
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
            else if (!strncmp(key, "asset:", 6)) have = asset_url(key, url, sizeof url);
            else if (strlen(key) < sizeof url) { strcpy(url, key); have = 1; }
            if (have && f) {
                sbuf b; sb_init(&b, 0);
                if (fetch(url, &b)) {
                    px = decode(f, (const unsigned char *)b.p, b.len);
                    if (!px) { char w[64]; snprintf(w, sizeof w, "풀기 실패 (%zu바이트)", b.len); report_fail(key, w); }
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
    if (thread) return;
    notify_w = notify;
    notify_m = msg;
    InitializeCriticalSection(&lock);
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

void img_stop(void) {
    if (!thread) return;
    quit = 1;
    SetEvent(wake);
    WaitForSingleObject(thread, 3000);   // 받는 중이면 시간 초과까지 안 기다린다 (곧 끝나는 프로세스)
}

int img_get(const char *key, const uint32_t **px) {
    *px = NULL;
    int is_data = key && !strncmp(key, "data:image/", 11);
    if (!thread || !key || !*key || strlen(key) > (is_data ? 1024 * 1024 : 2048)) return -1;
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
            if (now - s->at >= RETRY_MS) { s->st = ST_QUEUED; SetEvent(wake); r = 0; }
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
            if (s->key) SetEvent(wake);
        }
    }
    LeaveCriticalSection(&lock);
    return r;
}

int img_pending(void) {
    if (!thread) return 0;
    int n = 0;
    EnterCriticalSection(&lock);
    for (int i = 0; i < NSLOT; i++) if (S[i].key && (S[i].st == ST_QUEUED || S[i].st == ST_LOADING)) n++;
    LeaveCriticalSection(&lock);
    return n;
}
