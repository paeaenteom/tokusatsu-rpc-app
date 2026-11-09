// Discord IPC 테스트용 콘솔 프로그램 — ipc-test.js 가 띄운다 (창 없음, 진짜 Discord 파이프 이름은 쓰지 않는다)
//  dipc_cli <파이프 접두어> <client_id>
//  표준 입력 명령: "REQ <cmd> <args JSON 또는 ->" · "RAW <op> <JSON>" · "QUIT"
//  출력: OPEN <번호> · FAIL <오류> · SENT <nonce> · RECV <op> <JSON> · DEAD · BYE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/base/sbuf.h"
#include "../src/base/utf.h"
#include "../src/discord/ipc.h"

static void on_frame(dipc *d, int op, const jval *msg, void *u) {
    char *s = json_stringify(msg);
    printf("RECV %d %s\n", op, s ? s : "?");
    fflush(stdout);
    free(s);
}

// 표준 입력에서 한 줄 (기다리지 않음). 줄이 완성되면 1
static int stdin_line(HANDLE in, sbuf *acc, sbuf *line) {
    DWORD avail = 0;
    if (PeekNamedPipe(in, NULL, 0, NULL, &avail, NULL) && avail) {
        char tmp[4096];
        DWORD got = 0;
        if (ReadFile(in, tmp, avail < sizeof tmp ? avail : sizeof tmp, &got, NULL)) sb_putn(acc, tmp, got);
    }
    for (size_t i = 0; i < acc->len; i++) {
        if (acc->p[i] == '\n') {
            sb_reset(line);
            sb_putn(line, acc->p, (i && acc->p[i - 1] == '\r') ? i - 1 : i);
            sb_consume(acc, i + 1);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    wchar_t *prefix = utf8_to_w(argv[1], strlen(argv[1]));
    int err = 0;
    dipc *d = dipc_open(prefix, argv[2], &err);
    free(prefix);
    if (!d) { printf("FAIL %d\n", err); return 1; }
    printf("OPEN %d\n", dipc_index(d));
    fflush(stdout);
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    sbuf acc, line;
    sb_init(&acc, 0);
    sb_init(&line, 0);
    int alive = 1;
    for (;;) {
        if (alive && dipc_poll(d, on_frame, NULL) < 0) { alive = 0; puts("DEAD"); fflush(stdout); }
        while (stdin_line(in, &acc, &line)) {
            char *l = line.p ? line.p : (char *)"";
            if (!strncmp(l, "QUIT", 4)) {
                if (alive) dipc_close(d); else dipc_close(d);
                puts("BYE");
                fflush(stdout);
                return 0;
            }
            if (!alive) continue;
            if (!strncmp(l, "REQ ", 4)) {
                char *cmd = l + 4, *sp = strchr(cmd, ' ');
                if (!sp) continue;
                *sp = '\0';
                jval *args = strcmp(sp + 1, "-") ? json_parse(sp + 1, strlen(sp + 1), NULL) : NULL;
                char nonce[37];
                if (dipc_request(d, cmd, args, nonce)) printf("SENT %s\n", nonce);
                else puts("SENDFAIL");
                fflush(stdout);
                json_free(args);
            } else if (!strncmp(l, "RAW ", 4)) {
                int op = atoi(l + 4);
                char *js = strchr(l + 4, ' ');
                if (js) dipc_send_json(d, op, js + 1, strlen(js + 1));
            }
        }
        Sleep(5);
    }
}
