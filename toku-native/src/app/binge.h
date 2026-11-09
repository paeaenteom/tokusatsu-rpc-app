// 정주행 모드 웹훅 기록 — 지금 앱 binge-webhook.js (BingeLogger) + extension-bridge.js 의 정주행 부분을 그대로
//  ⚠ 웹훅 주소는 비밀 — 로그·화면에 내보내지 않는다
#ifndef TOKU_BINGE_H
#define TOKU_BINGE_H
#include <windows.h>
#include "../base/json.h"

#define WM_BINGE_LOG (WM_APP + 5)   // 보내는 스레드의 로그 (wp = 수준, lp = malloc 한 글) → main 이 찍고 free

void binge_init(HWND notify);
int binge_state(void);                         // 1 켜짐 · 0 꺼짐 · -1 모름 (확장이 안 알려 줌)
void binge_set_state(int v);                   // BINGE_STATE / 확장이 다 끊김(-1)
void binge_remember_thumb(const jval *url, const jval *data_url);   // THUMB_BYTES (원본 우선)
void binge_on_video(const jval *msg);          // VIDEO 마다
void binge_end(const jval *msg);               // BINGE_END
void binge_tick(void);                         // main 반복마다 (안정화 확인·썸네일 대기)

//  시험 보내기 (웹훅 탭): 결과는 done(ok, status) 로 창 스레드에서 (PostMessage 로 돌아온 뒤)
//  character: NULL 또는 { name, avatar, avatarUntil }
void binge_send_test(const char *url, const char *title, const jval *character, int tag);
#define WM_BINGE_TEST (WM_APP + 6)   // wp = tag, lp = (ok << 16) | status

//  보충 기록 (창 '보충' 탭) — 지금 앱 sendBackfill. when = 본 시각 (이 PC 시각). folder = 고른 폴더 작품명 (free, 기본 웹훅이면 "")
//  NULL = 보내기 시작 → 끝나면 WM_BINGE_BF, "nohook" = 보낼 웹훅 없음
#define WM_BINGE_BF (WM_APP + 9)     // wp = tag, lp = (ok << 16) | status
//  site: ttfc|imagination 이면 사이트 이름, 그 밖의 글이면 그대로 카드 위 출처 줄(직접 입력의 '출처 표시'), "" 이면 없음
//  img_url: 바이트가 없을 때 쓸 이미지 링크 (보내는 스레드가 받아 첨부, 못 받으면 링크 그대로) — NULL·"" 이면 그림 없음
const char *binge_send_backfill(const char *site, const char *page_url, const char *series, const char *ep_no, const char *ep_title,
                                const unsigned char *bytes, size_t nbytes, const char *type, const char *img_url, const SYSTEMTIME *when, int tag, char **folder);
const char *binge_sniff_image(const unsigned char *b, size_t n);   // image/png|jpeg|gif|webp, 아니면 NULL

//  작품명으로 고른 보낼 곳 (title = 폴더, 기본 웹훅이면 "") — url·title 은 free. 없으면 url = ""
void binge_pick_target(const char *series, char **url, char **title);
jval *binge_read_secrets(void);   // 알파 전용 사본이 있으면 그것, 없으면 지금 앱 것 (json_free)
char *binge_clean_ws(const char *s, size_t n, size_t max16);   // v.replace(/s+/g, " ").trim().slice(0, max) — free
int binge_hook_url_ok(const char *s, size_t n);                  // secrets.js HOOK_RE
extern double (*binge_rnd)(void);   // 무작위 [0,1) — 시험용으로 바꿀 수 있다
jval *binge_character_fields(const char *title, const jval *fixed);   // 시험용 (characterFields)

#endif
