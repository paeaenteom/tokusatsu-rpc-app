// JSON 읽기·쓰기 (RFC 8259)
//  · 읽기: 밖에서 온 입력(확장 메시지·Discord 응답·설정 파일)이라 엄격하게 본다 — 깨진 문법·제어 문자·잘못된 UTF-8·
//    깊이 초과(64)는 전부 NULL (부분 결과를 쓰지 않는다). 단 \u 로 적힌 짝 없는 서로게이트는 U+FFFD 로 바꿔 받는다
//    (JS 문자열엔 있을 수 있어서 — 제목 한 글자 때문에 메시지 전체를 버리지 않게)
//  · 문자열은 UTF-8, '\0' 을 포함할 수 있어 길이를 같이 들고 다닌다
//  · 같은 키가 여러 번 나오면 마지막 값을 쓴다 (JS 의 JSON.parse 와 같게)
#ifndef TOKU_JSON_H
#define TOKU_JSON_H
#include <stddef.h>
#include "sbuf.h"

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval jval;
struct jval {
    jtype t;
    int b;          // J_BOOL
    double n;       // J_NUM
    char *s;        // J_STR (UTF-8, '\0' 로 끝남)
    size_t slen;
    jval **items;   // J_ARR 값 / J_OBJ 값
    char **keys;    // J_OBJ 키 (UTF-8)
    size_t *klens;
    size_t count;
    size_t cap;
};

#define JSON_MAX_DEPTH 64

// 실패하면 NULL. err 가 있으면 실패한 바이트 위치를 넣는다
jval *json_parse(const char *s, size_t len, size_t *err_pos);
void json_free(jval *v);

// 읽기 도우미 (없거나 형이 다르면 기본값)
const jval *json_get(const jval *obj, const char *key);
const char *json_str(const jval *v, const char *dflt);
double json_num(const jval *v, double dflt);
int json_bool(const jval *v, int dflt);
const char *json_get_str(const jval *obj, const char *key, const char *dflt);
double json_get_num(const jval *obj, const char *key, double dflt);
int json_get_bool(const jval *obj, const char *key, int dflt);

// 만들기
jval *json_new(jtype t);
jval *json_new_str(const char *s);
jval *json_new_strn(const char *s, size_t n);
jval *json_new_num(double n);
jval *json_new_bool(int b);
int json_arr_push(jval *arr, jval *v);              // 실패하면 v 도 해제하고 0
int json_obj_set(jval *obj, const char *key, jval *v);   // 같은 키가 있으면 바꾼다. 실패하면 v 해제하고 0

// 쓰기 (한 줄, 공백 없음 — JSON.stringify 와 같은 모양)
void json_write(sbuf *b, const jval *v);
void json_write_str(sbuf *b, const char *s, size_t len);
// JS 의 String(숫자) 와 같은 글자 (NaN·Infinity 는 "NaN"·"Infinity" — JSON 쓰기에선 null)
void js_number_str(sbuf *b, double v);
char *json_stringify(const jval *v);   // malloc, 실패하면 NULL

#endif
