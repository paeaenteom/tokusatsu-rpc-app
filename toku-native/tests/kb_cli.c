// keybinds.c 교차 점검용 — 한 줄에 명령 하나(JSON), 결과 한 줄(JSON): {"r":{ok,error,id,moved},"p":KEYBINDS}
//  저장은 argv[1] 폴더(시험용)의 config.json
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/keybinds.h"
#include "../src/app/settings.h"

static unsigned ctr;
static void rnd5(unsigned char o[5]) { ctr++; for (int i = 0; i < 5; i++) o[i] = (unsigned char)((ctr * 37u + (unsigned)i * 11u) & 255); }

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    int copied;
    settings_init(argv[1], L"kbtest", &copied);
    kb_random5 = rnd5;
    static char line[1 << 20];
    while (fgets(line, sizeof line, stdin)) {
        jval *m = json_parse(line, strlen(line), NULL);
        if (!m) { puts("{\"bad\":1}"); fflush(stdout); continue; }
        const char *op = json_get_str(m, "op", "");
        const char *site = json_get_str(m, "site", "");
        const char *id = json_get_str(m, "id", "");
        kb_res r;
        memset(&r, 0, sizeof r);
        if (!strcmp(op, "add")) r = kb_add(site, json_get(m, "target"), json_get(m, "label"));
        else if (!strcmp(op, "setTarget")) r = kb_set_target(site, id, json_get(m, "target"));
        else if (!strcmp(op, "setKey")) r = kb_set_key(site, id, json_get(m, "combo"));
        else if (!strcmp(op, "rename")) r = kb_rename(site, id, json_get(m, "label"));
        else if (!strcmp(op, "remove")) r = kb_remove(site, id);
        else if (!strcmp(op, "siteOn")) r = kb_set_site_on(site, json_get_bool(m, "on", 0));
        else if (!strcmp(op, "markFs")) r = kb_mark_fs(site, id);
        else if (!strcmp(op, "adopt")) { r.ok = kb_adopt_local(json_get(m, "kb"), json_get(m, "on")); }
        else if (!strcmp(op, "raw")) {   // 저장소에 쓰레기 값을 직접 넣기 (거르기 점검)
            const jval *v = json_get(m, "v");
            char *s = v ? json_stringify(v) : NULL;
            settings_set("keybinds", NULL, s ? json_parse(s, strlen(s), NULL) : json_new(J_NULL));
            free(s);
            const jval *o = json_get(m, "on");
            if (o) { s = json_stringify(o); settings_set("keybindsOn", NULL, json_parse(s, strlen(s), NULL)); free(s); }
            r.ok = 1;
        }
        sbuf b; sb_init(&b, 0);
        sb_printf(&b, "{\"r\":{\"ok\":%s,\"error\":", r.ok ? "true" : "false");
        if (r.error) json_write_str(&b, r.error, strlen(r.error)); else sb_puts(&b, "null");
        sb_puts(&b, ",\"id\":");
        if (*r.id) json_write_str(&b, r.id, strlen(r.id)); else sb_puts(&b, "null");
        sb_puts(&b, ",\"moved\":");
        if (r.has_moved) json_write_str(&b, r.moved, strlen(r.moved)); else sb_puts(&b, "null");
        sb_puts(&b, "},\"p\":");
        kb_payload(&b);
        sb_putc(&b, '}');
        fwrite(b.p, 1, b.len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&b);
        json_free(m);
    }
    return 0;
}
