#include "backfill.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/sbuf.h"
#include "../core/js.h"

// ── JS 정규식 \s (ECMAScript WhiteSpace + LineTerminator) ──
static int js_space_cp(unsigned cp) {
    return (cp >= 0x09 && cp <= 0x0D) || cp == 0x20 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF;
}
//  s[i] 에서 한 글자 (UTF-8) → 코드 포인트, 길이
static unsigned cp_at(const char *s, size_t n, size_t i, size_t *len) {
    const unsigned char *u = (const unsigned char *)s + i;
    size_t left = n - i;
    if (u[0] < 0x80 || left < 2) { *len = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0) { *len = 2; return ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu); }
    if ((u[0] & 0xF0) == 0xE0 && left >= 3) { *len = 3; return ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu); }
    if ((u[0] & 0xF8) == 0xF0 && left >= 4) { *len = 4; return ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) | ((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu); }
    *len = 1;
    return u[0];
}

//  /^https:\/\/[^\s"'<>]+$/
static int https_ok(const char *s, size_t n) {
    if (n <= 8 || memcmp(s, "https://", 8)) return 0;
    for (size_t i = 8; i < n;) {
        size_t l;
        unsigned cp = cp_at(s, n, i, &l);
        if (js_space_cp(cp) || cp == '"' || cp == '\'' || cp == '<' || cp == '>') return 0;
        i += l;
    }
    return 1;
}

//  str(v, n): typeof v === 'string' ? v.replace(/[\u0000-\u001f]/g, ' ').trim().slice(0, n) : ''
static jval *clean_str(const jval *v, size_t n16) {
    if (!v || v->t != J_STR) return json_new_str("");
    char *c = (char *)malloc(v->slen + 1);
    if (!c) return json_new_str("");
    for (size_t i = 0; i < v->slen; i++) c[i] = (unsigned char)v->s[i] < 0x20 ? ' ' : v->s[i];
    c[v->slen] = 0;
    size_t a, b;
    js_trim(c, v->slen, &a, &b);
    sbuf o; sb_init(&o, 0);
    js_slice16(&o, c + a, b - a, n16);
    jval *r = json_new_strn(o.p ? o.p : "", o.len);
    sb_free(&o);
    free(c);
    return r;
}

//  /^data:image\/(?:jpeg|png|webp);base64,[A-Za-z0-9+/=]+$/ 그리고 길이 12MB 이하
static int thumb_ok(const jval *v) {
    if (!v || v->t != J_STR || v->slen > 12u * 1024 * 1024) return 0;
    const char *s = v->s;
    size_t n = v->slen, p;
    if (n >= 23 && !memcmp(s, "data:image/jpeg;base64,", 23)) p = 23;
    else if (n >= 22 && !memcmp(s, "data:image/png;base64,", 22)) p = 22;
    else if (n >= 23 && !memcmp(s, "data:image/webp;base64,", 23)) p = 23;
    else return 0;
    if (p >= n) return 0;
    for (size_t i = p; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) return 0;
    }
    return 1;
}

static int is_str(const jval *v, const char *s) { return v && v->t == J_STR && v->slen == strlen(s) && !memcmp(v->s, s, v->slen); }

jval *bf_clean_resolved(const jval *msg) {
    jval *r = json_new(J_OBJ);
    if (!r) return NULL;
    if (js_truthy(json_get(msg, "ok"))) {
        const jval *site = json_get(msg, "site");
        const char *st = is_str(site, "imagination") ? "imagination" : is_str(site, "ttfc") ? "ttfc" : "";
        if (!*st) {   //  if (!r.site) r = { ok: false, error: 'fetch' }
            json_obj_set(r, "ok", json_new_bool(0));
            json_obj_set(r, "error", json_new_str("fetch"));
            return r;
        }
        json_obj_set(r, "ok", json_new_bool(1));
        json_obj_set(r, "site", json_new_str(st));
        //  /re/.test(msg.url || '') — 문자열이 아니면 String() 으로 시험하고, 값은 str() 이라 '' (지금 앱과 같게)
        const jval *u = json_get(msg, "url");
        sbuf us; sb_init(&us, 0);
        js_str_or_empty(&us, u);
        json_obj_set(r, "url", https_ok(us.p ? us.p : "", us.len) ? clean_str(u, 500) : json_new_str(""));
        sb_free(&us);
        json_obj_set(r, "seriesName", clean_str(json_get(msg, "seriesName"), 200));
        json_obj_set(r, "episodeNumber", clean_str(json_get(msg, "episodeNumber"), 60));
        json_obj_set(r, "episodeTitle", clean_str(json_get(msg, "episodeTitle"), 200));
        const jval *th = json_get(msg, "thumbDataUrl");
        json_obj_set(r, "thumbDataUrl", thumb_ok(th) ? json_new_strn(th->s, th->slen) : json_new_str(""));
        return r;
    }
    const jval *e = json_get(msg, "error");
    static const char *const OK[] = { "badurl", "blocked", "empty", "fetch" };
    const char *err = "fetch";
    for (int i = 0; i < 4; i++) if (is_str(e, OK[i])) err = OK[i];
    json_obj_set(r, "ok", json_new_bool(0));
    json_obj_set(r, "error", json_new_str(err));
    return r;
}

