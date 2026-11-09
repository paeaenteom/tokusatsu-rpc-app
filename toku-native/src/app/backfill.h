// 보충 기록 (창 '보충' 탭) — 지금 앱 extension-bridge.js _onResolved · main.js backfill-* · rpc-window.js BF_LINK 를 그대로
//  모바일로 본 화는 확장이 못 봐 기록이 빠진다 → 링크를 확장이 읽어 작품·화·썸네일을 주고, 본 시각을 정해 나중에 보낸다
#ifndef TOKU_BACKFILL_H
#define TOKU_BACKFILL_H
#include <windows.h>
#include "../base/json.h"

//  확장의 RESOLVED 값 거르기 (모양·길이) → { ok:true, site, url, seriesName, episodeNumber, episodeTitle, thumbDataUrl }
//  또는 { ok:false, error } (badurl|blocked|empty|fetch) — json_free
jval *bf_clean_resolved(const jval *msg);
int bf_link(const char *s, size_t n);   // BF_LINK — 붙여 넣으면 바로 불러올 링크인가

//  본 시각 — 창에서는 'YYYY-MM-DD HH:MM' 글 칸 (지금 앱은 datetime-local)
int bf_parse_when(const char *s, SYSTEMTIME *out);           // 1 = 날짜로 읽힘 (없는 날짜·시각이면 0)
void bf_fmt_when(char *out, size_t n, const SYSTEMTIME *t);  // 칸에 넣는 모양
void bf_card_date(char *out, size_t n, const SYSTEMTIME *t); // 미리보기 카드 날짜 줄 '2026. 11. 08. 18:49'
//  보낼 수 있는 시각인가 (main.js: 2000년 이후, 지금 + 10분 까지). now_ms = 지금 (epoch ms)
int bf_when_ok(const SYSTEMTIME *local, double now_ms);
double bf_local_to_epoch(const SYSTEMTIME *local);           // 이 PC 시간대 → epoch ms (못 바꾸면 NaN)
//  썸네일 data URL → 바이트 (malloc, 형식이 아니면 NULL·type ""). 거른 값이면 늘 jpeg|png|webp
unsigned char *bf_thumb_bytes(const char *s, size_t n, char type[16], size_t *out_n);

#endif
