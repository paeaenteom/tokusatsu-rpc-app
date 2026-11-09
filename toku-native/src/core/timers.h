// 타이머 (setTimeout · setInterval 흉내) — 한 스레드에서 돈다
//  · 기다리는 시간이 1 보다 작거나 너무 크면 1ms (Node 와 같다)
//  · 같은 시각이면 먼저 건 것부터 (시각, 건 순서)
//  · 반복 타이머는 콜백이 끝난 뒤 '지금 + 간격' 으로 다시 건다 (콜백 안에서 지웠으면 안 건다)
#ifndef TOKU_TIMERS_H
#define TOKU_TIMERS_H
#include <stddef.h>
#include <stdint.h>

typedef void (*tm_fn)(void *ctx, int id);

typedef struct {
    int64_t at;
    uint64_t seq;
    int id;
    int64_t every;   // 0 = 한 번
    tm_fn fn;
    void *ctx;
} tm_entry;

typedef struct {
    tm_entry *v;
    size_t n, cap;
    uint64_t seq;
    int next_id;
} tm_queue;

void tm_init(tm_queue *q);
void tm_free(tm_queue *q);
int tm_set(tm_queue *q, int64_t now, int64_t delay, int repeat, tm_fn fn, void *ctx);   // 0 = 실패
void tm_clear(tm_queue *q, int id);
int tm_alive(const tm_queue *q, int id);
int64_t tm_next(const tm_queue *q);              // 가장 이른 시각, 없으면 -1
int tm_fire_one(tm_queue *q, int64_t now);       // 그 시각이 됐으면 하나 실행하고 1

#endif
