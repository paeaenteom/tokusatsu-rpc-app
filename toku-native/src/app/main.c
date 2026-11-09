// TOKU RPC — C 알파 (트레이 앱)
//  · 기존 앱의 설정(config.json)·비밀(secrets.json)·썸네일 캐시(thumb-cache.json)는 읽기만 한다 (쓰지 않는다)
//  · 로그는 %APPDATA%\toku-rpc-native\logs\main.log 에 따로
//  · 포트 7690 을 잡은 뒤에야 Discord 에 붙는다 — 기존 앱이 켜져 있으면 3초마다 다시 시도하며 기다린다
//  · 아직 없는 것: 설정 창, 정주행 웹훅, 키 설정 고르기, 자동 시작 (PLAN.md 4~7단계)
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../base/json.h"
#include "../base/sbuf.h"
#include "../base/utf.h"
#include "../core/js.h"
#include "../core/rpc.h"
#include "../discord/ipc.h"
#include "../net/ws.h"
#include "app.h"
#include "settings.h"
#include "autostart.h"
#include "keybinds.h"
#include "binge.h"
#include "upload.h"
#include "traymenu.h"
#include "backfill.h"
#include "channel.h"
#include "hooks.h"
#include "accent.h"
#include "../base/median.h"

#define PORT_REAL 7690
//  시험용 (환경 변수가 있을 때만): 포트·Discord 파이프 이름을 바꾼다 — 실제 사용엔 쓰지 않는다
static int PORT = PORT_REAL;
static wchar_t PIPE_PREFIX[64] = L"discord-ipc-";
#define WM_TRAY (WM_APP + 1)
#define ID_RPC 101
#define ID_RECONNECT 102
#define ID_LOGS 103
#define ID_QUIT 104
#define ID_OPEN 105
#define ID_BINGE 106
#define READY_TIMEOUT_MS 10000

static core *C;
static ws_server *WS;
static HWND hwnd;
static NOTIFYICONDATAW nid;
static UINT msg_taskbar_created;
static FILE *g_logf;
static wchar_t log_dir[MAX_PATH];
static char lang[8] = "ko";
static char level[16] = "normal";
static int rpc_on = 1, auto_connect = 1, connected_once_port = 0, running = 1;
static ULONGLONG next_listen_try;
static int listen_fail_logged;

// ── 시계·로그 ───────────────────────────────────────────────

static double now_hr_ms(void *u) {   //  QueryPerformanceCounter (µs 단위)
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}
static int64_t now_epoch_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(t / 10000ULL - 11644473600000ULL);
}

//  창 콘솔용 — 최근 600줄 (지금 앱의 app-console.js 와 같은 상한)
#define LOG_RING 600
static char *ring[LOG_RING];
static int ring_head, ring_count;

static void log_line(int lvl, const char *text) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const char *L[] = { "[info]  ", "[warn]  ", "[error] " };
    char head[64];
    snprintf(head, sizeof head, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             L[lvl < 0 || lvl > 2 ? 0 : lvl]);
    if (g_logf) { fprintf(g_logf, "%s%s\n", head, text); fflush(g_logf); }
    size_t n = strlen(head) + strlen(text) + 1;
    char *line = (char *)malloc(n);
    if (line) {
        snprintf(line, n, "%s%s", head, text);
        int slot = (ring_head + ring_count) % LOG_RING;
        if (ring_count == LOG_RING) { free(ring[ring_head]); ring_head = (ring_head + 1) % LOG_RING; slot = (ring_head + LOG_RING - 1) % LOG_RING; }
        else ring_count++;
        ring[slot] = line;
    }
    win_refresh();
}
int app_log_count(void) { return ring_count; }
const char *app_log_line(int i) { return (i >= 0 && i < ring_count) ? ring[(ring_head + i) % LOG_RING] : ""; }
static void logf_(int lvl, const char *fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    log_line(lvl, b);
}

// ── 파일 읽기 (기존 앱 것 — 읽기만) ───────────────────────────

static jval *read_json_file(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    jval *v = NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart < 64 * 1024 * 1024) {
        char *buf = (char *)malloc((size_t)sz.QuadPart + 1);
        DWORD got = 0;
        if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL)) {
            size_t off = (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
            v = json_parse(buf + off, got - off, NULL);
        }
        free(buf);
    }
    CloseHandle(h);
    return v;
}

