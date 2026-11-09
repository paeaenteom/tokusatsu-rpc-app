// 카드 엔진 테스트 진행기 — 시나리오(JSON 줄)를 받아 가짜 시계·가짜 Discord 위에서 core 를 돌리고 일어난 일을 JSON 줄로 낸다
//  core-diff.js 가 같은 시나리오를 진짜 discord-rpc.js·extension-bridge.js 에 넣은 결과와 비교한다
//  ⚠ 진짜 Discord·포트에는 닿지 않는다 (전부 메모리 안)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/base/json.h"
#include "../src/base/sbuf.h"
#include "../src/core/rpc.h"

#define READY_DELAY 5

static core *C;
static int64_t clock_ms;
static int discord_up = 1;
static int reply_stall = 0;
static int rpc_on = 1;
static char no_app[256] = "";

typedef struct { int conn; int open; int destroyed; } connrec;
static connrec conns[4096];
static int nconns;

typedef struct { int req; int ok; int conn; double type, sdt; int is_set; } pend;
static pend pending[65536];   // 답을 기다리는 요청 (stall 모드에서 쌓인다)
static int npend;
static pend queue_[65536];    // 이번 일이 끝나면 넘길 답
static int nq;

static connrec *conn_of(int id) { for (int i = 0; i < nconns; i++) if (conns[i].conn == id) return &conns[i]; return NULL; }

static void out_rec(const char *k, const char *extra) {
    printf("{\"t\":%lld,\"k\":\"%s\"%s}\n", (long long)clock_ms, k, extra ? extra : "");
}

static int64_t h_now(void *u) { return clock_ms; }
static void h_log(void *u, int lvl, const char *text) {
    sbuf b; sb_init(&b, 0);
    sb_printf(&b, ",\"lvl\":%d,\"text\":", lvl);
    json_write_str(&b, text, strlen(text));
    out_rec("log", b.p);
    sb_free(&b);
}

static void ready_timer(void *ctx, int id) {
    int conn = (int)(intptr_t)ctx;
    connrec *r = conn_of(conn);
    if (!r || r->destroyed) return;
    if (discord_up) { r->open = 1; core_ready(C, conn, "tester"); }
    else core_login_failed(C, conn, "Could not connect");
}

static void h_connect(void *u, int conn, const char *site, const char *app) {
    connrec *r = &conns[nconns++];
    r->conn = conn; r->open = 0; r->destroyed = 0;
    sbuf b; sb_init(&b, 0);
    sb_printf(&b, ",\"conn\":%d,\"site\":\"%s\"", conn, site);
    out_rec("connect", b.p);
    sb_free(&b);
    core_host_timer(C, READY_DELAY, ready_timer, (void *)(intptr_t)conn);
}

// 이 연결에서 답을 기다리던 요청을 모두 거부 ('connection closed')
static void reject_pending(int conn) {
    int k = 0;
    for (int i = 0; i < npend; i++) {
        if (pending[i].conn == conn) { pend p = pending[i]; p.ok = 0; queue_[nq++] = p; }
        else pending[k++] = pending[i];
    }
    npend = k;
}

static void h_destroy(void *u, int conn) {
    char e[48];
    snprintf(e, sizeof e, ",\"conn\":%d", conn);
    out_rec("destroy", e);
    connrec *r = conn_of(conn);
    if (r) { r->open = 0; r->destroyed = 1; }
    reject_pending(conn);
}

static int h_request(void *u, int conn, int req, const char *args) {
    connrec *r = conn_of(conn);
    if (!r || !r->open) return 0;
    sbuf b; sb_init(&b, 0);
    sb_printf(&b, ",\"conn\":%d,\"args\":", conn);
    sb_puts(&b, args);
    out_rec("req", b.p);
    sb_free(&b);
    jval *a = json_parse(args, strlen(args), NULL);
    const jval *act = json_get(a, "activity");
    pend p = { req, 1, conn, json_get_num(act, "type", 0), json_get_num(act, "status_display_type", 0), act != NULL };
    json_free(a);
    if (reply_stall) pending[npend++] = p;
    else queue_[nq++] = p;
    return 1;
}

static void h_ws(void *u, int client, const char *json) {
    sbuf b; sb_init(&b, 0);
    sb_printf(&b, ",\"client\":%d,\"json\":", client);
    json_write_str(&b, json, strlen(json));
    out_rec("ws", b.p);
    sb_free(&b);
}

static const char *TEST_IDS[][2] = { { "ttfc", "100000000000000001" }, { "imagination", "100000000000000002" }, { "youtube", "100000000000000003" },
    { "disneyplus", "100000000000000004" }, { "chzzk", "100000000000000005" }, { "danime", "100000000000000006" } };
static const char *h_app(void *u, const char *site) {
    char tag[32];
    snprintf(tag, sizeof tag, ",%s,", site);
    if (strstr(no_app, tag)) return "";
    for (int i = 0; i < 6; i++) if (!strcmp(TEST_IDS[i][0], site)) return TEST_IDS[i][1];
    return "";
}

