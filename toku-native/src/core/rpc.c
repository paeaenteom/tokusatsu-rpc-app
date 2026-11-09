// Discord 카드 엔진 — discord-rpc.js 와 extension-bridge.js(카드 부분)를 줄 단위로 옮겼다
//  JS 의 동작(값이 없으면 undefined, || 기본값, Math.round 등)을 그대로 따른다 — 결과가 한 글자도 다르면 안 된다
//  원본의 '왜' 주석은 해당 함수 위에 짧게 남긴다 (자세한 사연은 원본 파일에)
#include "../base/median.h"
#include "rpc.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/sbuf.h"
#include "../base/utf.h"
#include "js.h"
#include "url.h"

// ── 상수 (discord-rpc.js 와 같은 값) ─────────────────────────
#define RPC_BURST 5
#define RPC_REFILL_MS 4000
//  ── 전송 V2 (테스트 버전(알파) 모드에서만 — core_set_pacing_v2, 2026-11-08) ──
//  Discord 클라이언트 웹 JS 실측(ConnectionStore presence didCommit): 서버로는 20초 안에 5번까지,
//  넘치면 마지막 상태만 가장 오래된 칸이 풀릴 때. 내용이 같은 갱신은 세지 않는다(깊은 비교).
//  모든 RPC 앱이 한 presence 를 같이 쓰므로 창은 허브 하나에 둔다. 재생·정지·이동은 잠깐 모았다가 마지막 것만
#define V2_WINDOW_MS 20000
#define V2_WINDOW_N 5
#define V2_SETTLE_MS 350
#define RPC_STALL_MS 15000
#define RPC_SLOW_MS 3000
#define MSG_AGE_CAP_S 60
#define WATCH_ENDS_BROWSING_MS 30000
#define REVIVE_MAX_SILENCE_MS (10 * 60 * 1000)
#define THUMB_TTL (2.0 * 60 * 60 * 1000)
#define HOSTED_CACHE_MAX 300
#define CLIENT_CACHE_MAX 60

typedef struct { const char *name; int refresh_ms; int drift; } level_t;
static const level_t LEVELS[] = { { "max", 15000, 2 }, { "high", 20000, 3 }, { "normal", 30000, 5 }, { "low", 60000, 8 } };
static const level_t *tuning_of(const char *lv) {
    if (lv) for (int i = 0; i < 4; i++) if (!strcmp(LEVELS[i].name, lv)) return &LEVELS[i];
    return &LEVELS[2];
}
static int level_valid(const char *lv) { if (!lv) return 0; for (int i = 0; i < 4; i++) if (!strcmp(LEVELS[i].name, lv)) return 1; return 0; }

// 사이트 표 — ⚠ 애플리케이션 ID 는 여기 두지 않는다 (CLAUDE.md: 코드에 쓰지 않음). host->app_id 로만 읽는다
typedef struct { const char *id, *name, *home, *logo, *play, *pause, *live; int live_dvr_fallback; } site_def;
static const site_def SITES[] = {
    { "ttfc", "\xE6\x9D\xB1\xE6\x98\xA0\xE7\x89\xB9\xE6\x92\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xA9\xE3\x83\x96", "https://pc.tokusatsu-fc.jp/", "ttfc_logo", "play", "pause", NULL, 1 },
    { "imagination", "TSUBURAYA IMAGINATION", "https://imagination.m-78.jp/", "tsuburaya_imagination_logo", "play", "pause", NULL, 1 },
    { "youtube", "YouTube", "https://www.youtube.com/", "logo", "play", "pause", "live", 1 },
    { "disneyplus", "Disney+", "https://www.disneyplus.com/", "disney__logo", "play", "pause", NULL, 1 },
    { "chzzk", "CHZZK", "https://chzzk.naver.com/", "chzzk_logo", "play", "pause", "live", 0 },
    { "danime", "d\xE3\x82\xA2\xE3\x83\x8B\xE3\x83\xA1\xE3\x82\xB9\xE3\x83\x88\xE3\x82\xA2", "https://animestore.docomo.ne.jp/animestore/", "danime_logo", "play", "pause", NULL, 1 },
};
#define NSITES 6
static const site_def *site_find(const char *id) {
    if (id) for (int i = 0; i < NSITES; i++) if (!strcmp(SITES[i].id, id)) return &SITES[i];
    return NULL;
}
static const site_def *site_of(const char *id) { const site_def *s = site_find(id); return s ? s : &SITES[0]; }

// rpc.* 문구 (locales/{ko,en,ja}.js 와 같은 값). 모르는 언어는 영어 (i18n.js 의 FALLBACK)
enum { T_VIEW, T_WATCHING, T_PAUSED, T_LIVE, T_BROWSING };
static const char *TR[3][5] = {
    { "\xEC\x82\xAC\xEC\x9D\xB4\xED\x8A\xB8\xEC\x97\x90\xEC\x84\x9C \xEB\xB3\xB4\xEA\xB8\xB0",
      "{site} \xEC\x8B\x9C\xEC\xB2\xAD \xEC\xA4\x91",
      "{site}\xE3\x83\xBB\xEC\x9D\xBC\xEC\x8B\x9C\xEC\xA0\x95\xEC\xA7\x80",
      "{site} \xEB\x9D\xBC\xEC\x9D\xB4\xEB\xB8\x8C \xEC\x8B\x9C\xEC\xB2\xAD \xEC\xA4\x91",
      "\xEB\x91\x98\xEB\x9F\xAC\xEB\xB3\xB4\xEB\x8A\x94 \xEC\xA4\x91" },
    { "View on site", "Watching {site}", "{site}\xE3\x83\xBBPaused", "Watching {site} live", "Browsing" },
    { "\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x88\xE3\x81\xA7\xE8\xA6\x8B\xE3\x82\x8B",
      "{site} \xE8\xA6\x96\xE8\x81\xB4\xE4\xB8\xAD",
      "{site}\xE3\x83\xBB\xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2\xE4\xB8\xAD",
      "{site} \xE3\x83\xA9\xE3\x82\xA4\xE3\x83\x96\xE8\xA6\x96\xE8\x81\xB4\xE4\xB8\xAD",
      "\xE9\x96\xB2\xE8\xA6\xA7\xE4\xB8\xAD" },
};
static void tr(sbuf *b, const char *lang, int key, const char *site) {
    int li = lang && !strcmp(lang, "ko") ? 0 : lang && !strcmp(lang, "ja") ? 2 : 1;
    const char *s = TR[li][key];
    if (!site) { sb_puts(b, s); return; }
    const char *k = strstr(s, "{site}");
    if (!k) { sb_puts(b, s); return; }
    sb_putn(b, s, (size_t)(k - s));
    sb_puts(b, site);
    sb_puts(b, k + 6);
}

// ── 작은 도우미 ─────────────────────────────────────────────

static char *dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}
static char *dupn(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
static void set_str(char **dst, const char *src) { char *n = dup(src); free(*dst); *dst = n; }
static int seq(const char *a, const char *b) { return !strcmp(a ? a : "", b ? b : ""); }
static const char *nz(const char *s) { return s ? s : ""; }
// ASCII 대소문자 무시 비교 (n 바이트)
static int strncasecmp_portable(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return x - y;
    }
    return 0;
}
static int is_http_url(const char *u) {
    return u && (!strncasecmp_portable(u, "http://", 7) || !strncasecmp_portable(u, "https://", 8));
}

// ── 썸네일 캐시 (넣은 순서를 지키는 Map) ─────────────────────

typedef struct { char *url, *hosted; double at, keep; } thumb_e;
typedef struct { thumb_e *v; size_t n, cap; } tmap;
static long tm_find_e(const tmap *m, const char *url) { for (size_t i = 0; i < m->n; i++) if (!strcmp(m->v[i].url, url)) return (long)i; return -1; }
static void tmap_del_at(tmap *m, size_t i) {
    free(m->v[i].url); free(m->v[i].hosted);
    memmove(m->v + i, m->v + i + 1, (m->n - i - 1) * sizeof *m->v);
    m->n--;
}
static void tmap_del(tmap *m, const char *url) { long i = tm_find_e(m, url); if (i >= 0) tmap_del_at(m, (size_t)i); }
// Map.set — 이미 있으면 자리는 그대로 두고 값만 바꾼다
static void tmap_set(tmap *m, const char *url, const char *hosted, double at, double keep) {
    long i = tm_find_e(m, url);
    if (i >= 0) { set_str(&m->v[i].hosted, hosted); m->v[i].at = at; m->v[i].keep = keep; return; }
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16;
        thumb_e *nv = (thumb_e *)realloc(m->v, cap * sizeof *nv);
        if (!nv) return;
        m->v = nv;
        m->cap = cap;
    }
    m->v[m->n].url = dup(url);
    m->v[m->n].hosted = dup(hosted);
    m->v[m->n].at = at;
    m->v[m->n].keep = keep;
    m->n++;
}
static void tmap_free(tmap *m) { while (m->n) tmap_del_at(m, m->n - 1); free(m->v); m->v = NULL; m->cap = 0; }

// 문자열 → 문자열 Map (넣은 순서). 키는 JS 값의 종류를 붙인 글자 (같은 '5' 라도 숫자와 문자열은 다른 키)
typedef struct { char **k, **v; size_t n, cap; } smap;
static long smap_find(const smap *m, const char *k) { for (size_t i = 0; i < m->n; i++) if (!strcmp(m->k[i], k)) return (long)i; return -1; }
static const char *smap_get(const smap *m, const char *k) { long i = smap_find(m, k); return i >= 0 ? m->v[i] : NULL; }
static void smap_del_at(smap *m, size_t i) {
    free(m->k[i]); free(m->v[i]);
    memmove(m->k + i, m->k + i + 1, (m->n - i - 1) * sizeof(char *));
    memmove(m->v + i, m->v + i + 1, (m->n - i - 1) * sizeof(char *));
    m->n--;
}
static void smap_set(smap *m, const char *k, const char *v) {
    long i = smap_find(m, k);
    if (i >= 0) { set_str(&m->v[i], v); return; }
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16;
        char **nk = (char **)realloc(m->k, cap * sizeof(char *));
        if (!nk) return;
        m->k = nk;
        char **nv = (char **)realloc(m->v, cap * sizeof(char *));
        if (!nv) return;
        m->v = nv;
        m->cap = cap;
    }
    m->k[m->n] = dup(k);
    m->v[m->n] = dup(v);
    m->n++;
}
static void smap_free(smap *m) { while (m->n) smap_del_at(m, m->n - 1); free(m->k); free(m->v); memset(m, 0, sizeof *m); }

// JS 값 → Map 키 (종류 표시 + 글자)
static void key_of(sbuf *b, const jval *v) {
    if (!v) { sb_puts(b, "\x01u"); return; }
    switch (v->t) {
    case J_STR: sb_putc(b, 's'); sb_putn(b, v->s, v->slen); break;
    case J_NUM: sb_putc(b, 'n'); js_number_str(b, v->n); break;
    case J_BOOL: sb_puts(b, v->b ? "btrue" : "bfalse"); break;
    case J_NULL: sb_puts(b, "\x01n"); break;
    default: sb_printf(b, "o%p", (const void *)v); break;   // 객체는 매번 다른 키 (JS 와 같다)
    }
}

// ── 활동(카드) 객체 ─────────────────────────────────────────

typedef struct activity {
    int ref;
    char *details, *state, *large_key, *large_text, *small_key, *small_text;
    int has_start, has_end;
    double start, end;
    double type, sdt;
    int has_buttons;
    char *btn_label, *btn_url;
    char *url_for;   // _urlOf (창에 쓸 '사이트에서 보기' 주소)
} activity;

static activity *act_new(void) { activity *a = (activity *)calloc(1, sizeof *a); if (a) a->ref = 1; return a; }
static activity *act_ref(activity *a) { if (a) a->ref++; return a; }
static void act_unref(activity *a) {
    if (!a || --a->ref > 0) return;
    free(a->details); free(a->state); free(a->large_key); free(a->large_text); free(a->small_key); free(a->small_text);
    free(a->btn_label); free(a->btn_url); free(a->url_for);
    free(a);
}
static void act_set(activity **dst, activity *a) { activity *old = *dst; *dst = act_ref(a); act_unref(old); }

static int js_truthy_num(double x) { return !(x == 0 || isnan(x)); }

// ── 본체 구조 ───────────────────────────────────────────────

