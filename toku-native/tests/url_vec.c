// URL 버튼 주소 교차 검증 도구 — 줄 = 모드(yt|chzzk|danime) + ' ' + b64(주소) → b64(결과)
#include <stdio.h>
#include <string.h>
#include "../src/base/crypto.h"
#include "../src/base/sbuf.h"
#include "../src/core/url.h"

int main(void) {
    char line[1 << 16];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        char *sp = strchr(line, ' ');
        if (!sp) { puts("BAD"); continue; }
        *sp = 0;
        sbuf raw, out, enc;
        sb_init(&raw, 0); sb_init(&out, 0); sb_init(&enc, 0);
        if (!b64_decode(&raw, sp + 1, strlen(sp + 1))) { puts("BAD"); continue; }
        const char *r = raw.p ? raw.p : "";
        if (!strcmp(line, "yt")) yt_button_url(&out, r, raw.len, "https://www.youtube.com/");
        else if (!strcmp(line, "chzzk")) chzzk_button_url(&out, r, raw.len, "https://chzzk.naver.com/");
        else danime_button_url(&out, r, raw.len, "https://animestore.docomo.ne.jp/animestore/");
        b64_encode(&enc, out.p ? out.p : "", out.len);
        printf("%s\n", enc.p ? enc.p : "");
        fflush(stdout);
        sb_free(&raw); sb_free(&out); sb_free(&enc);
    }
    return 0;
}
