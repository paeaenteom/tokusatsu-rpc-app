#include "js.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double js_round(double x) {
    if (!isfinite(x) || x == 0) return x;
    double r = floor(x);
    if (x - r >= 0.5) r += 1;
    if (r == 0 && x < 0) return -0.0;   // Math.round(-0.4) === -0
    return r;
}

// JS 의 공백 (WhiteSpace + LineTerminator) 한 글자면 바이트 수, 아니면 0
static size_t js_space(const unsigned char *p, size_t avail) {
    unsigned char c = p[0];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') return 1;
    if (c == 0xC2 && avail >= 2 && p[1] == 0xA0) return 2;                        // U+00A0
    if (c == 0xE1 && avail >= 3 && p[1] == 0x9A && p[2] == 0x80) return 3;        // U+1680
    if (c == 0xE2 && avail >= 3) {
        if (p[1] == 0x80 && (p[2] >= 0x80 && p[2] <= 0x8A)) return 3;            // U+2000~200A
        if (p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9 || p[2] == 0xAF)) return 3;   // 2028 2029 202F
        if (p[1] == 0x81 && p[2] == 0x9F) return 3;                               // U+205F
    }
    if (c == 0xE3 && avail >= 3 && p[1] == 0x80 && p[2] == 0x80) return 3;        // U+3000
    if (c == 0xEF && avail >= 3 && p[1] == 0xBB && p[2] == 0xBF) return 3;        // U+FEFF
    return 0;
}

void js_trim(const char *s, size_t len, size_t *start, size_t *end) {
    const unsigned char *p = (const unsigned char *)s;
    size_t a = 0, b = len;
    for (;;) {
        if (a >= b) break;
        size_t n = js_space(p + a, b - a);
        if (!n) break;
        a += n;
    }
    //  뒤에서: 글자 시작을 찾아 그 글자가 공백인지 본다
    while (b > a) {
        size_t k = b - 1;
        while (k > a && (p[k] & 0xC0) == 0x80) k--;
        size_t n = js_space(p + k, b - k);
        if (!n || k + n != b) break;
        b = k;
    }
    *start = a;
    *end = b;
}

// StringToNumber — JS 문법에 맞을 때만 strtod (strtod 가 받아 주는 inf·nan·0x1p3 등은 NaN)
static double str_to_number(const char *s, size_t len) {
    size_t a, b;
    js_trim(s, len, &a, &b);
    if (a == b) return 0;
    const char *p = s + a;
    size_t n = b - a;
    if (n >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X' || p[1] == 'o' || p[1] == 'O' || p[1] == 'b' || p[1] == 'B')) {
        int base = (p[1] == 'x' || p[1] == 'X') ? 16 : (p[1] == 'o' || p[1] == 'O') ? 8 : 2;
        if (n == 2) return NAN;
        double v = 0;
        for (size_t i = 2; i < n; i++) {
            int d;
            char c = p[i];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return NAN;
            if (d >= base) return NAN;
            v = v * base + d;
        }
        return v;
    }
    size_t i = 0;
    int neg = 0;
    if (p[0] == '+' || p[0] == '-') { neg = p[0] == '-'; i = 1; }
    if (n - i == 8 && memcmp(p + i, "Infinity", 8) == 0) return neg ? -INFINITY : INFINITY;
    size_t digits = 0;
    size_t j = i;
    while (j < n && p[j] >= '0' && p[j] <= '9') { j++; digits++; }
    if (j < n && p[j] == '.') { j++; while (j < n && p[j] >= '0' && p[j] <= '9') { j++; digits++; } }
    if (!digits) return NAN;
    if (j < n && (p[j] == 'e' || p[j] == 'E')) {
        j++;
        if (j < n && (p[j] == '+' || p[j] == '-')) j++;
        size_t e0 = j;
        while (j < n && p[j] >= '0' && p[j] <= '9') j++;
        if (j == e0) return NAN;
    }
    if (j != n) return NAN;
    char small[64];
    char *tmp = n < sizeof small ? small : (char *)malloc(n + 1);
    if (!tmp) return NAN;
    memcpy(tmp, p, n);
    tmp[n] = '\0';
    double v = strtod(tmp, NULL);
    if (tmp != small) free(tmp);
    return v;
}

