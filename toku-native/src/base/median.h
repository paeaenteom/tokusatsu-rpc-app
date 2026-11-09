// 최근 값 5개의 중간값 — 트레이 메뉴 지연(ms) 표시가 한두 번 튄 값(크롬·Discord 쪽 순간 지연)에 흔들리지 않게
//  표시용만. 엔진의 다른 계산에는 쓰지 않는다
#ifndef TOKU_MEDIAN_H
#define TOKU_MEDIAN_H

#define MED_N 5
typedef struct { double v[MED_N], at[MED_N]; int n, i; } med5;

static inline void med5_push(med5 *m, double v, double at) {
    m->v[m->i] = v;
    m->at[m->i] = at;
    m->i = (m->i + 1) % MED_N;
    if (m->n < MED_N) m->n++;
}
static inline void med5_clear(med5 *m) { m->n = m->i = 0; }
//  max_age 안의 값만 (max_age <= 0 이면 나이 상관없이). 없으면 fallback. 짝수 개면 가운데 둘의 평균
static inline double med5_get(const med5 *m, double now, double max_age, double fallback) {
    double s[MED_N];
    int k = 0;
    for (int j = 0; j < m->n; j++) if (max_age <= 0 || now - m->at[j] <= max_age) s[k++] = m->v[j];
    if (!k) return fallback;
    for (int a = 1; a < k; a++) { double x = s[a]; int b = a - 1; while (b >= 0 && s[b] > x) { s[b + 1] = s[b]; b--; } s[b + 1] = x; }
    return k % 2 ? s[k / 2] : (s[k / 2 - 1] + s[k / 2]) / 2;
}

#endif