typedef struct settings_ref { int ref; jval *v; } settings_ref;
static settings_ref *sref_new(const jval *v) {
    settings_ref *r = (settings_ref *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->ref = 1;
    if (v) { char *s = json_stringify(v); if (s) { r->v = json_parse(s, strlen(s), NULL); free(s); } }
    return r;
}
static settings_ref *sref_ref(settings_ref *r) { if (r) r->ref++; return r; }
static void sref_unref(settings_ref *r) { if (!r || --r->ref > 0) return; json_free(r->v); free(r); }

typedef struct presence presence;
typedef struct req req;

struct presence {
    core *hub;
    char site_id[16];
    char *client_id;
    int conn;               // this.client (0 = null)
    int connected, logging_in;
    int reconnect_timer;
    double reconnect_wait;  // 0 = undefined
    char *lang;
    // currentState
    struct {
        int isWatching;
        double startedAtMs;
        int isLive;
        double liveStartedAt;
        int isPlaying;
        char *site, *siteName, *seriesName, *episodeTitle, *episodeNumber;
        double currentTime, duration;
        char *thumbnail, *pageTitle, *pageDetail, *pageBanner, *pageUrl;
        double playbackRate;    // NaN = undefined
        settings_ref *settings; // NULL = undefined
    } s;
    struct {
        char *episode;
        int playing;            // -1 = null
        double currentTime, duration;
        char *thumbnail;        // NULL = undefined
        int isLive;
        double liveStartedAt, rate, at;
    } ls;
    double browsingStart;
    char *browsingUrl;
    char *lastBrowsingKey;
    int shownType;              // 0 null · 1 watching · 2 browsing
    double lastApiCall;
    int pendingUpdate;          // 0 null · 1 watching · 2 browsing
    int pendingTimer, refreshTimer, idleTimer, applyTimer;
    int settleTimer;            // V2: 재생·정지·이동을 모으는 중
    char *lastArgs;             // V2: 마지막으로 보낸 SET_ACTIVITY 내용 (같으면 창에서 안 센다)
    double idleTimeout, idleDeadline;
    activity *lastActivity, *afterSwitch;
    char *shownUrl;
    tmap cache;
    double lastInputAt, watchSince, shownStartTs;
    int img_logged;             // _lastLoggedImage 가 정해졌는가
    char *lastLoggedImage;
    char *lastTypeLog;          // NULL = undefined
    int tokens_set;
    double tokens, tokenAt;
    char *level;                // NULL = undefined
    double stalls, slowLogAt, rttMs, rttAt;
    med5 rttHist;               // 표시용: 최근 왕복 5개 (중간값)
    char *switchingTo;
    char *user;             // client.user.username (창 표시용)
};

struct req {
    int id;
    presence *p;
    int conn;
    int is_set;
    int typelog;
    double type, sdt;
    double sent_at, hr_at;
    int stall_timer;
    int done;
    req *next;
};

typedef struct ctxnode { void *p; struct ctxnode *next; } ctxnode;

struct core {
    core_host h;
    tm_queue tq;
    int v2;                      // 1 = 전송 V2 (테스트 버전) — 시작할 때 한 번 정한다
    double winAt[V2_WINDOW_N];   // V2: 내용이 바뀐 전송 시각 (오래된 것부터)
    int winN;
    presence *clients[NSITES];   // 만든 순서 (Map 순서)
    int nclients;
    char *lang;
    int enabled;
    int has_idle;
    double idleTimeout;
    char *responseLevel;
    tmap hosted;
    smap uploading;              // 올리는 중인 원본 주소 (값은 안 씀)
    int saveTimer;
    smap unknownSite, noAppId;
    int next_conn, next_req;
    req *reqs;
    ctxnode *ctxs;
    // 브리지 상태
    settings_ref *settings;
    smap videoLogKeys, browsingLogKeys, thumbSites;
    struct { char **k; double *at; double *n; size_t cnt, cap; } asks;
    struct thumbjob *jobs;
    int next_job;
};

typedef struct thumbjob {
    int id;
    char *url, *data;
    int attempt;
    struct thumbjob *next;
} thumbjob;

static double now_ms(core *c) { return (double)c->h.now(c->h.user); }

static void logf_(core *c, int lvl, const char *fmt, ...) {
    sbuf b;
    sb_init(&b, 0);
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n >= 0 && sb_reserve(&b, (size_t)n)) { vsnprintf(b.p, (size_t)n + 1, fmt, ap2); b.len = (size_t)n; }
    va_end(ap2);
    if (!b.err && c->h.log) c->h.log(c->h.user, lvl, b.p ? b.p : "");
    sb_free(&b);
}
static void log_sb(core *c, int lvl, sbuf *b) { if (!b->err && c->h.log) c->h.log(c->h.user, lvl, b->p ? b->p : ""); }

static void *ctx_alloc(core *c, size_t n) {
    ctxnode *cn = (ctxnode *)malloc(sizeof *cn);
    void *p = calloc(1, n);
    if (!cn || !p) { free(cn); free(p); return NULL; }
    cn->p = p;
    cn->next = c->ctxs;
    c->ctxs = cn;
    return p;
}
static void ctx_free(core *c, void *p) {
    for (ctxnode **pp = &c->ctxs; *pp; pp = &(*pp)->next)
        if ((*pp)->p == p) { ctxnode *d = *pp; *pp = d->next; free(d->p); free(d); return; }
}

static int set_timer(core *c, int64_t delay, int repeat, tm_fn fn, void *ctx) {
    return tm_set(&c->tq, c->h.now(c->h.user), delay, repeat, fn, ctx);
}
static void clr(core *c, int *id) { if (*id) { tm_clear(&c->tq, *id); *id = 0; } }

static void notify_status(presence *p) { if (p->hub->h.status) p->hub->h.status(p->hub->h.user); }

static const char *app_id_of(core *c, const char *site) {
    const char *a = c->h.app_id ? c->h.app_id(c->h.user, site) : NULL;
    return (a && *a) ? a : "";
}

static void forceUpdate(presence *p, int type);
static void p_connect(presence *p);
static void p_clearActivity(presence *p);
static void reset_lastSent(presence *p);
static void idleExpire(presence *p);
static void startRefreshTimer(presence *p);
static void stopRefreshTimer(presence *p);
static void armIdle(presence *p, double deadline);
static void sendNow(presence *p, activity *a);
static void resetIdleTimer(presence *p);

// ── 이미지 ──────────────────────────────────────────────────

//  디스코드 이미지 프록시가 직접 불러올 수 있는 호스트 (정사각본이 준비될 때까지의 임시 표시용)
//   /\.cloudfront\.net\//i · /\bdisney\.images\.edge\.bamgrid\.com\//i
static int can_discord_load(const char *url) {
    size_t n = strlen(url);
    static const char a[] = ".cloudfront.net/";
    static const char b[] = "disney.images.edge.bamgrid.com/";
    for (size_t i = 0; i + sizeof a - 1 <= n; i++) if (!strncasecmp_portable(url + i, a, sizeof a - 1)) return 1;
    for (size_t i = 0; i + sizeof b - 1 <= n; i++) {
        if (strncasecmp_portable(url + i, b, sizeof b - 1)) continue;
        char prev = i ? url[i - 1] : 0;
        int pw = (prev >= 'A' && prev <= 'Z') || (prev >= 'a' && prev <= 'z') || (prev >= '0' && prev <= '9') || prev == '_';
        if (!pw) return 1;   // \b — 앞이 낱말 글자가 아니어야
    }
    return 0;
}

// 재호스팅 주소 (만료분은 무효) — 클라이언트 캐시에 없으면 허브 캐시
static const char *cachedThumb(presence *p, const char *url) {
    long i = tm_find_e(&p->cache, url);
    thumb_e *hit = i >= 0 ? &p->cache.v[i] : NULL;
    if (!hit) { long k = tm_find_e(&p->hub->hosted, url); hit = k >= 0 ? &p->hub->hosted.v[k] : NULL; }
    if (!hit) return "";
    double keep = js_truthy_num(hit->keep) ? hit->keep : THUMB_TTL;
    if (now_ms(p->hub) - hit->at > keep) { tmap_del(&p->cache, url); return ""; }
    return hit->hosted;
}

static int asset_key_like(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 32) return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

// 이미지 주소 → 디스코드가 그릴 수 있는 것: 재호스팅본 > 직접 되는 호스트 원본 > 로고
static char *resolveImage(presence *p, const char *url, const char *logo) {
    if (!url || !*url) return dup(logo);
    if (!is_http_url(url)) return dup(asset_key_like(url) ? url : logo);
    const char *cached = cachedThumb(p, url);
    if (*cached) return dup(cached);
    if (can_discord_load(url)) return dup(url);
    return dup(logo);
}

// ── 연결 ────────────────────────────────────────────────────

static void p_request_clear(presence *p);

static void scheduleReconnect_cb(void *ctx, int id) {
    presence *p = (presence *)ctx;
    p->reconnect_timer = 0;
    p_connect(p);
}

//  계속 실패하면 간격을 늘린다 (10s → 20s → 40s … 최대 2분)
static void scheduleReconnect(presence *p) {
    if (p->reconnect_timer) return;
    double wait = p->reconnect_wait ? p->reconnect_wait : 10000;
    p->reconnect_wait = fmin(wait * 2, 120000);
    p->reconnect_timer = set_timer(p->hub, (int64_t)wait, 0, scheduleReconnect_cb, p);
}

static void p_connect(presence *p) {
    if (p->connected) return;
    if (p->logging_in) return;   // 로그인 중에 갈아엎지 않는다 (버려진 쪽이 나중에 상태를 되돌린다)
    //  이전 연결은 반드시 버린다 (핸들이 쌓이면 Discord 가 전부 거부한다 — 2026-08-18)
    if (p->conn) { p->hub->h.destroy(p->hub->h.user, p->conn); p->conn = 0; }
    p->conn = ++p->hub->next_conn;
    p->logging_in = 1;
    p->hub->h.connect(p->hub->h.user, p->conn, p->site_id, p->client_id);
}

static void p_disconnect(presence *p) {
    core *c = p->hub;
    clr(c, &p->reconnect_timer);
    stopRefreshTimer(p);
    clr(c, &p->pendingTimer);
    clr(c, &p->settleTimer);
    clr(c, &p->idleTimer);
    if (p->conn) {
        p_request_clear(p);
        c->h.destroy(c->h.user, p->conn);
        p->conn = 0;
    }
    p->logging_in = 0;
    p->connected = 0;
}

typedef struct { presence *p; int conn; double readyAt; } ready_ctx;

static int current_type(presence *p) { return p->s.isWatching ? 1 : 2; }

static void ready_cb(void *ctx, int id) {
    ready_ctx *rc = (ready_ctx *)ctx;
    presence *p = rc->p;
    core *c = p->hub;
    int conn = rc->conn;
    double readyAt = rc->readyAt;
    ctx_free(c, rc);
    if (conn != p->conn || !p->connected) return;
    double idle = p->idleTimeout;
    int playing = p->s.isWatching && p->s.isPlaying;
    double now = now_ms(c);
    if (js_truthy_num(idle) && (p->afterSwitch || p->lastActivity) && now - p->lastInputAt > idle) {
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "[RPC] \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB0\xEA\xB2\xB0\xEB\x90\xA8 \xE2\x80\x94 \xED\x99\x95\xEC\x9E\xA5 \xEC\x86\x8C\xEC\x8B\x9D\xEC\x9D\xB4 ");
        js_number_str(&b, js_round(idle / 60000));
        sb_puts(&b, "\xEB\xB6\x84 \xEB\x84\x98\xEA\xB2\x8C \xEC\x97\x86\xEC\x96\xB4 \xEB\x90\x98\xEC\x98\xAC\xEB\xA6\xAC\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x8C");
        log_sb(c, 0, &b); sb_free(&b);
        p_clearActivity(p);
        return;
    }
    activity *a = p->afterSwitch;
    p->afterSwitch = NULL;
    if (a) {
        sendNow(p, a);
        act_unref(a);
    } else if (p->lastActivity && p->lastApiCall < readyAt && !p->pendingTimer) {
        //  끊긴 사이 자동 해제 시각이 지났으면 내리고, 아니면 되올리되 원래 해제 시각을 지킨다
        if (!playing && js_truthy_num(idle) && js_truthy_num(p->idleDeadline) && now >= p->idleDeadline) {
            idleExpire(p);
        } else {
            double deadline = p->idleDeadline;
            forceUpdate(p, current_type(p));
            if (!playing && js_truthy_num(idle) && deadline > now_ms(c)) armIdle(p, deadline);
        }
    }
    //  재생 중이면 주기 보정을 되살린다 — 확장 소식이 살아 있을 때만 (죽은 카드 되살리기 금지)
    double silence = now_ms(c) - p->lastInputAt;
    if (playing && silence <= (js_truthy_num(idle) ? idle : REVIVE_MAX_SILENCE_MS) && !p->lastActivity && !p->pendingTimer) {
        forceUpdate(p, 1);
    }
    if (playing && (p->lastActivity || p->pendingTimer) && !p->refreshTimer) startRefreshTimer(p);
}

