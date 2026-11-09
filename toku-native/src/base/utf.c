#include "utf.h"
#include <stdlib.h>
#include <windows.h>

// 한 글자의 바이트 수와 UTF-16 단위 수. 깨진 바이트는 1바이트 = 1단위로 본다
static size_t step(const unsigned char *s, size_t avail, int *units) {
    unsigned char c = s[0];
    size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (n > avail) n = 1;
    for (size_t k = 1; k < n; k++)
        if ((s[k] & 0xC0) != 0x80) { n = 1; break; }
    *units = (n == 4) ? 2 : 1;
    return n;
}

int utf8_valid(const char *s, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        unsigned char c = p[i];
        if (c < 0x80) { i++; continue; }
        size_t n;
        unsigned int cp;
        if (c >= 0xC2 && c <= 0xDF) { n = 2; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 3; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 4; cp = c & 0x07; }
        else return 0;
        if (len - i < n) return 0;
        for (size_t k = 1; k < n; k++) {
            if ((p[i + k] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if (n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
        if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
        i += n;
    }
    return 1;
}

size_t utf16_len(const char *s, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0, u = 0;
    while (i < len) {
        int du;
        i += step(p + i, len - i, &du);
        u += (size_t)du;
    }
    return u;
}

void utf8_clamp(sbuf *out, const char *s, size_t len, size_t max) {
    if (utf16_len(s, len) <= max || max == 0) { sb_putn(out, s, len); return; }
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0, u = 0;
    while (i < len) {
        int du;
        size_t n = step(p + i, len - i, &du);
        if (u + (size_t)du > max - 1) break;   // 이모지(2단위)가 경계에 걸리면 통째로 뺀다 (JS 가 반쪽을 지우는 것과 같은 결과)
        u += (size_t)du;
        i += n;
    }
    sb_putn(out, s, i);
    sb_puts(out, "\xE2\x80\xA6");   // …
}

wchar_t *utf8_to_w(const char *s, size_t len) {
    if (len > 0x7FFFFFFF) return NULL;
    int n = len ? MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0) : 0;
    if (len && n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    if (n) MultiByteToWideChar(CP_UTF8, 0, s, (int)len, w, n);
    w[n] = 0;
    return w;
}

char *w_to_utf8(const wchar_t *w, size_t wlen) {
    if (wlen > 0x7FFFFFFF) return NULL;
    int n = wlen ? WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, NULL, 0, NULL, NULL) : 0;
    if (wlen && n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) return NULL;
    if (n) WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, s, n, NULL, NULL);
    s[n] = '\0';
    return s;
}
