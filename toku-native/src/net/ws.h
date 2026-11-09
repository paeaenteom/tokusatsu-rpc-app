// WebSocket 서버 (RFC 6455) — 크롬 확장이 붙는 곳. 지금 앱의 ws 8 + extension-bridge.js 설정과 같게 동작한다
//  · 127.0.0.1 만, 다른 프로그램이 같은 포트를 가로채지 못하게(SO_EXCLUSIVEADDRUSE)
//  · Origin 이 없거나 chrome-extension://[a-p]{32} 일 때만 받는다 (아니면 401)
//  · 압축(permessage-deflate) 안 함, 클라이언트 프레임은 마스크 필수, 메시지 최대 max_payload
//  · 한 스레드에서 ws_poll() 을 불러 돌린다 (콜백도 그 안에서 불린다)
#ifndef TOKU_WS_H
#define TOKU_WS_H
#include <stddef.h>

typedef struct ws_server ws_server;
typedef struct ws_client ws_client;

typedef struct {
    void *user;
    void (*on_open)(ws_server *s, ws_client *c, void *user);
    void (*on_message)(ws_server *s, ws_client *c, const char *data, size_t len, int binary, void *user);
    void (*on_close)(ws_server *s, ws_client *c, int code, void *user);   // 열린 연결이 끝날 때만
    void (*on_pong)(ws_server *s, ws_client *c, void *user);
    void (*on_reject)(ws_server *s, int status, const char *why, void *user);   // 연결 절차에서 거절 (로그용)
} ws_handlers;

// 실패하면 NULL, *err 에 WSA 오류 (WSAEADDRINUSE = 포트 사용 중)
ws_server *ws_listen(unsigned short port, const ws_handlers *h, size_t max_payload, int *err);
// timeout_ms 만큼 기다리며 들어온 것을 처리한다. 0 이면 기다리지 않음
void ws_poll(ws_server *s, int timeout_ms);
void ws_server_close(ws_server *s);   // 모든 연결을 닫고 해제

int ws_send_text(ws_client *c, const char *data, size_t len);
int ws_send_binary(ws_client *c, const void *data, size_t len);
int ws_ping(ws_client *c);
void ws_close(ws_client *c, int code);   // 닫기 프레임을 보내고 끊는다
size_t ws_count(const ws_server *s);      // 연결 절차가 끝난(열린) 연결 수
void *ws_event(const ws_server *s);       // 연결·받을 것·닫힘이 오면 켜지는 이벤트 (HANDLE, NULL = 없음) — 기다리기만
ws_client *ws_at(ws_server *s, size_t i); // 열린 연결 i 번째 (없으면 NULL)
int ws_id(const ws_client *c);

#endif
