// binge.c · accent.c 교차 점검용 — 한 줄에 명령 하나(JSON), 결과 한 줄
//   {"op":"accent","px":[B,G,R,A,…]}                → {"c":정수|null}
//   {"op":"pick","series":"…","r":0.3}               → {"url":"…","title":"…"}   (비밀 파일 = TOKU_NATIVE_TEST_SECRETS)
//   {"op":"chars","folders":{…},"title":"…","fixed":{…}|null,"r":0.3} → {"f":{…}|null}
//  설정은 argv[1] 폴더(시험용)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/accent.h"
#include "../src/app/binge.h"
#include "../src/app/settings.h"
#include "../src/core/rpc.h"

//  binge.c 가 부르는 본체 함수들 — 시험에선 쓰지 않거나 흉내만
const char *app_lang(void) { return "ko"; }
core *app_core(void) { return NULL; }
void app_log(int lvl, const char *t) { (void)lvl; (void)t; }
void win_refresh(void) {}

static double R;
static double fixed_rnd(void) { return R; }

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    int copied;
    settings_init(argv[1], L"bingetest", &copied);
    binge_rnd = fixed_rnd;
    static char line[1 << 22];
    while (fgets(line, sizeof line, stdin)) {
        jval *m = json_parse(line, strlen(line), NULL);
        sbuf b; sb_init(&b, 0);
        const char *op = json_get_str(m, "op", "");
        R = json_get_num(m, "r", 0);
        if (!strcmp(op, "accent")) {
            const jval *px = json_get(m, "px");
            size_t n = px ? px->count : 0;
            unsigned char *p = (unsigned char *)malloc(n ? n : 1);
            for (size_t i = 0; i < n; i++) p[i] = (unsigned char)json_num(px->items[i], 0);
            int c = 0;
            if (accent_from_pixels(p, n / 4, &c)) sb_printf(&b, "{\"c\":%d}", c); else sb_puts(&b, "{\"c\":null}");
            free(p);
        } else if (!strcmp(op, "pick")) {
            char *u = NULL, *t = NULL;
            binge_pick_target(json_get_str(m, "series", ""), &u, &t);
            sb_puts(&b, "{\"url\":");
            json_write_str(&b, u, strlen(u));
            sb_puts(&b, ",\"title\":");
            json_write_str(&b, t, strlen(t));
            sb_putc(&b, '}');
            free(u); free(t);
        } else if (!strcmp(op, "chars")) {
            const jval *f = json_get(m, "folders");
            char *s = f ? json_stringify(f) : NULL;
            settings_set("hookFolders", NULL, s ? json_parse(s, strlen(s), NULL) : json_new(J_NULL));
            free(s);
            const jval *fx = json_get(m, "fixed");
            jval *r = binge_character_fields(json_get_str(m, "title", ""), (fx && fx->t != J_NULL) ? fx : NULL);
            sb_puts(&b, "{\"f\":");
            if (r) json_write(&b, r); else sb_puts(&b, "null");
            sb_putc(&b, '}');
            json_free(r);
        } else sb_puts(&b, "{\"bad\":1}");
        fwrite(b.p, 1, b.len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&b);
        json_free(m);
    }
    return 0;
}
