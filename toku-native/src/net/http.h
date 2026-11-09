// HTTP(S) — WinHTTP (Windows 기본). 썸네일 업로드(POST)·창 그림 받기(GET)
#ifndef TOKU_HTTP_H
#define TOKU_HTTP_H
#include <stddef.h>
#include "../base/sbuf.h"

// headers: "이름: 값\r\n" 여러 줄 (NULL 가능). 응답 본문은 최대 1MB 까지 resp 에.
//  성공(응답을 받음)이면 1 과 *status, 실패(연결·시간 초과)면 0 과 err
int http_post(const char *url, const char *headers, const void *body, size_t len, int timeout_ms,
              int *status, sbuf *resp, char *err, size_t errn);

// GET — 본문이 max_bytes 를 넘으면 실패 ("too large"). 썸네일·Discord 에셋 받기용
int http_get(const char *url, const char *headers, int timeout_ms, size_t max_bytes,
             int *status, sbuf *resp, char *err, size_t errn);

// multipart/form-data 본문 만들기
typedef struct { sbuf body; char boundary[48]; } form_t;
void form_init(form_t *f);
void form_field(form_t *f, const char *name, const char *value);
void form_file(form_t *f, const char *name, const char *filename, const char *type, const void *data, size_t len);
void form_end(form_t *f);
void form_free(form_t *f);

#endif