double js_to_number(const jval *v) {
    if (!v) return NAN;
    switch (v->t) {
    case J_NULL: return 0;
    case J_BOOL: return v->b ? 1 : 0;
    case J_NUM: return v->n;
    case J_STR: return str_to_number(v->s, v->slen);
    case J_ARR:
        //  [] → 0, [x] → Number(String(x)), 그 밖 NaN
        if (v->count == 0) return 0;
        if (v->count == 1) {
            sbuf b;
            sb_init(&b, 0);
            js_join_item(&b, v->items[0]);
            double r = b.err ? NAN : str_to_number(b.p ? b.p : "", b.len);
            sb_free(&b);
            return r;
        }
        return NAN;
    default: return NAN;
    }
}

int js_truthy(const jval *v) {
    if (!v) return 0;
    switch (v->t) {
    case J_NULL: return 0;
    case J_BOOL: return v->b;
    case J_NUM: return !(v->n == 0 || isnan(v->n));
    case J_STR: return v->slen > 0;
    default: return 1;
    }
}

void js_to_string(sbuf *b, const jval *v) {
    if (!v) { sb_puts(b, "undefined"); return; }
    switch (v->t) {
    case J_NULL: sb_puts(b, "null"); break;
    case J_BOOL: sb_puts(b, v->b ? "true" : "false"); break;
    case J_NUM: js_number_str(b, v->n); break;
    case J_STR: sb_putn(b, v->s, v->slen); break;
    case J_ARR:
        for (size_t i = 0; i < v->count; i++) {
            if (i) sb_putc(b, ',');
            js_join_item(b, v->items[i]);
        }
        break;
    case J_OBJ: sb_puts(b, "[object Object]"); break;
    }
}

void js_join_item(sbuf *b, const jval *v) {
    if (!v || v->t == J_NULL) return;
    js_to_string(b, v);
}

void js_str_or_empty(sbuf *b, const jval *v) {
    if (js_truthy(v)) js_to_string(b, v);
}

void js_slice16(sbuf *b, const char *s, size_t len, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0, u = 0;
    while (i < len && u < n) {
        unsigned char c = p[i];
        size_t k = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (k > len - i) k = 1;
        size_t du = k == 4 ? 2 : 1;
        if (u + du > n) {
            //  이모지(서로게이트 쌍)의 앞 반쪽만 남는다 — Node 가 UTF-8 로 쓰면 U+FFFD
            sb_puts(b, "\xEF\xBF\xBD");
            return;
        }
        sb_putn(b, s + i, k);
        i += k;
        u += du;
    }
}

void js_to_fixed(sbuf *b, double x, int f) {
    if (isnan(x)) { sb_puts(b, "NaN"); return; }
    if (fabs(x) >= 1e21) { js_number_str(b, x); return; }
    int neg = x < 0;
    if (neg) x = -x;
    //  정확한 십진 전개(넉넉한 자릿수)에서 f 자리로 반올림 — 같은 거리면 큰 쪽 (JS 규칙)
    char t[512];
    snprintf(t, sizeof t, "%.*f", f + 80, x);
    char *dot = strchr(t, '.');
    size_t ip = dot ? (size_t)(dot - t) : strlen(t);
    //  자리: 정수부 + 소수 f 자리
    char digs[400];
    size_t nd = 0;
    for (size_t i = 0; i < ip && nd < sizeof digs - 2; i++) digs[nd++] = t[i];
    for (int i = 0; i < f && dot; i++) digs[nd++] = dot[1 + i];
    int up = 0;
    if (dot) {
        const char *rest = dot + 1 + f;
        if (*rest >= '5') up = 1;   // 0.5 이상이면 올림 (정확히 0.5 도 올림)
    }
    //  올림 전파
    if (up) {
        size_t k = nd;
        while (k > 0) {
            if (digs[k - 1] == '9') { digs[k - 1] = '0'; k--; continue; }
            digs[k - 1]++;
            break;
        }
        if (k == 0) { memmove(digs + 1, digs, nd); digs[0] = '1'; nd++; ip++; }
    }
    //  -0.0 처럼 결과가 0 이면 JS 도 "-0.0" 이 아니라... toFixed 는 x<0 일 때 '-' 를 붙인다 (값이 0 으로 반올림돼도)
    if (neg) sb_putc(b, '-');
    sb_putn(b, digs, ip);
    if (f > 0) { sb_putc(b, '.'); sb_putn(b, digs + ip, (size_t)f); }
}
