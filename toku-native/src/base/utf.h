// UTF-8 도우미 — Discord 길이 제한(UTF-16 단위)과 Windows API(UTF-16) 사이 변환
#ifndef TOKU_UTF_H
#define TOKU_UTF_H
#include <stddef.h>
#include <wchar.h>
#include "sbuf.h"

// 엄격한 UTF-8 검사 (긴 표현·서로게이트·U+10FFFF 초과·잘린 글자 거절) — WebSocket 텍스트 프레임 규칙(RFC 6455 8.1)
int utf8_valid(const char *s, size_t len);

// UTF-8 문자열을 JS 의 str.length 처럼 UTF-16 단위로 센다
size_t utf16_len(const char *s, size_t len);

// discord-rpc.js clamp() 와 같다: UTF-16 으로 max 이하면 그대로, 넘으면 max-1 단위까지(서로게이트 반쪽 없이) + '…'
void utf8_clamp(sbuf *out, const char *s, size_t len, size_t max);

// Windows API 용 변환 (malloc, 실패하면 NULL). 잘못된 바이트는 U+FFFD 로
wchar_t *utf8_to_w(const char *s, size_t len);
char *w_to_utf8(const wchar_t *w, size_t wlen);

#endif
