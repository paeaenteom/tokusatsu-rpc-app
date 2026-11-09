// 창 ↔ 본체(main.c) 사이 — 창은 이 함수들로만 상태를 읽고 바꾼다
#ifndef TOKU_APP_H
#define TOKU_APP_H
#include <wchar.h>
#include <windows.h>
#include "../core/rpc.h"

#include "channel.h"
int app_test_channel(void);            // 1 = 테스트 버전(알파) 모드로 떠 있음
void app_set_test_channel(int on);     // 설정 저장 후 앱을 다시 시작해 그 모드로
const char *app_version(void);         // 창 머리·트레이 메뉴에 보이는 버전 (모드마다)
#define APP_VERSION app_version()
#define CH_LABEL (app_test_channel() ? S_NA_alpha : S_NA_beta)

core *app_core(void);
const char *app_lang(void);           // 실제로 쓰는 언어 ko|en|ja
int app_rpc_on(void);
void app_set_rpc(int on);
void app_reconnect(void);
int app_ext_count(void);
int app_port_ready(void);             // 7690 을 잡았나

// 설정 바꾸기 (저장 + 바로 반영)
void app_set_rpc_flag(const char *key, int on);        // showSeries·showEpisode·showThumbnail·showButtons
void app_set_time_mode(const char *mode);              // progress|remaining|none
void app_set_activity_type(int t);                     // 0|2|3
void app_set_status_display(const char *site, int v);  // 0|1|2
void app_set_idle_minutes(double m);                   // 0 = 안 사라짐
void app_set_level(const char *lv);
void app_set_lang_setting(const char *v);              // auto|ko|en|ja
int app_autostart(void);                               // 로그온 시 시작 (rpc.autoStart)
int app_faststart(void);                               // 로그온 즉시 시작 (작업 스케줄러)
void app_set_autostart(int on);
void app_set_faststart(int on);

int app_log_count(void);
const char *app_log_line(int i);      // 0 = 가장 오래된 것
void app_open_logs(void);
const wchar_t *app_config_path(void);
const char *app_app_id(const char *site);   // 카드 에셋 그림 받기용 (⚠ 값은 어디에도 출력하지 않는다)

// 영상 버튼 키 설정 (keybinds.c 저장소 + 확장과 버튼 고르기)
#include "keybinds.h"
const char *app_kb_pick(const char *site, const char *id);   // NULL = 시작함, 아니면 noext|full
void app_kb_cancel(void);
int app_kb_ext(void);                                        // 확장이 붙어 있나
kb_res app_kb_edit(int op, const char *site, const char *id, const jval *arg, int on);   // 0 키 · 1 이름 · 2 삭제 · 3 사이트 켜고 끄기

// 정주행 (binge.c 와 main.c)
const wchar_t *app_data_dir(void);          // %APPDATA% 아래 알파 폴더 이름 (시험 모드면 toku-rpc-native-test)
void app_log(int lvl, const char *text);   // 0 info · 1 warn · 2 error (창 스레드에서만)
int app_set_binge(int on);
void app_ping_begin(void);                          // 트레이 메뉴: 확장 왕복 재기 시작
int app_ping_get(int *clients, double *ms);          // 1 = 결과 있음 (ms -1 = 응답 없음)
void app_discord_latency(int *connected, double *ms);                  // 확장에 SET_BINGE — 확장이 없으면 0
//  보충 탭 (main.js backfill-*) — 결과는 win_bf_resolved · win_bf_sent 로
const char *app_bf_resolve(const char *url);   // NULL = 읽기 시작, 아니면 바로 실패 (noext)
void app_bf_target(const char *series, char **folder, int *has_hook, int *chars);   // folder 는 free
const unsigned char *app_bf_thumb(const char *token, size_t *n);   // 읽은 썸네일 바이트 (미리보기용, 빌려 줌)
const char *app_bf_send(const char *token, const char *series, const char *ep_no, const char *ep_title, const SYSTEMTIME *when, int tag);
//  직접 입력: NULL = 보내는 중 (끝나면 win_bf_sent), 아니면 series|when|imgurl|nohook. 이미지는 바이트(파일) 또는 https 링크
const char *app_bf_send_manual(const char *series, const char *ep_no, const char *ep_title, const char *source, const char *img_url,
                               const unsigned char *bytes, size_t n, const char *type, const SYSTEMTIME *when, int tag);
int app_bf_img_url_ok(const char *u);   // 빈 값 또는 https 주소 (2000자 이하, 빈칸·따옴표·<> 없음)   // NULL = 보내는 중, 아니면 stale|series|when|nohook

// 창 (window.c)
void win_init(HINSTANCE inst);
void win_show(void);
void win_refresh(void);               // 상태가 바뀌었다 (보일 때만 다시 그림)
int win_visible(void);
int win_snapshot(const wchar_t *dir);
void win_binge_test_result(int tag, LPARAM r);   // 웹훅 시험 보내기 결과 (r = ok<<16 | status)
void win_kb_pick_result(const char *site, const char *id, int ok, const char *err);   // 버튼 고르기 끝 (ok 면 id = 그 줄)   // 시험용: 탭마다 BMP 저장, 저장한 수
void win_bf_resolved(const jval *r);              // 보충 링크 읽기 끝 ({ ok, … } 또는 { ok:false, error })
void win_bf_sent(int tag, int ok, int status);     // 보충 기록 보내기 끝
void win_bf_test_fill(const jval *m);              // 시험 모드 그림용: 보충 직접 입력 칸 채우기

#endif
