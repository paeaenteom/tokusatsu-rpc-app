// JS 동작 흉내 — discord-rpc.js·extension-bridge.js 를 C 로 옮길 때 계산 결과가 한 글자도 다르지 않게
//  값이 '없음(undefined)' 이면 jval* 가 NULL 이다
#ifndef TOKU_JS_H
#define TOKU_JS_H
#include <stddef.h>
#include "../base/json.h"
#include "../base/sbuf.h"

double js_round(double x);                    // Math.round (0.5 는 +∞ 쪽)
double js_to_number(const jval *v);           // Number(v)
int js_truthy(const jval *v);                 // !!v
void js_to_string(sbuf *b, const jval *v);    // String(v) / `${v}`
void js_join_item(sbuf *b, const jval *v);    // [..].join() 안의 한 칸 (undefined·null → 빈칸)
void js_to_fixed(sbuf *b, double x, int digits);   // Number.prototype.toFixed
void js_trim(const char *s, size_t len, size_t *start, size_t *end);   // String.prototype.trim (바이트 범위)
// str.slice(0, n) — UTF-16 단위. 이모지 가운데서 잘리면 JS 는 반쪽이 남는다 → UTF-8 로 쓸 때 Node 처럼 U+FFFD
void js_slice16(sbuf *b, const char *s, size_t len, size_t n);
// v || '' 를 문자열로 (문자열 필드용): 거짓 같은 값이면 빈 문자열, 아니면 String(v)
void js_str_or_empty(sbuf *b, const jval *v);

#endif
