// 늘어나는 바이트 버퍼 — 고정 크기 배열에 복사하지 않기 위해 모든 문자열 조립은 이걸 쓴다
//  · 할당에 실패하면 err=1 로 남고, 그 뒤 쓰기는 아무것도 하지 않는다 (호출부는 마지막에 err 만 보면 된다)
//  · p 는 늘 '\0' 으로 끝난다 (len 은 '\0' 제외)
#ifndef TOKU_SBUF_H
#define TOKU_SBUF_H
#include <stddef.h>

typedef struct {
    char *p;
    size_t len;
    size_t cap;
    size_t max;   // 0 = 제한 없음. 넘으면 err=1 (밖에서 온 입력이 메모리를 끝없이 키우지 못하게)
    int err;
} sbuf;

void sb_init(sbuf *b, size_t max);
void sb_free(sbuf *b);
void sb_reset(sbuf *b);
int  sb_reserve(sbuf *b, size_t extra);
void sb_putn(sbuf *b, const void *s, size_t n);
void sb_puts(sbuf *b, const char *s);
void sb_putc(sbuf *b, char c);
void sb_printf(sbuf *b, const char *fmt, ...);
// 앞에서 n 바이트를 버린다 (받은 데이터를 처리한 만큼 지울 때)
void sb_consume(sbuf *b, size_t n);
// 버퍼를 넘겨받는다 (호출부가 free). 비어 있으면 빈 문자열을 새로 만든다
char *sb_detach(sbuf *b);

#endif