void core_ready(core *c, int conn, const char *username) {
    for (int i = 0; i < c->nclients; i++) {
        presence *p = c->clients[i];
        if (p->conn != conn) continue;
        p->logging_in = 0;
        p->connected = 1;
        p->reconnect_wait = 10000;
        free(p->switchingTo); p->switchingTo = NULL;
        set_str(&p->user, username ? username : "");
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "[RPC] Connected: ");
        sb_puts(&b, username ? username : "undefined");
        log_sb(c, 0, &b); sb_free(&b);
        notify_status(p);
        ready_ctx *rc = (ready_ctx *)ctx_alloc(c, sizeof *rc);
        if (rc) {
            rc->p = p; rc->conn = conn; rc->readyAt = now_ms(c);
            set_timer(c, 300, 0, ready_cb, rc);
        }
        return;
    }
}

void core_disconnected(core *c, int conn) {
    for (int i = 0; i < c->nclients; i++) {
        presence *p = c->clients[i];
        if (p->conn != conn) continue;
        p->logging_in = 0;
        p->connected = 0;
        stopRefreshTimer(p);
        //  끊긴 동안은 자동 해제 시계도 멈춘다 (해제 시각은 남긴다)
        clr(c, &p->idleTimer);
        notify_status(p);
        scheduleReconnect(p);
        return;
    }
}

void core_login_failed(core *c, int conn, const char *message) {
    for (int i = 0; i < c->nclients; i++) {
        presence *p = c->clients[i];
        if (p->conn != conn) continue;
        p->logging_in = 0;
        logf_(c, 1, "[RPC] Connect failed: %s", message ? message : "undefined");
        p->connected = 0;
        scheduleReconnect(p);
        return;
    }
}

// ── 전송 한도 (토큰 버킷: 5개 / 20초) ─────────────────────────

static void refill(presence *p) {
    double now = now_ms(p->hub);
    if (!p->tokens_set) { p->tokens_set = 1; p->tokens = RPC_BURST; p->tokenAt = now; }
    double gained = floor((now - p->tokenAt) / RPC_REFILL_MS);
    if (gained > 0) {
        p->tokens = fmin(RPC_BURST, p->tokens + gained);
        p->tokenAt += gained * RPC_REFILL_MS;
    }
    if (p->tokens >= RPC_BURST) { p->tokens = RPC_BURST; p->tokenAt = now; }
}
static void win_purge(core *c) {
    double now = now_ms(c);
    int k = 0;
    while (k < c->winN && c->winAt[k] + V2_WINDOW_MS <= now) k++;
    if (k) { memmove(c->winAt, c->winAt + k, sizeof(double) * (size_t)(c->winN - k)); c->winN -= k; }
}
static double msUntilSlotV2(presence *p) {
    core *c = p->hub;
    win_purge(c);
    if (c->winN < V2_WINDOW_N) return 0;
    return fmax(0, c->winAt[0] + V2_WINDOW_MS - now_ms(c));
}
//  보낸 내용이 지난번과 다르면 창에 한 칸 (Discord 도 같은 내용은 서버로 안 보낸다)
static void win_count(presence *p, const char *args) {
    core *c = p->hub;
    if (p->lastArgs && !strcmp(p->lastArgs, args)) return;
    free(p->lastArgs);
    p->lastArgs = dup(args);
    win_purge(c);
    if (c->winN >= V2_WINDOW_N) { memmove(c->winAt, c->winAt + 1, sizeof(double) * (V2_WINDOW_N - 1)); c->winN--; }
    c->winAt[c->winN++] = now_ms(c);
}
static double msUntilSlot(presence *p) {
    if (p->hub->v2) return msUntilSlotV2(p);
    refill(p);
    if (p->tokens > 0) return 0;
    return fmax(0, RPC_REFILL_MS - (now_ms(p->hub) - p->tokenAt));
}
static void spendSlot(presence *p) { refill(p); if (p->tokens > 0) p->tokens--; }

static void buildAndSendWatching(presence *p);
static void buildAndSendBrowsing(presence *p);

static void flushUpdate(presence *p, int type) {
    p->shownType = type == 1 ? 1 : 2;
    if (type == 1) {
        if (!js_truthy_num(p->watchSince)) p->watchSince = now_ms(p->hub);
        buildAndSendWatching(p);
    } else {
        buildAndSendBrowsing(p);
    }
}

static void pending_cb(void *ctx, int id) {
    presence *p = (presence *)ctx;
    p->pendingTimer = 0;
    int t = p->pendingUpdate;
    p->pendingUpdate = 0;
    if (t) flushUpdate(p, t);
}

//  토큰이 있으면 그 자리에서 보낸다. 늦게 보낼 때도 '마지막 상태 하나' 만
static void schedule(presence *p, int type) {
    clr(p->hub, &p->pendingTimer);
    p->pendingUpdate = 0;
    if (p->afterSwitch) { act_unref(p->afterSwitch); p->afterSwitch = NULL; }   // 보류된 것도 버린다
    double wait = msUntilSlot(p);
    if (wait <= 0) { flushUpdate(p, type); return; }
    p->pendingUpdate = type;
    p->pendingTimer = set_timer(p->hub, (int64_t)(wait + 20), 0, pending_cb, p);
}
static void forceUpdate(presence *p, int type) { schedule(p, type); }
static void settle_cb(void *ctx, int id) {
    presence *p = (presence *)ctx;
    p->settleTimer = 0;
    schedule(p, 1);
}
//  재생·정지·이동 — 0.35초 안에 또 바뀌면 다시 센다 (이동 한 번 = 정지+재생 두 소식이 0.1초 간격으로 온다)
static void settleUpdate(presence *p) {
    clr(p->hub, &p->settleTimer);
    p->settleTimer = set_timer(p->hub, V2_SETTLE_MS, 0, settle_cb, p);
}
static void queueUpdate(presence *p, int type) { schedule(p, type); }

// ── 반응 속도 · 주기 보정 ───────────────────────────────────

static void p_setResponseLevel(presence *p, const char *level) {
    const char *next = level_valid(level) ? level : "normal";
    if (p->level && !strcmp(next, p->level)) return;
    set_str(&p->level, next);
    if (p->refreshTimer) startRefreshTimer(p);
}

static void refresh_cb(void *ctx, int id) {
    presence *p = (presence *)ctx;
    core *c = p->hub;
    //  확장이 소식을 끊었으면 되살리지 말고 거둔다 (안 그러면 자동 해제가 영영 안 돈다)
    if (js_truthy_num(p->idleTimeout) && now_ms(c) - p->lastInputAt > p->idleTimeout) {
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "[RPC] \xED\x99\x95\xEC\x9E\xA5 \xEC\x86\x8C\xEC\x8B\x9D ");
        js_number_str(&b, js_round(p->idleTimeout / 60000));
        sb_puts(&b, "\xEB\xB6\x84 \xEB\x81\x8A\xEA\xB9\x80 \xE2\x86\x92 Activity \xEC\xA0\x9C\xEA\xB1\xB0");
        log_sb(c, 0, &b); sb_free(&b);
        p_clearActivity(p);
        return;
    }
    if (p->s.isWatching && p->s.isPlaying) queueUpdate(p, 1);
}

static void startRefreshTimer(presence *p) {
    stopRefreshTimer(p);
    p->refreshTimer = set_timer(p->hub, tuning_of(p->level)->refresh_ms, 1, refresh_cb, p);
}
static void stopRefreshTimer(presence *p) { clr(p->hub, &p->refreshTimer); }

// ── 카드 만들기 ─────────────────────────────────────────────

static int jstrict_false(const jval *v) { return v && v->t == J_BOOL && !v->b; }
static const jval *sget(presence *p, const char *k) { return p->s.settings ? json_get(p->s.settings->v, k) : NULL; }

// '사이트에서 보기' — 검색어·추적값을 떼고, 512자를 넘으면 홈
static void siteUrl(presence *p, sbuf *out) {
    const site_def *site = site_of(p->s.site);
    const char *raw = is_http_url(p->s.pageUrl) ? p->s.pageUrl : site->home;
    sbuf u; sb_init(&u, 0);
    if (seq(p->s.site, "youtube")) yt_button_url(&u, raw, strlen(raw), site->home);
    else if (seq(p->s.site, "chzzk")) chzzk_button_url(&u, raw, strlen(raw), site->home);
    else if (seq(p->s.site, "danime")) danime_button_url(&u, raw, strlen(raw), site->home);
    else sb_puts(&u, raw);
    if (utf16_len(u.p ? u.p : "", u.len) > 512) { sb_reset(&u); sb_puts(&u, site->home); }
    sb_putn(out, u.p ? u.p : "", u.len);
    sb_free(&u);
}

static void buildButtons(presence *p, activity *a) {
    if (jstrict_false(sget(p, "showButtons"))) return;
    sbuf l, u; sb_init(&l, 0); sb_init(&u, 0);
    tr(&l, p->lang, T_VIEW, NULL);
    siteUrl(p, &u);
    a->has_buttons = 1;
    a->btn_label = dupn(nz(l.p), l.len);
    a->btn_url = dupn(nz(u.p), u.len);
    sb_free(&l); sb_free(&u);
}

// Discord 128자 제한 — UTF-16 으로 넘으면 127자 + '…'
static char *clamp_s(const char *s) {
    if (!s) return NULL;
    sbuf b; sb_init(&b, 0);
    utf8_clamp(&b, s, strlen(s), 128);
    char *r = dupn(nz(b.p), b.len);
    sb_free(&b);
    return r;
}

// details/state 가 2글자 미만이면 Discord 가 활동 전체를 거부한다 → 사이트 이름을 덧붙인다
static char *atLeast2(const char *str, const char *pad) {
    const char *s0 = str ? str : "";
    size_t a, b;
    js_trim(s0, strlen(s0), &a, &b);
    if (utf16_len(s0 + a, b - a) >= 2) return dupn(s0 + a, b - a);
    if (a == b) return NULL;
    const char *p0 = pad ? pad : "";
    size_t pa, pb;
    js_trim(p0, strlen(p0), &pa, &pb);
    sbuf r; sb_init(&r, 0);
    sb_putn(&r, s0 + a, b - a);
    if (pb > pa) { sb_puts(&r, " \xC2\xB7 "); sb_putn(&r, p0 + pa, pb - pa); }
    else sb_putc(&r, ' ');
    char *out = dupn(nz(r.p), r.len);
    sb_free(&r);
    return out;
}

static double rate_of(double r) {
    if (!(r > 0) || !isfinite(r) || r > 16 || fabs(r - 1) < 0.01) return 1;
    return r;
}

static int status_display_of(presence *p) {
    const jval *map = sget(p, "statusDisplay");
    const jval *v = (map && map->t == J_OBJ) ? json_get(map, p->s.site ? p->s.site : "") : NULL;
    //  map 이 문자열·숫자면 JS 에선 그 위의 속성 → undefined
    double n = js_to_number(v);
    return (n == 0 || n == 1 || n == 2) ? (int)n : 2;
}
static int activity_type_of(presence *p) {
    double n = js_to_number(sget(p, "activityType"));
    return (n == 0 || n == 2 || n == 3) ? (int)n : 3;
}

//  마지막 확장 소식 이후 흐른 만큼 더한 지금 위치 (최대 60초)
static double liveCurrentTime(presence *p) {
    double ct = p->s.currentTime;
    if (!p->s.isPlaying || !js_truthy_num(p->lastInputAt)) return ct;
    double age = fmin(MSG_AGE_CAP_S, fmax(0, (now_ms(p->hub) - p->lastInputAt) / 1000));
    return ct + age * rate_of(p->s.playbackRate);
}

// settings.timeMode || 'progress' 와 문자열 비교
static int time_mode_is(presence *p, const char *want) {
    const jval *tm = sget(p, "timeMode");
    if (!js_truthy(tm)) return !strcmp(want, "progress");
    return tm->t == J_STR && tm->slen == strlen(want) && !memcmp(tm->s, want, tm->slen);
}

