// 시험용 할당기 — 해제한 메모리를 0xDD 로 덮고 돌려주지 않는다 (격리)
//  해제 후 읽기(use-after-free)가 있으면 그 값이 쓰레기로 바뀌어 JS 와의 교차 비교에서 바로 드러난다
//  core_cli 를 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free 로 묶을 때만 쓴다
#include <stddef.h>
#include <string.h>

void *__real_malloc(size_t n);
void __real_free(void *p);

#define HDR 16   // 크기 + 표지 (16바이트 정렬 유지)
#define MAGIC 0x5AFE5AFEu

typedef struct { size_t n; unsigned magic; unsigned live; } hdr_t;

void *__wrap_malloc(size_t n) {
    unsigned char *b = (unsigned char *)__real_malloc(n + HDR);
    if (!b) return NULL;
    hdr_t *h = (hdr_t *)b;
    h->n = n;
    h->magic = MAGIC;
    h->live = 1;
    memset(b + HDR, 0xCD, n);   // 초기화 안 한 값 읽기도 드러나게
    return b + HDR;
}

void *__wrap_calloc(size_t a, size_t b) {
    if (b && a > (size_t)-1 / b) return NULL;
    void *p = __wrap_malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}

void __wrap_free(void *p) {
    if (!p) return;
    hdr_t *h = (hdr_t *)((unsigned char *)p - HDR);
    if (h->magic != MAGIC) { __real_free(p); return; }   // 우리가 준 게 아니면 그냥 (CRT 내부 할당)
    if (!h->live) { *(volatile int *)0 = 0; }            // 두 번 해제 → 바로 죽인다
    h->live = 0;
    memset(p, 0xDD, h->n);
    //  격리: 돌려주지 않는다 (같은 자리에 새 값이 들어와 우연히 맞는 일을 막음)
}

void *__wrap_realloc(void *p, size_t n) {
    if (!p) return __wrap_malloc(n);
    hdr_t *h = (hdr_t *)((unsigned char *)p - HDR);
    void *q = __wrap_malloc(n);
    if (!q) return NULL;
    memcpy(q, p, h->n < n ? h->n : n);
    __wrap_free(p);
    return q;
}
