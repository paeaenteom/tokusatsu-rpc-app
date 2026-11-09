#include "binge.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "accent.h"
#include "app.h"
#include "settings.h"
#include "strings.h"
#include "../base/sbuf.h"
#include "../base/utf.h"
#include "../core/js.h"
#include "../net/http.h"

#define RELOG_COOLDOWN 600000      // 같은 화 재기록 금지 10분
#define SETTLE_DELAY 6000          // 안정화 최대 대기
#define QUICK_SETTLE 1500          // 같은 화가 '재생 중' 으로 이만큼 이어지면 바로
#define SETTLE_POLL 250
#define THUMB_WAIT 20000
#define THUMB_POLL 150

static HWND NOTIFY;
static int STATE = -1;

static int64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (int64_t)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000ULL - 11644473600000ULL);
}
static double rnd(void) {
    unsigned int v = 0;
    BCryptGenRandom(NULL, (PUCHAR)&v, sizeof v, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v / 4294967296.0;
}
static char *dupn(const char *s, size_t n) { char *d = (char *)malloc(n + 1); if (d) { memcpy(d, s, n); d[n] = 0; } return d; }
static char *dups(const char *s) { return dupn(s ? s : "", strlen(s ? s : "")); }

static const char *tr(int id) {
    const char *l = app_lang();
    const char *const *t = !strcmp(l, "ja") ? STR_JA : !strcmp(l, "en") ? STR_EN : STR_KO;
    return t[id];
}

// ── JS 의 \s (공백·줄바꿈·NBSP·전각 공백·BOM 등) ──
static size_t ws_at(const char *p, size_t left) {
    unsigned char c = (unsigned char)p[0];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') return 1;
    if (left >= 2 && c == 0xC2 && (unsigned char)p[1] == 0xA0) return 2;
    if (left >= 3) {
        unsigned char b = (unsigned char)p[1], d = (unsigned char)p[2];
        if (c == 0xE1 && b == 0x9A && d == 0x80) return 3;                                   // U+1680
        if (c == 0xE2 && b == 0x80 && (d <= 0x8A || d == 0xA8 || d == 0xA9 || d == 0xAF)) return 3;   // U+2000-200A 2028 2029 202F
        if (c == 0xE2 && b == 0x81 && d == 0x9F) return 3;                                   // U+205F
        if (c == 0xE3 && b == 0x80 && d == 0x80) return 3;                                   // U+3000
        if (c == 0xEF && b == 0xBB && d == 0xBF) return 3;                                   // U+FEFF
    }
    return 0;
}
//  v.replace(/\s+/g, ' ').trim().slice(0, max)
static char *collapse_trim(const char *s, size_t n, size_t max16);
char *binge_clean_ws(const char *s, size_t n, size_t max16) { return collapse_trim(s, n, max16); }
static char *collapse_trim(const char *s, size_t n, size_t max16) {
    sbuf a; sb_init(&a, 0);
    for (size_t i = 0; i < n;) {
        size_t k = ws_at(s + i, n - i);
        if (k) { sb_putc(&a, ' '); i += k; while (i < n && (k = ws_at(s + i, n - i))) i += k; }
        else sb_putc(&a, s[i++]);
    }
    size_t st = 0, en = a.len;
    while (st < en && a.p[st] == ' ') st++;
    while (en > st && a.p[en - 1] == ' ') en--;
    sbuf b; sb_init(&b, 0);
    js_slice16(&b, a.p ? a.p + st : "", en - st, max16);
    char *r = dupn(b.p ? b.p : "", b.len);
    sb_free(&a);
    sb_free(&b);
    return r;
}
static char *slice16(const char *s, size_t max16) {
    sbuf b; sb_init(&b, 0);
    js_slice16(&b, s, strlen(s), max16);
    char *r = dupn(b.p ? b.p : "", b.len);
    sb_free(&b);
    return r;
}

// ── 비밀 (작품별 웹훅·기본 웹훅) — 매번 새로 읽는다 (창에서 바꾼 것이 바로 반영) ──
double (*binge_rnd)(void) = rnd;   //  시험용으로 바꿀 수 있다

jval *binge_read_secrets(void) {
    wchar_t ad[MAX_PATH], p[MAX_PATH], test[MAX_PATH];
    //  시험 때만: 진짜 비밀 파일 대신 이 파일 (tests/binge-*.js)
    DWORD tn = GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SECRETS", test, MAX_PATH);
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad) != S_OK) return NULL;
    const wchar_t *subs[2] = { L"toku-rpc-native\\secrets.json", L"toku-rpc\\secrets.json" };
    for (int i = 0; i < 2; i++) {
        if (tn && tn < MAX_PATH) { if (i) break; wcscpy(p, test); }
        else _snwprintf(p, MAX_PATH - 1, L"%ls\\%ls", ad, subs[i]);
        p[MAX_PATH - 1] = 0;
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
        HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return NULL;
        LARGE_INTEGER sz;
        jval *v = NULL;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart < 16 * 1024 * 1024) {
            char *buf = (char *)malloc((size_t)sz.QuadPart + 1);
            DWORD got = 0;
            if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL)) {
                size_t off = (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
                v = json_parse(buf + off, got - off, NULL);
            }
            free(buf);
        }
        CloseHandle(h);
        return v;   //  있는 파일이 깨졌으면 다음 후보로 넘어가지 않는다 (지금 앱 readJson 은 null → 웹훅 없음)
    }
    return NULL;
}