static void buildAndSendWatching(presence *p) {
    core *c = p->hub;
    const site_def *site = site_of(p->s.site);
    const char *siteName = (p->s.siteName && *p->s.siteName) ? p->s.siteName : site->name;
    const char *seriesName = (!jstrict_false(sget(p, "showSeries")) && p->s.seriesName && *p->s.seriesName) ? p->s.seriesName : siteName;
    sbuf ep; sb_init(&ep, 0);
    if (!jstrict_false(sget(p, "showEpisode"))) {
        const char *n = nz(p->s.episodeNumber), *t = nz(p->s.episodeTitle);
        if (*n && *t) { sb_puts(&ep, n); sb_puts(&ep, " / "); sb_puts(&ep, t); }
        else if (*n) sb_puts(&ep, n);
        else if (*t) sb_puts(&ep, t);
    }
    activity *a = act_new();
    if (!a) { sb_free(&ep); return; }
    char *cs = clamp_s(seriesName);
    a->details = atLeast2(cs, siteName);
    if (ep.len) { char *ce = clamp_s(ep.p); a->state = atLeast2(ce, siteName); free(ce); }
    a->large_key = jstrict_false(sget(p, "showThumbnail")) ? dup(site->logo) : resolveImage(p, p->s.thumbnail, site->logo);
    a->large_text = atLeast2(cs, siteName);
    free(cs);
    a->type = activity_type_of(p);
    a->sdt = status_display_of(p);
    a->small_key = dup(p->s.isPlaying ? ((p->s.isLive && site->live) ? site->live : site->play) : site->pause);
    sbuf st; sb_init(&st, 0);
    tr(&st, p->lang, p->s.isPlaying ? (p->s.isLive ? T_LIVE : T_WATCHING) : T_PAUSED, siteName);
    a->small_text = dupn(nz(st.p), st.len);
    sb_free(&st);
    sb_free(&ep);
    buildButtons(p, a);
    { sbuf u; sb_init(&u, 0); siteUrl(p, &u); a->url_for = dupn(nz(u.p), u.len); sb_free(&u); }

    //  시간 — 라이브는 방송 시작 시각만, 일반 영상은 진행 바(또는 남은 시간)
    int showTime = !jstrict_false(sget(p, "showTime"));
    if (p->s.isLive && !time_mode_is(p, "none") && showTime && p->s.isPlaying) {
        if (p->s.liveStartedAt > 0) { a->has_start = 1; a->start = floor(p->s.liveStartedAt / 1000); }
        else if (site->live_dvr_fallback) { a->has_start = 1; a->start = floor(now_ms(c) / 1000) - p->s.currentTime; }
    } else if (!p->s.isLive && !time_mode_is(p, "none") && showTime && p->s.duration > 0 && p->s.isPlaying) {
        double now = floor(now_ms(c) / 1000);
        double rate = rate_of(p->s.playbackRate);
        double ct = liveCurrentTime(p);
        if (time_mode_is(p, "progress")) {
            a->has_start = 1;
            a->start = p->s.startedAtMs > 0 ? js_round(p->s.startedAtMs / 1000) : now - js_round(ct / rate);
            a->has_end = 1;
            a->end = a->start + js_round(p->s.duration / rate);
        } else if (time_mode_is(p, "remaining")) {
            a->has_end = 1;
            a->end = now + js_round((p->s.duration - ct) / rate);
        }
    }
    int start_truthy = a->has_start && js_truthy_num(a->start);
    p->shownStartTs = (start_truthy && !p->s.isLive) ? a->start : 0;
    if (start_truthy && p->s.duration > 0 && !p->s.isLive) {
        double shown = floor(now_ms(c) / 1000) - a->start;
        double actual = liveCurrentTime(p);
        if (fabs(shown - actual / rate_of(p->s.playbackRate)) > 2) {
            sbuf b; sb_init(&b, 0);
            sb_puts(&b, "[RPC] \xEC\xA7\x84\xED\x96\x89 \xEB\xB0\x94 \xEC\x96\xB4\xEA\xB8\x8B\xEB\x82\xA8: \xEC\xB9\xB4\xEB\x93\x9C ");
            js_number_str(&b, shown);
            sb_puts(&b, "s vs \xEC\x8B\xA4\xEC\xA0\x9C ");
            js_number_str(&b, js_round(actual));
            sb_puts(&b, "s (\xEA\xB8\xB0\xEC\xA4\x80\xEC\xA0\x90 ");
            sb_puts(&b, js_truthy_num(p->s.startedAtMs) ? "\xEC\x9E\x88\xEC\x9D\x8C" : "\xEC\x97\x86\xEC\x9D\x8C");
            sb_puts(&b, ")");
            log_sb(c, 1, &b); sb_free(&b);
        }
    }
    sendNow(p, a);
    act_unref(a);
}

static void buildAndSendBrowsing(presence *p) {
    core *c = p->hub;
    const site_def *site = site_of(p->s.site);
    const char *siteName = (p->s.siteName && *p->s.siteName) ? p->s.siteName : site->name;
    const char *pageTitle = nz(p->s.pageTitle), *pageDetail = nz(p->s.pageDetail), *pageBanner = nz(p->s.pageBanner);
    activity *a = act_new();
    if (!a) return;
    sbuf t; sb_init(&t, 0);
    if (*pageTitle) sb_puts(&t, pageTitle); else tr(&t, p->lang, T_BROWSING, NULL);
    char *det = atLeast2(nz(t.p), siteName);
    sb_free(&t);
    a->details = clamp_s(det);
    free(det);
    if (*pageDetail) { char *cd = clamp_s(pageDetail); a->state = atLeast2(cd, siteName); free(cd); }
    a->large_key = jstrict_false(sget(p, "showThumbnail")) ? dup(site->logo) : resolveImage(p, pageBanner, site->logo);
    { char *cl = clamp_s(*pageDetail ? pageDetail : siteName); a->large_text = atLeast2(cl, siteName); free(cl); }
    a->type = activity_type_of(p);
    a->sdt = status_display_of(p);
    if (!time_mode_is(p, "none") && !jstrict_false(sget(p, "showTime"))) {
        a->has_start = 1;
        a->start = js_truthy_num(p->browsingStart) ? p->browsingStart : floor(now_ms(c) / 1000);
    }
    buildButtons(p, a);
    { sbuf u; sb_init(&u, 0); siteUrl(p, &u); a->url_for = dupn(nz(u.p), u.len); sb_free(&u); }
    sendNow(p, a);
    act_unref(a);
}

// ── Discord 로 보내기 (단일 호출 지점) ───────────────────────

//  discord-rpc 라이브러리 setActivity() 가 만드는 모양 그대로: state·details·timestamps·assets·buttons·instance
//  + 앱이 가로채 얹는 type·status_display_type. 값이 없는(undefined) 키는 빠진다
static int build_args(core *c, activity *a, sbuf *out, const char **err) {
    int ts = (a->has_start && js_truthy_num(a->start)) || (a->has_end && js_truthy_num(a->end));
    if (ts) {
        if (a->has_start && a->start > 2147483647000.0) { *err = "timestamps.start must fit into a unix timestamp"; return 0; }
        if (a->has_end && a->end > 2147483647000.0) { *err = "timestamps.end must fit into a unix timestamp"; return 0; }
    }
    sb_printf(out, "{\"pid\":%d,\"activity\":{", c->h.pid);
    int first = 1;
    #define KEY(k) do { if (!first) sb_putc(out, ','); first = 0; sb_puts(out, "\"" k "\":"); } while (0)
    if (a->state) { KEY("state"); json_write_str(out, a->state, strlen(a->state)); }
    if (a->details) { KEY("details"); json_write_str(out, a->details, strlen(a->details)); }
    if (ts) {
        KEY("timestamps");
        sb_putc(out, '{');
        int f2 = 1;
        jval n; memset(&n, 0, sizeof n); n.t = J_NUM;
        if (a->has_start) { sb_puts(out, "\"start\":"); n.n = a->start; json_write(out, &n); f2 = 0; }
        if (a->has_end) { if (!f2) sb_putc(out, ','); sb_puts(out, "\"end\":"); n.n = a->end; json_write(out, &n); }
        sb_putc(out, '}');
    }
    if ((a->large_key && *a->large_key) || (a->large_text && *a->large_text) || (a->small_key && *a->small_key) || (a->small_text && *a->small_text)) {
        KEY("assets");
        sb_putc(out, '{');
        int f2 = 1;
        #define AK(k, v) do { if (v) { if (!f2) sb_putc(out, ','); f2 = 0; sb_puts(out, "\"" k "\":"); json_write_str(out, v, strlen(v)); } } while (0)
        AK("large_image", a->large_key);
        AK("large_text", a->large_text);
        AK("small_image", a->small_key);
        AK("small_text", a->small_text);
        #undef AK
        sb_putc(out, '}');
    }
    if (a->has_buttons) {
        KEY("buttons");
        sb_puts(out, "[{\"label\":");
        json_write_str(out, a->btn_label, strlen(a->btn_label));
        sb_puts(out, ",\"url\":");
        json_write_str(out, a->btn_url, strlen(a->btn_url));
        sb_puts(out, "}]");
    }
    KEY("instance");
    sb_puts(out, "false");
    jval n; memset(&n, 0, sizeof n); n.t = J_NUM;
    sb_puts(out, ",\"type\":"); n.n = a->type; json_write(out, &n);
    sb_puts(out, ",\"status_display_type\":"); n.n = a->sdt; json_write(out, &n);
    sb_puts(out, "}}");
    #undef KEY
    return !out->err;
}

static req *req_add(core *c, presence *p, int is_set) {
    req *r = (req *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->id = ++c->next_req;
    r->p = p;
    r->conn = p->conn;
    r->is_set = is_set;
    r->next = c->reqs;
    c->reqs = r;
    return r;
}
static void req_remove(core *c, req *r) {
    for (req **pp = &c->reqs; *pp; pp = &(*pp)->next) if (*pp == r) { *pp = r->next; return; }
}
static int pending_on(core *c, int conn) {
    int n = 0;
    for (req *r = c->reqs; r; r = r->next) if (r->conn == conn) n++;
    return n;
}

// client.clearActivity() — 연결이 없으면 라이브러리가 조용히 거부한다
static void p_request_clear(presence *p) {
    core *c = p->hub;
    if (!p->conn) return;
    req *r = req_add(c, p, 0);
    if (!r) return;
    char args[64];
    snprintf(args, sizeof args, "{\"pid\":%d}", c->h.pid);
    if (c->v2) win_count(p, args);
    if (!c->h.request(c->h.user, p->conn, r->id, args)) { req_remove(c, r); free(r); }
}

//  보낸 setActivity 에 15초 안에 답이 없으면 연결이 멈춘 것으로 보고 다시 붙는다
static void stall_cb(void *ctx, int id) {
    req *r = (req *)ctx;
    presence *p = r->p;
    core *c = p->hub;
    r->stall_timer = 0;
    if (r->done || r->conn != p->conn || !p->connected) return;
    p->stalls++;
    int waiting = pending_on(c, r->conn);
    logf_(c, 1, "[RPC] \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 15\xEC\xB4\x88 \xEB\x84\x98\xEA\xB2\x8C \xEB\x8B\xB5\xEC\x9D\xB4 \xEC\x97\x86\xEC\x9D\x8C (\xEB\x8C\x80\xEA\xB8\xB0 %d\xEA\xB1\xB4) \xE2\x86\x92 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB0\xEA\xB2\xB0", waiting);
    p_disconnect(p);
    if (p->stalls >= 2) scheduleReconnect(p);
    else p_connect(p);
}

static int ensureApp(presence *p) {
    const char *want = app_id_of(p->hub, p->s.site);
    if (!*want || !strcmp(want, nz(p->client_id))) return 1;
    //  ⚠ 애플리케이션 ID 는 로그에 남기지 않는다 — 사이트 이름만
    logf_(p->hub, 0, "[RPC] \xEC\x95\xB1 \xEC\xA0\x84\xED\x99\x98: %s \xEC\x95\xA0\xED\x94\x8C\xEB\xA6\xAC\xEC\xBC\x80\xEC\x9D\xB4\xEC\x85\x98\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB0\xEA\xB2\xB0", nz(p->s.site));
    set_str(&p->switchingTo, want);
    p_disconnect(p);
    char *w = dup(want);
    free(p->client_id);
    p->client_id = w;
    p_connect(p);
    return 0;
}

static void sendNow(presence *p, activity *a) {
    core *c = p->hub;
    if (!ensureApp(p)) { act_set(&p->afterSwitch, a); return; }
    //  아직 연결 전이면 보류 — ready 가 보낸다
    if (!p->connected || !p->conn) { act_set(&p->afterSwitch, a); return; }
    p->lastApiCall = now_ms(c);
    if (!c->v2) spendSlot(p);
    resetIdleTimer(p);
    const char *imgKey = nz(a->large_key);
    if (!p->img_logged || strcmp(imgKey, nz(p->lastLoggedImage))) {
        p->img_logged = 1;
        set_str(&p->lastLoggedImage, imgKey);
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "[RPC] setActivity largeImageKey = ");
        js_slice16(&b, imgKey, strlen(imgKey), 110);
        log_sb(c, 0, &b); sb_free(&b);
    }
    sbuf args; sb_init(&args, 0);
    const char *err = NULL;
    if (!build_args(c, a, &args, &err)) {
        logf_(c, 2, "[RPC] Failed: %s", err ? err : "out of memory");
        sb_free(&args);
        return;
    }
    if (c->v2) win_count(p, args.p ? args.p : "");
    req *r = req_add(c, p, 1);
    if (!r) { sb_free(&args); return; }
    r->type = a->type;
    r->sdt = a->sdt;
    r->sent_at = now_ms(c);
    r->hr_at = c->h.now_hr ? c->h.now_hr(c->h.user) : r->sent_at;
    {
        sbuf w; sb_init(&w, 0);
        js_number_str(&w, a->type); sb_putc(&w, '/'); js_number_str(&w, a->sdt);
        if (!p->lastTypeLog || strcmp(p->lastTypeLog, nz(w.p))) { set_str(&p->lastTypeLog, nz(w.p)); r->typelog = 1; }
        sb_free(&w);
    }
    if (!c->h.request(c->h.user, p->conn, r->id, args.p)) {
        //  연결이 열려 있는데 못 보낸 경우 — JS 에선 거부로 처리된다 (catch 경고 + 정리)
        r->done = 1;
        req_remove(c, r);
        logf_(c, 1, "[RPC] setActivity \xEA\xB1\xB0\xEB\xB6\x80: %s", "transport not open");
        free(r);
    } else {
        r->stall_timer = set_timer(c, RPC_STALL_MS, 0, stall_cb, r);
    }
    sb_free(&args);
    act_set(&p->lastActivity, a);
    set_str(&p->shownUrl, a->url_for);
    notify_status(p);
}

