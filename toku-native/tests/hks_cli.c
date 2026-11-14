//  웹훅 검색 시험 도구 — 한 줄에 JSON 하나
//  {"q":"히비키","hay":["仮面ライダー響鬼"]} → {"m":1}  ·  {"key":"..."} → {"key":"..."}  ·  {"read":"..."} → {"read":"..."}
//  {"fake":1} 이후로는 읽기 분석기 대신 '읽기 없음' (분석기가 없는 PC 흉내)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/hksearch.h"
#include "../src/base/json.h"
#include "../src/base/sbuf.h"

static const char *no_read(const char *s) { (void)s; return NULL; }

int main(void) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   //  앱 창 스레드와 같게
    static char line[1 << 16];
    while (fgets(line, sizeof line, stdin)) {
        jval *m = json_parse(line, strlen(line), NULL);
        sbuf b; sb_init(&b, 0);
        if (json_get(m, "fake")) { hks_read_hook = no_read; sb_puts(&b, "{}"); }
        else if (json_get(m, "key")) {
            char *k = hks_debug_key(json_get_str(m, "key", ""));
            sb_puts(&b, "{\"key\":");
            if (k) json_write_str(&b, k, strlen(k)); else sb_puts(&b, "null");
            sb_putc(&b, '}');
            free(k);
        } else if (json_get(m, "read")) {
            const char *r = hks_reading(json_get_str(m, "read", ""));
            sb_puts(&b, "{\"read\":");
            if (r) json_write_str(&b, r, strlen(r)); else sb_puts(&b, "null");
            sb_putc(&b, '}');
        } else {
            const jval *h = json_get(m, "hay");
            int n = (h && h->t == J_ARR) ? (int)h->count : 0;
            const char **hs = (const char **)calloc((size_t)(n ? n : 1), sizeof *hs);
            for (int i = 0; hs && i < n; i++) hs[i] = h->items[i]->t == J_STR ? h->items[i]->s : "";
            int r = hs ? hks_match(json_get_str(m, "q", ""), hs, n) : -1;
            free(hs);
            sb_printf(&b, "{\"m\":%d}", r);
        }
        fwrite(b.p, 1, b.len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&b);
        json_free(m);
    }
    return 0;
}