static wchar_t appdata[MAX_PATH];
static void path_in(wchar_t *out, const wchar_t *sub) { _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls", appdata, sub); out[MAX_PATH - 1] = 0; }

static jval *SECRETS, *CONFIG;

static const jval *cfg(const char *a, const char *b) {
    const jval *x = json_get(CONFIG, a);
    return b ? json_get(x, b) : x;
}

// ── Discord 연결 (host) ─────────────────────────────────────

typedef struct { int req; char nonce[37]; } pendreq;
typedef struct dconn {
    int conn;
    dipc *d;
    int ready, dead;
    char site[16];
    pendreq *p;
    int np, cap;
    struct dconn *next;
} dconn;
static dconn *conns;
static int rej_q[4096], nrej;

static dconn *dc_find(int conn) { for (dconn *c = conns; c; c = c->next) if (c->conn == conn) return c; return NULL; }
static void dc_remove(dconn *x) {
    for (dconn **pp = &conns; *pp; pp = &(*pp)->next) if (*pp == x) { *pp = x->next; break; }
    if (x->d) dipc_close(x->d);
    free(x->p);
    free(x);
}
// 기다리던 요청을 거부로 (지금 일이 끝난 뒤 넘긴다)
static void dc_reject_all(dconn *x) {
    for (int i = 0; i < x->np; i++) if (nrej < 4096) rej_q[nrej++] = x->p[i].req;
    x->np = 0;
}
static void drain_rejects(void) {
    for (int i = 0; i < nrej; i++) core_reply(C, rej_q[i], 0, NULL, "connection closed");
    nrej = 0;
}

static void fail_cb(void *ctx, int id) { core_login_failed(C, (int)(intptr_t)ctx, "Could not connect"); }
static void timeout_cb(void *ctx, int id) {
    dconn *x = dc_find((int)(intptr_t)ctx);
    if (x && !x->ready && !x->dead) core_login_failed(C, x->conn, "RPC_CONNECTION_TIMEOUT");
}

static const char *h_app_id(void *u, const char *site) {
    const jval *ids = json_get(SECRETS, "discordAppIds");
    const jval *v = json_get(ids, site);
    return (v && v->t == J_STR) ? v->s : "";
}

const char *app_app_id(const char *site) { return h_app_id(NULL, site); }

static void h_connect(void *u, int conn, const char *site, const char *app) {
    dconn *x = (dconn *)calloc(1, sizeof *x);
    if (!x) return;
    x->conn = conn;
    snprintf(x->site, sizeof x->site, "%s", site);
    x->next = conns;
    conns = x;
    int err = 0;
    x->d = dipc_open(PIPE_PREFIX, app, &err);
    //  login 실패는 비동기다 (JS 와 같게 다음 차례에)
    if (!x->d) { x->dead = 1; core_host_timer(C, 1, fail_cb, (void *)(intptr_t)conn); return; }
    core_host_timer(C, READY_TIMEOUT_MS, timeout_cb, (void *)(intptr_t)conn);
}

static void h_destroy(void *u, int conn) {
    dconn *x = dc_find(conn);
    if (!x) return;
    dc_reject_all(x);
    dc_remove(x);   // CLOSE {} 를 보내고 닫는다
}

static int h_request(void *u, int conn, int req, const char *args) {
    dconn *x = dc_find(conn);
    if (!x || !x->d || x->dead) return 0;
    char nonce[37];
    dipc_uuid(nonce);
    sbuf j; sb_init(&j, 0);
    sb_puts(&j, "{\"cmd\":\"SET_ACTIVITY\",\"args\":");
    sb_puts(&j, args);
    sb_printf(&j, ",\"nonce\":\"%s\"}", nonce);
    int ok = !j.err && dipc_send_json(x->d, DIPC_FRAME, j.p, j.len);
    sb_free(&j);
    if (!ok) return 0;
    if (x->np == x->cap) {
        int cap = x->cap ? x->cap * 2 : 8;
        pendreq *np = (pendreq *)realloc(x->p, (size_t)cap * sizeof *np);
        if (!np) return 1;
        x->p = np;
        x->cap = cap;
    }
    x->p[x->np].req = req;
    memcpy(x->p[x->np].nonce, nonce, 37);
    x->np++;
    return 1;
}

static void update_tip(void);

static void on_frame(dipc *d, int op, const jval *msg, void *user) {
    dconn *x = (dconn *)user;
    if (op != DIPC_FRAME) return;
    const char *cmd = json_get_str(msg, "cmd", "");
    const char *evt = json_get_str(msg, "evt", "");
    if (!strcmp(cmd, "DISPATCH") && !strcmp(evt, "READY")) {
        if (x->ready) return;
        x->ready = 1;
        const jval *du = json_get(json_get(msg, "data"), "user");
        core_ready(C, x->conn, json_get_str(du, "username", "undefined"));
        update_tip();
        return;
    }
    const char *nonce = json_get_str(msg, "nonce", NULL);
    if (!nonce) return;
    for (int i = 0; i < x->np; i++) {
        if (strcmp(x->p[i].nonce, nonce)) continue;
        int req = x->p[i].req;
        x->p[i] = x->p[--x->np];
        const jval *data = json_get(msg, "data");
        int ok = strcmp(evt, "ERROR") != 0;
        core_reply(C, req, ok, data, ok ? NULL : json_get_str(data, "message", "undefined"));
        return;
    }
}

static void poll_discord(void) {
    for (dconn *x = conns; x; ) {
        dconn *nx = x->next;
        if (x->d && !x->dead) {
            if (dipc_poll(x->d, on_frame, x) < 0) {
                //  연결이 닫혔다 — discord-rpc 와 같게: 기다리던 요청 거부 + disconnected (+ 붙기 전이면 login 실패)
                x->dead = 1;
                int conn = x->conn, was_ready = x->ready;
                dc_reject_all(x);
                core_disconnected(C, conn);
                if (!was_ready) core_login_failed(C, conn, "connection closed");
                drain_rejects();
                update_tip();
            }
        }
        x = nx;
    }
}

static int discord_connected(void) { for (dconn *x = conns; x; x = x->next) if (x->ready && !x->dead) return 1; return 0; }

// ── 확장 (WebSocket) ────────────────────────────────────────

static void h_ws_send(void *u, int client, const char *json) {
    for (size_t i = 0; WS; i++) {
        ws_client *c = ws_at(WS, i);
        if (!c) break;
        if (ws_id(c) == client) { ws_send_text(c, json, strlen(json)); return; }
    }
}

static void send_hello(ws_client *c) {
    sbuf b; sb_init(&b, 0);
    int booster = !strcmp(level, "max") || !strcmp(level, "high");
    sb_printf(&b, "{\"type\":\"CONNECTED\",\"lang\":\"%s\",\"booster\":%s,\"level\":\"%s\"}", lang, booster ? "true" : "false", level);
    ws_send_text(c, b.p, b.len);
    sb_reset(&b);
    //  영상 버튼 키 설정 — 알파 설정에 저장된 것을 거른 그대로 (keybinds.js payload 와 같음)
    kb_payload(&b);
    if (!b.err) ws_send_text(c, b.p, b.len);
    sb_free(&b);
}

// ── 영상 버튼 키 설정 (main.js kb-* · extension-bridge.js pickButton 과 같은 흐름) ──
static void broadcast(const char *json);
static void kb_changed(void) {
    sbuf b; sb_init(&b, 0);
    kb_payload(&b);
    if (!b.err) broadcast(b.p);
    sb_free(&b);
}

//  버튼 고르기 — 확장이 그 사이트 탭을 앞으로 띄우고 고르기 모드. 4초 안에 '시작했다' 가 없으면 예전 확장(oldext)
static int TEST_MODE;   // 시험 모드 (포트를 바꿨을 때)
static int TEST_CH;     // 테스트 버전(알파) 모드 — 설정 testChannel, 시작할 때 한 번 읽는다
static int RESTARTING;  // 모드를 바꿔 다시 시작하는 중
static int TRAY_SNAP;   // 시험: 지연 재기가 끝나면 트레이 메뉴 그림 저장
static int LAT_LOG;     // 시험: 지연 재기가 끝나면 값을 로그로
static struct { int active, started; char req[16], site[16], id[32]; int64_t deadline; } PICK;
#define PICK_WAIT_MS 180000

static void pick_finish(int ok, const char *err, const jval *target, const char *label) {
    if (!PICK.active) return;
    char site[16], bid[32];
    snprintf(site, sizeof site, "%s", PICK.site);
    snprintf(bid, sizeof bid, "%s", PICK.id);
    PICK.active = 0;
    //  사용자는 크롬에서 버튼을 눌렀다 — 키를 정하러 창으로 돌아오게 앞으로 띄운다
    if ((ok || !strcmp(err, "canceled") || !strcmp(err, "timeout")) && !TEST_MODE) win_show();   // 시험 모드는 화면에 안 띄움
    if (TEST_MODE) logf_(0, "[Test] 고르기 끝: %s", ok ? "ok" : err);
    if (!ok) { win_kb_pick_result(site, bid, 0, err); return; }
    jval lab; memset(&lab, 0, sizeof lab);
    lab.t = J_STR; lab.s = (char *)(label ? label : ""); lab.slen = strlen(lab.s);
    kb_res w = *bid ? kb_set_target(site, bid, target) : kb_add(site, target, &lab);
    if (w.ok) {
        logf_(0, "[Keys] 버튼 고름: %s %s", site, *bid ? "(다시 고름)" : "(새 줄)");
        kb_changed();
    }
    win_kb_pick_result(site, *bid ? bid : w.id, w.ok, w.ok ? NULL : w.error);
}

//  0 = 시작함 (결과는 win_kb_pick_result 로), 아니면 바로 실패한 까닭 (noext·full)
const char *app_kb_pick(const char *site, const char *id) {
    if (!id || !*id) {
        jval *all = kb_all();
        const jval *l = json_get(all, site);
        int full = l && l->count >= KB_MAX_PER_SITE;
        json_free(all);
        if (full) return "full";
    }
    if (!WS || !ws_count(WS)) return "noext";
    if (PICK.active) app_kb_cancel();   // 앞의 고르기는 접는다
    unsigned char rb[6];
    BCryptGenRandom(NULL, rb, 6, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (int i = 0; i < 6; i++) snprintf(PICK.req + i * 2, 3, "%02x", rb[i]);
    snprintf(PICK.site, sizeof PICK.site, "%s", site);
    snprintf(PICK.id, sizeof PICK.id, "%s", id ? id : "");
    PICK.active = 1;
    PICK.started = 0;
    PICK.deadline = now_epoch_ms() + 4000;
    char m[96];
    snprintf(m, sizeof m, "{\"type\":\"KB_PICK\",\"reqId\":\"%s\",\"site\":\"%s\"}", PICK.req, PICK.site);
    broadcast(m);
    return NULL;
}
void app_kb_cancel(void) {
    if (!PICK.active) return;
    char m[80];
    snprintf(m, sizeof m, "{\"type\":\"KB_PICK_CANCEL\",\"reqId\":\"%s\"}", PICK.req);
    broadcast(m);
    pick_finish(0, "canceled", NULL, NULL);
}
static void pick_tick(void) {
    if (!PICK.active || now_epoch_ms() < PICK.deadline) return;
    if (!PICK.started) { pick_finish(0, "oldext", NULL, NULL); return; }
    char m[80];
    snprintf(m, sizeof m, "{\"type\":\"KB_PICK_CANCEL\",\"reqId\":\"%s\"}", PICK.req);
    broadcast(m);
    pick_finish(0, "timeout", NULL, NULL);
}
int app_kb_ext(void) { return WS && ws_count(WS) > 0; }

kb_res app_kb_edit(int op, const char *site, const char *id, const jval *arg, int on) {
    kb_res r;
    if (op == 0) r = kb_set_key(site, id, arg);
    else if (op == 1) r = kb_rename(site, id, arg);
    else if (op == 2) r = kb_remove(site, id);
    else r = kb_set_site_on(site, on);
    if (r.ok) kb_changed();
    return r;
}

// ── 보충 기록 (main.js backfill-resolve·target·send + extension-bridge.js resolvePage) ──
//  일본 사이트는 크롬 VPN 확장을 거쳐야 열린다 → 붙어 있는 확장 모두에 RESOLVE_PAGE 를 보내고 먼저 온 성공 답을 쓴다
//  예전 확장은 이 메시지를 모른다 → 30초 동안 아무 답도 없으면 'oldext'
static struct { int active, left; char id[17]; jval *first_fail; int64_t deadline; } RSV;
//  읽은 링크 (backfillPending) — 썸네일 바이트는 창에 안 보내고 여기 들고 있다가 보낼 때 첨부
static struct { int has; char token[17], site[16], type[16]; char *url; unsigned char *thumb; size_t nthumb; } BFP;
static char BF_SENDING[17];
static void bf_pending_free(void) { free(BFP.url); free(BFP.thumb); memset(&BFP, 0, sizeof BFP); }
static void rand_hex(char *out, int nbytes) {
    unsigned char rb[8];
    BCryptGenRandom(NULL, rb, (ULONG)nbytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (int i = 0; i < nbytes; i++) snprintf(out + i * 2, 3, "%02x", rb[i]);
}
//  backfillTarget: 작품명으로 고를 폴더 · 웹훅이 있나 · 쓸 캐릭터 수 (주소는 창에 안 준다)
void app_bf_target(const char *series, char **folder, int *has_hook, int *chars) {
    char *url = NULL, *title = NULL;
    binge_pick_target(series ? series : "", &url, &title);
    *has_hook = url && *url;
    *chars = 0;
    if (title && *title) {
        hk_char *cl;
        int cn = hk_chars(title, &cl);
        for (int i = 0; i < cn; i++) if (!cl[i].off) (*chars)++;
        hk_chars_free(cl, cn);
    }
    *folder = title ? title : _strdup("");
    free(url);
}
const unsigned char *app_bf_thumb(const char *token, size_t *n) {
    *n = 0;
    if (!BFP.has || strcmp(BFP.token, token) || !BFP.thumb) return NULL;
    *n = BFP.nthumb;
    return BFP.thumb;
}
//  읽기 끝 (r 은 여기서 해제) → 창에 { ok, token, site, url, seriesName, episodeNumber, episodeTitle, thumb, accent, folder, hasHook, chars } 또는 { ok:false, error }
static void rsv_finish(jval *r) {
    RSV.active = 0;
    if (RSV.first_fail && RSV.first_fail != r) json_free(RSV.first_fail);
    RSV.first_fail = NULL;
    if (!json_get_bool(r, "ok", 0)) {
        if (TEST_MODE) { char *js = json_stringify(r); logf_(0, "[Test] bf resolved %s", js ? js : ""); free(js); }
        win_bf_resolved(r);
        json_free(r);
        return;
    }
    bf_pending_free();
    const jval *th = json_get(r, "thumbDataUrl");
    BFP.thumb = bf_thumb_bytes(th ? th->s : "", th ? th->slen : 0, BFP.type, &BFP.nthumb);
    rand_hex(BFP.token, 8);
    snprintf(BFP.site, sizeof BFP.site, "%s", json_get_str(r, "site", ""));
    BFP.url = _strdup(json_get_str(r, "url", ""));
    BFP.has = 1;
    {   //  log.info('[Binge] 보충 링크 읽음:', site, seriesName.slice(0, 30), episodeNumber)
        sbuf sn; sb_init(&sn, 0);
        const char *ser = json_get_str(r, "seriesName", "");
        js_slice16(&sn, ser, strlen(ser), 30);
        logf_(0, "[Binge] 보충 링크 읽음: %s %s %s", BFP.site, sn.p ? sn.p : "", json_get_str(r, "episodeNumber", ""));
        sb_free(&sn);
    }
    jval *o = json_new(J_OBJ);
    json_obj_set(o, "ok", json_new_bool(1));
    json_obj_set(o, "token", json_new_str(BFP.token));
    static const char *const K[] = { "site", "url", "seriesName", "episodeNumber", "episodeTitle" };
    for (int i = 0; i < 5; i++) json_obj_set(o, K[i], json_new_str(json_get_str(r, K[i], "")));
    json_obj_set(o, "thumb", json_new_bool(BFP.thumb != NULL));
    int ac = 0;
    if (BFP.thumb && accent_from_buffer(BFP.thumb, BFP.nthumb, &ac)) json_obj_set(o, "accent", json_new_num(ac));
    else json_obj_set(o, "accent", json_new(J_NULL));
    char *folder = NULL;
    int has = 0, chars = 0;
    app_bf_target(json_get_str(r, "seriesName", ""), &folder, &has, &chars);
    json_obj_set(o, "folder", json_new_str(folder ? folder : ""));
    json_obj_set(o, "hasHook", json_new_bool(has));
    json_obj_set(o, "chars", json_new_num(chars));
    free(folder);
    json_free(r);
    if (TEST_MODE) { char *js = json_stringify(o); logf_(0, "[Test] bf resolved %s", js ? js : ""); free(js); }
    win_bf_resolved(o);
    json_free(o);
}
static void bf_on_resolved(const jval *m) {
    if (!RSV.active) return;
    sbuf id; sb_init(&id, 0);
    js_str_or_empty(&id, json_get(m, "id"));
    int mine = id.p && id.len == strlen(RSV.id) && !strcmp(id.p, RSV.id);
    sb_free(&id);
    if (!mine) return;
    jval *r = bf_clean_resolved(m);
    if (!r) return;
    if (json_get_bool(r, "ok", 0)) { rsv_finish(r); return; }
    if (!RSV.first_fail) RSV.first_fail = r; else json_free(r);
    if (--RSV.left <= 0) { jval *f = RSV.first_fail; RSV.first_fail = NULL; rsv_finish(f); }
}
static void rsv_tick(void) {
    if (!RSV.active || now_epoch_ms() < RSV.deadline) return;
    jval *f = RSV.first_fail;
    RSV.first_fail = NULL;
    if (!f) { f = json_new(J_OBJ); json_obj_set(f, "ok", json_new_bool(0)); json_obj_set(f, "error", json_new_str("oldext")); }
    rsv_finish(f);
}
static void broadcast(const char *json);
const char *app_bf_resolve(const char *url) {
    if (RSV.active) { RSV.active = 0; json_free(RSV.first_fail); RSV.first_fail = NULL; }   //  앞 링크는 버린다 (창이 새 링크를 넣음)
    if (!WS || !ws_count(WS)) return "noext";
    rand_hex(RSV.id, 8);
    sbuf b; sb_init(&b, 0);
    sbuf u; sb_init(&u, 0);
    js_slice16(&u, url ? url : "", strlen(url ? url : ""), 2000);
    sb_puts(&b, "{\"type\":\"RESOLVE_PAGE\",\"id\":\"");
    sb_puts(&b, RSV.id);
    sb_puts(&b, "\",\"url\":");
    json_write_str(&b, u.p ? u.p : "", u.len);
    sb_putc(&b, '}');
    sb_free(&u);
    RSV.left = 0;
    for (size_t i = 0; WS; i++) { ws_client *c = ws_at(WS, i); if (!c) break; if (b.p && ws_send_text(c, b.p, b.len)) RSV.left++; }
    sb_free(&b);
    if (RSV.left <= 0) return "noext";
    RSV.first_fail = NULL;
    RSV.deadline = now_epoch_ms() + 30000;
    RSV.active = 1;
    return NULL;
}
//  backfill-send: 같은 링크 표(token) · 작품명 · 시각(2000년~지금+10분) 확인 → 보내기. NULL = 보내는 중 (끝나면 win_bf_sent)
const char *app_bf_send(const char *token, const char *series, const char *ep_no, const char *ep_title, const SYSTEMTIME *when, int tag) {
    if (!BFP.has || strcmp(BFP.token, token ? token : "")) return "stale";
    char *sn = binge_clean_ws(series ? series : "", strlen(series ? series : ""), 200);
    char *en = binge_clean_ws(ep_no ? ep_no : "", strlen(ep_no ? ep_no : ""), 60);
    char *et = binge_clean_ws(ep_title ? ep_title : "", strlen(ep_title ? ep_title : ""), 200);
    const char *e = NULL;
    if (!sn || !en || !et || !*sn) e = "series";
    else if (!bf_when_ok(when, (double)now_epoch_ms())) e = "when";
    else e = binge_send_backfill(BFP.site, BFP.url ? BFP.url : "", sn, en, et, BFP.thumb, BFP.nthumb, BFP.type, NULL, when, tag, NULL);
    if (!e) snprintf(BF_SENDING, sizeof BF_SENDING, "%s", BFP.token);
    free(sn); free(en); free(et);
    return e;
}

//  보충 '직접 입력' (TTFC·IMAGINATION 에 없는 DVD 특전·극장 한정 영상 등) — 링크 없이 적은 그대로 보낸다
//  이미지: 바이트(파일) 또는 링크(https, 보내는 스레드가 받아 첨부). 보낼 곳·캐릭터 고르기는 실시간 기록과 같다 (작품명 → 폴더)
int app_bf_img_url_ok(const char *u) {
    if (!u || !*u) return 1;
    size_t n = strlen(u);
    int https = n > 8 && !strncmp(u, "https://", 8);
    int local = TEST_MODE && n > 17 && !strncmp(u, "http://127.0.0.1:", 17);   //  시험 모드만: 로컬 시험 서버
    if ((!https && !local) || n > 2000) return 0;
    for (const char *p = u; *p; p++) if ((unsigned char)*p <= ' ' || *p == '"' || *p == '\'' || *p == '<' || *p == '>') return 0;
    return 1;
}
const char *app_bf_send_manual(const char *series, const char *ep_no, const char *ep_title, const char *source, const char *img_url,
                               const unsigned char *bytes, size_t n, const char *type, const SYSTEMTIME *when, int tag) {
    char *sn = binge_clean_ws(series ? series : "", strlen(series ? series : ""), 200);
    char *en = binge_clean_ws(ep_no ? ep_no : "", strlen(ep_no ? ep_no : ""), 60);
    char *et = binge_clean_ws(ep_title ? ep_title : "", strlen(ep_title ? ep_title : ""), 200);
    char *src = binge_clean_ws(source ? source : "", strlen(source ? source : ""), 80);
    const char *e = NULL;
    if (!sn || !en || !et || !src || !*sn) e = "series";
    else if (!bf_when_ok(when, (double)now_epoch_ms())) e = "when";
    else if (!(bytes && n) && !app_bf_img_url_ok(img_url)) e = "imgurl";
    else {
        //  출처 글이 ttfc·imagination 이면 그 사이트 이름으로, 그 밖의 글은 그대로 카드 위 출처 줄
        const char *site = src;
        e = binge_send_backfill(site, "", sn, en, et, bytes, n, type, (bytes && n) ? NULL : img_url, when, tag, NULL);
    }
    if (!e) logf_(0, "[Binge] 보충 직접 입력 보냄 준비: %s %s", sn, en);
    free(sn); free(en); free(et); free(src);
    return e;
}

//  확장이 보낸 키 설정 메시지 — 1 이면 여기서 처리함
static int kb_message(const jval *m) {
    const char *t = json_get_str(m, "type", "");
    //  시험 모드에서만: 창 대신 '버튼 고르기'·'취소' 를 누른 것처럼 (tests/kb-pick-test.js)
    if (TEST_MODE && !strcmp(t, "TEST_KB_PICK")) {
        const char *e = app_kb_pick(json_get_str(m, "site", ""), json_get_str(m, "id", ""));
        if (e) logf_(0, "[Test] pick 바로 실패: %s", e);
        return 1;
    }
    if (TEST_MODE && !strcmp(t, "TEST_KB_CANCEL")) { app_kb_cancel(); return 1; }
    //  보충 탭 링크 읽기 결과 (extension-bridge.js _onResolved)
    if (!strcmp(t, "RESOLVED")) { bf_on_resolved(m); return 1; }
    //  시험 모드에서만: 보충 탭 '불러오기'·'기록 보내기' 를 누른 것처럼 (tests/backfill-flow-test.js)
    if (TEST_MODE && !strcmp(t, "TEST_BF_RESOLVE")) {
        const char *e = app_bf_resolve(json_get_str(m, "url", ""));
        if (e) logf_(0, "[Test] bf resolve 바로 실패: %s", e);
        return 1;
    }
    if (TEST_MODE && !strcmp(t, "TEST_BF_FILL")) { win_bf_test_fill(m); return 1; }
    if (TEST_MODE && !strcmp(t, "TEST_BF_MANUAL")) {
        SYSTEMTIME w;
        const jval *du = json_get(m, "imageDataUrl");
        char ty[16] = "";
        size_t bn = 0;
        unsigned char *bb = du && du->t == J_STR ? bf_thumb_bytes(du->s, du->slen, ty, &bn) : NULL;
        const char *e = bf_parse_when(json_get_str(m, "when", ""), &w)
            ? app_bf_send_manual(json_get_str(m, "seriesName", ""), json_get_str(m, "episodeNumber", ""), json_get_str(m, "episodeTitle", ""),
                                 json_get_str(m, "source", ""), json_get_str(m, "imageUrl", ""), bb, bn, ty, &w, 77)
            : "when";
        free(bb);
        logf_(0, "[Test] bf manual: %s", e ? e : "started");
        return 1;
    }
    if (TEST_MODE && !strcmp(t, "TEST_BF_SEND")) {
        SYSTEMTIME w;
        const char *e = bf_parse_when(json_get_str(m, "when", ""), &w)
            ? app_bf_send(json_get_str(m, "token", ""), json_get_str(m, "seriesName", ""), json_get_str(m, "episodeNumber", ""), json_get_str(m, "episodeTitle", ""), &w, 77)
            : "when";
        logf_(0, "[Test] bf send: %s", e ? e : "started");
        return 1;
    }
    //  시험 모드에서만: 트레이 메뉴를 화면에 띄우지 않고 그림으로 (확장 지연을 잰 뒤)
    if (TEST_MODE && !strcmp(t, "TEST_TRAY_SNAP")) { app_ping_begin(); TRAY_SNAP = 1; return 1; }
    if (TEST_MODE && !strcmp(t, "TEST_LATENCY")) { app_ping_begin(); LAT_LOG = 1; return 1; }
    if (TEST_MODE && !strcmp(t, "TEST_SET_CHANNEL")) { app_set_test_channel(json_get_bool(m, "on", 0)); return 1; }
    //  시험 모드에서만: 웹훅 탭 '시험 보내기' 와 같은 카드를 보낸다 (tests/binge-flow-test.js)
    if (TEST_MODE && !strcmp(t, "TEST_HOOK_SEND")) {
        const jval *ch = json_get(m, "character");
        binge_send_test(json_get_str(m, "url", ""), json_get_str(m, "title", ""), (ch && ch->t == J_OBJ) ? ch : NULL, 0);
        return 1;
    }
    if (!strcmp(t, "KEYBIND_PICK_START") || !strcmp(t, "KEYBIND_PICK_FAIL") || !strcmp(t, "KEYBIND_PICKED")) {
        sbuf rq; sb_init(&rq, 0);
        js_str_or_empty(&rq, json_get(m, "reqId"));
        int mine = PICK.active && rq.p && !strcmp(rq.p, PICK.req) && rq.len == strlen(PICK.req);
        sb_free(&rq);
        if (!mine) return 1;
        if (!strcmp(t, "KEYBIND_PICK_START")) {
            if (!PICK.started) { PICK.started = 1; PICK.deadline = now_epoch_ms() + PICK_WAIT_MS; }
        } else if (!strcmp(t, "KEYBIND_PICK_FAIL")) {
            const char *e = json_get_str(m, "error", "");
            pick_finish(0, !strcmp(e, "canceled") ? "canceled" : "notab", NULL, NULL);
        } else {
            const jval *s = json_get(m, "site");
            if (!s || s->t != J_STR || strcmp(s->s, PICK.site)) { pick_finish(0, "notab", NULL, NULL); return 1; }
            const jval *lb = json_get(m, "label");
            pick_finish(1, NULL, json_get(m, "target"), (lb && lb->t == J_STR) ? lb->s : "");
        }
        return 1;
    }
    if (!strcmp(t, "KEYBIND_FS")) {
        sbuf s, i; sb_init(&s, 0); sb_init(&i, 0);
        js_str_or_empty(&s, json_get(m, "site"));
        js_str_or_empty(&i, json_get(m, "id"));
        if (kb_mark_fs(s.p ? s.p : "", i.p ? i.p : "").ok) {
            logf_(0, "[Keys] 전체화면 키로 기억: %s", s.p ? s.p : "");
            kb_changed();
            win_refresh();
        }
        sb_free(&s); sb_free(&i);
        return 1;
    }
    if (!strcmp(t, "KEYBINDS_LOCAL")) {
        if (kb_adopt_local(json_get(m, "keybinds"), json_get(m, "on"))) {
            log_line(0, "[Keys] 확장에 있던 키 설정을 앱으로 가져옴");
            kb_changed();
            win_refresh();
        }
        return 1;
    }
    return 0;
}

static void w_open(ws_server *s, ws_client *c, void *u) {
    logf_(0, "[Bridge] \xED\x81\xAC\xEB\xA1\xAC \xED\x99\x95\xEC\x9E\xA5 \xEC\x97\xB0\xEA\xB2\xB0\xEB\x90\xA8 (%zu\xEA\xB0\x9C)", ws_count(s));
    send_hello(c);
    update_tip();
    win_refresh();
}
static void w_message(ws_server *s, ws_client *c, const char *d, size_t n, int bin, void *u) {
    jval *m = json_parse(d, n, NULL);
    if (!m) { log_line(1, "[Bridge] \xEB\xA9\x94\xEC\x8B\x9C\xEC\xA7\x80 \xED\x8C\x8C\xEC\x8B\xB1 \xEC\x97\x90\xEB\x9F\xAC"); return; }
    //  정주행 (extension-bridge.js 와 같은 자리에서): 상태·완주는 여기서 끝, 썸네일 바이트·영상은 엔진에도 넘긴다
    const char *t = json_get_str(m, "type", "");
    if (!strcmp(t, "BINGE_STATE")) { binge_set_state(js_truthy(json_get(m, "on")) ? 1 : 0); json_free(m); return; }
    if (!strcmp(t, "BINGE_END")) { binge_end(m); json_free(m); return; }
    if (!strcmp(t, "THUMB_BYTES")) {
        const jval *raw = json_get(m, "rawDataUrl");
        binge_remember_thumb(json_get(m, "url"), js_truthy(raw) ? raw : json_get(m, "dataUrl"));   //  알림 첨부는 원본 우선
    }
    int video = !strcmp(t, "VIDEO") || (!strcmp(t, "SET_ACTIVITY") && js_truthy(json_get(m, "seriesName")));
    if (!kb_message(m)) core_ext_message(C, ws_id(c), m);
    if (video) binge_on_video(m);
    json_free(m);
    drain_rejects();
}

void app_log(int lvl, const char *text) { log_line(lvl, text); }

// ── 모드 (베타 ↔ 테스트 버전 알파) ──
int app_test_channel(void) { return TEST_CH; }
const char *app_version(void) { return TEST_CH ? CH_VERSION_ALPHA : CH_VERSION_BETA; }
//  저장하고 자기 자신을 --restart 로 띄운 뒤 정상 종료 (새 것은 이 프로세스가 끝날 때까지 기다렸다 이어받는다)
void app_set_test_channel(int on) {
    if (RESTARTING || !!on == TEST_CH) return;
    settings_set("testChannel", NULL, json_new_bool(on));
    settings_save();
    wchar_t exe[MAX_PATH], line[MAX_PATH + 40];
    DWORD k = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!k || k >= MAX_PATH) { log_line(2, "[Channel] exe 경로를 못 읽어 다시 시작하지 못함 — 설정은 저장됨, 직접 다시 켜면 적용"); return; }
    _snwprintf(line, MAX_PATH + 39, L"\"%ls\" --restart%ls", exe, win_visible() ? L"" : L" --hidden");
    line[MAX_PATH + 39] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        logf_(2, "[Channel] 다시 시작 실패 (%lu) — 설정은 저장됨, 직접 다시 켜면 적용", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logf_(0, "[Channel] %s 모드로 바꾸려고 다시 시작합니다", on ? "테스트 버전(알파)" : "베타");
    RESTARTING = 1;
    running = 0;
}

// ── 트레이 메뉴 지연 시간: 확장 왕복 (extension-bridge.js measurePing — ping 을 보내고 가장 빠른 pong, 600ms 까지) ──
static struct { int active, left, clients, have, rclients; double best, result; LARGE_INTEGER t0; int64_t deadline; } PING;
//  표시는 최근 5번(1분 안)의 중간값 — 크롬이 가끔 늦게 pong 한 한 번에 숫자가 튀지 않게. 응답 없음(-1)은 그대로 보인다
static med5 PING_HIST;
static void ping_finish(void) {
    PING.result = PING.best; PING.rclients = PING.clients; PING.have = 1; PING.active = 0;
    if (!PING.clients) med5_clear(&PING_HIST);
    else if (PING.best >= 0) med5_push(&PING_HIST, PING.best, (double)now_epoch_ms());
}
static void w_pong(ws_server *s, ws_client *c, void *u) {
    if (!PING.active) return;
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    double ms = (double)(t.QuadPart - PING.t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    if (PING.best < 0 || ms < PING.best) PING.best = ms;
    if (--PING.left <= 0) ping_finish();
}
void app_ping_begin(void) {
    if (PING.active) return;
    int n = WS ? (int)ws_count(WS) : 0;
    PING.best = -1;
    PING.clients = n;
    if (!n) { ping_finish(); return; }
    QueryPerformanceCounter(&PING.t0);
    PING.left = 0;
    for (size_t i = 0; WS; i++) { ws_client *c = ws_at(WS, i); if (!c) break; if (ws_ping(c)) PING.left++; }
    if (!PING.left) { ping_finish(); return; }
    PING.active = 1;
    PING.deadline = now_epoch_ms() + 600;
}
//  1 = 결과 있음 (clients 0 = 확장 없음, ms -1 = 응답 없음)
int app_ping_get(int *clients, double *ms) { *clients = PING.rclients; *ms = PING.result < 0 ? PING.result : med5_get(&PING_HIST, (double)now_epoch_ms(), 60000, PING.result); return PING.have; }
void app_discord_latency(int *connected, double *ms) { core_latency(C, connected, ms); }
//  정주행 모드 켜기·끄기 — 확장에 바꾸라고만 한다 (진짜 상태는 확장이 BINGE_STATE 로 알려 줌). 확장이 없으면 0
int app_set_binge(int on) {
    if (!WS || !ws_count(WS)) return 0;
    broadcast(on ? "{\"type\":\"SET_BINGE\",\"on\":true}" : "{\"type\":\"SET_BINGE\",\"on\":false}");
    return 1;
}
static void w_close(ws_server *s, ws_client *c, int code, void *u) {
    size_t left = ws_count(s);
    if (left > 0) left--;   // 지금 닫히는 것 빼고
    logf_(0, "[Bridge] \xED\x81\xAC\xEB\xA1\xAC \xED\x99\x95\xEC\x9E\xA5 \xEC\x97\xB0\xEA\xB2\xB0 \xED\x95\xB4\xEC\xA0\x9C (%zu\xEA\xB0\x9C)", left);
    if (left == 0) { core_clear(C, NULL); binge_set_state(-1); }   // 확장이 없으면 카드도 내리고 정주행 상태도 모름
    update_tip();
    win_refresh();
}
static void w_reject(ws_server *s, int status, const char *why, void *u) {
    if (status == 401) log_line(1, "[Bridge] \xED\x99\x95\xEC\x9E\xA5\xEC\x9D\xB4 \xEC\x95\x84\xEB\x8B\x8C \xEA\xB3\xB3\xEC\x97\x90\xEC\x84\x9C \xEC\x98\xA8 \xEC\x97\xB0\xEA\xB2\xB0\xEC\x9D\x84 \xEA\xB1\xB0\xEB\xB6\x80\xED\x96\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4");
}

static void try_listen(void) {
    if (WS || GetTickCount64() < next_listen_try) return;
    ws_handlers h = { NULL, w_open, w_message, w_close, w_pong, w_reject };
    int err = 0;
    WS = ws_listen(PORT, &h, 16u * 1024 * 1024, &err);
    if (!WS) {
        next_listen_try = GetTickCount64() + 3000;
        if (!listen_fail_logged) {
            listen_fail_logged = 1;
            logf_(1, "[Bridge] \xED\x8F\xAC\xED\x8A\xB8 %d \xEC\x9D\xB4\xEB\xAF\xB8 \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91 (\xEA\xB8\xB0\xEC\xA1\xB4 \xEC\x95\xB1\xEC\x9D\xB4 \xEC\xBC\x9C\xEC\xA0\xB8 \xEC\x9E\x88\xEC\x9D\x8C) \xE2\x80\x94 3\xEC\xB4\x88\xEB\xA7\x88\xEB\x8B\xA4 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84 (\xEC\x98\xA4\xEB\xA5\x98 %d)", PORT, err);
        }
        update_tip();
        return;
    }
    logf_(0, "[Bridge] WebSocket \xEC\x84\x9C\xEB\xB2\x84 \xEC\x8B\x9C\xEC\x9E\x91: ws://127.0.0.1:%d", PORT);
    //  포트를 잡았다 = 기존 앱이 꺼져 있다 → 이제 Discord 에 붙는다
    if (!connected_once_port) {
        connected_once_port = 1;
        if (rpc_on && auto_connect) core_connect(C);
    }
    update_tip();
}

// ── 트레이 ──────────────────────────────────────────────────

static void update_tip(void) {
    if (!hwnd) return;
    wchar_t tip[128];
    if (!WS) _snwprintf(tip, 127, L"TOKU RPC \x2014 \xD3EC\xD2B8 7690 \xB300\xAE30 \xC911 (\xAE30\xC874 \xC571 \xC885\xB8CC \xD544\xC694)");
    else _snwprintf(tip, 127, L"TOKU RPC \x2014 Discord %ls \x00B7 \xD655\xC7A5 %d\xAC1C%ls",
                    discord_connected() ? L"\xC5F0\xACB0\xB428" : L"\xC5F0\xACB0 \xC548 \xB428", (int)ws_count(WS), rpc_on ? L"" : L" \x00B7 RPC \xAEBC\xC9D0");
    tip[127] = 0;
    wcsncpy(nid.szTip, tip, 127);
    nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void h_status(void *u) { update_tip(); win_refresh(); }

static void reconnect_cb(void *ctx, int id) {
    if (!rpc_on) { log_line(0, "[RPC] \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB0\xEA\xB2\xB0 \xEC\xB7\xA8\xEC\x86\x8C \xE2\x80\x94 \xEA\xB7\xB8 \xEC\x82\xAC\xEC\x9D\xB4 RPC \xEA\xB0\x80 \xEA\xBA\xBC\xEC\xA1\x8C\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"); return; }
    core_connect(C);
}

// ── 창 ↔ 본체 (app.h) ───────────────────────────────────────

static void load_settings(void);
static void resolve_lang(void);

core *app_core(void) { return C; }
const char *app_lang(void) { return lang; }
int app_rpc_on(void) { return rpc_on; }
int app_ext_count(void) { return WS ? (int)ws_count(WS) : 0; }
int app_port_ready(void) { return WS != NULL; }
const wchar_t *app_config_path(void) { return settings_path(); }
void app_open_logs(void) { ShellExecuteW(NULL, L"open", log_dir, NULL, NULL, SW_SHOWNORMAL); }

//  RPC 켜고 끄기 — 지금 앱의 toggleRPC 처럼 저장한다 (알파 설정 파일에)
void app_set_rpc(int on) {
    rpc_on = on ? 1 : 0;
    settings_set("rpc", "enabled", json_new_bool(rpc_on));
    settings_save();
    log_line(0, rpc_on ? "[RPC] \xEC\xBC\x9C\xEC\xA7\x90" : "[RPC] \xEA\xBA\xBC\xEC\xA7\x90");
    if (rpc_on) { if (WS) core_connect(C); }
    else { core_clear(C, NULL); core_disconnect(C); }
    drain_rejects();
    update_tip();
    win_refresh();
}

void app_reconnect(void) {
    core_disconnect(C);
    core_host_timer(C, 1000, reconnect_cb, NULL);
    drain_rejects();
}

static void broadcast(const char *json) {
    for (size_t i = 0; WS; i++) { ws_client *c = ws_at(WS, i); if (!c) break; ws_send_text(c, json, strlen(json)); }
}

//  표시 설정 — 저장하고 바로 지금 카드에 반영 (main.js 의 feature-set → applySettings(rpcSettings()))
static void apply_display(void) {
    settings_save();
    load_settings();
    core_apply_settings(C);
    drain_rejects();
    win_refresh();
}
void app_set_rpc_flag(const char *key, int on) { settings_set("rpc", key, json_new_bool(on)); logf_(0, "[Settings] %s = %s", key, on ? "true" : "false"); apply_display(); }
void app_set_time_mode(const char *mode) { settings_set("rpc", "timeMode", json_new_str(mode)); logf_(0, "[Settings] timeMode = %s", mode); apply_display(); }
void app_set_activity_type(int t) { settings_set("rpc", "activityType", json_new_num(t)); logf_(0, "[Settings] activityType = %d", t); apply_display(); }
void app_set_status_display(const char *site, int v) {
    const jval *old = settings_get("rpc", "statusDisplay");
    jval *m = json_new(J_OBJ);
    for (size_t i = 0; old && old->t == J_OBJ && i < old->count; i++) {
        char *s = json_stringify(old->items[i]);
        jval *cp = s ? json_parse(s, strlen(s), NULL) : NULL;
        free(s);
        if (cp) json_obj_set(m, old->keys[i], cp);
    }
    json_obj_set(m, site, json_new_num(v));
    settings_set("rpc", "statusDisplay", m);
    logf_(0, "[Settings] statusDisplay.%s = %d", site, v);
    apply_display();
}
void app_set_idle_minutes(double mins) {
    settings_set("rpc", "idleTimeout", json_new_num(mins));
    settings_save();
    core_set_idle_timeout(C, mins * 60 * 1000);
    if (mins) logf_(0, "[RPC] \xEC\x9E\x90\xEB\x8F\x99 \xED\x95\xB4\xEC\xA0\x9C: %g\xEB\xB6\x84", mins);
    else log_line(0, "[RPC] \xEC\x9E\x90\xEB\x8F\x99 \xED\x95\xB4\xEC\xA0\x9C: \xEC\x95\x88 \xED\x95\xA8");
    drain_rejects();
    win_refresh();
}
//  반응 속도 — 확장에도 알린다 (지금 앱의 BOOSTER 메시지: on 은 예전 확장용)
void app_set_level(const char *lv) {
    snprintf(level, sizeof level, "%s", lv);
    settings_set("responseLevel", NULL, json_new_str(lv));
    settings_save();
    core_set_response_level(C, lv);
    char m[96];
    int booster = !strcmp(lv, "max") || !strcmp(lv, "high");
    snprintf(m, sizeof m, "{\"type\":\"BOOSTER\",\"on\":%s,\"level\":\"%s\"}", booster ? "true" : "false", lv);
    broadcast(m);
    char up[16];
    snprintf(up, sizeof up, "%s", lv);
    for (char *q = up; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    logf_(0, "[Response] \xEB\xB0\x98\xEC\x9D\x91 \xEC\x86\x8D\xEB\x8F\x84: %s", up);
    drain_rejects();
    win_refresh();
}
//  언어 — 카드 문구·창·확장(LANG) 모두 바꾼다
void app_set_lang_setting(const char *v) {
    settings_set("lang", NULL, json_new_str(v));
    settings_save();
    resolve_lang();
    core_set_lang(C, lang);
    char m[64];
    snprintf(m, sizeof m, "{\"type\":\"LANG\",\"lang\":\"%s\"}", lang);
    broadcast(m);
    logf_(0, "[i18n] \xEC\x96\xB8\xEC\x96\xB4: %s", lang);
    drain_rejects();
    update_tip();
    win_refresh();
}

// ── 로그온 시 자동 시작 (지금 앱 main.js 의 applyAutoStart · applyFastStart · setAutoStart 와 같은 규칙) ──
//  rpc.autoStart = 켜고 끄기 (기본 켜짐), fastStart = 어떻게 띄울지 (작업 스케줄러 / Run 키)
//  설치본(%LOCALAPPDATA%\Programs\TOKU RPC Alpha)이 아니면 등록을 건드리지 않는다 — 시험 실행이 부팅 항목을 바꾸지 않게
int app_autostart(void) { const jval *v = settings_get("rpc", "autoStart"); return !(v && v->t == J_BOOL && !v->b); }
int app_faststart(void) { const jval *v = settings_get("fastStart", NULL); return v && v->t == J_BOOL && v->b; }

static int apply_autostart(int on) {
    if (!as_installed()) { log_line(0, "[AutoStart] 설치본이 아니라 부팅 자동 시작 등록은 건드리지 않습니다"); return 1; }
    int ok = as_run_set(on);
    if (!ok) log_line(1, on ? "[AutoStart] 등록 실패" : "[AutoStart] 해제 실패");
    else if (on) logf_(0, "[AutoStart] 부팅 자동 시작 등록 (Run: %s)", CH_RUN_NAME_A);
    else log_line(0, "[AutoStart] 부팅 자동 시작 해제");
    return ok;
}

static int apply_faststart(int en) {
    if (!as_installed()) return 1;   // 설정은 그대로 둔다
    if (en && !app_autostart()) {    // 자동 시작이 꺼져 있으면 남은 작업도 지운다
        as_task_set(0);
        apply_autostart(0);
        return 1;
    }
    if (en) {
        int ok = as_task_set(1);
        if (ok) { logf_(0, "[FastStart] 작업 등록 (%s)", CH_TASK_NAME_A); apply_autostart(0); }   // Run 키 제거 — 두 번 뜨지 않게
        else { log_line(1, "[FastStart] 등록 실패"); apply_autostart(app_autostart()); }
        return ok;
    }
    as_task_set(0);
    log_line(0, "[FastStart] 작업 해제");
    apply_autostart(app_autostart());   // Run 키 복구
    return 1;
}

void app_set_autostart(int next) {
    settings_set("rpc", "autoStart", json_new_bool(next));
    settings_save();
    if (app_faststart()) {
        if (!apply_faststart(1)) { settings_set("fastStart", NULL, json_new_bool(0)); settings_save(); }
    } else if (!apply_autostart(next)) {
        settings_set("rpc", "autoStart", json_new_bool(!next));   // 등록 실패 — 화면이 사실과 어긋나지 않게
        settings_save();
    }
    win_refresh();
}

void app_set_faststart(int v) {
    int ok = apply_faststart(v);
    settings_set("fastStart", NULL, json_new_bool(ok && v));
    settings_save();
    win_refresh();
}

//  시작할 때: 설치 경로가 바뀌었을 수 있으니 지금 경로로 다시 맞춘다
static void startup_autostart(void) {
    if (!as_installed()) { log_line(0, "[AutoStart] 설치본이 아니라 자동 시작 등록은 그대로 둡니다"); return; }
    //  예전 알파 이름으로 남은 부팅 항목은 지운다 — 그 자리(Alpha 폴더)엔 이제 테스트 버전이 있어 부팅 때 그게 뜨면 안 된다
    if (as_remove_named(CH_LEGACY_RUN_NAME, CH_LEGACY_TASK_NAME)) log_line(0, "[AutoStart] 예전 알파 부팅 항목 정리 (TOKU RPC Alpha · Fast Start)");
    else log_line(1, "[AutoStart] 예전 알파 부팅 항목을 지우지 못함 — 다음 시작 때 다시 시도");
    if (app_faststart()) {
        if (!app_autostart()) apply_faststart(1);
        else if (as_task_set(1)) { logf_(0, "[FastStart] 작업 등록 (%s)", CH_TASK_NAME_A); apply_autostart(0); }
        else log_line(1, "[FastStart] 시작 때 다시 등록하지 못함 — 기존 작업·설정은 그대로 두고 다음에 다시 시도");
    } else {
        apply_autostart(app_autostart());
    }
}

static void show_menu(void) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"TOKU RPC");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_DEFAULT, ID_OPEN, L"\xCC3D \xC5F4\xAE30");
    AppendMenuW(m, MF_STRING | (rpc_on ? MF_CHECKED : 0), ID_RPC, L"RPC \xD45C\xC2DC");
    AppendMenuW(m, MF_STRING, ID_RECONNECT, L"Discord \xC7AC\xC5F0\xACB0");
    //  정주행 모드 — 확장이 상태를 알려 줬을 때만 (지금 앱 트레이와 같게)
    if (binge_state() >= 0) AppendMenuW(m, MF_STRING | (binge_state() ? MF_CHECKED : 0), ID_BINGE, L"\xC815\xC8FC\xD589 \xBAA8\xB4DC");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_LOGS, L"\xB85C\xADF8 \xD3F4\xB354 \xC5F4\xAE30");
    AppendMenuW(m, MF_STRING, ID_QUIT, L"\xC885\xB8CC");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void add_tray(void) {
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy(nid.szTip, L"TOKU RPC");
    Shell_NotifyIconW(NIM_ADD, &nid);
    update_tip();
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == msg_taskbar_created) { add_tray(); return 0; }   // 탐색기가 다시 켜지면 아이콘 다시
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            //  지금 앱과 같은 모양의 메뉴 — 못 띄우면 Windows 기본 메뉴로
            POINT pt;
            GetCursorPos(&pt);
            if (!trm_show(pt)) show_menu();
        }
        else if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) win_show();   // 아이콘 클릭 → 창
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: win_show(); break;
        case ID_RPC: app_set_rpc(!rpc_on); break;
        case ID_RECONNECT: app_reconnect(); break;
        case ID_LOGS: app_open_logs(); break;
        case ID_BINGE: if (binge_state() >= 0) app_set_binge(!binge_state()); break;
        case ID_QUIT: running = 0; break;
        }
        return 0;
    case WM_BINGE_LOG: log_line((int)wp, (const char *)lp); free((void *)lp); return 0;   // 정주행 보내는 스레드의 로그
    case WM_BINGE_TEST: win_binge_test_result((int)wp, lp); return 0;
    case WM_BINGE_BF: {   //  보충 기록 결과 — 됐으면 그 링크는 끝 (같은 화를 실수로 두 번 남기지 않게)
        int good = (int)(lp >> 16) & 1, st = (int)(lp & 0xFFFF);
        if (good && BFP.has && !strcmp(BFP.token, BF_SENDING)) bf_pending_free();
        if (TEST_MODE) logf_(0, "[Test] bf sent %d %d", good, st);
        win_bf_sent((int)wp, good, st);
        return 0;
    }
    case WM_ENDSESSION:
        if (wp) running = 0;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ── 업로드 연결 ─────────────────────────────────────────────