//  /^https:\/\/(?:(?:ptb|canary)\.)?discord(?:app)?\.com\/api\/(?:v\d+\/)?webhooks\/\d{15,25}\/[\w-]{20,}$/
static int hook_url_ok(const char *s, size_t n);
int binge_hook_url_ok(const char *s, size_t n) { return hook_url_ok(s, n); }
static int hook_url_ok(const char *s, size_t n) {
    const char *e = s + n, *p = s;
    #define EAT(lit) (((size_t)(e - p) >= strlen(lit) && !strncmp(p, lit, strlen(lit))) ? (p += strlen(lit), 1) : 0)
    if (!EAT("https://")) return 0;
    if (!EAT("ptb.")) EAT("canary.");
    if (!EAT("discord")) return 0;
    EAT("app");
    if (!EAT(".com/api/")) return 0;
    {   const char *q = p;
        if (q < e && *q == 'v') { q++; const char *d = q; while (q < e && *q >= '0' && *q <= '9') q++; if (q > d && q < e && *q == '/') p = q + 1; } }
    if (!EAT("webhooks/")) return 0;
    #undef EAT
    const char *d = p;
    while (p < e && *p >= '0' && *p <= '9') p++;
    if (p - d < 15 || p - d > 25 || p >= e || *p != '/') return 0;
    p++;
    const char *t = p;
    while (p < e && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) p++;
    return p == e && p - t >= 20;
}

//  normTitle: NFKC → 공백 빼기 → 소문자 (UTF-16)
static wchar_t *norm_title(const char *s) {
    wchar_t *w = utf8_to_w(s, strlen(s));
    if (!w) return NULL;
    int need = NormalizeString(NormalizationKC, w, -1, NULL, 0);
    wchar_t *k = NULL;
    for (int tries = 0; need > 0 && tries < 4; tries++) {
        free(k);
        k = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)need);
        if (!k) break;
        int got = NormalizeString(NormalizationKC, w, -1, k, need);
        if (got > 0) { need = 0; break; }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) { free(k); k = NULL; break; }
        need = -got;
    }
    free(w);
    if (!k) return NULL;
    size_t o = 0;
    for (size_t i = 0; k[i]; i++) {
        wchar_t c = k[i];
        int sp = c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
                 c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
        if (!sp) k[o++] = c;
    }
    k[o] = 0;
    if (o) LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, k, (int)o, k, (int)o + 1, NULL, NULL, 0);
    return k;
}

void binge_pick_target(const char *series, char **url, char **title) {
    *url = NULL; *title = NULL;
    jval *sec = binge_read_secrets();
    wchar_t *want = norm_title(series ? series : "");
    const jval *list = json_get(sec, "titleWebhooks");
    wchar_t *best = NULL;
    int hits[128], nh = 0;
    for (size_t i = 0; want && list && list->t == J_ARR && i < list->count; i++) {
        const jval *h = list->items[i];
        const jval *tv = json_get(h, "title"), *uv = json_get(h, "url");
        if (!tv || tv->t != J_STR || !uv || uv->t != J_STR) continue;
        char *t = collapse_trim(tv->s, tv->slen, 100);
        size_t a, b;
        js_trim(uv->s, uv->slen, &a, &b);
        int good = t && *t && hook_url_ok(uv->s + a, b - a);
        wchar_t *nt = good ? norm_title(t) : NULL;
        free(t);
        if (!nt || !*nt || !wcsstr(want, nt)) { free(nt); continue; }
        //  가장 긴 이름 하나 — 같은 이름(정규화 뒤 글자까지 같음)이면 함께 후보
        size_t len = wcslen(nt), bl = best ? wcslen(best) : 0;
        if (len > bl) { free(best); best = nt; nh = 0; hits[nh++] = (int)i; }
        else if (best && !wcscmp(nt, best) && nh < 128) { hits[nh++] = (int)i; free(nt); }
        else free(nt);
    }
    if (nh) {
        const jval *h = list->items[hits[(int)(binge_rnd() * nh)]];
        const jval *uv = json_get(h, "url"), *tv = json_get(h, "title");
        size_t a, b;
        js_trim(uv->s, uv->slen, &a, &b);
        *url = dupn(uv->s + a, b - a);
        *title = collapse_trim(tv->s, tv->slen, 100);
    } else {
        char env[2048];
        DWORD n = GetEnvironmentVariableA("TOKU_BINGE_WEBHOOK", env, sizeof env);
        const char *d = (n && n < sizeof env) ? env : json_get_str(sec, "bingeWebhookUrl", "");
        *url = dups(d);
        *title = dups("");
    }
    free(best);
    free(want);
    json_free(sec);
}