void core_reply(core *c, int reqid, int ok, const jval *data, const char *err) {
    req *r = NULL;
    for (req *x = c->reqs; x; x = x->next) if (x->id == reqid) { r = x; break; }
    if (!r) return;
    req_remove(c, r);
    presence *p = r->p;
    if (r->is_set) {
        if (ok && r->typelog) {
            const jval *got = data;
            const jval *ga = data && data->t == J_OBJ ? json_get(data, "activity") : NULL;
            if (js_truthy(ga)) got = ga;
            sbuf b; sb_init(&b, 0);
            sb_puts(&b, "[RPC] \xED\x99\x9C\xEB\x8F\x99 \xEC\xA2\x85\xEB\xA5\x98 \xEB\xB3\xB4\xEB\x83\x84 type=");
            js_number_str(&b, r->type);
            sb_puts(&b, " status_display_type=");
            js_number_str(&b, r->sdt);
            sb_puts(&b, " \xE2\x86\x92 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEC\x9D\x91\xEB\x8B\xB5 type=");
            //  got && got.type — got 이 없으면 got 자체(undefined·null)
            if (js_truthy(got)) js_to_string(&b, got->t == J_OBJ ? json_get(got, "type") : NULL); else js_to_string(&b, got);
            sb_puts(&b, " status_display_type=");
            if (js_truthy(got)) js_to_string(&b, got->t == J_OBJ ? json_get(got, "status_display_type") : NULL); else js_to_string(&b, got);
            log_sb(c, 0, &b); sb_free(&b);
        }
        if (!ok) logf_(c, 1, "[RPC] setActivity \xEA\xB1\xB0\xEB\xB6\x80: %s", err ? err : "undefined");
        //  settle — 버린 연결의 답은 세지도 재지도 않는다
        if (!r->done) {
            r->done = 1;
            clr(c, &r->stall_timer);
            if (r->conn == p->conn) {
                p->stalls = 0;
                double took = now_ms(c) - r->sent_at;
                if (took > RPC_SLOW_MS && now_ms(c) - p->slowLogAt > 60000) {
                    p->slowLogAt = now_ms(c);
                    sbuf b; sb_init(&b, 0);
                    sb_puts(&b, "[RPC] \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEC\x9D\x91\xEB\x8B\xB5 \xEB\x8A\x90\xEB\xA6\xBC: ");
                    js_to_fixed(&b, took / 1000, 1);
                    sb_puts(&b, "\xEC\xB4\x88");
                    log_sb(c, 1, &b); sb_free(&b);
                }
            }
        }
        if (ok && r->conn == p->conn) { p->rttMs = (c->h.now_hr ? c->h.now_hr(c->h.user) : now_ms(c)) - r->hr_at; p->rttAt = now_ms(c); med5_push(&p->rttHist, p->rttMs, p->rttAt); }
    }
    free(r);
}

// ── 자동 해제 ───────────────────────────────────────────────

static void idle_cb(void *ctx, int id) { idleExpire((presence *)ctx); }

static void armIdle(presence *p, double deadline) {
    clr(p->hub, &p->idleTimer);
    p->idleDeadline = deadline;
    p->idleTimer = set_timer(p->hub, (int64_t)fmax(0, deadline - now_ms(p->hub)), 0, idle_cb, p);
}

static void resetIdleTimer(presence *p) {
    if (!js_truthy_num(p->idleTimeout)) { clr(p->hub, &p->idleTimer); p->idleDeadline = 0; return; }
    armIdle(p, now_ms(p->hub) + p->idleTimeout);
}

static void idleExpire(presence *p) {
    core *c = p->hub;
    p->idleTimer = 0;   // JS 와 같게 지우지 않고 잊기만 한다 (타이머에서 불렸으면 이미 끝난 것)
    p->idleDeadline = 0;
    clr(c, &p->applyTimer);
    sbuf b; sb_init(&b, 0);
    sb_puts(&b, "[RPC] ");
    js_number_str(&b, js_round(p->idleTimeout / 60000));
    sb_puts(&b, "\xEB\xB6\x84 \xEB\xB9\x84\xED\x99\x9C\xEB\x8F\x99 \xE2\x86\x92 Activity \xEC\xA0\x9C\xEA\xB1\xB0");
    log_sb(c, 0, &b); sb_free(&b);
    stopRefreshTimer(p);
    p_request_clear(p);
    act_set(&p->lastActivity, NULL);
    p->shownType = 0;
    notify_status(p);
}

static void p_setIdleTimeout(presence *p, double ms) {
    double v = ms;
    if (isnan(v) || v == 0) v = 0;
    p->idleTimeout = fmax(0, v);
    clr(p->hub, &p->idleTimer);
    p->idleDeadline = 0;
    if (!js_truthy_num(p->idleTimeout) || !p->lastActivity) return;
    //  끊긴 동안엔 시계를 걸지 않고 해제 시각만 적어 둔다
    if (p->connected) resetIdleTimer(p);
    else p->idleDeadline = now_ms(p->hub) + p->idleTimeout;
}

static void p_clearActivity(presence *p) {
    core *c = p->hub;
    //  내부 정리는 무조건, Discord 호출만 연결돼 있을 때 (끊긴 사이 온 CLEAR 가 무시되면 낡은 카드가 되살아난다)
    stopRefreshTimer(p);
    clr(c, &p->idleTimer);
    clr(c, &p->pendingTimer);
    clr(c, &p->applyTimer);
    clr(c, &p->settleTimer);
    p->pendingUpdate = 0;
    if (p->afterSwitch) { act_unref(p->afterSwitch); p->afterSwitch = NULL; }
    p->s.isWatching = 0;
    reset_lastSent(p);   // _lastSent 를 새 객체로 — 썸네일·라이브·배속 기억도 비워진다
    if (p->connected && p->conn) p_request_clear(p);
    act_set(&p->lastActivity, NULL);
    set_str(&p->lastBrowsingKey, "");
    set_str(&p->browsingUrl, "");
    p->browsingStart = 0;
    p->watchSince = 0;
    p->idleDeadline = 0;
    p->shownType = 0;
    notify_status(p);
}

// ── 설정·언어 ───────────────────────────────────────────────

static void p_setLang(presence *p, const char *lang) {
    if (seq(lang, p->lang)) return;
    set_str(&p->lang, lang);
    if (p->connected && p->lastActivity) forceUpdate(p, current_type(p));
}

static void apply_cb(void *ctx, int id) {
    presence *p = (presence *)ctx;
    p->applyTimer = 0;
    if (p->connected) forceUpdate(p, current_type(p));
}

static void p_applySettings(presence *p, settings_ref *s) {
    settings_ref *old = p->s.settings;
    p->s.settings = sref_ref(s);
    sref_unref(old);
    if (!p->connected || !p->lastActivity) return;
    //  지웠다가 새로 올린다 — 이미 떠 있는 활동은 일부 필드(status_display_type 등)를 Discord 가 무시한다
    p_request_clear(p);
    act_set(&p->lastActivity, NULL);
    set_str(&p->lastBrowsingKey, "");
    p->shownType = 0;
    clr(p->hub, &p->applyTimer);
    p->applyTimer = set_timer(p->hub, 250, 0, apply_cb, p);
}

// ── 확장 소식 → 카드 ─────────────────────────────────────────

typedef struct {
    const char *site, *siteName, *seriesName, *episodeTitle, *episodeNumber, *thumbnail, *pageUrl;
    int isPlaying, isLive;
    double currentTime, duration, liveStartedAt, startedAtMs, playbackRate;
} vstate;

static void reset_lastSent(presence *p) {
    set_str(&p->ls.episode, "");
    p->ls.playing = -1;
    p->ls.currentTime = 0;
    p->ls.duration = 0;
    free(p->ls.thumbnail); p->ls.thumbnail = NULL;
    p->ls.isLive = 0;
    p->ls.liveStartedAt = 0;
    p->ls.rate = 0;
    p->ls.at = 0;
}

static void p_updateFromVideoState(presence *p, const vstate *d, settings_ref *settings) {
    core *c = p->hub;
    p->lastInputAt = now_ms(c);
    settings_ref *old = p->s.settings;
    p->s.settings = settings ? sref_ref(settings) : sref_new(NULL);
    sref_unref(old);
    p->s.isWatching = 1;
    set_str(&p->s.site, d->site); set_str(&p->s.siteName, d->siteName);
    p->s.isPlaying = d->isPlaying;
    set_str(&p->s.seriesName, d->seriesName); set_str(&p->s.episodeTitle, d->episodeTitle); set_str(&p->s.episodeNumber, d->episodeNumber);
    p->s.currentTime = d->currentTime; p->s.duration = d->duration;
    set_str(&p->s.thumbnail, d->thumbnail); set_str(&p->s.pageUrl, d->pageUrl);
    p->s.isLive = d->isLive; p->s.liveStartedAt = d->liveStartedAt; p->s.startedAtMs = d->startedAtMs; p->s.playbackRate = d->playbackRate;

    sbuf ne; sb_init(&ne, 0);
    sb_puts(&ne, nz(p->s.site)); sb_puts(&ne, nz(p->s.episodeNumber)); sb_puts(&ne, nz(p->s.episodeTitle)); sb_puts(&ne, nz(p->s.seriesName));
    const char *newEp = nz(ne.p);
    int playChanged = p->ls.playing < 0 || p->s.isPlaying != p->ls.playing;
    int episodeChanged = strcmp(newEp, nz(p->ls.episode)) != 0;
    double rate = rate_of(p->s.playbackRate);
    double nowMs = now_ms(c);
    double expectCt = p->ls.currentTime;
    if (p->ls.playing == 1 && js_truthy_num(p->ls.at)) expectCt += (nowMs - p->ls.at) / 1000 * rate;
    int seeked = fabs(p->s.currentTime - expectCt) > 3;
    int durationLoaded = p->s.duration > 0 && p->ls.duration == 0;
    int firstWatch = p->ls.playing < 0;
    int thumbChanged = strcmp(nz(p->s.thumbnail), nz(p->ls.thumbnail)) != 0;
    int liveChanged = (!!p->s.isLive) != (!!p->ls.isLive);
    int liveStartChanged = (js_truthy_num(p->s.liveStartedAt) ? p->s.liveStartedAt : 0) != (js_truthy_num(p->ls.liveStartedAt) ? p->ls.liveStartedAt : 0);
    int rateChanged = rate != (js_truthy_num(p->ls.rate) ? p->ls.rate : 1);

    if (!playChanged && !episodeChanged && !seeked && !durationLoaded && !firstWatch && !thumbChanged && !liveChanged && !liveStartChanged && !rateChanged) {
        //  자연 경과 — 위치만 따라간다. 카드 기준점과 실제가 허용치 넘게 벌어지면 그 자리에서 다시 잡는다
        p->ls.currentTime = p->s.currentTime;
        p->ls.at = nowMs;
        int redrawPending = p->pendingTimer && p->pendingUpdate == 1;
        redrawPending = redrawPending || p->settleTimer;
        if (js_truthy_num(p->shownStartTs) && p->shownType == 1 && !redrawPending && p->s.isPlaying && !p->s.isLive && p->s.duration > 0) {
            double cardPos = floor(now_ms(c) / 1000) - p->shownStartTs;
            double off = js_round(fabs(cardPos - p->s.currentTime / rate));
            if (off > tuning_of(p->level)->drift) {
                sbuf b; sb_init(&b, 0);
                sb_puts(&b, "[RPC] \xEC\xA7\x84\xED\x96\x89 \xEB\xB0\x94 ");
                js_number_str(&b, off);
                sb_puts(&b, "s \xEC\x96\xB4\xEA\xB8\x8B\xEB\x82\xA8 \xE2\x86\x92 \xEA\xB8\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x9E\xA1\xEC\x9D\x8C (");
                if (p->level) { for (const char *q = p->level; *q; q++) sb_putc(&b, (char)(*q >= 'a' && *q <= 'z' ? *q - 32 : *q)); }
                else sb_puts(&b, "UNDEFINED");
                sb_puts(&b, ")");
                log_sb(c, 0, &b); sb_free(&b);
                forceUpdate(p, 1);
            }
        }
        sb_free(&ne);
        return;
    }
    set_str(&p->ls.episode, newEp);
    p->ls.playing = p->s.isPlaying ? 1 : 0;
    p->ls.currentTime = p->s.currentTime;
    p->ls.duration = p->s.duration;
    set_str(&p->ls.thumbnail, nz(p->s.thumbnail));
    p->ls.isLive = !!p->s.isLive;
    p->ls.liveStartedAt = js_truthy_num(p->s.liveStartedAt) ? p->s.liveStartedAt : 0;
    p->ls.rate = rate;
    p->ls.at = nowMs;
    sb_free(&ne);
    if (c->v2 && !firstWatch && !episodeChanged) settleUpdate(p);
    else { clr(c, &p->settleTimer); forceUpdate(p, 1); }
    if (p->s.isPlaying) startRefreshTimer(p);
    else stopRefreshTimer(p);
}

