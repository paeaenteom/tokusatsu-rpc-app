// SHA-1 · Base64 — WebSocket 연결 절차(Sec-WebSocket-Accept)와 업로드·이미지 전달에 쓴다
//  SHA-1 은 보안용이 아니라 WebSocket 규격(RFC 6455)이 정한 계산이라 쓴다
#ifndef TOKU_CRYPTO_H
#define TOKU_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
#include "sbuf.h"

void sha1(const void *data, size_t len, uint8_t out[20]);

void b64_encode(sbuf *out, const void *data, size_t len);
// 공백·줄바꿈 없는 표준 Base64 만 받는다 (= 채움 포함). 실패하면 0
int b64_decode(sbuf *out, const char *s, size_t len);

#endif