//  characterFields(title, fixed): 켜 둔 캐릭터 중 무작위 한 명 → { username, avatar_url? } (없으면 NULL)
static int avatar_ok(const char *s) {
    if (strncmp(s, "https://", 8) || !s[8]) return 0;
    for (const char *p = s + 8; *p;) {
        if (*p == '"' || *p == '\'' || *p == '<' || *p == '>') return 0;
        size_t k = ws_at(p, strlen(p));
        if (k) return 0;
        p++;
    }
    return 1;
}
static int bad_name(const char *s) {   // /discord|clyde/i
    for (const char *p = s; *p; p++) if (!_strnicmp(p, "discord", 7) || !_strnicmp(p, "clyde", 5)) return 1;
    return 0;
}
jval *binge_character_fields(const char *title, const jval *fixed);
static jval *character_fields(const char *title, const jval *fixed) { return binge_character_fields(title, fixed); }
jval *binge_character_fields(const char *title, const jval *fixed) {
    const jval *one[1];
    const jval **list = NULL;
    size_t n = 0;
    const jval *chars = NULL;
    if (fixed) { one[0] = fixed; list = one; n = 1; }
    else if (title && *title) {
        chars = settings_get("hookFolders", title);
        chars = json_get(chars, "characters");
        if (chars && chars->t == J_ARR) { list = (const jval **)chars->items; n = chars->count; }
    }
    const jval *ok[64];
    size_t nu = 0;
    for (size_t i = 0; i < n && nu < 64; i++) {
        const jval *c = list[i];
        if (!c || c->t != J_OBJ) continue;
        const jval *name = json_get(c, "name");
        if (!fixed) {   //  hookChars + activeHookChars: id·name 이 문자열, off !== true
            const jval *id = json_get(c, "id"), *off = json_get(c, "off");
            if (!id || id->t != J_STR || !name || name->t != J_STR) continue;
            if (off && off->t == J_BOOL && off->b) continue;
        }
        if (!name || name->t != J_STR) continue;
        size_t a, b;
        js_trim(name->s, name->slen, &a, &b);
        if (a == b) continue;
        char *nm = dupn(name->s, name->slen);
        int bad = bad_name(nm);
        free(nm);
        if (bad) continue;
        ok[nu++] = c;
    }
    if (!nu) return NULL;
    const jval *c = ok[(int)(binge_rnd() * nu)];
    const jval *name = json_get(c, "name");
    jval *o = json_new(J_OBJ);
    char *u = collapse_trim(name->s, name->slen, 80);
    json_obj_set(o, "username", json_new_str(u ? u : ""));
    free(u);
    const char *av = json_get_str(c, "avatar", "");
    const jval *until = json_get(c, "avatarUntil");
    int expired = js_truthy(until) && (double)now_ms() > js_to_number(until);
    if (avatar_ok(av) && !expired) json_obj_set(o, "avatar_url", json_new_str(av));
    return o;
}

//  "2026\. 11. 04. 18:49" — 첫 점을 이스케이프해야 Discord 가 번호 목록으로 안 읽는다
static void fmt_date_at(char *out, size_t n, const SYSTEMTIME *t) {   //  d 를 주면 그 시각 (보충 기록)
    snprintf(out, n, "%u\\. %02u. %02u. %02u:%02u", t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute);
}
static void fmt_date(char *out, size_t n) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    fmt_date_at(out, n, &t);
}

// ── 썸네일 바이트 (허브에 한 벌, 20장) ──
typedef struct { char *url; unsigned char *buf; size_t n; char type[64]; } tbytes;
static tbytes TB[20];
static int TBN;
void binge_remember_thumb(const jval *url, const jval *data_url) {
    if (!url || url->t != J_STR || !url->slen || !data_url || data_url->t != J_STR) return;
    //  /^data:([^;]+);base64,(.+)$/  (. 은 줄바꿈 빼고)
    const char *s = data_url->s;
    size_t n = data_url->slen;
    if (n < 6 || strncmp(s, "data:", 5)) return;
    const char *semi = memchr(s + 5, ';', n - 5);
    if (!semi || semi == s + 5) return;
    if ((size_t)(s + n - semi) < 9 || strncmp(semi, ";base64,", 8)) return;
    const char *b64 = semi + 8;
    size_t bl = (size_t)(s + n - b64);
    if (!bl) return;
    //  . 은 줄 끝(\n \r U+2028 U+2029)과 맞지 않는다
    for (size_t i = 0; i < bl; i++) {
        if (b64[i] == '\n' || b64[i] == '\r') return;
        if (i + 2 < bl && (unsigned char)b64[i] == 0xE2 && (unsigned char)b64[i + 1] == 0x80 && ((unsigned char)b64[i + 2] == 0xA8 || (unsigned char)b64[i + 2] == 0xA9)) return;
    }
    //  Node Buffer.from(base64) 처럼 너그럽게 푼다 (A-Za-z0-9+/ 와 -_ 만 쓰고 나머지는 건너뜀, = 에서 멈춤)
    unsigned char *out = (unsigned char *)malloc(bl * 3 / 4 + 3);
    if (!out) return;
    size_t o = 0;
    unsigned v = 0;
    int bits = 0;
    for (size_t i = 0; i < bl; i++) {
        char c = b64[i];
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : (c == '+' || c == '-') ? 62 : (c == '/' || c == '_') ? 63 : c == '=' ? -2 : -1;
        if (d == -2) break;
        if (d < 0) continue;
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)(v >> bits); }
    }
    if (TBN >= 20) {   //  가장 오래된 것부터 (Map 순서)
        free(TB[0].url); free(TB[0].buf);
        memmove(TB, TB + 1, sizeof(tbytes) * 19);
        TBN = 19;
    }
    int at = -1;
    for (int i = 0; i < TBN; i++) if (!strcmp(TB[i].url, url->s)) at = i;
    if (at < 0) { at = TBN++; TB[at].url = dupn(url->s, url->slen); }
    else free(TB[at].buf);
    TB[at].buf = out;
    TB[at].n = o;
    snprintf(TB[at].type, sizeof TB[at].type, "%.*s", (int)(semi - (s + 5)), s + 5);
}
static tbytes *thumb_bytes(const char *url) {
    for (int i = 0; i < TBN; i++) if (!strcmp(TB[i].url, url)) return &TB[i];
    return NULL;
}