static void h_upload(void *u, int job, const char *data_url) { up_push(job, data_url); }

static void pump_uploads(void) {
    up_result r;
    while (up_pop(&r)) {
        if (r.log) log_line(0, r.log);
        core_upload_done(C, r.job, r.ok, r.url, r.keep, r.err);
        up_result_free(&r);
        drain_rejects();
    }
}

static void h_log(void *u, int lvl, const char *text) { log_line(lvl, text); }
static int64_t h_now(void *u) { return now_epoch_ms(); }

// ── 시작 ────────────────────────────────────────────────────

static void load_settings(void) {
    //  rpcSettings() 와 같은 모양
    const jval *rpc = cfg("rpc", NULL);
    jval *s = json_new(J_OBJ);
    const jval *tm = json_get(rpc, "timeMode");
    int none = tm && tm->t == J_STR && !strcmp(tm->s, "none");
    json_obj_set(s, "showTime", json_new_bool(!none));
    static const char *K[] = { "timeMode", "showSeries", "showEpisode", "showThumbnail", "showButtons", "activityType" };
    for (int i = 0; i < 6; i++) {
        const jval *v = json_get(rpc, K[i]);
        if (v) { char *t = json_stringify(v); jval *c2 = t ? json_parse(t, strlen(t), NULL) : NULL; free(t); if (c2) json_obj_set(s, K[i], c2); }
    }
    const jval *sd = json_get(rpc, "statusDisplay");
    //  store.get('rpc.statusDisplay') || {}
    if (js_truthy(sd)) { char *t = json_stringify(sd); jval *c2 = t ? json_parse(t, strlen(t), NULL) : NULL; free(t); json_obj_set(s, "statusDisplay", c2 ? c2 : json_new(J_OBJ)); }
    else json_obj_set(s, "statusDisplay", json_new(J_OBJ));
    core_set_settings(C, s);
    json_free(s);
}

