// WebSocket 서버 테스트용 콘솔 프로그램 — ws-test.js 가 띄우고 붙어 본다 (창 없음)
//  ws_srv <포트>    받은 텍스트는 그대로 되돌려 보낸다. 사건은 표준 출력에 한 줄씩:
//    OPEN id · MSG id len bin · CLOSE id code · PONG id · REJECT status why
//  특별 명령 (텍스트 그대로): "!ping" 서버가 ping · "!close 4001" 닫기 · "!big N" N 바이트 보내기 · "!quit" 끝
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/net/ws.h"

static int quit;

static void on_open(ws_server *s, ws_client *c, void *u) { printf("OPEN %d\n", ws_id(c)); fflush(stdout); }
static void on_close(ws_server *s, ws_client *c, int code, void *u) { printf("CLOSE %d %d\n", ws_id(c), code); fflush(stdout); }
static void on_pong(ws_server *s, ws_client *c, void *u) { printf("PONG %d\n", ws_id(c)); fflush(stdout); }
static void on_reject(ws_server *s, int st, const char *why, void *u) { printf("REJECT %d %s\n", st, why); fflush(stdout); }

static void on_message(ws_server *s, ws_client *c, const char *d, size_t n, int bin, void *u) {
    printf("MSG %d %zu %d\n", ws_id(c), n, bin);
    fflush(stdout);
    if (n == 5 && !memcmp(d, "!ping", 5)) { ws_ping(c); return; }
    if (n > 7 && !memcmp(d, "!close ", 7)) { ws_close(c, atoi(d + 7)); return; }
    if (n > 5 && !memcmp(d, "!big ", 5)) {
        size_t k = (size_t)strtoull(d + 5, NULL, 10);
        char *b = (char *)malloc(k ? k : 1);
        if (!b) return;
        memset(b, 'a', k);
        ws_send_text(c, b, k);
        free(b);
        return;
    }
    if (n == 5 && !memcmp(d, "!quit", 5)) { quit = 1; return; }
    if (bin) ws_send_binary(c, d, n);
    else ws_send_text(c, d, n);
}

int main(int argc, char **argv) {
    unsigned short port = (unsigned short)(argc > 1 ? atoi(argv[1]) : 17695);
    ws_handlers h = { NULL, on_open, on_message, on_close, on_pong, on_reject };
    int err = 0;
    ws_server *s = ws_listen(port, &h, 16u * 1024 * 1024, &err);
    if (!s) { printf("LISTEN_FAIL %d\n", err); return 1; }
    printf("READY %u\n", port);
    fflush(stdout);
    while (!quit) ws_poll(s, 50);
    ws_server_close(s);
    printf("BYE\n");
    return 0;
}