// ── 보내기 (따로 도는 스레드) ──
typedef struct job {
    struct job *next;
    int kind;                 // 0 시작 카드 · 1 끝 카드 · 2 시험 · 3 보충 기록
    int tag;
    char *url;                // 웹훅 (비밀)
    char *title, *desc, *site_name, *page_url;
    int site_color, fixed_color;
    jval *chars;              // { username, avatar_url } 또는 NULL
    unsigned char *bytes; size_t nbytes; char type[64];
    char *hosted;
    char *log_name;           // 보충: 로그에 남길 '작품명(30자) 화 번호'
} job;
static job *Q, *QT;
static CRITICAL_SECTION QL;
static HANDLE QE, QTH;

static void tlog(int lvl, const char *fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    char *m = dups(buf);
    if (!NOTIFY || !m || !PostMessageW(NOTIFY, WM_BINGE_LOG, (WPARAM)lvl, (LPARAM)m)) free(m);
}

//  같은 주소의 색은 기억 (100개)
static struct { char *url; int has, c; } UC[101];
static int UCN;
static int accent_from_url(const char *url, int *out) {
    if (strncmp(url, "https://", 8)) return 0;
    for (int i = 0; i < UCN; i++) if (!strcmp(UC[i].url, url)) { *out = UC[i].c; return UC[i].has; }
    int st = 0, has = 0, c = 0;
    sbuf b; sb_init(&b, 0);
    char err[64];
    if (http_get(url, NULL, 6000, 5 * 1024 * 1024, &st, &b, err, sizeof err) && st >= 200 && st < 300) has = accent_from_buffer((unsigned char *)b.p, b.len, &c);
    sb_free(&b);
    if (UCN > 100) { free(UC[0].url); memmove(UC, UC + 1, sizeof UC[0] * (size_t)(UCN - 1)); UCN--; }
    UC[UCN].url = dups(url); UC[UCN].has = has; UC[UCN].c = c; UCN++;
    *out = c;
    return has;
}

static void write_embed(sbuf *b, const job *j, int color) {
    if (j->kind == 2) {
        sb_puts(b, "{\"author\":{\"name\":\"TOKU RPC\"},\"title\":");
        json_write_str(b, j->title, strlen(j->title));
        sb_puts(b, ",\"description\":");
        json_write_str(b, j->desc, strlen(j->desc));
        sb_printf(b, ",\"color\":%d}", color);
        return;
    }
    sb_puts(b, "{\"title\":");
    json_write_str(b, j->title, strlen(j->title));
    sb_puts(b, ",\"description\":");
    json_write_str(b, j->desc, strlen(j->desc));
    sb_printf(b, ",\"color\":%d", color);
    if (j->site_name && *j->site_name) { sb_puts(b, ",\"author\":{\"name\":"); json_write_str(b, j->site_name, strlen(j->site_name)); sb_putc(b, '}'); }
    if (j->page_url && *j->page_url) { sb_puts(b, ",\"url\":"); json_write_str(b, j->page_url, strlen(j->page_url)); }
}
static void write_chars(sbuf *b, const jval *chars, int lead_comma) {
    for (size_t i = 0; chars && i < chars->count; i++) {
        if (lead_comma || i) sb_putc(b, ',');
        json_write_str(b, chars->keys[i], chars->klens[i]);
        sb_putc(b, ':');
        json_write(b, chars->items[i]);
    }
}

//  그림 바이트 앞부분으로 형식 (보충 직접 입력의 이미지 링크 — 받은 게 그림인지)
const char *binge_sniff_image(const unsigned char *b, size_t n) {
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return "image/jpeg";
    if (n >= 6 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) return "image/gif";
    if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return "image/webp";
    return NULL;
}