// 이번 일이 끝났다 → 쌓인 답을 넘긴다 (JS 의 Promise 처리 순서)
static void drain(void) {
    for (int i = 0; i < nq; i++) {
        pend p = queue_[i];
        if (p.ok) {
            if (p.is_set) {
                jval *d = json_new(J_OBJ);
                json_obj_set(d, "type", json_new_num(p.type));
                json_obj_set(d, "status_display_type", json_new_num(p.sdt));
                core_reply(C, p.req, 1, d, NULL);
                json_free(d);
            } else {
                jval *nul = json_new(J_NULL);
                core_reply(C, p.req, 1, nul, NULL);
                json_free(nul);
            }
        } else core_reply(C, p.req, 0, NULL, "connection closed");
    }
    nq = 0;
}

static void run_until(int64_t t) {
    for (;;) {
        int64_t d = core_next_deadline(C);
        if (d < 0 || d > t) break;
        clock_ms = d;
        core_fire_one(C);
        drain();
    }
    clock_ms = t;
}

static void reconnect_timer(void *ctx, int id) {
    if (!rpc_on) {
        h_log(NULL, 0, "[RPC] \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB0\xEA\xB2\xB0 \xEC\xB7\xA8\xEC\x86\x8C \xE2\x80\x94 \xEA\xB7\xB8 \xEC\x82\xAC\xEC\x9D\xB4 RPC \xEA\xB0\x80 \xEA\xBA\xBC\xEC\xA1\x8C\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4");
        return;
    }
    core_connect(C);
}

int main(void) {
    core_host h;
    memset(&h, 0, sizeof h);
    h.now = h_now; h.log = h_log; h.connect = h_connect; h.destroy = h_destroy; h.request = h_request;
    h.ws_send = h_ws; h.app_id = h_app; h.pid = 0;
    static char line[1 << 22];
    int started = 0;
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        jval *e = json_parse(line, n && line[n - 1] == '\n' ? n - 1 : n, NULL);
        if (!e) { fprintf(stderr, "bad line\n"); return 2; }
        const char *ev = json_get_str(e, "ev", "");
        int64_t t = (int64_t)json_get_num(e, "t", 0);
        if (!strcmp(ev, "init")) {
            clock_ms = t;
            const jval *na = json_get(e, "noApp");
            strcpy(no_app, ",");
            for (size_t i = 0; na && na->t == J_ARR && i < na->count; i++) { strncat(no_app, json_str(na->items[i], ""), 40); strcat(no_app, ","); }
            C = core_new(&h);
            if (getenv("TOKU_CORE_V2")) core_set_pacing_v2(C, 1);   //  테스트 버전 전송 (presence-v2-test.js)
            started = 1;
            json_free(e);
            continue;
        }
        if (!started) { json_free(e); continue; }
        run_until(t);
        if (!strcmp(ev, "msg")) core_ext_message(C, (int)json_get_num(e, "client", 1), json_get(e, "msg"));
        else if (!strcmp(ev, "discord")) {
            int up = json_get_bool(e, "up", 1);
            if (!up && discord_up) {
                //  붙어 있던 연결 모두 끊김 (만든 순서대로): 기다리던 요청 거부 → disconnected
                for (int i = 0; i < nconns; i++) {
                    if (!conns[i].open) continue;
                    conns[i].open = 0;
                    reject_pending(conns[i].conn);
                    core_disconnected(C, conns[i].conn);
                }
            }
            discord_up = up;
        } else if (!strcmp(ev, "reply")) reply_stall = !strcmp(json_get_str(e, "mode", "ok"), "stall");
        else if (!strcmp(ev, "settings")) { core_set_settings(C, json_get(e, "settings")); core_apply_settings(C); }
        else if (!strcmp(ev, "lang")) core_set_lang(C, json_get_str(e, "lang", "ko"));
        else if (!strcmp(ev, "idle")) core_set_idle_timeout(C, json_get_num(e, "ms", 0));
        else if (!strcmp(ev, "level")) core_set_response_level(C, json_get_str(e, "level", "normal"));
        else if (!strcmp(ev, "rpc")) {
            rpc_on = json_get_bool(e, "on", 1);
            if (rpc_on) core_connect(C); else { core_clear(C, NULL); core_disconnect(C); }
        } else if (!strcmp(ev, "reconnect")) { core_disconnect(C); core_host_timer(C, 1000, reconnect_timer, NULL); }
        else if (!strcmp(ev, "hosted")) core_thumb_hosted(C, json_get_str(e, "url", ""), json_get_str(e, "hosted", ""), json_get_num(e, "keep", 0));
        else if (!strcmp(ev, "end")) { /* run_until 로 끝 */ }
        drain();
        fflush(stdout);
        json_free(e);
    }
    core_free(C);
    return 0;
}