//  썸네일 재호스팅 결과 — 알파 전용 파일에 저장한다 (지금 앱과 같은 모양: { 원래주소: { url, at, keepMs } }, 최대 300건)
//  처음엔 지금 앱의 thumb-cache.json 을 읽기만 한다 (기존 파일은 안 건드림)
static wchar_t DATA_DIR[64] = L"toku-rpc-native";   // 시험 모드면 toku-rpc-native-test
const wchar_t *app_data_dir(void) { return DATA_DIR; }

static void h_save_hosted(void *u) {
    sbuf b; sb_init(&b, 0);
    sb_putc(&b, '{');
    size_t n = core_hosted_count(C), first = n > 300 ? n - 300 : 0;
    int k = 0;
    for (size_t i = first; i < n; i++) {
        const char *url, *hosted;
        double at, keep;
        if (!core_hosted_at(C, i, &url, &hosted, &at, &keep)) continue;
        if (k++) sb_putc(&b, ',');
        json_write_str(&b, url, strlen(url));
        sb_puts(&b, ":{\"url\":");
        json_write_str(&b, hosted, strlen(hosted));
        sb_printf(&b, ",\"at\":%.0f,\"keepMs\":%.0f}", at, keep);
    }
    sb_putc(&b, '}');
    wchar_t p[MAX_PATH], t[MAX_PATH], sub[MAX_PATH];
    _snwprintf(sub, MAX_PATH - 1, L"%ls\\thumb-cache.json", DATA_DIR);
    sub[MAX_PATH - 1] = 0;
    path_in(p, sub);
    _snwprintf(t, MAX_PATH - 1, L"%ls.tmp", p);
    t[MAX_PATH - 1] = 0;
    int ok = 0;
    if (!b.err) {
        HANDLE f = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            ok = WriteFile(f, b.p, (DWORD)b.len, &w, NULL) && w == b.len;
            CloseHandle(f);
            if (ok) ok = MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!ok) DeleteFileW(t);
        }
    }
    if (!ok) log_line(1, "[RPC] \xEC\x8D\xB8\xEB\x84\xA4\xEC\x9D\xBC \xEC\xBA\x90\xEC\x8B\x9C \xEC\xA0\x80\xEC\x9E\xA5 \xEC\x8B\xA4\xED\x8C\xA8");
    sb_free(&b);
}