static void run_job(job *j) {
    if (j->kind == 3 && !(j->bytes && j->nbytes) && j->hosted && *j->hosted) {
        //  직접 입력의 이미지 링크: 받아서 첨부한다 (Discord 가 못 여는 링크도 보이게). 못 받으면 링크 그대로 — 주소는 로그에 안 남김
        int st = 0;
        char err[64] = "";
        sbuf b; sb_init(&b, 0);
        const char *mime = NULL;
        if (http_get(j->hosted, NULL, 15000, 8 * 1024 * 1024, &st, &b, err, sizeof err) && st >= 200 && st < 300) mime = binge_sniff_image((unsigned char *)b.p, b.len);
        if (mime) {
            j->bytes = (unsigned char *)malloc(b.len);
            if (j->bytes) { memcpy(j->bytes, b.p, b.len); j->nbytes = b.len; snprintf(j->type, sizeof j->type, "%s", mime); }
            tlog(0, "[Binge] 보충 이미지 링크 받음 (%zu바이트) — 첨부", b.len);
        } else tlog(1, "[Binge] 보충 이미지 링크를 받지 못함 (%s) — 링크 그대로 보냄", st ? "그림이 아님/HTTP 오류" : "연결 실패");
        sb_free(&b);
    }
    int color = j->fixed_color;
    if (j->kind == 0) {
        int c = 0, has = 0;
        if (j->bytes && j->nbytes) has = accent_from_buffer(j->bytes, j->nbytes, &c);
        if (!has && j->hosted && !strncmp(j->hosted, "https://", 8)) has = accent_from_url(j->hosted, &c);
        if (has) tlog(0, "[Binge] 카드 색: #%06x (썸네일)", c); else tlog(0, "[Binge] 카드 색: 사이트 색");
        color = has ? c : j->site_color;
    } else if (j->kind == 3) {   //  보충: 받아 둔 썸네일 바이트로만 (sendBackfill)
        int c = 0, has = j->bytes && j->nbytes && accent_from_buffer(j->bytes, j->nbytes, &c);
        if (has) tlog(0, "[Binge] 보충 카드 색: #%06x (썸네일)", c); else tlog(0, "[Binge] 보충 카드 색: 사이트 색");
        color = has ? c : j->site_color;
    }
    if (!j->url || !*j->url) {
        if (j->kind == 2) PostMessageW(NOTIFY, WM_BINGE_TEST, (WPARAM)j->tag, 0);
        if (j->kind == 3) PostMessageW(NOTIFY, WM_BINGE_BF, (WPARAM)j->tag, 0);
        return;
    }
    sbuf p; sb_init(&p, 0);
    int st = 0, ok = 0;
    char err[96] = "";
    sbuf r; sb_init(&r, 0);
    if (j->kind == 2) {
        //  Object.assign(캐릭터, { embeds }) — 캐릭터가 먼저
        sb_putc(&p, '{');
        write_chars(&p, j->chars, 0);
        if (j->chars && j->chars->count) sb_putc(&p, ',');
        sb_puts(&p, "\"embeds\":[");
        write_embed(&p, j, color);
        sb_puts(&p, "]}");
        ok = http_post(j->url, "Content-Type: application/json\r\n", p.p, p.len, 30000, &st, &r, err, sizeof err);
        if (ok && st >= 200 && st < 300) tlog(0, "[Binge] 웹훅 시험 전송 OK");
        else if (ok) tlog(0, "[Binge] 웹훅 시험 전송 HTTP %d", st);
        else tlog(1, "[Binge] 웹훅 시험 전송 실패: %s", err);
        int good = ok && st >= 200 && st < 300;
        PostMessageW(NOTIFY, WM_BINGE_TEST, (WPARAM)j->tag, (LPARAM)((good ? 1 << 16 : 0) | (ok ? (st & 0xFFFF) : 0)));
    } else if ((j->kind == 0 || j->kind == 3) && j->bytes && j->nbytes) {
        //  1순위: 이미지를 디스코드에 직접 첨부 (실제 형식에 맞춘 확장자)
        const char *ext = (strstr(j->type, "png") || strstr(j->type, "PNG")) ? "png" : (strstr(j->type, "webp") || strstr(j->type, "WEBP")) ? "webp" : (strstr(j->type, "gif") || strstr(j->type, "GIF")) ? "gif" : "jpg";
        char fname[16];
        snprintf(fname, sizeof fname, "thumb.%s", ext);
        sb_puts(&p, "{\"embeds\":[");
        write_embed(&p, j, color);
        sb_printf(&p, ",\"thumbnail\":{\"url\":\"attachment://%s\"}}]", fname);
        write_chars(&p, j->chars, 1);
        sb_putc(&p, '}');
        form_t f;
        form_init(&f);
        form_field(&f, "payload_json", p.p);
        form_file(&f, "files[0]", fname, *j->type ? j->type : "image/jpeg", j->bytes, j->nbytes);
        form_end(&f);
        char hdr[128];
        snprintf(hdr, sizeof hdr, "Content-Type: multipart/form-data; boundary=%s\r\n", f.boundary);
        ok = http_post(j->url, hdr, f.body.p, f.body.len, 30000, &st, &r, err, sizeof err);
        form_free(&f);
        if (j->kind == 3) { /* 아래에서 한 번에 */ }
        else if (ok && st >= 200 && st < 300) tlog(0, "[Binge] 웹훅 전송 OK (이미지 첨부)");
        else if (ok) { sbuf t; sb_init(&t, 0); js_slice16(&t, r.p ? r.p : "", r.len, 80); tlog(1, "[Binge] 웹훅(첨부) 실패: HTTP %d %s", st, t.p ? t.p : ""); sb_free(&t); }
        else if (!ok) tlog(1, "[Binge] 웹훅(첨부) 실패: %s", err);
    } else {
        //  2순위: 재호스팅/원본 주소
        sb_puts(&p, "{\"embeds\":[");
        write_embed(&p, j, color);
        if ((j->kind == 0 || j->kind == 3) && j->hosted && (!strncmp(j->hosted, "https://", 8) || !strncmp(j->hosted, "http://", 7))) {
            sb_puts(&p, ",\"thumbnail\":{\"url\":");
            json_write_str(&p, j->hosted, strlen(j->hosted));
            sb_putc(&p, '}');
        }
        sb_puts(&p, "}]");
        write_chars(&p, j->chars, 1);
        sb_putc(&p, '}');
        ok = http_post(j->url, "Content-Type: application/json\r\n", p.p, p.len, 30000, &st, &r, err, sizeof err);
        if (j->kind == 3) { /* 아래에서 한 번에 */ }
        else if (ok && st >= 200 && st < 300) tlog(0, "[Binge] 웹훅 전송 OK (URL 이미지)");
        else if (ok) tlog(1, "[Binge] 웹훅 실패: HTTP %d", st);
        else tlog(1, "[Binge] 웹훅 실패: %s", err);
    }
    if (j->kind == 3) {   //  sendBackfill: '보충 기록 전송 OK|HTTP n · 작품 화' — 주소는 남기지 않는다
        int good = ok && st >= 200 && st < 300;
        if (ok) { char hs[16]; snprintf(hs, sizeof hs, "HTTP %d", st); tlog(0, "[Binge] 보충 기록 전송 %s · %s", good ? "OK" : hs, j->log_name ? j->log_name : ""); }
        else tlog(1, "[Binge] 보충 기록 실패: %s", err);
        PostMessageW(NOTIFY, WM_BINGE_BF, (WPARAM)j->tag, (LPARAM)((good ? 1 << 16 : 0) | (ok ? (st & 0xFFFF) : 0)));
    }
    sb_free(&p);
    sb_free(&r);
}

