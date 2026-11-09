#include "sbuf.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(sbuf *b, size_t max) {
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
    b->max = max;
    b->err = 0;
}

void sb_free(sbuf *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

void sb_reset(sbuf *b) {
    b->len = 0;
    b->err = 0;
    if (b->p) b->p[0] = '\0';
}

int sb_reserve(sbuf *b, size_t extra) {
    if (b->err) return 0;
    if (extra > (size_t)-1 - b->len - 1) { b->err = 1; return 0; }
    size_t need = b->len + extra + 1;
    if (b->max && need - 1 > b->max) { b->err = 1; return 0; }
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) { cap = need; break; }
        cap *= 2;
    }
    char *np = (char *)realloc(b->p, cap);
    if (!np) { b->err = 1; return 0; }
    b->p = np;
    b->cap = cap;
    return 1;
}

void sb_putn(sbuf *b, const void *s, size_t n) {
    if (!sb_reserve(b, n)) return;
    if (n) memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void sb_puts(sbuf *b, const char *s) {
    sb_putn(b, s, strlen(s));
}

void sb_putc(sbuf *b, char c) {
    sb_putn(b, &c, 1);
}

void sb_printf(sbuf *b, const char *fmt, ...) {
    if (b->err) return;
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { b->err = 1; va_end(ap2); return; }
    if (!sb_reserve(b, (size_t)n)) { va_end(ap2); return; }
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

void sb_consume(sbuf *b, size_t n) {
    if (n >= b->len) { b->len = 0; if (b->p) b->p[0] = '\0'; return; }
    memmove(b->p, b->p + n, b->len - n);
    b->len -= n;
    b->p[b->len] = '\0';
}

char *sb_detach(sbuf *b) {
    char *p = b->p;
    if (!p) {
        p = (char *)malloc(1);
        if (p) p[0] = '\0';
    }
    b->p = NULL;
    b->len = b->cap = 0;
    return p;
}