static void load_thumb_cache(void) {
    wchar_t p[MAX_PATH], sub[MAX_PATH];
    _snwprintf(sub, MAX_PATH - 1, L"%ls\\thumb-cache.json", DATA_DIR);
    sub[MAX_PATH - 1] = 0;
    path_in(p, sub);
    jval *j = read_json_file(p);
    if (!j) { path_in(p, L"toku-rpc\\thumb-cache.json"); j = read_json_file(p); }   // 처음: 지금 앱 것을 읽기만
    if (!j || j->t != J_OBJ) { json_free(j); return; }
    double now = (double)now_epoch_ms();
    int kept = 0, dropped = 0;
    for (size_t i = 0; i < j->count; i++) {
        const jval *e = j->items[i];
        const char *url = json_get_str(e, "url", NULL);
        double at = json_get_num(e, "at", 0), keep = json_get_num(e, "keepMs", 0);
        if (!url || !at) continue;
        if (now - at > (keep ? keep : 2.0 * 3600 * 1000)) { dropped++; continue; }
        core_load_hosted(C, j->keys[i], url, at, keep);
        kept++;
    }
    logf_(0, "[RPC] \xEC\x8D\xB8\xEB\x84\xA4\xEC\x9D\xBC \xEC\xBA\x90\xEC\x8B\x9C \xEB\xB3\xB5\xEC\x9B\x90: %d\xEA\xB1\xB4 (\xEB\xA7\x8C\xEB\xA3\x8C %d\xEA\xB1\xB4 \xEB\xB2\x84\xEB\xA6\xBC)", kept, dropped);
    json_free(j);
}