static void job_free(job *j) {
    free(j->url); free(j->title); free(j->desc); free(j->site_name); free(j->page_url); free(j->bytes); free(j->hosted); free(j->log_name);
    json_free(j->chars);
    free(j);
}

static DWORD WINAPI sender(LPVOID arg) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (;;) {
        WaitForSingleObject(QE, INFINITE);
        for (;;) {
            EnterCriticalSection(&QL);
            job *j = Q;
            if (j) { Q = j->next; if (!Q) QT = NULL; }
            LeaveCriticalSection(&QL);
            if (!j) break;
            run_job(j);
            job_free(j);
        }
    }
    return 0;
}
static void push(job *j) {
    if (!QTH) { job_free(j); return; }
    j->next = NULL;
    EnterCriticalSection(&QL);
    if (QT) QT->next = j; else Q = j;
    QT = j;
    LeaveCriticalSection(&QL);
    SetEvent(QE);
}

void binge_init(HWND notify) {
    NOTIFY = notify;
    if (QTH) return;
    InitializeCriticalSection(&QL);
    QE = CreateEventW(NULL, FALSE, FALSE, NULL);
    QTH = CreateThread(NULL, 0, sender, NULL, 0, NULL);
}

// ── 상태 (확장이 알려 준 값) ──
int binge_state(void) { return STATE; }
void binge_set_state(int v) {
    if (STATE == v) return;
    STATE = v;
    app_log(0, v < 0 ? "[Binge] 정주행 모드: 모름" : v ? "[Binge] 정주행 모드: ON" : "[Binge] 정주행 모드: OFF");
    win_refresh();
}

// ── 기록기 (BingeLogger) ──
typedef struct { char *site, *series, *ep_title, *ep_no, *thumb, *url; } vstate;
static void vs_free(vstate *s) { free(s->site); free(s->series); free(s->ep_title); free(s->ep_no); free(s->thumb); free(s->url); memset(s, 0, sizeof *s); }
static void vs_copy(vstate *d, const vstate *s) { d->site = dups(s->site); d->series = dups(s->series); d->ep_title = dups(s->ep_title); d->ep_no = dups(s->ep_no); d->thumb = dups(s->thumb); d->url = dups(s->url); }

static char *LAST_KEY, *CUR_KEY, *LAST_SERIES;
static int64_t CUR_SINCE;
static vstate CUR;
static int HAS_CUR;
typedef struct { char *key; int64_t at; } logged_e;
static logged_e LOGGED[201];
static int NLOGGED;

typedef struct { int used; char *key; int64_t t0, next; vstate s; } check_t;
static check_t CHECKS[16];
typedef struct { int used; vstate s; int64_t t0, next; } wait_t;
static wait_t WAITS[16];

static void str_of(char **out, const jval *v) {   //  (v || '') 를 String 으로
    sbuf b; sb_init(&b, 0);
    js_str_or_empty(&b, v);
    *out = dupn(b.p ? b.p : "", b.len);
    sb_free(&b);
}

void binge_on_video(const jval *msg) {
    if (!js_truthy(json_get(msg, "binge"))) return;
    vstate s;
    memset(&s, 0, sizeof s);
    const jval *site = json_get(msg, "site");
    if (js_truthy(site)) str_of(&s.site, site); else s.site = dups("ttfc");
    str_of(&s.series, json_get(msg, "seriesName"));
    str_of(&s.ep_title, json_get(msg, "episodeTitle"));
    str_of(&s.ep_no, json_get(msg, "episodeNumber"));
    str_of(&s.thumb, json_get(msg, "thumbnail"));
    str_of(&s.url, json_get(msg, "url"));
    if (!*s.series) { vs_free(&s); return; }
    sbuf k; sb_init(&k, 0);
    sb_printf(&k, "%s|%s|%s|%s", s.site, s.series, s.ep_no, s.ep_title);
    char *key = dupn(k.p ? k.p : "", k.len);
    sb_free(&k);
    if (!CUR_KEY || strcmp(key, CUR_KEY)) { free(CUR_KEY); CUR_KEY = dups(key); CUR_SINCE = now_ms(); }
    if (HAS_CUR) vs_free(&CUR);
    vs_copy(&CUR, &s);
    HAS_CUR = 1;
    if (LAST_KEY && !strcmp(key, LAST_KEY)) { free(key); vs_free(&s); return; }   //  같은 화 (일시정지·재개 포함)
    int64_t prev = 0;
    for (int i = 0; i < NLOGGED; i++) if (!strcmp(LOGGED[i].key, key)) prev = LOGGED[i].at;
    free(LAST_KEY);
    LAST_KEY = dups(key);
    if (now_ms() - prev < RELOG_COOLDOWN) { free(key); vs_free(&s); return; }
    free(LAST_SERIES);
    LAST_SERIES = dups(s.series);
    char m[700];
    snprintf(m, sizeof m, "[Binge] 새 에피소드 감지: %s %s — 재생 확인되면 바로 (최대 %d초)", s.series, s.ep_no, SETTLE_DELAY / 1000);
    app_log(0, m);
    int slot = -1;
    for (int i = 0; i < 16; i++) if (!CHECKS[i].used) { slot = i; break; }
    if (slot < 0) { free(key); vs_free(&s); return; }
    CHECKS[slot].used = 1;
    CHECKS[slot].key = key;
    CHECKS[slot].t0 = now_ms();
    CHECKS[slot].next = CHECKS[slot].t0 + SETTLE_POLL;
    CHECKS[slot].s = s;
}

