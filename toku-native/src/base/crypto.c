#include "crypto.h"
#include <string.h>

// ── SHA-1 (FIPS 180-4) ───────────────────────────────────────

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const void *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    const uint8_t *p = (const uint8_t *)data;
    size_t n = len;
    while (n >= 64) { sha1_block(h, p); p += 64; n -= 64; }
    uint8_t tail[128];
    memset(tail, 0, sizeof tail);
    if (n) memcpy(tail, p, n);
    tail[n] = 0x80;
    size_t tl = (n < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha1_block(h, tail);
    if (tl == 128) sha1_block(h, tail + 64);
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
}

// ── Base64 (RFC 4648) ────────────────────────────────────────

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void b64_encode(sbuf *out, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    if (!sb_reserve(out, (len + 2) / 3 * 4)) return;
    size_t i = 0;
    char q[4];
    for (; i + 3 <= len; i += 3) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
        q[0] = B64[v >> 18]; q[1] = B64[(v >> 12) & 63]; q[2] = B64[(v >> 6) & 63]; q[3] = B64[v & 63];
        sb_putn(out, q, 4);
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)p[i] << 16;
        q[0] = B64[v >> 18]; q[1] = B64[(v >> 12) & 63]; q[2] = '='; q[3] = '=';
        sb_putn(out, q, 4);
    } else if (len - i == 2) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
        q[0] = B64[v >> 18]; q[1] = B64[(v >> 12) & 63]; q[2] = B64[(v >> 6) & 63]; q[3] = '=';
        sb_putn(out, q, 4);
    }
}

static int b64v(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int b64_decode(sbuf *out, const char *s, size_t len) {
    if (len % 4) return 0;
    if (!sb_reserve(out, len / 4 * 3)) return 0;
    for (size_t i = 0; i < len; i += 4) {
        int a = b64v((unsigned char)s[i]), b = b64v((unsigned char)s[i + 1]);
        int last = (i + 4 == len);
        int c, d;
        if (a < 0 || b < 0) return 0;
        if (last && s[i + 2] == '=' && s[i + 3] == '=') {
            if (b & 15) return 0;   // 남는 비트가 0 이 아니면 정규 표현이 아니다
            char o = (char)((a << 2) | (b >> 4));
            sb_putn(out, &o, 1);
            break;
        }
        c = b64v((unsigned char)s[i + 2]);
        if (c < 0) return 0;
        if (last && s[i + 3] == '=') {
            if (c & 3) return 0;
            char o[2] = { (char)((a << 2) | (b >> 4)), (char)((b << 4) | (c >> 2)) };
            sb_putn(out, o, 2);
            break;
        }
        d = b64v((unsigned char)s[i + 3]);
        if (d < 0) return 0;
        char o[3] = { (char)((a << 2) | (b >> 4)), (char)((b << 4) | (c >> 2)), (char)((c << 6) | d) };
        sb_putn(out, o, 3);
    }
    return !out->err;
}