static void resolve_lang(void) {
    const char *l = json_get_str(CONFIG, "lang", "auto");
    if (!strcmp(l, "ko") || !strcmp(l, "en") || !strcmp(l, "ja")) { strcpy(lang, l); return; }
    wchar_t loc[LOCALE_NAME_MAX_LENGTH] = L"";
    GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
    char a = (char)towlower(loc[0]), b = (char)towlower(loc[1]);
    if (a == 'k' && b == 'o') strcpy(lang, "ko");
    else if (a == 'j' && b == 'a') strcpy(lang, "ja");
    else strcpy(lang, "en");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   // 창 스레드: 파일 고르기 대화상자·WIC (그림 자르기)
    //  하나만 실행
    {
        wchar_t v[64];
        if (GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_PORT", v, 64)) PORT = _wtoi(v);
        if (GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_PIPE", v, 64)) { wcsncpy(PIPE_PREFIX, v, 63); PIPE_PREFIX[63] = 0; }
    }
    HANDLE mutex = CreateMutexW(NULL, TRUE, PORT != PORT_REAL ? CH_MUTEX L"-test" : CH_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS && cmd && wcsstr(cmd, L"--restart")) {
        //  모드를 바꿔 다시 시작: 앞의 것이 정상 종료할 때까지 (최대 20초) 기다렸다가 이어받는다
        for (int i = 0; i < 100; i++) {
            CloseHandle(mutex);
            Sleep(200);
            mutex = CreateMutexW(NULL, TRUE, PORT != PORT_REAL ? CH_MUTEX L"-test" : CH_MUTEX);
            if (GetLastError() != ERROR_ALREADY_EXISTS) break;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        //  두 번째 실행: --quit 이면 돌고 있는 것에 정상 종료를 부탁한다 (설치·업데이트·시험용)
        HWND w = FindWindowW(PORT != PORT_REAL ? CH_CLASS L"Test" : CH_CLASS, NULL);
        if (cmd && wcsstr(cmd, L"--quit")) {
            if (w) PostMessageW(w, WM_COMMAND, ID_QUIT, 0);
        } else if (w && !(cmd && wcsstr(cmd, L"--hidden"))) {
            //  그냥 한 번 더 켜면 돌고 있는 쪽 창을 연다 (기존 앱과 같게)
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(w, WM_COMMAND, ID_OPEN, 0);
        }
        return 0;
    }
    if (cmd && wcsstr(cmd, L"--quit")) { ReleaseMutex(mutex); return 0; }   // 돌고 있는 게 없다
    //  다른 채널(베타 ↔ 알파)이 돌고 있으면 정상 종료를 부탁하고 넘겨받는다 — 7690·Discord 파이프를 같이 못 쓴다 (강제 종료 X)
    int took_over = 0;
    if (PORT == PORT_REAL) {
        HWND other = FindWindowW(CH_OTHER_CLASS, NULL);
        if (other) {
            PostMessageW(other, WM_COMMAND, ID_QUIT, 0);
            for (int i = 0; i < 150 && IsWindow(other); i++) Sleep(100);   //  최대 15초 (끝내기에서 카드를 내리고 연결을 정리한다)
            took_over = IsWindow(other) ? 2 : 1;
        }
    }
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK) return 1;
    //  로그 (기존 앱 폴더가 아니라 따로)
    //  시험 모드(포트를 바꿨을 때)는 로그도 따로 — 진짜 로그와 섞이지 않게
    int test_mode = PORT != PORT_REAL;
    if (test_mode) wcscpy(DATA_DIR, L"toku-rpc-native-test");
    TEST_MODE = test_mode;
    path_in(log_dir, test_mode ? L"toku-rpc-native-test" : L"toku-rpc-native");
    CreateDirectoryW(log_dir, NULL);
    path_in(log_dir, test_mode ? L"toku-rpc-native-test\\logs" : L"toku-rpc-native\\logs");
    CreateDirectoryW(log_dir, NULL);
    wchar_t lp[MAX_PATH];
    _snwprintf(lp, MAX_PATH - 1, L"%ls\\main.log", log_dir);
    lp[MAX_PATH - 1] = 0;
    g_logf = _wfopen(lp, L"ab");

    wchar_t p[MAX_PATH];
    //  App ID·웹훅: 알파·베타 데이터 폴더 사본이 먼저, 없으면 예전 Electron 앱 폴더 (11/8 — Electron 을 지워도 되게)
    path_in(p, L"toku-rpc-native\\secrets.json");
    SECRETS = read_json_file(p);
    if (!SECRETS) { path_in(p, L"toku-rpc\\secrets.json"); SECRETS = read_json_file(p); }
    //  설정: 알파 전용 복사본 (처음엔 기존 앱 config.json 을 복사 — 기존 파일은 안 건드림)
    int copied = 0;
    settings_init(appdata, test_mode ? L"toku-rpc-native-test" : L"toku-rpc-native", &copied);
    CONFIG = settings_root();
    TEST_CH = json_get_bool(CONFIG, "testChannel", 0);
    logf_(0, "=== TOKU RPC \xEC\x8B\x9C\xEC\x9E\x91 (C %s %s) ===", TEST_CH ? "\xEC\x95\x8C\xED\x8C\x8C" : "\xEB\xB2\xA0\xED\x83\x80", app_version());
    if (cmd && wcsstr(cmd, L"--restart")) log_line(0, "[Channel] 모드를 바꿔 다시 시작했습니다");
    if (took_over == 1) log_line(0, "[Channel] 예전 알파 설치본이 돌고 있어 정상 종료를 부탁하고 넘겨받았습니다");
    else if (took_over == 2) log_line(1, "[Channel] 예전 알파가 15초 안에 안 꺼짐 — 포트가 풀릴 때까지 기다립니다");
    if (copied) log_line(0, "[Config] \xEA\xB8\xB0\xEC\xA1\xB4 \xEC\x95\xB1 \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\x84 \xEC\x95\x8C\xED\x8C\x8C \xEC\xA0\x84\xEC\x9A\xA9\xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB3\xB5\xEC\x82\xAC\xED\x96\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4");
    if (!SECRETS) log_line(1, "[Secrets] secrets.json \xEC\x9D\x84 \xEB\xAA\xBB \xEC\x9D\xBD\xEC\x9D\x8C \xE2\x80\x94 Discord \xEC\xB9\xB4\xEB\x93\x9C\xEB\xA5\xBC \xEB\x9D\x84\xEC\x9A\xB8 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4");

    resolve_lang();
    const char *lv = json_get_str(CONFIG, "responseLevel", "");
    snprintf(level, sizeof level, "%s", (!strcmp(lv, "max") || !strcmp(lv, "high") || !strcmp(lv, "normal") || !strcmp(lv, "low")) ? lv : "normal");
    const jval *en = cfg("rpc", "enabled"), *ac = cfg("rpc", "autoConnect");
    rpc_on = !(en && en->t == J_BOOL && !en->b);
    auto_connect = !(ac && ac->t == J_BOOL && !ac->b);

    core_host h;
    memset(&h, 0, sizeof h);
    h.now = h_now; h.log = h_log; h.connect = h_connect; h.destroy = h_destroy; h.request = h_request;
    h.now_hr = now_hr_ms;
    h.ws_send = h_ws_send; h.app_id = h_app_id; h.status = h_status; h.upload = h_upload; h.save_hosted = h_save_hosted;
    h.pid = (int)GetCurrentProcessId();
    C = core_new(&h);
    core_set_pacing_v2(C, TEST_CH);
    if (TEST_CH) log_line(0, "[Channel] 테스트 버전(알파) — 전송 V2: 20초 5번 창(사이트 공유·같은 내용은 안 셈) + 재생·정지·이동 0.35초 모으기");
    logf_(0, "[i18n] \xEC\x96\xB8\xEC\x96\xB4: %s", lang);
    core_set_lang(C, lang);
    //  자동 해제 — 저장된 값이 없으면 5분, 0 이면 안 함
    const jval *it = cfg("rpc", "idleTimeout");
    double mins = 5;
    if (it && it->t != J_NULL) { double n = it->t == J_NUM ? it->n : -1; mins = (n >= 0) ? n : 5; }
    core_set_idle_timeout(C, mins * 60 * 1000);
    if (mins) logf_(0, "[RPC] \xEC\x9E\x90\xEB\x8F\x99 \xED\x95\xB4\xEC\xA0\x9C: %g\xEB\xB6\x84", mins);
    else log_line(0, "[RPC] \xEC\x9E\x90\xEB\x8F\x99 \xED\x95\xB4\xEC\xA0\x9C: \xEC\x95\x88 \xED\x95\xA8");
    core_set_response_level(C, level);
    logf_(0, "[Response] \xEB\xB0\x98\xEC\x9D\x91 \xEC\x86\x8D\xEB\x8F\x84: %s", level);
    load_settings();
    load_thumb_cache();
    //  시험용: 읽은 캐시를 바로 써 본다 (저장 모양·다시 읽기 왕복 점검 — 실제 업로드 없이)
    if (test_mode && GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SAVE_HOSTED", NULL, 0)) h_save_hosted(NULL);
    if (!test_mode) startup_autostart();   // 시험 모드는 부팅 항목을 절대 안 건드림
    if (!rpc_on) core_disconnect(C);   // 꺼 둔 채 저장했으면 꺼짐으로 시작

    const jval *au = json_get(SECRETS, "avatarUpload");
    up_start(json_get_str(au, "url", NULL), json_get_str(au, "token", NULL));
    logf_(0, "[Upload] Cloudflare Worker: %s", json_get_str(au, "url", NULL) ? "\xEC\x82\xAC\xEC\x9A\xA9" : "\xEC\x97\x86\xEC\x9D\x8C (\xEA\xB3\xB5\xEA\xB0\x9C \xED\x98\xB8\xEC\x8A\xA4\xED\x8A\xB8\xEB\xA7\x8C)");

    //  보이지 않는 메시지 창 + 트레이
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = PORT != PORT_REAL ? CH_CLASS L"Test" : CH_CLASS;   // 시험 모드는 이름을 달리 (진짜 알파를 건드리지 않게)
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"TOKU RPC", 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    binge_init(hwnd);
    win_init(inst);
    trm_init(inst, hwnd);
    add_tray();
    //  직접 켜면 창도 연다 (--hidden 이면 트레이만 — 기존 앱과 같게). 시험 모드는 화면에 안 띄움
    if (!test_mode && !(cmd && wcsstr(cmd, L"--hidden"))) win_show();

    while (running) {
        try_listen();
        //  확장 소켓·Discord 파이프에 받을 것이 오면 그 순간 깬다 (지연이 Windows 타이머 칸 15.6ms 에 묶이지 않게)
        //  시간 기다림은 타이머·대기 확인용 — 받는 쪽은 이벤트가 맡는다
        HANDLE hs[MAXIMUM_WAIT_OBJECTS - 1];
        DWORD nh = 0;
        if (WS && ws_event(WS)) hs[nh++] = (HANDLE)ws_event(WS);
        for (dconn *x = conns; x && nh < MAXIMUM_WAIT_OBJECTS - 1; x = x->next) if (x->d && !x->dead && dipc_event(x->d)) hs[nh++] = (HANDLE)dipc_event(x->d);
        int64_t wait = PING.active ? 1 : 10;   //  지연 재는 동안은 마감을 바로 보게
        int64_t d = core_next_deadline(C);
        if (d >= 0) { int64_t left = d - now_epoch_ms(); if (left < wait) wait = left < 0 ? 0 : left; }
        MsgWaitForMultipleObjectsEx(nh, nh ? hs : NULL, (DWORD)wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) running = 0;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (WS) ws_poll(WS, 0);
        poll_discord();
        pump_uploads();
        pick_tick();
        binge_tick();
        if (PING.active && now_epoch_ms() >= PING.deadline) ping_finish();
        rsv_tick();
        if (LAT_LOG && !PING.active) {
            int cl, dc; double em, dm;
            LAT_LOG = 0;
            app_ping_get(&cl, &em);
            app_discord_latency(&dc, &dm);
            logf_(0, "[Test] latency ext=%.3f clients=%d discord=%.3f connected=%d raw_ext=%.3f", em, cl, dm, dc, PING.result);
        }
        if (TRAY_SNAP && !PING.active) {
            wchar_t sd[MAX_PATH];
            TRAY_SNAP = 0;
            if (GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SNAP", sd, MAX_PATH)) logf_(0, "[Test] tray snap %d", trm_snapshot(sd));
        }
        while (core_next_deadline(C) >= 0 && core_next_deadline(C) <= now_epoch_ms()) { core_fire_one(C); drain_rejects(); }
        drain_rejects();
    }

    //  끝내기: 카드를 내리고 Discord 연결을 정리한 뒤 나간다
    log_line(0, "[Shutdown] \xEC\xA2\x85\xEB\xA3\x8C");
    if (test_mode) {
        //  시험용: 화면에 띄우지 않고 탭 그림을 BMP 로 남긴다
        wchar_t snap[MAX_PATH];
        if (GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SNAP", snap, MAX_PATH)) {
            char t[64];
            int got = win_snapshot(snap);
            snprintf(t, sizeof t, "[Snap] %d", got);
            log_line(0, t);
        }
    }
    core_clear(C, NULL);
    core_disconnect(C);
    drain_rejects();
    if (WS) ws_server_close(WS);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    up_stop();
    if (g_logf) fclose(g_logf);
    ReleaseMutex(mutex);
    return 0;
}