static void skip_log(const char *key) {
    char *k = slice16(key, 40);
    char m[300];
    snprintf(m, sizeof m, "[Binge] 에피소드가 안정되지 않아 기록 생략: %s", k ? k : "");
    free(k);
    app_log(0, m);
}

static void send_start(const vstate *s);

void binge_tick(void) {
    int64_t now = now_ms();
    for (int i = 0; i < 16; i++) {
        check_t *c = &CHECKS[i];
        if (!c->used || now < c->next) continue;
        int done = 1;
        if (!CUR_KEY || strcmp(CUR_KEY, c->key)) skip_log(c->key);
        else {
            int64_t stable = now - CUR_SINCE;
            //  ⚠ 지금 앱도 정주행 기록기에는 isPlaying·duration 을 넘기지 않는다 → 늘 '최대 대기' (같은 동작)
            int quick = 0;
            if (!quick && now - c->t0 < SETTLE_DELAY) { c->next = now + SETTLE_POLL; done = 0; }
            else if (!quick && stable < SETTLE_DELAY - 500) skip_log(c->key);
            else {
                if (NLOGGED > 200) { for (int k = 0; k < NLOGGED; k++) free(LOGGED[k].key); NLOGGED = 0; }
                int at = -1;
                for (int k = 0; k < NLOGGED; k++) if (!strcmp(LOGGED[k].key, c->key)) at = k;
                if (at < 0) { at = NLOGGED++; LOGGED[at].key = dups(c->key); }
                LOGGED[at].at = now;
                char m[120];
                snprintf(m, sizeof m, "[Binge] 확인 (%lldms, %s)", (long long)(now - c->t0), quick ? "재생 중" : "최대 대기");
                app_log(0, m);
                send_start(HAS_CUR ? &CUR : &c->s);
            }
        }
        if (done) { free(c->key); vs_free(&c->s); memset(c, 0, sizeof *c); }
    }
    for (int i = 0; i < 16; i++) {
        wait_t *w = &WAITS[i];
        if (!w->used || now < w->next) continue;
        //  썸네일 준비 대기 — 바이트가 왔거나 재호스팅 주소가 생기면 바로
        tbytes *tb = *w->s.thumb ? thumb_bytes(w->s.thumb) : NULL;
        char *hosted = *w->s.thumb ? core_resolve_image(app_core(), w->s.thumb) : dups("");
        int http = hosted && (!strncmp(hosted, "https://", 8) || !strncmp(hosted, "http://", 7));
        int ready = !*w->s.thumb || tb || http;
        if (!ready && now - w->t0 < THUMB_WAIT) { w->next = now + THUMB_POLL; free(hosted); continue; }
        if (!ready || !*w->s.thumb) { tb = NULL; free(hosted); hosted = dups(""); }
        //  카드 (색은 보내는 스레드가 썸네일에서 고른다)
        const vstate *s = &w->s;
        job *j = (job *)calloc(1, sizeof *j);
        if (j) {
            j->kind = 0;
            j->title = slice16(s->series, 256);
            const char *ep = *s->ep_no && *s->ep_title ? NULL : *s->ep_no ? s->ep_no : *s->ep_title ? s->ep_title : tr(S_hook_episode);
            char date[64], *d = (char *)malloc(strlen(s->ep_no) + strlen(s->ep_title) + strlen(tr(S_hook_episode)) + 96);
            fmt_date(date, sizeof date);
            if (d) {
                if (ep) sprintf(d, "**%s**\n%s", ep, date);
                else sprintf(d, "**%s / %s**\n%s", s->ep_no, s->ep_title, date);
            }
            j->desc = d;
            j->site_color = !strcmp(s->site, "imagination") ? 0xd32f5a : 0x2ea6ff;
            const char *sn = !strcmp(s->site, "ttfc") ? "\xE6\x9D\xB1\xE6\x98\xA0\xE7\x89\xB9\xE6\x92\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xA9\xE3\x83\x96"
                           : !strcmp(s->site, "imagination") ? "TSUBURAYA IMAGINATION" : s->site;
            j->site_name = slice16(sn, 256);
            j->page_url = (!strncmp(s->url, "https://", 8) || !strncmp(s->url, "http://", 7)) ? dups(s->url) : dups("");
            if (tb) { j->bytes = (unsigned char *)malloc(tb->n ? tb->n : 1); if (j->bytes) { memcpy(j->bytes, tb->buf, tb->n); j->nbytes = tb->n; } snprintf(j->type, sizeof j->type, "%s", tb->type); }
            j->hosted = hosted; hosted = NULL;
            //  웹훅·캐릭터는 이 기록 한 번에 한 번만 고른다
            char *title = NULL;
            binge_pick_target(s->series, &j->url, &title);
            j->chars = character_fields(title, NULL);
            free(title);
            push(j);
        }
        free(hosted);
        vs_free(&w->s);
        memset(w, 0, sizeof *w);
    }
}

static void send_start(const vstate *s) {
    for (int i = 0; i < 16; i++) {
        if (WAITS[i].used) continue;
        WAITS[i].used = 1;
        vs_copy(&WAITS[i].s, s);
        WAITS[i].t0 = now_ms();
        WAITS[i].next = WAITS[i].t0;   //  첫 확인은 바로 — 같은 binge_tick 의 아래 대기 반복이 곧바로 본다 (여기서 다시 부르면 위 확인이 되풀이됨)
        return;
    }
}

