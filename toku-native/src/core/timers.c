#include "timers.h"
#include <stdlib.h>
#include <string.h>

#define TIMEOUT_MAX 2147483647LL

void tm_init(tm_queue *q) { memset(q, 0, sizeof *q); }
void tm_free(tm_queue *q) { free(q->v); memset(q, 0, sizeof *q); }

static int64_t norm(int64_t d) { return (d >= 1 && d <= TIMEOUT_MAX) ? d : 1; }

int tm_set(tm_queue *q, int64_t now, int64_t delay, int repeat, tm_fn fn, void *ctx) {
    if (q->n == q->cap) {
        size_t cap = q->cap ? q->cap * 2 : 16;
        tm_entry *nv = (tm_entry *)realloc(q->v, cap * sizeof *nv);
        if (!nv) return 0;
        q->v = nv;
        q->cap = cap;
    }
    int64_t d = norm(delay);
    tm_entry *e = &q->v[q->n++];
    e->at = now + d;
    e->seq = ++q->seq;
    e->id = ++q->next_id;
    if (q->next_id >= 0x7ffffff0) q->next_id = 0;   // 실제로는 닿지 않는다
    e->every = repeat ? d : 0;
    e->fn = fn;
    e->ctx = ctx;
    return e->id;
}

static long find(const tm_queue *q, int id) {
    for (size_t i = 0; i < q->n; i++) if (q->v[i].id == id) return (long)i;
    return -1;
}

void tm_clear(tm_queue *q, int id) {
    if (!id) return;
    long i = find(q, id);
    if (i < 0) return;
    q->v[i] = q->v[q->n - 1];
    q->n--;
}

int tm_alive(const tm_queue *q, int id) { return id && find(q, id) >= 0; }

static long earliest(const tm_queue *q) {
    long best = -1;
    for (size_t i = 0; i < q->n; i++) {
        const tm_entry *e = &q->v[i];
        if (best < 0 || e->at < q->v[best].at || (e->at == q->v[best].at && e->seq < q->v[best].seq)) best = (long)i;
    }
    return best;
}

int64_t tm_next(const tm_queue *q) {
    long i = earliest(q);
    return i < 0 ? -1 : q->v[i].at;
}

int tm_fire_one(tm_queue *q, int64_t now) {
    long i = earliest(q);
    if (i < 0 || q->v[i].at > now) return 0;
    tm_entry e = q->v[i];
    if (!e.every) tm_clear(q, e.id);       // 한 번짜리는 실행 전에 뺀다
    e.fn(e.ctx, e.id);
    if (e.every) {
        long k = find(q, e.id);            // 콜백 안에서 지웠으면 다시 걸지 않는다
        if (k >= 0) { q->v[k].at = now + e.every; q->v[k].seq = ++q->seq; }
    }
    return 1;
}