typedef struct { const char *site, *siteName, *url, *title, *detail, *banner; } nstate;

static void p_updateFromNavigation(presence *p, const nstate *d, settings_ref *settings) {
    core *c = p->hub;
    p->lastInputAt = now_ms(c);
    set_str(&p->s.site, (d->site && *d->site) ? d->site : ((p->s.site && *p->s.site) ? p->s.site : "ttfc"));
    set_str(&p->s.siteName, d->siteName);
    set_str(&p->s.pageTitle, d->title);
    set_str(&p->s.pageDetail, d->detail);
    set_str(&p->s.pageBanner, d->banner);
    set_str(&p->s.pageUrl, d->url);
    if (settings) { settings_ref *old = p->s.settings; p->s.settings = sref_ref(settings); sref_unref(old); }
    p->s.isWatching = 0;
    reset_lastSent(p);
    stopRefreshTimer(p);
    //  영상을 30초 넘게 보다 돌아왔으면 같은 페이지라도 시간을 새로 센다
    if (js_truthy_num(p->watchSince)) {
        if (now_ms(c) - p->watchSince >= WATCH_ENDS_BROWSING_MS) set_str(&p->browsingUrl, "");
        p->watchSince = 0;
    }
    if (strcmp(nz(p->browsingUrl), nz(p->s.pageUrl))) {
        set_str(&p->browsingUrl, nz(p->s.pageUrl));
        p->browsingStart = floor(now_ms(c) / 1000);
    }
    sbuf k; sb_init(&k, 0);
    sb_puts(&k, nz(p->s.site)); sb_putc(&k, '|'); sb_puts(&k, nz(p->s.pageTitle)); sb_putc(&k, '|');
    sb_puts(&k, nz(p->s.pageDetail)); sb_putc(&k, '|'); sb_puts(&k, nz(p->s.pageBanner)); sb_putc(&k, '|'); sb_puts(&k, nz(p->s.pageUrl));
    //  같은 둘러보기 카드를 지금 실제로 띄우고 있을 때만 건너뛴다 (영상 카드가 떠 있으면 반드시 다시)
    if (!strcmp(nz(k.p), nz(p->lastBrowsingKey)) && p->shownType == 2) { sb_free(&k); return; }
    set_str(&p->lastBrowsingKey, nz(k.p));
    sb_free(&k);
    forceUpdate(p, 2);
}

// 이미 올라간 재호스팅 주소를 캐시에 꽂고, 지금 그걸 보여 주는 중이면 다시 그린다
static void p_adoptThumb(presence *p, const char *url, const char *hosted, double keep) {
    if (!url || !*url || !hosted || !*hosted) return;
    if (p->cache.n >= CLIENT_CACHE_MAX) tmap_del_at(&p->cache, 0);
    tmap_set(&p->cache, url, hosted, now_ms(p->hub), js_truthy_num(keep) ? keep : THUMB_TTL);
    if (p->lastActivity || p->afterSwitch) {
        if (p->s.isWatching && !strcmp(nz(p->s.thumbnail), url)) forceUpdate(p, 1);
        else if (!p->s.isWatching && !strcmp(nz(p->s.pageBanner), url)) forceUpdate(p, 2);
    }
}

// ── 허브 ────────────────────────────────────────────────────

