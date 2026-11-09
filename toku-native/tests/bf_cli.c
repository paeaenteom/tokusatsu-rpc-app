// 보충 거르기 교차 점검용 — 한 줄에 JSON 하나: {op:"resolved",msg} | {op:"link",s} | {op:"when",s,now}
//  답도 한 줄 JSON (tests/bf-diff.js 가 진짜 extension-bridge.js · rpc-window.js 와 비교)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/backfill.h"
#include "../src/base/json.h"
#include "../src/base/sbuf.h"

int main(void) {
    static char line[1 << 22];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        size_t ep;
        jval *m = json_parse(line, n, &ep);
        const char *op = json_get_str(m, "op", "");
        sbuf o; sb_init(&o, 0);
        if (!strcmp(op, "resolved")) {
            jval *r = bf_clean_resolved(json_get(m, "msg"));
            json_write(&o, r);
            json_free(r);
        } else if (!strcmp(op, "link")) {
            const jval *s = json_get(m, "s");
            sb_puts(&o, bf_link(s ? s->s : "", s ? s->slen : 0) ? "true" : "false");
        } else if (!strcmp(op, "thumb")) {
            const jval *s = json_get(m, "s");
            char type[16];
            size_t bn = 0;
            unsigned char *b = bf_thumb_bytes(s ? s->s : "", s ? s->slen : 0, type, &bn);
            if (!b) sb_puts(&o, "null");
            else {
                sb_printf(&o, "{\"type\":\"%s\",\"hex\":\"", type);
                for (size_t i = 0; i < bn; i++) sb_printf(&o, "%02x", b[i]);
                sb_puts(&o, "\"}");
            }
            free(b);
        } else if (!strcmp(op, "when")) {
            SYSTEMTIME t;
            int ok = bf_parse_when(json_get_str(m, "s", ""), &t);
            if (!ok) sb_puts(&o, "null");
            else {
                char a[40], b[40];
                bf_fmt_when(a, sizeof a, &t);
                bf_card_date(b, sizeof b, &t);
                sb_printf(&o, "{\"v\":\"%s\",\"card\":\"%s\",\"ok\":%s,\"epoch\":%.0f}", a, b, bf_when_ok(&t, json_get_num(m, "now", 0)) ? "true" : "false", bf_local_to_epoch(&t));
            }
        }
        fwrite(o.p ? o.p : "null", 1, o.p ? o.len : 4, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&o);
        json_free(m);
    }
    return 0;
}