//  /^\s*https:\/\/(?:pc\.tokusatsu-fc\.jp\/movies\/\d+\/movie-stories\/\d+|imagination\.m-78\.jp\/video\/[A-Za-z0-9_-]+)/
static size_t digits(const char *s, size_t n, size_t i) { size_t k = i; while (k < n && s[k] >= '0' && s[k] <= '9') k++; return k - i; }
int bf_link(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) { size_t l; unsigned cp = cp_at(s, n, i, &l); if (!js_space_cp(cp)) break; i += l; }
    static const char H[] = "https://";
    if (n - i < 8 || memcmp(s + i, H, 8)) return 0;
    i += 8;
    static const char T1[] = "pc.tokusatsu-fc.jp/movies/", T2[] = "/movie-stories/", I1[] = "imagination.m-78.jp/video/";
    if (n - i >= sizeof T1 - 1 && !memcmp(s + i, T1, sizeof T1 - 1)) {
        size_t k = i + sizeof T1 - 1, d = digits(s, n, k);
        if (!d) return 0;
        k += d;
        if (n - k < sizeof T2 - 1 || memcmp(s + k, T2, sizeof T2 - 1)) return 0;
        return digits(s, n, k + sizeof T2 - 1) > 0;
    }
    if (n - i >= sizeof I1 - 1 && !memcmp(s + i, I1, sizeof I1 - 1)) {
        size_t k = i + sizeof I1 - 1;
        if (k >= n) return 0;
        char c = s[k];
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }
    return 0;
}

// ── 시각 ──
static int days_in(int y, int m) {
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[m - 1];
}
//  숫자 5개 (연·월·일·시·분) — 사이는 숫자가 아닌 아무 글자 (2026-11-08 18:49 · 2026. 11. 08. 18:49 · 2026-11-08T18:49)
int bf_parse_when(const char *s, SYSTEMTIME *out) {
    long v[5];
    int k = 0;
    const char *p = s;
    while (*p && k < 5) {
        while (*p && !(*p >= '0' && *p <= '9')) {
            if ((unsigned char)*p >= 0x80 || (*p != ' ' && *p != '-' && *p != '.' && *p != ':' && *p != '/' && *p != 'T' && *p != '\t')) return 0;
            p++;
        }
        if (!*p) break;
        long x = 0;
        int nd = 0;
        while (*p >= '0' && *p <= '9') { if (++nd > 4) return 0; x = x * 10 + (*p - '0'); p++; }
        v[k++] = x;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (k != 5 || *p) return 0;
    if (v[0] < 1601 || v[0] > 9999 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > days_in((int)v[0], (int)v[1]) || v[3] > 23 || v[4] > 59) return 0;
    memset(out, 0, sizeof *out);
    out->wYear = (WORD)v[0]; out->wMonth = (WORD)v[1]; out->wDay = (WORD)v[2]; out->wHour = (WORD)v[3]; out->wMinute = (WORD)v[4];
    return 1;
}
void bf_fmt_when(char *out, size_t n, const SYSTEMTIME *t) {
    snprintf(out, n, "%04u-%02u-%02u %02u:%02u", t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute);
}
void bf_card_date(char *out, size_t n, const SYSTEMTIME *t) {
    snprintf(out, n, "%u. %02u. %02u. %02u:%02u", t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute);
}
double bf_local_to_epoch(const SYSTEMTIME *local) {
    SYSTEMTIME u;
    FILETIME ft;
    if (!TzSpecificLocalTimeToSystemTime(NULL, local, &u) || !SystemTimeToFileTime(&u, &ft)) return NAN;
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (double)((long long)(t / 10000ULL) - 11644473600000LL);
}
int bf_when_ok(const SYSTEMTIME *local, double now_ms) {
    if (local->wYear < 2000) return 0;
    double e = bf_local_to_epoch(local);
    return !isnan(e) && e <= now_ms + 10 * 60 * 1000;
}

//  main.js backfill-resolve: /^data:(image\/(?:jpeg|png|webp));base64,(.+)$/ → Buffer.from(m[2], 'base64')
//  Node 처럼 너그럽게 푼다 (A-Za-z0-9+/ 와 -_ 만 쓰고 나머지는 건너뜀, = 에서 멈춤) — binge.c 썸네일 풀기와 같은 규칙
unsigned char *bf_thumb_bytes(const char *s, size_t n, char type[16], size_t *out_n) {
    static const char *const T[] = { "image/jpeg", "image/png", "image/webp" };
    *out_n = 0;
    type[0] = 0;
    if (n < 5 || memcmp(s, "data:", 5)) return NULL;
    const char *b64 = NULL;
    for (int i = 0; i < 3; i++) {
        size_t tl = strlen(T[i]);
        if (n >= 5 + tl + 8 && !memcmp(s + 5, T[i], tl) && !memcmp(s + 5 + tl, ";base64,", 8)) { b64 = s + 5 + tl + 8; snprintf(type, 16, "%s", T[i]); break; }
    }
    if (!b64) return NULL;
    size_t bl = (size_t)(s + n - b64);
    if (!bl) { type[0] = 0; return NULL; }
    for (size_t i = 0; i < bl; i++) {   //  . 은 줄 끝과 맞지 않는다
        if (b64[i] == '\n' || b64[i] == '\r') { type[0] = 0; return NULL; }
        if (i + 2 < bl && (unsigned char)b64[i] == 0xE2 && (unsigned char)b64[i + 1] == 0x80 && ((unsigned char)b64[i + 2] == 0xA8 || (unsigned char)b64[i + 2] == 0xA9)) { type[0] = 0; return NULL; }
    }
    unsigned char *out = (unsigned char *)malloc(bl * 3 / 4 + 3);
    if (!out) { type[0] = 0; return NULL; }
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
    *out_n = o;
    return out;
}