static presence *presence_new(core *c, const char *site, const char *app_id) {
    presence *p = (presence *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->hub = c;
    snprintf(p->site_id, sizeof p->site_id, "%s", site);
    p->client_id = dup(app_id);
    p->lang = dup("ko");
    p->s.site = dup("ttfc");
    p->s.playbackRate = NAN;
    p->ls.episode = dup("");
    p->ls.playing = -1;
    p->browsingUrl = dup("");
    p->lastBrowsingKey = dup("");
    p->idleTimeout = 5 * 60 * 1000;
    return p;
}

// 그 사이트 전용 클라이언트 (없으면 만든다). 모르는 사이트·ID 없는 사이트는 NULL
static presence *hub_for(core *c, const jval *siteRaw) {
    sbuf sid; sb_init(&sid, 0);
    if (js_truthy(siteRaw)) js_to_string(&sid, siteRaw);
    const char *s = nz(sid.p);
    if (js_truthy(siteRaw) && !site_find(s)) {
        sbuf k; sb_init(&k, 0);
        key_of(&k, siteRaw);
        if (!smap_get(&c->unknownSite, nz(k.p)) && c->unknownSite.n < 20) {
            smap_set(&c->unknownSite, nz(k.p), "1");
            sbuf b; sb_init(&b, 0);
            sb_puts(&b, "[RPC] \xEC\xA7\x80\xEC\x9B\x90\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEB\x8A\x94 \xEC\x82\xAC\xEC\x9D\xB4\xED\x8A\xB8\xEB\x9D\xBC \xED\x91\x9C\xEC\x8B\x9C\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4: ");
            js_slice16(&b, s, sid.len, 40);
            log_sb(c, 1, &b); sb_free(&b);
        }
        sb_free(&k);
        sb_free(&sid);
        return NULL;
    }
    //  표의 고정 문자열을 쓴다 — s 는 sid 버퍼라 아래 sb_free 뒤엔 못 쓴다 (해제 후 읽기로 로그에 엉뚱한 글자가 찍혔음)
    const char *id = site_of(s)->id;
    for (int i = 0; i < c->nclients; i++) if (!strcmp(c->clients[i]->site_id, id)) { sb_free(&sid); return c->clients[i]; }
    const char *app = app_id_of(c, id);
    if (!*app) {
        if (!smap_get(&c->noAppId, id)) {
            smap_set(&c->noAppId, id, "1");
            logf_(c, 1, "[RPC] %s: Discord \xEC\x95\xA0\xED\x94\x8C\xEB\xA6\xAC\xEC\xBC\x80\xEC\x9D\xB4\xEC\x85\x98 ID \xEA\xB0\x80 \xEC\x97\x86\xEC\x96\xB4 \xED\x91\x9C\xEC\x8B\x9C\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (secrets.json \xEC\x9D\x98 discordAppIds.%s \xEC\x97\x90 \xEB\xAC\xB8\xEC\x9E\x90\xEC\x97\xB4\xEB\xA1\x9C \xEB\x84\xA3\xEA\xB3\xA0 \xEC\x95\xB1\xEC\x9D\x84 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\xBC\x9C\xEC\x84\xB8\xEC\x9A\x94)", id, id);
        }
        sb_free(&sid);
        return NULL;
    }
    presence *p = c->nclients < NSITES ? presence_new(c, id, app) : NULL;
    sb_free(&sid);
    if (!p) return NULL;
    //  이미 올려 둔 이미지는 다시 올리지 않게 심어 준다 (만료분 빼고)
    double now = now_ms(c);
    for (size_t i = 0; i < c->hosted.n; i++) {
        thumb_e *e = &c->hosted.v[i];
        if (now - e->at > (js_truthy_num(e->keep) ? e->keep : THUMB_TTL)) continue;
        tmap_set(&p->cache, e->url, e->hosted, e->at, e->keep);
    }
    p_setLang(p, c->lang);
    if (c->has_idle) p_setIdleTimeout(p, c->idleTimeout);
    if (c->responseLevel) p_setResponseLevel(p, c->responseLevel);
    c->clients[c->nclients++] = p;
    logf_(c, 0, "[RPC] \xEC\x82\xAC\xEC\x9D\xB4\xED\x8A\xB8 \xED\x81\xB4\xEB\x9D\xBC\xEC\x9D\xB4\xEC\x96\xB8\xED\x8A\xB8 \xEC\x83\x9D\xEC\x84\xB1: %s (\xEC\x95\xA0\xED\x94\x8C\xEB\xA6\xAC\xEC\xBC\x80\xEC\x9D\xB4\xEC\x85\x98 ID: \xEA\xB0\x9C\xEC\x9D\xB8 \xEC\x84\xA4\xEC\xA0\x95)", id);
    if (c->enabled) p_connect(p);
    return p;
}

static int hub_canShow(core *c, const jval *siteRaw) {
    sbuf sid; sb_init(&sid, 0);
    if (js_truthy(siteRaw)) js_to_string(&sid, siteRaw); else sb_puts(&sid, "ttfc");
    int r = c->enabled && site_find(nz(sid.p)) && *app_id_of(c, nz(sid.p));
    sb_free(&sid);
    return r;
}

static const char *hub_cachedThumb(core *c, const char *url) {
    long i = tm_find_e(&c->hosted, url);
    if (i >= 0) {
        thumb_e *e = &c->hosted.v[i];
        if (now_ms(c) - e->at <= (js_truthy_num(e->keep) ? e->keep : THUMB_TTL)) return e->hosted;
        tmap_del_at(&c->hosted, (size_t)i);   // 만료 — 다음 번에 다시 올린다
    }
    for (int k = 0; k < c->nclients; k++) { const char *r = cachedThumb(c->clients[k], url); if (*r) return r; }
    return "";
}

core *core_new(const core_host *h) {
    core *c = (core *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->h = *h;
    tm_init(&c->tq);
    c->lang = dup("ko");
    c->enabled = 1;
    c->settings = sref_new(NULL);
    return c;
}

static void presence_free(presence *p) {
    act_unref(p->lastActivity); act_unref(p->afterSwitch);
    free(p->client_id); free(p->lang);
    free(p->s.site); free(p->s.siteName); free(p->s.seriesName); free(p->s.episodeTitle); free(p->s.episodeNumber);
    free(p->s.thumbnail); free(p->s.pageTitle); free(p->s.pageDetail); free(p->s.pageBanner); free(p->s.pageUrl);
    sref_unref(p->s.settings);
    free(p->ls.episode); free(p->ls.thumbnail);
    free(p->browsingUrl); free(p->lastBrowsingKey); free(p->shownUrl);
    free(p->lastLoggedImage); free(p->lastTypeLog); free(p->level); free(p->switchingTo); free(p->user);
    free(p->lastArgs);
    tmap_free(&p->cache);
    free(p);
}

void core_free(core *c) {
    if (!c) return;
    for (int i = 0; i < c->nclients; i++) presence_free(c->clients[i]);
    while (c->reqs) { req *r = c->reqs; c->reqs = r->next; free(r); }
    while (c->jobs) { thumbjob *j = c->jobs; c->jobs = j->next; free(j->url); free(j->data); free(j); }
    while (c->ctxs) { ctxnode *n = c->ctxs; c->ctxs = n->next; free(n->p); free(n); }
    tm_free(&c->tq);
    free(c->lang); free(c->responseLevel);
    tmap_free(&c->hosted);
    smap_free(&c->uploading); smap_free(&c->unknownSite); smap_free(&c->noAppId);
    smap_free(&c->videoLogKeys); smap_free(&c->browsingLogKeys); smap_free(&c->thumbSites);
    for (size_t i = 0; i < c->asks.cnt; i++) free(c->asks.k[i]);
    free(c->asks.k); free(c->asks.at); free(c->asks.n);
    sref_unref(c->settings);
    free(c);
}

void core_set_settings(core *c, const jval *settings) {
    settings_ref *n = sref_new(settings);
    if (!n) return;
    sref_unref(c->settings);
    c->settings = n;
}

void core_apply_settings(core *c) {
    for (int i = 0; i < c->nclients; i++) p_applySettings(c->clients[i], c->settings);
}

void core_set_lang(core *c, const char *lang) {
    set_str(&c->lang, lang);
    for (int i = 0; i < c->nclients; i++) p_setLang(c->clients[i], lang);
}

void core_set_idle_timeout(core *c, double ms) {
    double v = (isnan(ms) || ms == 0) ? 0 : ms;
    c->has_idle = 1;
    c->idleTimeout = fmax(0, v);
    for (int i = 0; i < c->nclients; i++) p_setIdleTimeout(c->clients[i], c->idleTimeout);
}

void core_set_pacing_v2(core *c, int on) { c->v2 = on ? 1 : 0; }

void core_set_response_level(core *c, const char *level) {
    set_str(&c->responseLevel, level_valid(level) ? level : "normal");
    for (int i = 0; i < c->nclients; i++) p_setResponseLevel(c->clients[i], c->responseLevel);
}

void core_connect(core *c) {
    c->enabled = 1;
    //  _for() 가 새 클라이언트를 이미 connect 시킨다 — 같은 것에 또 부르면 로그인 중인 연결을 버린다
    if (c->nclients == 0) { jval t; memset(&t, 0, sizeof t); t.t = J_STR; t.s = (char *)"ttfc"; t.slen = 4; hub_for(c, &t); return; }
    for (int i = 0; i < c->nclients; i++) p_connect(c->clients[i]);
}

void core_disconnect(core *c) {
    c->enabled = 0;
    for (int i = 0; i < c->nclients; i++) p_disconnect(c->clients[i]);
}

void core_clear(core *c, const char *site) {
    if (site && *site) {
        for (int i = 0; i < c->nclients; i++) if (!strcmp(c->clients[i]->site_id, site)) { p_clearActivity(c->clients[i]); return; }
        return;   // 아직 만든 적 없는 사이트 → 지울 것도 없다
    }
    for (int i = 0; i < c->nclients; i++) p_clearActivity(c->clients[i]);
}

static void save_cb(void *ctx, int id) {
    core *c = (core *)ctx;
    c->saveTimer = 0;
    if (c->h.save_hosted) c->h.save_hosted(c->h.user);
}

void core_thumb_hosted(core *c, const char *url, const char *hosted, double keep) {
    long u = smap_find(&c->uploading, url);
    if (u >= 0) smap_del_at(&c->uploading, (size_t)u);
    logf_(c, 0, "[RPC] \xEC\x8D\xB8\xEB\x84\xA4\xEC\x9D\xBC \xEC\x9E\xAC\xED\x98\xB8\xEC\x8A\xA4\xED\x8C\x85 OK \xE2\x86\x92 %s", hosted);
    while (c->hosted.n >= HOSTED_CACHE_MAX) tmap_del_at(&c->hosted, 0);
    tmap_set(&c->hosted, url, hosted, now_ms(c), keep);
    if (!c->saveTimer) c->saveTimer = set_timer(c, 2000, 0, save_cb, c);
    for (int i = 0; i < c->nclients; i++) p_adoptThumb(c->clients[i], url, hosted, keep);
}

int64_t core_next_deadline(core *c) { return tm_next(&c->tq); }
int core_fire_one(core *c) { return tm_fire_one(&c->tq, c->h.now(c->h.user)); }
int core_host_timer(core *c, int64_t delay, tm_fn fn, void *ctx) { return set_timer(c, delay, 0, fn, ctx); }

// ── 브리지 (extension-bridge.js _handleMessage 의 카드 부분) ──────

static double rate_from(const jval *v) {
    double r = js_to_number(v);
    return (r > 0 && r <= 16 && isfinite(r)) ? r : 1;
}

// v || 0 (숫자 필드) — 숫자면 그대로, 거짓이면 0. ⚠ 문자열 숫자 등은 JS 가 이상하게 굴어(문자열 이어 붙이기) 0 으로 본다
static double num_or0(const jval *v) { return (v && v->t == J_NUM && js_truthy(v)) ? v->n : 0; }

static int canShow_thumb(core *c, const jval *siteRaw) {
    if (!hub_canShow(c, siteRaw)) return 0;
    const jval *st = c->settings ? json_get(c->settings->v, "showThumbnail") : NULL;
    return !jstrict_false(st);
}

static void noteThumbSite(core *c, const char *url, const jval *siteRaw) {
    if (!url || !*url || !js_truthy(siteRaw)) return;
    sbuf k; sb_init(&k, 0);
    key_of(&k, siteRaw);
    if (c->thumbSites.n >= 200) smap_del_at(&c->thumbSites, 0);
    smap_set(&c->thumbSites, url, nz(k.p));
    sb_free(&k);
}

static long ask_find(core *c, const char *url) { for (size_t i = 0; i < c->asks.cnt; i++) if (!strcmp(c->asks.k[i], url)) return (long)i; return -1; }
static void ask_del_at(core *c, size_t i) {
    free(c->asks.k[i]);
    memmove(c->asks.k + i, c->asks.k + i + 1, (c->asks.cnt - i - 1) * sizeof(char *));
    memmove(c->asks.at + i, c->asks.at + i + 1, (c->asks.cnt - i - 1) * sizeof(double));
    memmove(c->asks.n + i, c->asks.n + i + 1, (c->asks.cnt - i - 1) * sizeof(double));
    c->asks.cnt--;
}
static void ask_set(core *c, const char *url, double at, double n) {
    long i = ask_find(c, url);
    if (i >= 0) { c->asks.at[i] = at; c->asks.n[i] = n; return; }
    if (c->asks.cnt == c->asks.cap) {
        size_t cap = c->asks.cap ? c->asks.cap * 2 : 16;
        char **k = (char **)realloc(c->asks.k, cap * sizeof(char *)); if (!k) return; c->asks.k = k;
        double *a = (double *)realloc(c->asks.at, cap * sizeof(double)); if (!a) return; c->asks.at = a;
        double *nn = (double *)realloc(c->asks.n, cap * sizeof(double)); if (!nn) return; c->asks.n = nn;
        c->asks.cap = cap;
    }
    c->asks.k[c->asks.cnt] = dup(url);
    c->asks.at[c->asks.cnt] = at;
    c->asks.n[c->asks.cnt] = n;
    c->asks.cnt++;
}

// 재호스팅이 필요한데 캐시에 없는 이미지 → 확장에 바이트 요청 (처음 5번은 5초 간격, 그 뒤 두 배씩 최대 5분)
static void requestThumbIfNeeded(core *c, int client, const jval *urlv, const jval *siteRaw) {
    if (!js_truthy(urlv)) return;
    sbuf us; sb_init(&us, 0);
    js_to_string(&us, urlv);
    const char *url = nz(us.p);
    if (strncmp(url, "http:", 5) && strncmp(url, "https:", 6)) { sb_free(&us); return; }
    noteThumbSite(c, url, siteRaw);
    if (!canShow_thumb(c, siteRaw)) { sb_free(&us); return; }
    if (*hub_cachedThumb(c, url)) { long i = ask_find(c, url); if (i >= 0) ask_del_at(c, (size_t)i); sb_free(&us); return; }
    if (smap_find(&c->uploading, url) >= 0) { sb_free(&us); return; }
    double now = now_ms(c);
    long i = ask_find(c, url);
    double rat = i >= 0 ? c->asks.at[i] : 0, rn = i >= 0 ? c->asks.n[i] : 0;
    double gap = rn < 5 ? 5000 : fmin(300000, 5000 * pow(2, rn - 4));
    if (now - rat < gap) { sb_free(&us); return; }
    ask_set(c, url, now, rn + 1);
    if (c->asks.cnt > 50) ask_del_at(c, 0);
    sbuf m; sb_init(&m, 0);
    sb_puts(&m, "{\"type\":\"NEED_THUMB\",\"url\":");
    json_write_str(&m, url, us.len);
    sb_putc(&m, '}');
    if (!m.err && c->h.ws_send) c->h.ws_send(c->h.user, client, m.p);
    sb_free(&m);
    sb_free(&us);
}

// ── 썸네일 업로드 (discord-rpc.js cacheThumbnail) ─────────────────
//  허브에서 한 번만 올리고 결과 주소를 모든 사이트 클라이언트에 나눠 준다. 실패하면 15s → 30s → 60s 뒤 다시 (최대 4번)

static void cache_thumb(core *c, const char *url, const char *data, int attempt);

typedef struct { core *c; char *url, *data; int attempt; } retry_ctx;
static void retry_cb(void *ctx, int id) {
    retry_ctx *r = (retry_ctx *)ctx;
    core *c = r->c;
    char *url = r->url, *data = r->data;
    int attempt = r->attempt;
    ctx_free(c, r);
    cache_thumb(c, url, data, attempt);
    free(url);
    free(data);
}

//  재시도 직전에 다시 묻는다 — 기다리는 사이 RPC·썸네일 표시를 껐거나 그 사이트를 못 띄우게 됐으면 올리지 않는다
static int thumb_gate(core *c, const char *url) {
    const char *tag = smap_get(&c->thumbSites, url);
    jval s; memset(&s, 0, sizeof s);
    if (tag && tag[0] == 's') { s.t = J_STR; s.s = (char *)tag + 1; s.slen = strlen(tag + 1); return canShow_thumb(c, &s); }
    return canShow_thumb(c, NULL);
}

static void cache_thumb(core *c, const char *url, const char *data, int attempt) {
    if (!url || !*url) return;
    if (attempt == 0) {
        if (*hub_cachedThumb(c, url) || smap_find(&c->uploading, url) >= 0) return;
    } else if (!thumb_gate(c, url)) {
        long u = smap_find(&c->uploading, url);
        if (u >= 0) smap_del_at(&c->uploading, (size_t)u);
        return;
    }
    smap_set(&c->uploading, url, "1");   // 재시도 대기 중에도 유지 → 중복 업로드 방지
    thumbjob *j = (thumbjob *)calloc(1, sizeof *j);
    if (!j) return;
    j->id = ++c->next_job;
    j->url = dup(url);
    j->data = dup(data);
    j->attempt = attempt;
    j->next = c->jobs;
    c->jobs = j;
    c->h.upload(c->h.user, j->id, data);
}

void core_upload_done(core *c, int job, int ok, const char *hosted, double keep, const char *err) {
    thumbjob *j = NULL;
    for (thumbjob **pp = &c->jobs; *pp; pp = &(*pp)->next) if ((*pp)->id == job) { j = *pp; *pp = j->next; break; }
    if (!j) return;
    if (ok && hosted && *hosted) {
        core_thumb_hosted(c, j->url, hosted, keep);   // 올리는 중 표시 지우기·로그·캐시·카드 갱신
    } else {
        logf_(c, 1, "[RPC] \xEC\x8D\xB8\xEB\x84\xA4\xEC\x9D\xBC \xEC\x9E\xAC\xED\x98\xB8\xEC\x8A\xA4\xED\x8C\x85 \xEC\x8B\xA4\xED\x8C\xA8(%d/4): %s", j->attempt + 1, err ? err : "");
        if (j->attempt < 3) {
            retry_ctx *r = (retry_ctx *)ctx_alloc(c, sizeof *r);
            if (r) {
                r->c = c; r->url = dup(j->url); r->data = dup(j->data); r->attempt = j->attempt + 1;
                set_timer(c, (int64_t)(15000 * pow(2, j->attempt)), 0, retry_cb, r);
            }
        } else {
            long u = smap_find(&c->uploading, j->url);   // 최종 포기 → 로고 유지
            if (u >= 0) smap_del_at(&c->uploading, (size_t)u);
        }
    }
    free(j->url);
    free(j->data);
    free(j);
}

//  허브 _resolveImage(url, '') — 클라이언트마다 물어 처음 나온 답 (없으면 ""). 돌려준 문자열은 호출한 쪽이 free
char *core_resolve_image(core *c, const char *url) {
    for (int i = 0; i < c->nclients; i++) {
        char *r = resolveImage(c->clients[i], url, "");
        if (r && *r) return r;
        free(r);
    }
    return dup("");
}

size_t core_hosted_count(core *c) { return c->hosted.n; }
int core_hosted_at(core *c, size_t i, const char **url, const char **hosted, double *at, double *keep_ms) {
    if (i >= c->hosted.n) return 0;
    thumb_e *e = &c->hosted.v[i];
    *url = e->url; *hosted = e->hosted; *at = e->at; *keep_ms = e->keep;
    return 1;
}

void core_load_hosted(core *c, const char *url, const char *hosted, double at, double keep) {
    if (!url || !*url || !hosted || !*hosted) return;
    while (c->hosted.n >= HOSTED_CACHE_MAX) tmap_del_at(&c->hosted, 0);
    tmap_set(&c->hosted, url, hosted, at, keep);
}

static int type_is(const jval *t, const char *s) { return t && t->t == J_STR && t->slen == strlen(s) && !memcmp(t->s, s, t->slen); }

static char *field_str(const jval *msg, const char *k) {
    sbuf b; sb_init(&b, 0);
    js_str_or_empty(&b, json_get(msg, k));
    char *r = dupn(nz(b.p), b.len);
    sb_free(&b);
    return r;
}

void core_ext_message(core *c, int client, const jval *msg) {
    if (!msg || msg->t != J_OBJ) return;
    const jval *type = json_get(msg, "type");
    if (type_is(type, "PING")) { if (c->h.ws_send) c->h.ws_send(c->h.user, client, "{\"type\":\"PONG\"}"); return; }
    //  다른 모듈이 맡는 메시지 (정주행·키 설정·링크 읽기)
    if (type_is(type, "BINGE_STATE") || type_is(type, "KEYBIND_PICK_START") || type_is(type, "KEYBIND_PICK_FAIL") || type_is(type, "KEYBIND_PICKED")
        || type_is(type, "KEYBIND_FS") || type_is(type, "KEYBINDS_LOCAL") || type_is(type, "RESOLVED") || type_is(type, "BINGE_END")) return;
    const jval *siteRaw = json_get(msg, "site");
    if (type_is(type, "THUMB_BYTES")) {
        //  못 띄우는 사이트의 이미지는 올리지 않는다 (사이트는 메시지에 실려 오거나, 앞서 받은 카드에서 안다)
        const jval *u = json_get(msg, "url"), *d = json_get(msg, "dataUrl");
        if (!c->h.upload || !u || u->t != J_STR || !d || d->t != J_STR) return;
        jval tagged; memset(&tagged, 0, sizeof tagged);
        const jval *site = siteRaw;
        if (!js_truthy(site)) {
            const char *tag = smap_get(&c->thumbSites, u->s);
            if (tag && tag[0] == 's') { tagged.t = J_STR; tagged.s = (char *)tag + 1; tagged.slen = strlen(tag + 1); site = &tagged; }
            else site = NULL;
        }
        if (!canShow_thumb(c, site)) return;   // (바이트 기억은 정주행 단계에서)
        cache_thumb(c, u->s, d->s, 0);
        return;
    }
    if (type_is(type, "CLEAR")) {
        sbuf b; sb_init(&b, 0);
        sb_puts(&b, "[Bridge] CLEAR \xEC\x88\x98\xEC\x8B\xA0 ");
        if (js_truthy(siteRaw)) { sb_putc(&b, '['); js_to_string(&b, siteRaw); sb_putc(&b, ']'); }
        else sb_puts(&b, "(\xEC\xA0\x84\xEC\xB2\xB4)");
        log_sb(c, 0, &b); sb_free(&b);
        if (js_truthy(siteRaw)) {
            sbuf s; sb_init(&s, 0);
            js_to_string(&s, siteRaw);
            //  clients.has(siteId) — 문자열 키로만 찾는다 (숫자 5 는 'youtube' 같은 키와 같을 수 없다)
            if (siteRaw->t == J_STR) core_clear(c, nz(s.p));
            sb_free(&s);
        } else core_clear(c, NULL);
        return;
    }
    jval tt; memset(&tt, 0, sizeof tt); tt.t = J_STR; tt.s = (char *)"ttfc"; tt.slen = 4;
    const jval *siteOrT = js_truthy(siteRaw) ? siteRaw : &tt;
    if (type_is(type, "VIDEO") || (type_is(type, "SET_ACTIVITY") && js_truthy(json_get(msg, "seriesName")))) {
        //  로그는 내용이 바뀔 때만 (사이트별로)
        sbuf key; sb_init(&key, 0);
        const char *kf[] = { "site", "seriesName", "episodeNumber", "episodeTitle", "isPlaying" };
        for (int i = 0; i < 5; i++) { if (i) sb_putc(&key, '|'); js_join_item(&key, json_get(msg, kf[i])); }
        sbuf mk; sb_init(&mk, 0);
        key_of(&mk, siteRaw);
        const char *prev = smap_get(&c->videoLogKeys, nz(mk.p));
        if (!prev || strcmp(prev, nz(key.p))) {
            smap_set(&c->videoLogKeys, nz(mk.p), nz(key.p));
            double rate = rate_from(json_get(msg, "playbackRate"));
            sbuf b; sb_init(&b, 0);
            sb_puts(&b, "[Bridge] VIDEO \xE2\x86\x90 ");
            js_to_string(&b, siteOrT);
            sb_puts(&b, " | ");
            js_to_string(&b, json_get(msg, "seriesName"));
            sb_putc(&b, ' ');
            js_str_or_empty(&b, json_get(msg, "episodeNumber"));
            sb_putc(&b, ' ');
            js_str_or_empty(&b, json_get(msg, "episodeTitle"));
            sb_puts(&b, " (");
            if (js_truthy(json_get(msg, "isLive"))) sb_puts(&b, "LIVE");
            else {
                jval zero; memset(&zero, 0, sizeof zero); zero.t = J_NUM;
                const jval *ct = json_get(msg, "currentTime"), *du = json_get(msg, "duration");
                js_to_string(&b, js_truthy(ct) ? ct : &zero);
                sb_putc(&b, '/');
                js_to_string(&b, js_truthy(du) ? du : &zero);
                sb_putc(&b, 's');
            }
            sb_puts(&b, js_truthy(json_get(msg, "isPlaying")) ? " \xE2\x96\xB6)" : " \xE2\x8F\xB8)");
            if (js_truthy(json_get(msg, "startedAtMs"))) sb_puts(&b, " \xE2\x9A\x93");
            if (rate != 1) { sb_puts(&b, " \xC3\x97"); js_number_str(&b, rate); }
            log_sb(c, 0, &b); sb_free(&b);
        }
        sb_free(&mk);
        sb_free(&key);
        requestThumbIfNeeded(c, client, json_get(msg, "thumbnail"), siteOrT);
        presence *p = hub_for(c, siteOrT);
        if (p) {
            vstate v;
            char *site = field_str(msg, "site");
            if (!*site) { free(site); site = dup("ttfc"); }
            char *siteName = field_str(msg, "siteName"), *series = field_str(msg, "seriesName"), *etitle = field_str(msg, "episodeTitle");
            char *enum_ = field_str(msg, "episodeNumber"), *thumb = field_str(msg, "thumbnail"), *url = field_str(msg, "url");
            v.site = site; v.siteName = siteName; v.seriesName = series; v.episodeTitle = etitle; v.episodeNumber = enum_;
            v.thumbnail = thumb; v.pageUrl = url;
            v.isPlaying = js_truthy(json_get(msg, "isPlaying"));
            v.isLive = js_truthy(json_get(msg, "isLive"));
            v.currentTime = num_or0(json_get(msg, "currentTime"));
            v.duration = num_or0(json_get(msg, "duration"));
            v.liveStartedAt = num_or0(json_get(msg, "liveStartedAt"));
            v.startedAtMs = num_or0(json_get(msg, "startedAtMs"));
            v.playbackRate = rate_from(json_get(msg, "playbackRate"));
            p_updateFromVideoState(p, &v, c->settings);
            free(site); free(siteName); free(series); free(etitle); free(enum_); free(thumb); free(url);
        }
        return;
    }
    if (type_is(type, "BROWSING")) {
        sbuf key; sb_init(&key, 0);
        const char *kf[] = { "site", "page", "detail", "url" };
        for (int i = 0; i < 4; i++) { if (i) sb_putc(&key, '|'); js_join_item(&key, json_get(msg, kf[i])); }
        sbuf mk; sb_init(&mk, 0);
        key_of(&mk, siteRaw);
        const char *prev = smap_get(&c->browsingLogKeys, nz(mk.p));
        if (!prev || strcmp(prev, nz(key.p))) {
            smap_set(&c->browsingLogKeys, nz(mk.p), nz(key.p));
            sbuf b; sb_init(&b, 0);
            sb_puts(&b, "[Bridge] BROWSING \xE2\x86\x90 ");
            js_to_string(&b, siteOrT);
            sb_puts(&b, " | ");
            js_to_string(&b, json_get(msg, "page"));
            if (js_truthy(json_get(msg, "detail"))) { sb_puts(&b, " / "); js_to_string(&b, json_get(msg, "detail")); }
            log_sb(c, 0, &b); sb_free(&b);
        }
        sb_free(&mk);
        sb_free(&key);
        requestThumbIfNeeded(c, client, json_get(msg, "banner"), siteOrT);
        presence *p = hub_for(c, siteOrT);
        if (p) {
            nstate n;
            char *site = field_str(msg, "site");
            if (!*site) { free(site); site = dup("ttfc"); }
            char *siteName = field_str(msg, "siteName"), *url = field_str(msg, "url"), *title = field_str(msg, "page");
            char *detail = field_str(msg, "detail"), *banner = field_str(msg, "banner");
            n.site = site; n.siteName = siteName; n.url = url; n.title = title; n.detail = detail; n.banner = banner;
            p_updateFromNavigation(p, &n, c->settings);
            free(site); free(siteName); free(url); free(title); free(detail); free(banner);
        }
    }
}

// ── 창에 보여 줄 상태 ────────────────────────────────────────

int core_cards(core *c, core_card *out, int max) {
    int n = 0;
    for (int k = 0; k < NSITES && n < max; k++) {
        presence *p = NULL;
        for (int i = 0; i < c->nclients; i++) if (!strcmp(c->clients[i]->site_id, SITES[k].id)) p = c->clients[i];
        if (!p || !p->connected || !p->lastActivity) continue;
        activity *a = p->lastActivity;
        core_card *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->site = SITES[k].id;
        o->site_name = (p->s.siteName && *p->s.siteName) ? p->s.siteName : SITES[k].name;
        o->details = a->details; o->state = a->state;
        o->large_key = a->large_key; o->large_text = a->large_text;
        o->small_key = a->small_key; o->small_text = a->small_text;
        o->url = (p->shownUrl && *p->shownUrl) ? p->shownUrl : a->url_for;
        o->has_start = a->has_start; o->has_end = a->has_end; o->start = a->start; o->end = a->end;
        o->watching = p->shownType == 1;
        o->playing = p->s.isWatching && p->s.isPlaying;
        o->type = (int)a->type;
        if (a->has_buttons) { o->btn_label = a->btn_label; o->btn_url = a->btn_url; }
    }
    return n;
}

//  트레이 메뉴 지연 시간 (discord-rpc.js latency()): 붙은 연결 중 가장 최근에 잰 SET_ACTIVITY 응답 시간. 없으면 *ms = -1
//  ⚠ 알파는 그 연결의 최근 5번 중간값을 돌려준다 — Discord 앱이 가끔 늦게 답한 한 번에 숫자가 튀지 않게 (지금 앱은 마지막 한 번)
void core_latency(core *c, int *connected, double *ms) {
    presence *best = NULL;
    *connected = 0;
    for (int i = 0; i < c->nclients; i++) {
        presence *p = c->clients[i];
        if (!p->connected) continue;
        *connected = 1;
        if (p->rttAt && (!best || p->rttAt > best->rttAt)) best = p;
    }
    *ms = best ? med5_get(&best->rttHist, 0, 0, best->rttMs) : -1;
}

int core_discord_connected(core *c) {
    for (int i = 0; i < c->nclients; i++) if (c->clients[i]->connected) return 1;
    return 0;
}

const char *core_user(core *c) {
    for (int i = 0; i < c->nclients; i++) if (c->clients[i]->connected && c->clients[i]->user && *c->clients[i]->user) return c->clients[i]->user;
    return "";
}
