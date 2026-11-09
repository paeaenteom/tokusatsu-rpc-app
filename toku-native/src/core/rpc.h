// Discord 카드 엔진 — ttfc-app/src/discord-rpc.js (DiscordRichPresence + DiscordRpcHub) 와
//  extension-bridge.js 의 메시지 처리(_handleMessage 중 카드 관련)를 그대로 옮긴 것
//  · 시계·Discord 연결·확장 전송은 전부 host 가 준다 (실제 앱 = 진짜, 테스트 = 가짜 시계·가짜 Discord)
//  · 한 스레드. host 는 core_* 를 부른 뒤 / 타이머 하나를 돌린 뒤에 Discord 답(core_reply)을 넘긴다
//    (JS 의 '동기 코드가 끝난 뒤 Promise 처리' 와 같은 순서)
#ifndef TOKU_RPC_H
#define TOKU_RPC_H
#include <stdint.h>
#include "../base/json.h"
#include "timers.h"

typedef struct core core;

typedef struct {
    void *user;
    int64_t (*now)(void *user);                                   // Date.now()
    void (*log)(void *user, int level, const char *text);         // 0 info · 1 warn · 2 error
    // Discord 연결 하나 = conn 번호 (코어가 새로 만들 때마다 늘어난다 — JS 의 new RPC.Client)
    void (*connect)(void *user, int conn, const char *site, const char *app_id);   // login 시작 → core_ready / core_login_failed
    void (*destroy)(void *user, int conn);                        // client.destroy()
    // SET_ACTIVITY 보내기. args_json = {"pid":…,"activity":{…}} 또는 {"pid":…} (지우기)
    //  1 = 보냄 (답은 나중에 core_reply), 0 = 연결이 없어 못 보냄 (JS 에선 조용히 거부된다)
    int (*request)(void *user, int conn, int req, const char *args_json);
    void (*ws_send)(void *user, int client, const char *json);    // 확장에 보내기
    const char *(*app_id)(void *user, const char *site);          // secrets 의 discordAppIds (없으면 NULL·"")
    void (*status)(void *user);                                   // 창 갱신 (onStatusChange)
    void (*save_hosted)(void *user);                              // 썸네일 캐시 저장 (2초 몰아서)
    // 썸네일 올리기 (data:image/…;base64,… 를 공개 호스트에). 끝나면 core_upload_done. NULL 이면 THUMB_BYTES 를 무시
    void (*upload)(void *user, int job, const char *data_url);
    //  정밀 시계 (ms, 소수) — Discord 왕복 시간 표시에만. NULL 이면 now
    double (*now_hr)(void *user);
    int pid;
} core_host;

void core_upload_done(core *c, int job, int ok, const char *hosted, double keep_ms, const char *err);
// 저장된 썸네일 캐시(thumb-cache.json)를 처음에 넣는다 (로그 없음)
void core_load_hosted(core *c, const char *url, const char *hosted, double at, double keep_ms);
// 지금 아는 재호스팅 결과 (save_hosted 에서 파일로 쓸 때). 오래된 것부터 i=0..
char *core_resolve_image(core *c, const char *url);   // 정주행 기록이 쓸 썸네일 주소 (재호스팅·바로 쓸 수 있는 것, 없으면 "") — free
size_t core_hosted_count(core *c);
int core_hosted_at(core *c, size_t i, const char **url, const char **hosted, double *at, double *keep_ms);

core *core_new(const core_host *h);
void core_free(core *c);

// ── Discord 쪽 사건 ──
void core_ready(core *c, int conn, const char *username);
void core_login_failed(core *c, int conn, const char *message);
void core_disconnected(core *c, int conn);
// 요청 답. ok=1 이면 data = Discord 가 돌려준 data(없으면 NULL), ok=0 이면 err = 오류 메시지
void core_reply(core *c, int req, int ok, const jval *data, const char *err);

// ── 확장 메시지 (WebSocket 텍스트를 JSON 으로 읽은 것) ──
void core_ext_message(core *c, int client, const jval *msg);

// ── 설정·조작 (main.js 가 하던 일) ──
void core_set_settings(core *c, const jval *settings);   // rpcSettings() 가 돌려줄 값 (복사해 둔다)
void core_apply_settings(core *c);                       // discordRPC.applySettings(rpcSettings())
void core_set_lang(core *c, const char *lang);
void core_set_idle_timeout(core *c, double ms);
void core_set_response_level(core *c, const char *level);
//  전송 V2 (테스트 버전): Discord 클라이언트와 같은 20초 5번 창(모든 사이트 공유, 같은 내용은 안 셈) + 재생·정지·이동 0.35초 모으기. 기본 0 = 지금 앱과 같은 방식
void core_set_pacing_v2(core *c, int on);
void core_connect(core *c);
void core_disconnect(core *c);
void core_clear(core *c, const char *site);              // NULL = 전부
void core_thumb_hosted(core *c, const char *url, const char *hosted, double keep_ms);   // 재호스팅 끝 (cacheThumbnail 뒷부분)

// ── 창에 보여 줄 상태 (discord-rpc.js getStatus 와 같은 규칙: 사이트 표 순서, 연결돼 있고 카드가 떠 있는 것만) ──
//  문자열은 다음 core_* 호출 전까지만 유효
typedef struct {
    const char *site, *site_name;
    const char *details, *state, *large_key, *large_text, *small_key, *small_text, *url;
    int has_start, has_end;
    double start, end;     // 초
    int watching, playing;
    int type;                         // Discord 활동 종류 (0 플레이 · 2 듣기 · 3 시청) — 실제로 보낸 값
    const char *btn_label, *btn_url;  // 보낸 버튼 (없으면 NULL)
} core_card;
int core_cards(core *c, core_card *out, int max);
int core_discord_connected(core *c);
void core_latency(core *c, int *connected, double *ms);   // 트레이 메뉴용 Discord 응답 시간 (없으면 -1)
const char *core_user(core *c);   // 붙어 있는 Discord 사용자 이름 (없으면 "")

// ── 타이머 ──
int64_t core_next_deadline(core *c);     // 없으면 -1
int core_fire_one(core *c);              // now 가 됐으면 하나 실행하고 1
int core_host_timer(core *c, int64_t delay, tm_fn fn, void *ctx);   // host 도 같은 줄에 타이머를 건다 (순서 맞추기)

#endif
