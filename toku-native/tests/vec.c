// 교차 검증용 도구 — 줄마다 Base64 입력을 받아 결과를 Base64 로 낸다 (cross.js 가 JS 결과와 비교)
//  vec json  : 줄 = b64(입력)            → "OK b64(JSON 다시 쓰기)" 또는 "ERR"
//  vec clamp : 줄 = max + ' ' + b64(문자열) → b64(자른 결과) + ' ' + UTF-16 길이
//  vec sha1  : 줄 = b64(데이터)          → 16진수 40자
//  vec b64   : 줄 = b64(데이터)          → b64 다시 인코딩한 것 (디코드→인코드 왕복) 또는 "ERR"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/base/crypto.h"
#include "../src/base/json.h"
#include "../src/base/sbuf.h"
#include "../src/base/utf.h"

static int read_line(FILE *f, sbuf *line) {
    sb_reset(line);
    int c, any = 0;
    while ((c = fgetc(f)) != EOF) {
        any = 1;
        if (c == '\n') break;
        if (c != '\r') sb_putc(line, (char)c);
    }
    return any;
}

static void out_b64(const char *p, size_t n) {
    sbuf o;
    sb_init(&o, 0);
    b64_encode(&o, p, n);
    fwrite(o.p ? o.p : "", 1, o.len, stdout);
    sb_free(&o);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    const char *mode = argv[1];
    sbuf line, raw;
    sb_init(&line, 0);
    sb_init(&raw, 0);
    while (read_line(stdin, &line)) {
        sb_reset(&raw);
        const char *data = line.p ? line.p : "";
        size_t dlen = line.len;
        size_t mx = 0;
        if (!strcmp(mode, "clamp")) {
            char *sp = strchr(data, ' ');
            if (!sp) { puts("BAD"); continue; }
            mx = (size_t)strtoul(data, NULL, 10);
            dlen -= (size_t)(sp + 1 - data);
            data = sp + 1;
        }
        if (!b64_decode(&raw, data, dlen)) {
            if (!strcmp(mode, "b64")) { puts("ERR"); continue; }
            puts("BAD");
            continue;
        }
        const char *rp = raw.p ? raw.p : "";
        if (!strcmp(mode, "json")) {
            jval *v = json_parse(rp, raw.len, NULL);
            if (!v) { puts("ERR"); continue; }
            char *s = json_stringify(v);
            json_free(v);
            if (!s) { puts("OOM"); continue; }
            fputs("OK ", stdout);
            out_b64(s, strlen(s));
            putchar('\n');
            free(s);
        } else if (!strcmp(mode, "clamp")) {
            sbuf o;
            sb_init(&o, 0);
            utf8_clamp(&o, rp, raw.len, mx);
            out_b64(o.p ? o.p : "", o.len);
            printf(" %zu\n", utf16_len(o.p ? o.p : "", o.len));
            sb_free(&o);
        } else if (!strcmp(mode, "sha1")) {
            uint8_t h[20];
            sha1(rp, raw.len, h);
            for (int i = 0; i < 20; i++) printf("%02x", h[i]);
            putchar('\n');
        } else if (!strcmp(mode, "b64")) {
            out_b64(rp, raw.len);
            putchar('\n');
        }
        fflush(stdout);
    }
    sb_free(&line);
    sb_free(&raw);
    return 0;
}
