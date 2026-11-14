// hooks.c 교차 점검용 — 한 줄 명령(JSON) → 결과 한 줄: {"r":오류|null,"id":…,"list":[…],"folders":{…},"chars":{title:[…]}}
//  비밀 파일 = TOKU_NATIVE_TEST_SECRETS, 설정 = argv[1] 폴더
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/hooks.h"
#include "../src/app/settings.h"
#include "../src/core/rpc.h"

const char *app_lang(void) { return "ko"; }
core *app_core(void) { return NULL; }
void app_log(int lvl, const char *t) { (void)lvl; (void)t; }
void win_refresh(void) {}

static unsigned ctr;
static void rnd(unsigned char *o, int n) { ctr++; for (int i = 0; i < n; i++) o[i] = (unsigned char)((ctr * 29u + (unsigned)i * 7u) & 255); }

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    int copied;
    settings_init(argv[1], L"hktest", &copied);
    if (!getenv("HK_REAL_RNG")) hk_random = rnd;   //  HK_REAL_RNG = 진짜 시스템 난수 (무작위 분포 시험)
    static char line[1 << 21];
    while (fgets(line, sizeof line, stdin)) {
        jval *m = json_parse(line, strlen(line), NULL);
        const char *op = json_get_str(m, "op", "");
        const char *title = json_get_str(m, "title", ""), *id = json_get_str(m, "id", "");
        const char *r = NULL;
        char nid[16] = "";
        if (!strcmp(op, "add")) r = hk_add(json_get(m, "t"), json_get(m, "url"), json_get(m, "name"));
        else if (!strcmp(op, "rename")) r = hk_rename(id, json_get(m, "name"));
        else if (!strcmp(op, "remove")) r = hk_remove(id);
        else if (!strcmp(op, "icon")) r = hk_set_icon(title, json_get_str(m, "png", ""));
        else if (!strcmp(op, "iconClear")) hk_clear_icon(title);
        else if (!strcmp(op, "charAdd")) r = hk_char_add(title, json_get(m, "name"), nid);
        else if (!strcmp(op, "charRename")) r = hk_char_rename(title, id, json_get(m, "name"));
        else if (!strcmp(op, "charRemove")) r = hk_char_remove(title, id);
        else if (!strcmp(op, "charToggle")) r = hk_char_toggle(title, id, json_get_bool(m, "on", 0));
        else if (!strcmp(op, "charAll")) { int on2, n2; r = hk_char_set_all(title, (int)json_get_num(m, "mode", -1), &on2, &n2); }
        else if (!strcmp(op, "charUrl")) r = hk_char_avatar_url(title, id, json_get(m, "url"));
        else if (!strcmp(op, "folders")) {   //  설정에 쓰레기 값 직접 넣기
            const jval *f = json_get(m, "v");
            char *s = f ? json_stringify(f) : NULL;
            settings_set("hookFolders", NULL, s ? json_parse(s, strlen(s), NULL) : json_new(J_NULL));
            free(s);
        }
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "{\"r\":");
        if (r) json_write_str(&b, r, strlen(r)); else sb_puts(&b, "null");
        sb_puts(&b, ",\"id\":");
        if (*nid) json_write_str(&b, nid, strlen(nid)); else sb_puts(&b, "null");
        sb_puts(&b, ",\"list\":[");
        hk_item *l;
        int n = hk_list(&l);
        for (int i = 0; i < n; i++) {
            if (i) sb_putc(&b, ',');
            sb_puts(&b, "{\"id\":"); json_write_str(&b, l[i].id, strlen(l[i].id));
            sb_puts(&b, ",\"title\":"); json_write_str(&b, l[i].title, strlen(l[i].title));
            sb_puts(&b, ",\"name\":"); json_write_str(&b, l[i].name, strlen(l[i].name));
            sb_puts(&b, ",\"label\":"); json_write_str(&b, l[i].label, strlen(l[i].label));
            sb_putc(&b, '}');
        }
        sb_puts(&b, "],\"icons\":{");
        int first = 1;
        const jval *hf = settings_get("hookFolders", NULL);   //  hookIcons 는 폴더 순서
        for (size_t i = 0; hf && hf->t == J_OBJ && i < hf->count; i++) {
            const char *ic = hk_icon(hf->keys[i]);
            if (!ic) continue;
            if (!first) sb_putc(&b, ',');
            first = 0;
            json_write_str(&b, hf->keys[i], hf->klens[i]); sb_putc(&b, ':'); json_write_str(&b, ic, strlen(ic));
        }
        sb_puts(&b, "},\"chars\":{");
        first = 1;
        for (int i = 0; i < n; i++) {
            int dup = 0;
            for (int k = 0; k < i; k++) if (!strcmp(l[k].title, l[i].title)) dup = 1;
            if (dup) continue;
            hk_char *c;
            int cn = hk_chars(l[i].title, &c);
            if (!cn) { hk_chars_free(c, cn); continue; }
            if (!first) sb_putc(&b, ',');
            first = 0;
            json_write_str(&b, l[i].title, strlen(l[i].title));
            sb_puts(&b, ":[");
            for (int k = 0; k < cn; k++) {
                if (k) sb_putc(&b, ',');
                sb_puts(&b, "{\"id\":"); json_write_str(&b, c[k].id, strlen(c[k].id));
                sb_puts(&b, ",\"name\":"); json_write_str(&b, c[k].name, strlen(c[k].name));
                sb_puts(&b, ",\"avatar\":"); json_write_str(&b, c[k].avatar, strlen(c[k].avatar));
                sb_puts(&b, ",\"avatarUntil\":"); js_number_str(&b, c[k].until);
                sb_printf(&b, ",\"off\":%s}", c[k].off ? "true" : "false");
            }
            sb_putc(&b, ']');
            hk_chars_free(c, cn);
        }
        sb_puts(&b, "},\"folders\":");
        const jval *f = settings_get("hookFolders", NULL);
        if (f) json_write(&b, f); else sb_puts(&b, "null");
        sb_putc(&b, '}');
        hk_list_free(l, n);
        fwrite(b.p, 1, b.len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&b);
        json_free(m);
    }
    return 0;
}