void binge_end(const jval *msg) {
    sbuf sb; sb_init(&sb, 0);
    js_str_or_empty(&sb, json_get(msg, "seriesName"));
    char m[600];
    snprintf(m, sizeof m, "[Bridge] BINGE_END 수신: %s", sb.p ? sb.p : "");
    app_log(0, m);
    const jval *sv = json_get(msg, "site");
    char *site = NULL;
    str_of(&site, sv);
    int known = !strcmp(site, "ttfc") || !strcmp(site, "imagination");
    if (js_truthy(sv) && !known) { free(site); sb_free(&sb); return; }   //  정주행은 TTFC·IMAGINATION 만
    const char *name = sb.len ? sb.p : (LAST_SERIES ? LAST_SERIES : "");
    free(LAST_KEY);
    LAST_KEY = dups("");
    job *j = (job *)calloc(1, sizeof *j);
    if (j) {
        j->kind = 1;
        //  tr('hook.endTitle', { name }) — {name} 자리마다
        const char *tpl = tr(S_hook_endTitle);
        sbuf t; sb_init(&t, 0);
        for (const char *p = tpl; *p;) { if (!strncmp(p, "{name}", 6)) { sb_puts(&t, name); p += 6; } else sb_putc(&t, *p++); }
        j->title = slice16(t.p ? t.p : "", 256);
        sb_free(&t);
        char date[64];
        fmt_date(date, sizeof date);
        const char *ed = tr(S_hook_endDesc);
        j->desc = (char *)malloc(strlen(ed) + 80);
        if (j->desc) sprintf(j->desc, "**%s**\n%s", ed, date);
        j->fixed_color = 0xffc233;
        j->site_name = dups(!known ? "" : !strcmp(site, "ttfc") ? "\xE6\x9D\xB1\xE6\x98\xA0\xE7\x89\xB9\xE6\x92\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xA9\xE3\x83\x96" : "TSUBURAYA IMAGINATION");
        j->page_url = dups("");
        char *title = NULL;
        binge_pick_target(name, &j->url, &title);
        j->chars = character_fields(title, NULL);
        free(title);
        push(j);
    }
    free(site);
    sb_free(&sb);
}

void binge_send_test(const char *url, const char *title, const jval *character, int tag) {
    job *j = (job *)calloc(1, sizeof *j);
    if (!j) return;
    j->kind = 2;
    j->tag = tag;
    j->url = dups(url);
    j->title = slice16(title ? title : "", 256);
    char date[64];
    fmt_date(date, sizeof date);
    const char *td = tr(S_hook_testDesc);
    j->desc = (char *)malloc(strlen(td) + 80);
    if (j->desc) sprintf(j->desc, "**%s**\n%s", td, date);
    j->fixed_color = 0x7775fc;
    j->chars = character ? character_fields("", character) : NULL;
    push(j);
}

//  보충 기록 (sendBackfill): 실시간 기록과 같은 카드를 정한 시각으로 — 보낼 곳·캐릭터 고르기도 같다
//  NULL = 보내기 시작 (결과는 WM_BINGE_BF), "nohook" = 보낼 웹훅 없음
const char *binge_send_backfill(const char *site, const char *page_url, const char *series, const char *ep_no, const char *ep_title,
                                const unsigned char *bytes, size_t nbytes, const char *type, const char *img_url, const SYSTEMTIME *when, int tag, char **folder) {
    char *url = NULL, *title = NULL;
    binge_pick_target(series, &url, &title);
    if (folder) *folder = dups(title ? title : "");
    if (!url || !*url) { free(url); free(title); return "nohook"; }
    job *j = (job *)calloc(1, sizeof *j);
    if (!j) { free(url); free(title); return "nohook"; }
    j->kind = 3;
    j->tag = tag;
    j->url = url;
    j->title = slice16(series, 256);
    const char *ep = *ep_no && *ep_title ? NULL : *ep_no ? ep_no : *ep_title ? ep_title : tr(S_hook_episode);
    char date[64], *d = (char *)malloc(strlen(ep_no) + strlen(ep_title) + strlen(tr(S_hook_episode)) + 96);
    fmt_date_at(date, sizeof date, when);
    if (d) {
        if (ep) sprintf(d, "**%s**\n%s", ep, date);
        else sprintf(d, "**%s / %s**\n%s", ep_no, ep_title, date);
    }
    j->desc = d;
    j->site_color = !strcmp(site, "imagination") ? 0xd32f5a : 0x2ea6ff;
    j->site_name = dups(!strcmp(site, "ttfc") ? "\xE6\x9D\xB1\xE6\x98\xA0\xE7\x89\xB9\xE6\x92\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xA9\xE3\x83\x96" : !strcmp(site, "imagination") ? "TSUBURAYA IMAGINATION" : site);
    j->page_url = (!strncmp(page_url, "https://", 8) || !strncmp(page_url, "http://", 7)) ? dups(page_url) : dups("");
    if (bytes && nbytes) {
        j->bytes = (unsigned char *)malloc(nbytes);
        if (j->bytes) { memcpy(j->bytes, bytes, nbytes); j->nbytes = nbytes; }
        snprintf(j->type, sizeof j->type, "%s", type ? type : "");
    } else if (img_url && *img_url) j->hosted = dups(img_url);
    char *s30 = slice16(series, 30);
    size_t ln = strlen(s30 ? s30 : "") + strlen(ep_no) + 2;
    j->log_name = (char *)malloc(ln);
    if (j->log_name) snprintf(j->log_name, ln, "%s %s", s30 ? s30 : "", ep_no);
    free(s30);
    j->chars = character_fields(title, NULL);
    free(title);
    push(j);
    return NULL;
}
