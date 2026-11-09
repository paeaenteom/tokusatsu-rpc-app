// Discord IPC (이름 있는 파이프) — node_modules/discord-rpc 의 IPCTransport 와 같은 바이트를 주고받는다
//  틀: [op: int32 LE][len: int32 LE][JSON]. op 0 HANDSHAKE · 1 FRAME · 2 CLOSE · 3 PING · 4 PONG
//  · 연결: \\?\pipe\<prefix>0 ~ 9 를 차례로 (discord-rpc 와 같다). 진짜는 prefix "discord-ipc-", 테스트는 다른 이름
//  · 연결하자마자 HANDSHAKE {"v":1,"client_id":"…"} 를 보낸다
//  · PING 이 오면 같은 내용으로 PONG
//  · 읽기는 기다리지 않는다 — 겹친(overlapped) 읽기를 하나 걸어 두고, 데이터가 오면 dipc_event 가 켜진다
//    (메인 루프가 그 이벤트를 기다리다 바로 dipc_poll — 시간마다 들여다보지 않아 지연이 타이머 칸에 묶이지 않는다)
#ifndef TOKU_DIPC_H
#define TOKU_DIPC_H
#include <stddef.h>
#include <wchar.h>
#include "../base/json.h"

enum { DIPC_HANDSHAKE = 0, DIPC_FRAME = 1, DIPC_CLOSE = 2, DIPC_PING = 3, DIPC_PONG = 4 };
#define DIPC_MAX_FRAME (16u * 1024 * 1024)

typedef struct dipc dipc;
// msg 는 콜백 안에서만 쓸 수 있다 (끝나면 해제됨)
typedef void (*dipc_cb)(dipc *d, int op, const jval *msg, void *user);

// 실패하면 NULL (*err: Windows 오류 코드. 0~9 모두 없으면 ERROR_FILE_NOT_FOUND)
dipc *dipc_open(const wchar_t *prefix, const char *client_id, int *err);
// 들어온 프레임을 처리한다. 연결이 끊겼거나 규칙에 어긋난 프레임이면 -1 (그 뒤엔 dipc_close 만)
int dipc_poll(dipc *d, dipc_cb cb, void *user);
int dipc_send_json(dipc *d, int op, const char *json, size_t len);
// {"cmd":cmd,"args":args,"nonce":uuid} 를 FRAME 으로 보낸다 (args NULL 이면 키를 뺀다 — JS 의 undefined 처럼)
int dipc_request(dipc *d, const char *cmd, const jval *args, char nonce_out[37]);
// CLOSE {} 를 보내고 닫는다 (discord-rpc 의 transport.close 와 같게)
void dipc_close(dipc *d);
int dipc_index(const dipc *d);   // 붙은 파이프 번호 0~9
void *dipc_event(const dipc *d); // 읽을 것이 오면 켜지는 이벤트 (HANDLE) — 기다리기만, 닫지 말 것

void dipc_uuid(char out[37]);    // 무작위 nonce (RFC 4122 v4 모양)

#endif
