// bg.c 교차 점검용 — 한 줄 명령(JSON) → 결과 한 줄
//   {"op":"sniff","hex":"…"} → {"e":오류|null,"f":…,"w":…,"h":…}
//   {"op":"sanitize","v":…} / {"op":"validate","v":…} / {"op":"name","v":"…"}
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app/bg.h"
#include "../src/core/rpc.h"
#include "../src/base/sbuf.h"
#include "../src/base/utf.h"

const char *app_lang(void) { return "ko"; }
void app_log(int lvl, const char *t) { (void)lvl; (void)t; }
const wchar_t *app_data_dir(void) { return L"toku-rpc-native-bgtest"; }   //  흐름 시험 전용 폴더

static void put_set(sbuf *b, const bg_set *s) {
    sb_printf(b, "{\"enabled\":%s,\"image\":", s->enabled ? "true" : "false");
    json_write_str(b, s->image, strlen(s->image));
    sb_puts(b, ",\"name\":");
    json_write_str(b, s->name, strlen(s->name));
    sb_printf(b, ",\"width\":%d,\"height\":%d,\"fit\":\"%s\",\"posX\":%d,\"posY\":%d,\"zoom\":%d,\"opacity\":%d,\"dim\":%d,\"blur\":%d}",
              s->width, s->height, s->fit, s->posX, s->posY, s->zoom, s->opacity, s->dim, s->blur);
}

#include "../src/app/settings.h"
#include <shlobj.h>
static void put_dir(sbuf *b) {   //  관리 폴더의 파일 이름들
    wchar_t ad[MAX_PATH], pat[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad);
    _snwprintf(pat, MAX_PATH - 1, L"%ls\\toku-rpc-native-bgtest\\backgrounds\\*", ad);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    sb_putc(b, '[');
    int first = 1;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char *n = w_to_utf8(fd.cFileName, wcslen(fd.cFileName));
            if (!first) sb_putc(b, ',');
            first = 0;
            json_write_str(b, n, strlen(n));
            free(n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    sb_putc(b, ']');
}

int main(int argc, char **argv) {
    if (argc > 1) { wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, w, MAX_PATH); int c; settings_init(w, L"bgset", &c); }
    static char line[1 << 23];
    while (fgets(line, sizeof line, stdin)) {
        jval *m = json_parse(line, strlen(line), NULL);
        const char *op = json_get_str(m, "op", "");
        sbuf b; sb_init(&b, 0);
        if (!strcmp(op, "sniff")) {
            const char *hx = json_get_str(m, "hex", "");
            size_t n = strlen(hx) / 2;
            unsigned char *bytes = (unsigned char *)malloc(n ? n : 1);
            for (size_t i = 0; i < n; i++) { unsigned v; sscanf(hx + i * 2, "%2x", &v); bytes[i] = (unsigned char)v; }
            char f[8]; int w, h;
            const char *e = bg_sniff(bytes, n, f, &w, &h);
            if (e) { sb_puts(&b, "{\"e\":"); json_write_str(&b, e, strlen(e)); sb_putc(&b, '}'); }
            else sb_printf(&b, "{\"e\":null,\"f\":\"%s\",\"w\":%d,\"h\":%d}", f, w, h);
            free(bytes);
        } else if (!strcmp(op, "sanitize")) {
            bg_set s;
            bg_sanitize(json_get(m, "v"), &s);
            put_set(&b, &s);
        } else if (!strcmp(op, "validate")) {
            bg_set s;
            if (bg_validate(json_get(m, "v"), &s))
                sb_printf(&b, "{\"enabled\":%s,\"fit\":\"%s\",\"posX\":%d,\"posY\":%d,\"zoom\":%d,\"opacity\":%d,\"dim\":%d,\"blur\":%d}",
                          s.enabled ? "true" : "false", s.fit, s.posX, s.posY, s.zoom, s.opacity, s.dim, s.blur);
            else sb_puts(&b, "null");
        } else if (!strcmp(op, "pick") || !strcmp(op, "apply") || !strcmp(op, "discard") || !strcmp(op, "reset") || !strcmp(op, "get") || !strcmp(op, "cleanup")) {
            bg_set st; const char *e = NULL; char tok[24] = "", nm[512] = "", fm[8] = ""; int w = 0, h = 0, okd = -1;
            memset(&st, 0, sizeof st);
            if (!strcmp(op, "pick")) { wchar_t p[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, json_get_str(m, "path", ""), -1, p, MAX_PATH); e = bg_pick(p, tok, nm, &w, &h, fm); }
            else if (!strcmp(op, "apply")) { bg_set o; const jval *tk = json_get(m, "token"); if (!bg_validate(json_get(m, "v"), &o)) e = "invalid"; else e = bg_apply(&o, (tk && tk->t == J_STR) ? tk->s : NULL, &st); }
            else if (!strcmp(op, "discard")) { const jval *tk = json_get(m, "token"); okd = bg_discard((tk && tk->t == J_STR) ? tk->s : NULL); }
            else if (!strcmp(op, "reset")) e = bg_reset(&st);
            else if (!strcmp(op, "get")) e = bg_get(&st);
            else bg_cleanup();
            bg_set saved; bg_load(&saved);
            sb_puts(&b, "{\"e\":"); if (e) json_write_str(&b, e, strlen(e)); else sb_puts(&b, "null");
            sb_printf(&b, ",\"token\":\"%s\",\"name\":", tok); json_write_str(&b, nm, strlen(nm));
            sb_printf(&b, ",\"w\":%d,\"h\":%d,\"fmt\":\"%s\",\"d\":%d,\"saved\":", w, h, fm, okd); put_set(&b, &saved);
            sb_puts(&b, ",\"files\":"); put_dir(&b); sb_putc(&b, '}');
        } else if (!strcmp(op, "name")) {
            char *c = bg_clean_name(json_get_str(m, "v", ""));
            json_write_str(&b, c, strlen(c));
            free(c);
        }
        fwrite(b.p, 1, b.len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        sb_free(&b);
        json_free(m);
    }
    return 0;
}
