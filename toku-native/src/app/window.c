// 알파 창 — 지금 앱 창(rpc-window.html)과 같은 색·배치를 C 로 직접 그린다
//  탭: 활동 · 표시 설정 · 앱 설정 · 키 설정 · 배경 꾸미기 · 웹훅 (뒤의 셋은 다음 단계 — 지금은 안내만)
//  매번 전체를 다시 그리고, 그릴 때 클릭할 수 있는 영역(hit)을 적어 둔다
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <imm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/utf.h"
#include "app.h"
#include "canvas.h"
#include "img.h"
#include "hooks.h"
#include "bg.h"
#include "imgutil.h"
#include "../net/http.h"
#include <time.h>
#include "binge.h"
#include "../core/js.h"
#include "settings.h"
#include "strings.h"
#include "backfill.h"
#include "accent.h"

// ── 색 (rpc-window.html :root 와 같은 값) ──
#define C_BG 0x111112
#define C_BAR 0x121315
#define C_CARD 0x17171a
#define C_THUMB 0x2b2b30
#define C_FIELD 0x161618
#define C_FIELD_HI 0x1b1b1f
#define C_BORDER 0x2b2c31
#define C_DIV 0x232328
#define C_TEXT 0xf4f5f8
#define C_TEXT2 0xc9cedb
#define C_DIM 0x9aa0b0
#define C_ACC 0x7775fc
#define C_ACCHI 0x908eff
#define C_ACC_SOFT 0x26263e    // accent 16% (카드 위)
#define C_AMBER_SOFT 0x3a3122
#define C_TRACK 0x3a3942
#define C_GREEN 0x3ae373
#define C_AMBER 0xf2b84b
#define C_RED 0xf2555a
#define VERSION app_version()
#define WM_IMG (WM_APP + 20)   // 카드 그림이 도착 (img.c → 창)

enum {
    ID_NONE, ID_TAB0, ID_TAB5 = ID_TAB0 + 5, ID_TAB6,
    ID_RPC, ID_RECONNECT, ID_VIEWALL, ID_OPENLOGS,
    ID_OPEN0, ID_OPEN5 = ID_OPEN0 + 5,
    ID_TIMEMODE, ID_SHOWSERIES, ID_SHOWEPISODE, ID_SHOWTHUMB, ID_SHOWBUTTONS, ID_ACTTYPE, ID_IDLE, ID_LEVEL,
    ID_SD0, ID_SD5 = ID_SD0 + 5,
    ID_LANG,
    ID_AUTOSTART, ID_FASTSTART, ID_BINGE, ID_TESTCH,
    ID_MIN, ID_MAX, ID_CLOSE,   // 머리줄 창 버튼 (WM_NCHITTEST 로 HTMINBUTTON 등)
};

static const char *SITE_IDS[6] = { "ttfc", "imagination", "youtube", "disneyplus", "chzzk", "danime" };
static const int SITE_STR[6] = { S_site_ttfc, S_site_imagination, S_site_youtube, S_site_disneyplus, S_site_chzzk, S_site_danime };

static HWND W;
static HINSTANCE INST;
static canvas CV;
static float S = 1;
static int tab, hover, press;
static int scroll_[7], content_h[7], view_h;
static HFONT f_body, f_bold, f_small, f_tab, f_mono, f_brand, f_title, f_initial, f_icon, f_bf12b, f_bf14b, f_bf13b, f_bf13;
static int caption_h = 52;   // 머리줄 높이 (그릴 때 적어 둠 — 끌기 영역)
static HICON icon_small;
typedef struct { int id; RECT r; } hit;
static hit hits[256];
static int nhits;
static core_card cards[6];
static struct { int busy, want, err_id; DWORD deadline, err_until; } binge_ui;   // 정주행 스위치

static int D(float v) { return (int)lroundf(v * S); }

static const char *T(int id) {
    const char *l = app_lang();
    const char *const *tbl = !strcmp(l, "ja") ? STR_JA : !strcmp(l, "en") ? STR_EN : STR_KO;
    return tbl[id];
}
// "{n}" / "{t}" 채우기
static void fill(char *out, size_t n, const char *tpl, const char *key, const char *val) {
    const char *k = strstr(tpl, key);
    if (!k) { snprintf(out, n, "%s", tpl); return; }
    snprintf(out, n, "%.*s%s%s", (int)(k - tpl), tpl, val, k + strlen(key));
}

static HFONT mkfont(float px, int weight, const wchar_t *face) {
    return CreateFontW(-D(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
//  창 버튼 글리프 글꼴: Windows 11 은 Segoe Fluent Icons, 10 은 Segoe MDL2 Assets (코드 포인트 같음)
static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) { *(int *)lp = 1; return 0; }
static const wchar_t *icon_face(void) {
    static const wchar_t *face;
    if (face) return face;
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy(lf.lfFaceName, L"Segoe Fluent Icons");
    int found = 0;
    HDC dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    face = found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return face;
}

static void make_fonts(void) {
    HFONT *all[] = { &f_body, &f_bold, &f_small, &f_tab, &f_mono, &f_brand, &f_title, &f_initial, &f_icon, &f_bf12b, &f_bf14b, &f_bf13b, &f_bf13 };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) if (*all[i]) { DeleteObject(*all[i]); *all[i] = NULL; }
    f_body = mkfont(14.5f, FW_NORMAL, L"Segoe UI");
    f_bold = mkfont(15.5f, FW_SEMIBOLD, L"Segoe UI");
    f_small = mkfont(12.5f, FW_NORMAL, L"Segoe UI");
    f_tab = mkfont(14.5f, FW_NORMAL, L"Segoe UI");
    f_mono = mkfont(12.5f, FW_NORMAL, L"Consolas");
    f_brand = mkfont(15, FW_SEMIBOLD, L"Segoe UI");
    f_title = mkfont(13, FW_SEMIBOLD, L"Segoe UI");
    f_initial = mkfont(28, FW_SEMIBOLD, L"Segoe UI");
    f_icon = mkfont(10, FW_NORMAL, icon_face());
    f_bf12b = mkfont(12, FW_SEMIBOLD, L"Segoe UI");
    f_bf14b = mkfont(14, FW_SEMIBOLD, L"Segoe UI");
    f_bf13b = mkfont(13, FW_BOLD, L"Segoe UI");
    f_bf13 = mkfont(13, FW_NORMAL, L"Segoe UI");
    if (icon_small) DestroyIcon(icon_small);
    icon_small = (HICON)LoadImageW(INST, MAKEINTRESOURCEW(1), IMAGE_ICON, D(22), D(22), 0);
}

static void add_hit(int id, int x, int y, int w, int h) {
    if (nhits >= 256) return;
    RECT r = { x, y, x + w, y + h };
    RECT c = CV.clip;
    if (r.left < c.left) r.left = c.left;
    if (r.top < c.top) r.top = c.top;
    if (r.right > c.right) r.right = c.right;
    if (r.bottom > c.bottom) r.bottom = c.bottom;
    if (r.right <= r.left || r.bottom <= r.top) return;
    hits[nhits].id = id;
    hits[nhits].r = r;
    nhits++;
}

// ── 설정 값 (지금 앱과 같은 규칙) ──
static int flag(const char *k) { const jval *v = settings_get("rpc", k); return !(v && v->t == J_BOOL && !v->b); }
static const char *time_mode(void) {
    const jval *v = settings_get("rpc", "timeMode");
    if (v && v->t == J_STR && (!strcmp(v->s, "remaining") || !strcmp(v->s, "none"))) return v->s;
    return "progress";
}
static int act_type(void) { const jval *v = settings_get("rpc", "activityType"); double n = v && v->t == J_NUM ? v->n : (v && v->t == J_STR ? atof(v->s) : 3); return (n == 0 || n == 2) ? (int)n : 3; }
static int sd_of(const char *site) {
    const jval *m = settings_get("rpc", "statusDisplay");
    const jval *v = json_get(m, site);
    double n = v && v->t == J_NUM ? v->n : (v && v->t == J_STR ? atof(v->s) : 2);
    return (n == 0 || n == 1) ? (int)n : 2;
}
static double idle_min(void) { const jval *v = settings_get("rpc", "idleTimeout"); if (!v || v->t == J_NULL) return 5; return (v->t == J_NUM && v->n >= 0) ? v->n : 5; }
static const char *level_of(void) {
    const jval *v = settings_get("responseLevel", NULL);
    if (v && v->t == J_STR && (!strcmp(v->s, "max") || !strcmp(v->s, "high") || !strcmp(v->s, "low"))) return v->s;
    return "normal";
}
static const char *lang_setting(void) {
    const jval *v = settings_get("lang", NULL);
    if (v && v->t == J_STR && (!strcmp(v->s, "ko") || !strcmp(v->s, "en") || !strcmp(v->s, "ja"))) return v->s;
    return "auto";
}

// ── 위젯 ──
static void w_switch(int id, int x, int y, int on) {
    float tw = (float)D(46), th = (float)D(26);
    uint32_t track = on ? (hover == id ? C_ACCHI : C_ACC) : (hover == id ? 0x46454f : C_TRACK);
    cv_round(&CV, (float)x, (float)y, tw, th, th / 2, track, 1);
    float kr = (float)D(10);
    float kx = on ? x + tw - D(3) - kr : x + D(3) + kr;
    cv_circle(&CV, kx, y + th / 2, kr, 0xffffff);
    add_hit(id, x, y, (int)tw, (int)th);
}
static void w_button(int id, int x, int y, int w, const char *label) {
    int h = D(34);
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), hover == id ? C_FIELD_HI : C_FIELD, 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), S > 1 ? S : 1, C_BORDER);
    cv_text(&CV, f_body, label, x, y, w, h, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    add_hit(id, x, y, w, h);
}
static void w_select(int id, int x, int y, int w, const char *label) {
    int h = D(34);
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), hover == id ? C_FIELD_HI : C_FIELD, 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), S > 1 ? S : 1, hover == id ? 0x3a3b42 : C_BORDER);
    cv_text(&CV, f_body, label, x + D(12), y, w - D(36), h, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_text(&CV, f_small, "\xE2\x96\xBE", x + w - D(26), y, D(16), h, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    add_hit(id, x, y, w, h);
}
static void w_link(int id, int x, int y, int w, int h, const char *label, UINT align) {
    cv_text(&CV, f_small, label, x, y, w, h, hover == id ? C_ACCHI : C_ACC, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int tw = cv_text_w(&CV, f_small, label);
    if (tw > w) tw = w;
    add_hit(id, (align & DT_RIGHT) ? x + w - tw : x, y, tw, h);
}
static int BG_ON;   //  창 바탕에 배경 사진이 깔렸나 (카드·줄을 반투명으로)
static void card(int x, int y, int w, int h) {
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(10), C_CARD, BG_ON ? 0.86f : 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(10), S > 1 ? S : 1, C_BORDER);
}
static void section_title(int x, int y, int w, const char *t) {
    cv_text(&CV, f_title, t, x, y, w, D(20), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
// 설정 한 줄: 왼쪽 이름(+설명), 오른쪽 컨트롤 자리 x 를 돌려준다
static int row(int x, int y, int w, int h, const char *name, const char *hint, int last, int ctrl_w) {
    int tx = x + D(20), tw = w - D(40) - ctrl_w - D(16);
    if (hint) {
        cv_text(&CV, f_body, name, tx, y + D(12), tw, D(20), C_TEXT, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        cv_text(&CV, f_small, hint, tx, y + D(33), tw, D(18), C_DIM, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        cv_text(&CV, f_body, name, tx, y, tw, h, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (!last) cv_fill(&CV, x + D(20), y + h - 1, w - D(40), S > 1 ? (int)S : 1, C_DIV);
    return x + w - D(20) - ctrl_w;
}

static void fmt_time(char *out, size_t n, double sec) {
    if (sec < 0) sec = 0;
    long s = (long)sec, h = s / 3600, m = (s / 60) % 60, ss = s % 60;
    if (h) snprintf(out, n, "%ld:%02ld:%02ld", h, m, ss);
    else snprintf(out, n, "%ld:%02ld", m, ss);
}

// ── Discord 활동 카드 (프로필에 보이는 모양: 활동 종류 → 큰 그림 + 작은 아이콘 · 이름 · details · state · 시간 → 버튼) ──
#define DCK_BG 0x1e1f22
#define DCK_BORDER 0x2b2d31
#define DCK_HEAD 0xf2f3f5
#define DCK_TEXT 0xdbdee1
#define DCK_MUTED 0xb5bac1
#define DCK_BTN 0x4e5058
#define DCK_BTN_HI 0x6d6f78

//  그림 키: https 주소는 그대로, 그 밖의 값은 Discord 앱 에셋 이름 ("asset:<앱>:<이름>")
static int img_key(char *out, size_t n, const char *site, const char *k) {
    out[0] = 0;
    if (!k || !*k || strlen(k) + 40 >= n) return 0;
    if (!strncmp(k, "https://", 8)) { snprintf(out, n, "%s", k); return 1; }
    if (strchr(k, ':') || strchr(k, '/')) return 0;    // http:// 등 — 받지 않는다
    const char *app = app_app_id(site);
    if (!app || !*app) return 0;
    snprintf(out, n, "asset:%s:%s", app, k);
    return 1;
}

static int discord_card(const core_card *c, int i, int x, int y, int w, double now) {
    int pad = D(16), L = D(80), sm = D(28), ring = D(3);
    //  높이를 먼저 잰다 — 글줄 수에 따라 달라진다
    int lines = 1 + (c->details && *c->details) + (c->state && *c->state);
    int timed = (c->has_start && c->has_end && c->end > c->start) || c->has_start || c->has_end;
    int text_h = lines * D(20) + (timed ? D(24) : 0);
    int row_h = text_h > L ? text_h : L;
    int has_btn = c->btn_label && *c->btn_label;
    int h = pad + D(26) + row_h + (has_btn ? D(12) + D(32) : 0) + pad;
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), DCK_BG, 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), S > 1 ? S : 1, DCK_BORDER);
    //  머리: 활동 종류 (Discord 와 같이 굵은 작은 글씨) · 버튼이 없을 때만 오른쪽에 '사이트에서 보기'
    const char *verb = c->type == 0 ? T(S_NA_verbPlaying) : c->type == 2 ? T(S_NA_verbListening) : T(S_NA_verbWatching);
    int lkw = (!has_btn && c->url && *c->url) ? cv_text_w(&CV, f_small, T(S_openSite)) + D(4) : 0;
    cv_text(&CV, f_title, verb, x + pad, y + pad, w - 2 * pad - lkw, D(18), DCK_HEAD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (lkw) w_link(ID_OPEN0 + i, x + w - pad - lkw, y + pad, lkw, D(18), T(S_openSite), DT_RIGHT);
    //  큰 그림 (못 받았으면 빈 칸 + 사이트 첫 글자)
    int iy = y + pad + D(26), ix = x + pad;
    char key[2200];
    const uint32_t *px = NULL;
    if (!(img_key(key, sizeof key, c->site, c->large_key) && img_get(key, &px) == 1)) px = NULL;
    if (px) cv_image(&CV, px, IMG_SIZE, (float)ix, (float)iy, (float)L, (float)D(8));
    else {
        cv_round(&CV, (float)ix, (float)iy, (float)L, (float)L, (float)D(8), C_THUMB, 1);
        char ini[8] = "";
        const unsigned char *s = (const unsigned char *)c->site_name;
        size_t k = s[0] < 0x80 ? 1 : s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : 2;
        memcpy(ini, s, k);
        ini[k] = 0;
        cv_text(&CV, f_initial, ini, ix, iy, L, L, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    //  작은 아이콘 (재생·일시정지·라이브) — 카드 색 테두리를 두른 원, 큰 그림 오른쪽 아래
    const uint32_t *spx = NULL;
    if (img_key(key, sizeof key, c->site, c->small_key) && img_get(key, &spx) == 1 && spx) {
        float cx = (float)(ix + L - sm / 2 + D(4)), cy = (float)(iy + L - sm / 2 + D(4));
        cv_circle(&CV, cx, cy, sm / 2.0f + ring, DCK_BG);
        cv_image(&CV, spx, IMG_SIZE, cx - sm / 2.0f, cy - sm / 2.0f, (float)sm, sm / 2.0f);
    }
    //  글: 이름(굵게) · details · state · 시간
    int tx = ix + L + D(14), tw = x + w - pad - tx, ty = iy + (row_h - text_h) / 2;
    if (ty < iy) ty = iy;
    cv_text(&CV, f_bold, c->site_name, tx, ty, tw, D(20), DCK_HEAD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ty += D(20);
    if (c->details && *c->details) { cv_text(&CV, f_body, c->details, tx, ty, tw, D(20), DCK_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); ty += D(20); }
    if (c->state && *c->state) { cv_text(&CV, f_body, c->state, tx, ty, tw, D(20), DCK_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); ty += D(20); }
    char tb[64] = "", a1[24], a2[24];
    if (c->has_start && c->has_end && c->end > c->start) {
        //  진행 바: 왼쪽 지난 시간 · 막대 · 오른쪽 전체 길이 (Discord 시청 카드)
        double total = c->end - c->start, el = now - c->start;
        if (el < 0) el = 0;
        if (el > total) el = total;
        fmt_time(a1, sizeof a1, el);
        fmt_time(a2, sizeof a2, total);
        int w1 = cv_text_w(&CV, f_small, a1), w2 = cv_text_w(&CV, f_small, a2), by = ty + D(6);
        cv_text(&CV, f_small, a1, tx, by, w1, D(16), DCK_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cv_text(&CV, f_small, a2, tx + tw - w2, by, w2, D(16), DCK_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bx = tx + w1 + D(10), bw = tw - w1 - w2 - D(20);
        if (bw > D(20)) {
            cv_round(&CV, (float)bx, (float)(by + D(6)), (float)bw, (float)D(4), (float)D(2), DCK_BTN, 1);
            cv_round(&CV, (float)bx, (float)(by + D(6)), (float)(bw * (el / total)), (float)D(4), (float)D(2), DCK_HEAD, 1);
        }
    } else if (c->has_start || c->has_end) {
        if (c->has_start) { fmt_time(a1, sizeof a1, now - c->start); fill(tb, sizeof tb, T(S_timeElapsed), "{t}", a1); }
        else { fmt_time(a1, sizeof a1, c->end - now); fill(tb, sizeof tb, T(S_timeLeft), "{t}", a1); }
        cv_text(&CV, f_small, tb, tx, ty + D(6), tw, D(16), DCK_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    //  버튼 (Discord 에 실제로 보낸 것)
    if (has_btn) {
        int by = iy + row_h + D(12), bw = w - 2 * pad;
        cv_round(&CV, (float)(x + pad), (float)by, (float)bw, (float)D(32), (float)D(8), hover == ID_OPEN0 + i ? DCK_BTN_HI : DCK_BTN, 1);
        cv_text(&CV, f_body, c->btn_label, x + pad + D(12), by, bw - D(24), D(32), 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        add_hit(ID_OPEN0 + i, x + pad, by, bw, D(32));
    }
    return h;
}

// ── 탭: 활동 ──
static int page_activity(int x, int y, int w) {
    int two = w >= D(760);
    int gap = D(20);
    int lw = two ? (int)(w * 0.58f) : w, rx = two ? x + lw + gap : x, rw = two ? w - lw - gap : w;
    int ly = y;
    section_title(x, ly, lw, T(S_current));
    ly += D(28);
    int n = core_cards(app_core(), cards, 6);
    double nows = (double)GetTickCount64();   // 시계와 무관 — 아래는 실제 시각을 쓴다
    (void)nows;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    double now = (double)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000ULL - 11644473600000ULL) / 1000.0;
    if (!n) {
        int h = D(132);
        card(x, ly, lw, h);
        cv_text(&CV, f_bold, T(S_nothing), x, ly + D(36), lw, D(24), C_TEXT2, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        cv_text(&CV, f_small, T(S_nothingHint), x + D(20), ly + D(66), lw - D(40), D(40), C_DIM, DT_CENTER | DT_WORDBREAK);
        ly += h + D(12);
    }
    for (int i = 0; i < n; i++) {
        ly += discord_card(&cards[i], i, x, ly, lw, now) + D(12);
    }
    //  오른쪽 (좁으면 아래로)
    int ry = two ? y : ly + D(8);
    section_title(rx, ry, rw, T(S_connection));
    ry += D(28);
    int ch = D(104);
    card(rx, ry, rw, ch);
    int dc = core_discord_connected(app_core()), ext = app_ext_count();
    char buf[160];
    const char *user = core_user(app_core());
    if (dc && *user) snprintf(buf, sizeof buf, "%s \xC2\xB7 %s", T(S_dcConnected), user); else snprintf(buf, sizeof buf, "%s", dc ? T(S_dcConnected) : T(S_dcDisconnected));
    cv_text(&CV, f_body, "Discord", rx + D(20), ry + D(10), rw / 2, D(40), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cv_circle(&CV, (float)(rx + rw / 2), (float)(ry + D(30)), (float)D(4), dc ? C_GREEN : C_RED);
    cv_text(&CV, f_body, buf, rx + rw / 2 + D(12), ry + D(10), rw / 2 - D(32), D(40), C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_fill(&CV, rx + D(20), ry + D(52), rw - D(40), S > 1 ? (int)S : 1, C_DIV);
    char en[16];
    snprintf(en, sizeof en, "%d", ext);
    if (ext) fill(buf, sizeof buf, T(S_extConnected), "{n}", en); else snprintf(buf, sizeof buf, "%s", app_port_ready() ? T(S_extWaiting) : T(S_NA_portWait));
    cv_text(&CV, f_body, T(S_ext), rx + D(20), ry + D(54), rw / 2, D(40), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_circle(&CV, (float)(rx + rw / 2), (float)(ry + D(74)), (float)D(4), ext ? C_GREEN : C_AMBER);
    cv_text(&CV, f_body, buf, rx + rw / 2 + D(12), ry + D(54), rw / 2 - D(32), D(40), C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ry += ch + D(20);
    section_title(rx, ry, rw, T(S_quick));
    ry += D(28);
    int qh = D(192);
    card(rx, ry, rw, qh);
    int on = app_rpc_on();
    int cx = row(rx, ry, rw, D(64), on ? T(S_rpcOn) : T(S_rpcOff), T(S_rpcToggleHint), 0, D(46));
    w_switch(ID_RPC, cx, ry + D(19), on);
    cx = row(rx, ry + D(64), rw, D(64), T(S_reconnect), T(S_reconnectHint), 0, D(96));
    w_button(ID_RECONNECT, cx, ry + D(64) + D(15), D(96), T(S_reconnect));
    {   //  정주행 모드 (rpc-window.js applyBinge): 확장이 알려 준 값만 보인다. 모르면 끈 채로 안내
        int bs = binge_state(), known = bs >= 0;
        const char *hint = GetTickCount() < binge_ui.err_until ? T(binge_ui.err_id)
                         : !known ? (app_ext_count() ? T(S_bingeOldExt) : T(S_bingeNoExt)) : bs ? T(S_bingeOnHint) : T(S_bingeOffHint);
        cx = row(rx, ry + D(128), rw, D(64), T(S_binge), hint, 1, D(46));
        w_switch(known && !binge_ui.busy ? ID_BINGE : ID_NONE, cx, ry + D(128) + D(19), binge_ui.busy ? binge_ui.want : bs == 1);
    }
    ry += qh + D(20);
    section_title(rx, ry, rw - D(90), T(S_recentLogs));
    w_link(ID_VIEWALL, rx + rw - D(90), ry, D(90), D(20), T(S_viewAll), DT_RIGHT);
    ry += D(28);
    int lines = 12, lh = D(19);
    int lgh = D(16) * 2 + lines * lh;
    card(rx, ry, rw, lgh);
    int cnt = app_log_count();
    if (!cnt) cv_text(&CV, f_small, T(S_noLogs), rx, ry, rw, lgh, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int first = cnt > lines ? cnt - lines : 0;
    for (int i = first, k = 0; i < cnt; i++, k++) {
        const char *l = app_log_line(i);
        //  "[YYYY-MM-DD HH:MM:SS.mmm] [info]  내용" → 시각 + 내용
        char tm[16] = "";
        const char *body = l;
        if (strlen(l) > 26 && l[0] == '[') { memcpy(tm, l + 12, 8); tm[8] = 0; body = l + 25; while (*body == ' ') body++; if (*body == '[') { const char *e = strchr(body, ']'); if (e) { body = e + 1; while (*body == ' ') body++; } } }
        int yy = ry + D(16) + k * lh;
        int warn = strstr(l, "[warn]") || strstr(l, "[error]");
        cv_text(&CV, f_mono, tm, rx + D(16), yy, D(64), lh, C_DIM, DT_LEFT | DT_SINGLELINE);
        //  내용은 Segoe UI (Consolas 는 일본어 장음 ー 같은 글자가 네모로 나옴)
        cv_text(&CV, f_small, body, rx + D(16) + D(68), yy, rw - D(32) - D(68), lh, warn ? C_AMBER : C_TEXT2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    ry += lgh;
    int bottom = ly > ry ? ly : ry;
    return bottom - y + D(20);
}

// ── 탭: 표시 설정 ──
static const char *time_label(const char *m) { return !strcmp(m, "remaining") ? T(S_timeRemaining) : !strcmp(m, "none") ? T(S_timeNone) : T(S_timeProgress); }
static const char *act_label(int t) { return t == 2 ? T(S_actListening) : t == 0 ? T(S_actPlaying) : T(S_actWatching); }
static void idle_label(char *out, size_t n, double m) {
    char v[16];
    if (m <= 0) { snprintf(out, n, "%s", T(S_idleNever)); return; }
    if (m >= 60 && fmod(m, 60) == 0) { snprintf(v, sizeof v, "%g", m / 60); fill(out, n, T(S_idleHour), "{n}", v); return; }
    snprintf(v, sizeof v, "%g", m);
    fill(out, n, T(S_idleMin), "{n}", v);
}
static const char *level_label(const char *lv) {
    return !strcmp(lv, "max") ? T(S_NA_levelMax) : !strcmp(lv, "high") ? T(S_NA_levelHigh) : !strcmp(lv, "low") ? T(S_NA_levelLow) : T(S_NA_levelNormal);
}
static const char *sd_label(int v) { return v == 1 ? T(S_sdEpisode) : v == 0 ? T(S_sdAppName) : T(S_sdSeries); }

static int page_display(int x, int y, int w) {
    if (w > D(760)) w = D(760);
    int yy = y;
    section_title(x, yy, w, T(S_settings));
    yy += D(28);
    int rh = D(64), sw = D(240);
    int n = 8;
    card(x, yy, w, rh * n);
    int cy = yy;
    int cx;
    cx = row(x, cy, w, rh, T(S_timeMode), NULL, 0, sw); w_select(ID_TIMEMODE, cx, cy + D(15), sw, time_label(time_mode())); cy += rh;
    cx = row(x, cy, w, rh, T(S_showSeries), NULL, 0, D(46)); w_switch(ID_SHOWSERIES, cx, cy + D(19), flag("showSeries")); cy += rh;
    cx = row(x, cy, w, rh, T(S_showEpisode), NULL, 0, D(46)); w_switch(ID_SHOWEPISODE, cx, cy + D(19), flag("showEpisode")); cy += rh;
    cx = row(x, cy, w, rh, T(S_showThumbnail), NULL, 0, D(46)); w_switch(ID_SHOWTHUMB, cx, cy + D(19), flag("showThumbnail")); cy += rh;
    cx = row(x, cy, w, rh, T(S_showButtons), NULL, 0, D(46)); w_switch(ID_SHOWBUTTONS, cx, cy + D(19), flag("showButtons")); cy += rh;
    cx = row(x, cy, w, rh, T(S_activityType), T(S_activityTypeHint), 0, sw); w_select(ID_ACTTYPE, cx, cy + D(15), sw, act_label(act_type())); cy += rh;
    char il[64];
    idle_label(il, sizeof il, idle_min());
    cx = row(x, cy, w, rh, T(S_idleTimeout), T(S_idleTimeoutHint), 0, sw); w_select(ID_IDLE, cx, cy + D(15), sw, il); cy += rh;
    cx = row(x, cy, w, rh, T(S_responseLevel), T(S_responseLevelHint), 1, sw); w_select(ID_LEVEL, cx, cy + D(15), sw, level_label(level_of())); cy += rh;
    yy = cy + D(24);
    section_title(x, yy, w, T(S_statusDisplay));
    yy += D(22);
    cv_text(&CV, f_small, T(S_statusDisplayHint), x, yy, w, D(18), C_DIM, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    yy += D(26);
    int sh = D(56);
    card(x, yy, w, sh * 6);
    for (int i = 0; i < 6; i++) {
        cx = row(x, yy + i * sh, w, sh, T(SITE_STR[i]), NULL, i == 5, D(200));
        w_select(ID_SD0 + i, cx, yy + i * sh + D(11), D(200), sd_label(sd_of(SITE_IDS[i])));
    }
    yy += sh * 6;
    return yy - y + D(20);
}

// ── 탭: 앱 설정 ──
static const char *lang_label(const char *v) {
    return !strcmp(v, "ko") ? T(S_NA_langKo) : !strcmp(v, "en") ? T(S_NA_langEn) : !strcmp(v, "ja") ? T(S_NA_langJa) : T(S_langAuto);
}
static int page_app(int x, int y, int w) {
    if (w > D(760)) w = D(760);
    int yy = y, rh = D(64), cx;
    section_title(x, yy, w, T(S_tabApp));
    yy += D(28);
    card(x, yy, w, rh * 5);
    cx = row(x, yy, w, rh, T(S_language), NULL, 0, D(240)); w_select(ID_LANG, cx, yy + D(15), D(240), lang_label(lang_setting()));
    int on = app_rpc_on();
    cx = row(x, yy + rh, w, rh, on ? T(S_rpcOn) : T(S_rpcOff), T(S_rpcToggleHint), 0, D(46)); w_switch(ID_RPC, cx, yy + rh + D(19), on);
    cx = row(x, yy + 2 * rh, w, rh, T(S_autoStart), T(S_autoStartHint), 0, D(46)); w_switch(ID_AUTOSTART, cx, yy + 2 * rh + D(19), app_autostart());
    cx = row(x, yy + 3 * rh, w, rh, T(S_fastStart), T(S_fastStartHint), 0, D(46)); w_switch(ID_FASTSTART, cx, yy + 3 * rh + D(19), app_faststart());
    //  테스트 버전(알파) — 켜고 끄면 앱이 다시 시작해 그 모드로 뜬다
    cx = row(x, yy + 4 * rh, w, rh, T(S_NA_testCh), T(S_NA_testChHint), 1, D(46)); w_switch(ID_TESTCH, cx, yy + 4 * rh + D(19), app_test_channel());
    yy += rh * 5 + D(24);
    section_title(x, yy, w, T(S_NA_info));
    yy += D(28);
    card(x, yy, w, rh * 3);
    char ver[64];
    snprintf(ver, sizeof ver, "%s %s", T(CH_LABEL), VERSION);
    cx = row(x, yy, w, rh, T(S_version), NULL, 0, D(200));
    cv_text(&CV, f_mono, ver, cx, yy, D(200), rh, C_TEXT2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    char *path = w_to_utf8(app_config_path(), wcslen(app_config_path()));
    cx = row(x, yy + rh, w, rh, T(S_NA_configPath), NULL, 0, w - D(240));
    cv_text(&CV, f_mono, path ? path : "", cx, yy + rh, w - D(240), rh, C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    free(path);
    cx = row(x, yy + 2 * rh, w, rh, T(S_NA_openLogs), NULL, 1, D(150));
    w_button(ID_OPENLOGS, cx, yy + 2 * rh + D(15), D(150), T(S_NA_openLogs));
    yy += rh * 3;
    return yy - y + D(20);
}

// ── 탭: 키 설정 (rpc-window.js kbRender · kbRow 와 같은 내용) ──
//  hit id: ID_KB0 + 사이트*1000 + 줄*10 + 칸(0 이름 · 1 키 · 2 다시 고르기 · 3 삭제) / +900 켜고 끄기 / +901 버튼 고르기
#define ID_KB0 10000
#define KB_ID(s, r, p) (ID_KB0 + (s) * 1000 + (r) * 10 + (p))
#define WM_KBEDIT (WM_APP + 21)

static struct { int on; char site[16], id[32]; } kb_pick, kb_cap;   // 고르는 중 · 키 받는 중
static struct { char key[56]; char text[300]; int cls; } kb_msgs[48];   // cls: 0 보통 · 1 좋음 · 2 오류
static struct { char key[56]; DWORD at; } kb_armed;                    // '정말 삭제' (3초)

static const char *kb_site_name(int s) {
    static const char *N[6] = { "\xE6\x9D\xB1\xE6\x98\xA0\xE7\x89\xB9\xE6\x92\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xA9\xE3\x83\x96", "TSUBURAYA IMAGINATION", "YouTube", "Disney+", NULL, "d\xE3\x82\xA2\xE3\x83\x8B\xE3\x83\xA1\xE3\x82\xB9\xE3\x83\x88\xE3\x82\xA2" };
    return s == 4 ? T(S_kbChzzk) : N[s];
}
static int kb_site_index(const char *site) { for (int i = 0; i < 6; i++) if (!strcmp(SITE_IDS[i], site)) return i; return -1; }

static void kb_say(const char *key, const char *text, int cls) {
    int slot = -1;
    for (int i = 0; i < 48; i++) if (!strcmp(kb_msgs[i].key, key)) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < 48; i++) if (!kb_msgs[i].key[0]) { slot = i; break; }
    if (slot < 0) slot = 0;
    snprintf(kb_msgs[slot].key, sizeof kb_msgs[slot].key, "%s", key);
    snprintf(kb_msgs[slot].text, sizeof kb_msgs[slot].text, "%s", text);
    kb_msgs[slot].cls = cls;
}
static const char *kb_msg(const char *key, int *cls) {
    for (int i = 0; i < 48; i++) if (kb_msgs[i].key[0] && !strcmp(kb_msgs[i].key, key)) { *cls = kb_msgs[i].cls; return kb_msgs[i].text; }
    return NULL;
}
static void kb_unsay(const char *key) { for (int i = 0; i < 48; i++) if (!strcmp(kb_msgs[i].key, key)) kb_msgs[i].key[0] = 0; }
static const char *kb_err_text(const char *e) {
    if (!e) return T(S_kbErrSave);
    if (!strcmp(e, "noext")) return T(S_kbErrNoExt);
    if (!strcmp(e, "oldext")) return T(S_kbErrOldExt);
    if (!strcmp(e, "notab") || !strcmp(e, "target")) return T(S_kbErrNoTab);
    if (!strcmp(e, "timeout")) return T(S_kbErrTimeout);
    if (!strcmp(e, "full")) return T(S_kbErrFull);
    if (!strcmp(e, "key")) return T(S_kbErrKey);
    if (!strcmp(e, "missing")) return T(S_kbErrMissing);
    return T(S_kbErrSave);
}

//  키 이름 (kbKeyName)
static void kb_key_name(char *out, size_t n, const jval *b) {
    const char *code = json_get_str(b, "code", "");
    out[0] = 0;
    if (!*code) return;
    static const char *const NM[][2] = { { "Space", "Space" }, { "ArrowLeft", "\xE2\x86\x90" }, { "ArrowRight", "\xE2\x86\x92" }, { "ArrowUp", "\xE2\x86\x91" }, { "ArrowDown", "\xE2\x86\x93" },
        { "Enter", "Enter" }, { "Backspace", "Backspace" }, { "Tab", "Tab" }, { "Delete", "Del" }, { "Insert", "Ins" }, { "Home", "Home" }, { "End", "End" }, { "PageUp", "PgUp" },
        { "PageDown", "PgDn" }, { "Minus", "-" }, { "Equal", "=" }, { "BracketLeft", "[" }, { "BracketRight", "]" }, { "Backslash", "\\" }, { "Semicolon", ";" }, { "Quote", "'" },
        { "Comma", "," }, { "Period", "." }, { "Slash", "/" }, { "Backquote", "`" } };
    char k[40];
    snprintf(k, sizeof k, "%s", code);
    for (size_t i = 0; i < sizeof NM / sizeof NM[0]; i++) if (!strcmp(code, NM[i][0])) snprintf(k, sizeof k, "%s", NM[i][1]);
    if (!strncmp(code, "Key", 3) && strlen(code) == 4 && code[3] >= 'A' && code[3] <= 'Z') snprintf(k, sizeof k, "%s", code + 3);
    else if (!strncmp(code, "Digit", 5) && strlen(code) == 6 && code[5] >= '0' && code[5] <= '9') snprintf(k, sizeof k, "%s", code + 5);
    else if (!strncmp(code, "Numpad", 6)) snprintf(k, sizeof k, "Num %s", code + 6);
    snprintf(out, n, "%s%s%s%s%s", json_get_bool(b, "ctrl", 0) ? "Ctrl+" : "", json_get_bool(b, "alt", 0) ? "Alt+" : "", json_get_bool(b, "shift", 0) ? "Shift+" : "",
             json_get_bool(b, "meta", 0) ? "Win+" : "", k);
}

//  전체화면 키 표시 (kbLooksFs) — 확장이 배운 것(fs) 또는 이름표에 전체화면이 든 버튼
static int kb_looks_fs(const jval *b) {
    const jval *fs = json_get(b, "fs");
    if (fs && fs->t == J_BOOL && fs->b) return 1;
    const jval *t = json_get(b, "target");
    sbuf s; sb_init(&s, 0);
    const char *F[] = { "aria", "title", "text" };
    for (int i = 0; i < 3; i++) { sb_puts(&s, json_get_str(t, F[i], "")); sb_putc(&s, ' '); }
    const jval *cls = json_get(t, "cls");
    for (size_t i = 0; cls && cls->t == J_ARR && i < cls->count; i++) { if (i) sb_putc(&s, ' '); sb_puts(&s, json_str(cls->items[i], "")); }
    int found = 0;
    const char *p = s.p ? s.p : "";
    size_t n = s.len;
    //  \s 로 볼 것: ASCII 공백 · NBSP · 전각 공백 · BOM
    #define WS_AT(q) ((q) < n && (p[q] == ' ' || p[q] == '\t' || p[q] == '\n' || p[q] == '\r' || p[q] == '\f' || p[q] == '\v') ? 1 : \
        ((q) + 1 < n && (unsigned char)p[q] == 0xC2 && (unsigned char)p[(q) + 1] == 0xA0) ? 2 : \
        ((q) + 2 < n && (unsigned char)p[q] == 0xE3 && (unsigned char)p[(q) + 1] == 0x80 && (unsigned char)p[(q) + 2] == 0x80) ? 3 : \
        ((q) + 2 < n && (unsigned char)p[q] == 0xEF && (unsigned char)p[(q) + 1] == 0xBB && (unsigned char)p[(q) + 2] == 0xBF) ? 3 : 0)
    for (size_t i = 0; i < n && !found; i++) {
        if (!_strnicmp(p + i, "full", 4)) {
            size_t q = i + 4, k;
            while ((k = WS_AT(q))) q += k;
            if (q < n && p[q] == '-') { q++; while ((k = WS_AT(q))) q += k; }
            if (!_strnicmp(p + q, "screen", 6)) found = 1;
        }
        if (!_strnicmp(p + i, "maximize", 8) || !_strnicmp(p + i, "maximise", 8)) found = 1;
        if (!strncmp(p + i, "\xE5\x85\xA8\xE7\x94\xBB\xE9\x9D\xA2", 9) || !strncmp(p + i, "\xE3\x83\x95\xE3\x83\xAB\xE3\x82\xB9\xE3\x82\xAF\xE3\x83\xAA\xE3\x83\xBC\xE3\x83\xB3", 21)) found = 1;
        if (!strncmp(p + i, "\xEC\xA0\x84\xEC\xB2\xB4", 6)) {   // 전체 + \s* + 화면
            size_t q = i + 6, k;
            while ((k = WS_AT(q))) q += k;
            if (q + 6 <= n && !strncmp(p + q, "\xED\x99\x94\xEB\xA9\xB4", 6)) found = 1;
        }
    }
    #undef WS_AT
    sb_free(&s);
    return found;
}

static int fe_active(const char *key);
static void fe_mark(int x, int y, int w, int h);
static int text_h(HFONT f, const char *s, int w) {
    wchar_t *ws = utf8_to_w(s, strlen(s));
    if (!ws) return D(20);
    HFONT o = (HFONT)SelectObject(CV.dc, f);
    RECT r = { 0, 0, w, 10000 };
    DrawTextW(CV.dc, ws, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(CV.dc, o);
    free(ws);
    return r.bottom;
}
static int kb_btn(int id, int x, int y, int h, const char *label, uint32_t fg, int disabled) {
    int w = cv_text_w(&CV, f_small, label) + D(22);
    uint32_t bg = disabled ? C_FIELD : hover == id ? C_FIELD_HI : C_FIELD;
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(7), bg, 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(7), S > 1 ? S : 1, hover == id && !disabled ? 0x3a3b42 : C_BORDER);
    cv_text(&CV, f_small, label, x, y, w, h, disabled ? 0x5b5f6b : fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!disabled) add_hit(id, x, y, w, h);
    return w;
}

static int page_keys(int x, int y, int w) {
    if (w > D(900)) w = D(900);
    int yy = y;
    section_title(x, yy, w, T(S_kbTitle));
    yy += D(28);
    int ih = text_h(f_small, T(S_kbIntro), w);
    cv_text(&CV, f_small, T(S_kbIntro), x, yy, w, ih, C_DIM, DT_LEFT | DT_WORDBREAK);
    yy += ih + D(14);
    if (!app_kb_ext()) {
        int eh = text_h(f_small, T(S_kbErrNoExt), w - D(28)) + D(20);
        cv_round(&CV, (float)x, (float)yy, (float)w, (float)eh, (float)D(8), C_AMBER_SOFT, 1);
        cv_text(&CV, f_small, T(S_kbErrNoExt), x + D(14), yy + D(10), w - D(28), eh - D(20), C_AMBER, DT_LEFT | DT_WORDBREAK);
        yy += eh + D(14);
    }
    jval *all = kb_all(), *on = kb_on();
    int rh = D(48), bh = D(30);
    for (int s = 0; s < 6; s++) {
        const char *site = SITE_IDS[s];
        const jval *list = json_get(all, site);
        size_t cnt = list ? list->count : 0;
        int son = !json_get(on, site);
        //  줄마다 글줄 수 (메시지가 있으면 아래 한 줄 더)
        int ch = D(56) + D(56);
        for (size_t r = 0; r < cnt; r++) {
            char key[56]; int cls;
            snprintf(key, sizeof key, "%s:%s", site, json_get_str(list->items[r], "id", ""));
            int rep = kb_pick.on && !strcmp(kb_pick.site, site) && !strcmp(kb_pick.id, json_get_str(list->items[r], "id", ""));
            ch += rh + ((rep || kb_msg(key, &cls)) ? D(20) : 0);
        }
        card(x, yy, w, ch);
        uint32_t tc = son ? C_TEXT : C_DIM;
        //  머리: 사이트 이름 (+ 개수) · 켜고 끄기
        cv_text(&CV, f_bold, kb_site_name(s), x + D(20), yy, w - D(120), D(56), tc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (cnt) {
            char n[16], c[48];
            snprintf(n, sizeof n, "%u", (unsigned)cnt);
            fill(c, sizeof c, T(S_kbCount), "{n}", n);
            int nw = cv_text_w(&CV, f_bold, kb_site_name(s));
            cv_text(&CV, f_small, c, x + D(20) + nw + D(10), yy, D(80), D(56), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        w_switch(KB_ID(s, 0, 900), x + w - D(20) - D(46), yy + D(15), son);
        int ry = yy + D(56);
        for (size_t r = 0; r < cnt; r++) {
            const jval *b = list->items[r];
            const char *id = json_get_str(b, "id", "");
            char key[56];
            snprintf(key, sizeof key, "%s:%s", site, id);
            int rx = x + D(20), cy = ry + (rh - bh) / 2;
            cv_fill(&CV, x + D(20), ry, w - D(40), S > 1 ? (int)S : 1, C_DIV);
            //  이름 칸
            int nw = w / 3 > D(240) ? D(240) : w / 3;
            int nid = KB_ID(s, (int)r, 0);
            cv_round(&CV, (float)rx, (float)cy, (float)nw, (float)bh, (float)D(7), hover == nid ? C_FIELD_HI : C_FIELD, 1);
            cv_round_border(&CV, (float)rx, (float)cy, (float)nw, (float)bh, (float)D(7), S > 1 ? S : 1, C_BORDER);
            const char *lab = json_get_str(b, "label", "");
            char ekey[64];
            snprintf(ekey, sizeof ekey, "kbn:%s:%s", site, id);
            int editing = fe_active(ekey);
            if (!editing) cv_text(&CV, f_small, *lab ? lab : T(S_kbNamePh), rx + D(10), cy, nw - D(20), bh, *lab ? tc : 0x5b5f6b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (editing) fe_mark(rx, cy, nw, bh);
            add_hit(nid, rx, cy, nw, bh);
            rx += nw + D(8);
            //  키
            int waiting = kb_cap.on && !strcmp(kb_cap.site, site) && !strcmp(kb_cap.id, id);
            char kn[64];
            kb_key_name(kn, sizeof kn, b);
            const char *kl = waiting ? T(S_kbPressKey) : *kn ? kn : T(S_kbNoKey);
            int kid = KB_ID(s, (int)r, 1);
            int kw = cv_text_w(&CV, f_small, kl) + D(24);
            if (kw < D(96)) kw = D(96);
            cv_round(&CV, (float)rx, (float)cy, (float)kw, (float)bh, (float)D(7), waiting ? C_ACC_SOFT : hover == kid ? C_FIELD_HI : C_FIELD, 1);
            cv_round_border(&CV, (float)rx, (float)cy, (float)kw, (float)bh, (float)D(7), S > 1 ? S : 1, waiting ? C_ACC : C_BORDER);
            cv_text(&CV, f_small, kl, rx, cy, kw, bh, waiting ? C_ACCHI : *kn ? tc : C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            add_hit(kid, rx, cy, kw, bh);
            rx += kw + D(8);
            //  전체화면 표시
            if (kb_looks_fs(b)) {
                int tw = cv_text_w(&CV, f_small, T(S_kbFsTag)) + D(16);
                cv_round(&CV, (float)rx, (float)(cy + D(4)), (float)tw, (float)(bh - D(8)), (float)D(11), C_THUMB, 1);
                cv_text(&CV, f_small, T(S_kbFsTag), rx, cy + D(4), tw, bh - D(8), C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                rx += tw + D(8);
            }
            //  다시 고르기 / 취소 · 삭제 (오른쪽 끝)
            int rep = kb_pick.on && !strcmp(kb_pick.site, site) && !strcmp(kb_pick.id, id);
            int armed = kb_armed.key[0] && !strcmp(kb_armed.key, key) && GetTickCount() - kb_armed.at < 3000;
            const char *dl = armed ? T(S_kbRemoveConfirm) : T(S_kbRemove);
            int dw = cv_text_w(&CV, f_small, dl) + D(22);
            const char *pl = rep ? T(S_kbCancel) : T(S_kbRepick);
            int pw = cv_text_w(&CV, f_small, pl) + D(22);
            int ex = x + w - D(20);
            kb_btn(KB_ID(s, (int)r, 3), ex - dw, cy, bh, dl, armed ? 0xffffff : C_RED, 0);
            if (armed) { cv_round(&CV, (float)(ex - dw), (float)cy, (float)dw, (float)bh, (float)D(7), C_RED, 1); cv_text(&CV, f_small, dl, ex - dw, cy, dw, bh, 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
            kb_btn(KB_ID(s, (int)r, 2), ex - dw - D(8) - pw, cy, bh, pl, C_TEXT2, !rep && kb_pick.on);
            (void)rx;
            //  상태 글 (그 줄 아래)
            int cls = 0;
            const char *m = rep ? NULL : kb_msg(key, &cls);
            char pk[300];
            if (rep) { fill(pk, sizeof pk, T(S_kbPicking), "{site}", kb_site_name(s)); m = pk; cls = 1; }
            if (m) {
                cv_text(&CV, f_small, m, x + D(20), ry + rh - D(4), w - D(40), D(20), cls == 2 ? C_RED : cls == 1 ? C_GREEN : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                ry += D(20);
            }
            ry += rh;
        }
        //  아래: 버튼 고르기 (고르는 중이면 취소 + 안내)
        cv_fill(&CV, x + D(20), ry, w - D(40), S > 1 ? (int)S : 1, C_DIV);
        int fy = ry + (D(56) - bh) / 2;
        int picking = kb_pick.on && !strcmp(kb_pick.site, site) && !kb_pick.id[0];
        int bw2 = picking ? kb_btn(KB_ID(s, 0, 901), x + D(20), fy, bh, T(S_kbCancel), C_TEXT2, 0)
                          : kb_btn(KB_ID(s, 0, 901), x + D(20), fy, bh, T(S_kbPick), C_ACCHI, kb_pick.on || cnt >= KB_MAX_PER_SITE);
        int cls = 0;
        const char *m = kb_msg(site, &cls);
        char pk[300];
        if (picking) { fill(pk, sizeof pk, T(S_kbPicking), "{site}", kb_site_name(s)); m = pk; cls = 1; }
        else if (!m && !cnt) m = T(S_kbEmpty);
        if (m) cv_text(&CV, f_small, m, x + D(20) + bw2 + D(12), fy, w - D(52) - bw2, bh, cls == 2 ? C_RED : cls == 1 ? C_GREEN : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        yy += ch + D(14);
    }
    json_free(all);
    json_free(on);
    return yy - y + D(6);
}

//  ── 입력 칸 (키 설정 이름·웹훅 탭의 여러 칸 공통) ──
//  그릴 땐 직접 그리고, 누르면 그 자리에 진짜 입력 칸(EDIT)을 띄운다. Enter·다른 곳 누르기 = 끝내기, Esc = 그만
//  끝나면 fn(key, 글, enter, changed) — changed: 앞뒤 빈칸을 뗀 값이 처음과 다름 (지금 앱 commit 과 같은 규칙)
typedef void (*fe_fn)(const char *key, const char *text, int enter, int changed);
typedef void (*fe_live_fn)(const char *key, const char *text);   //  글이 바뀔 때마다 (input 이벤트)
static struct { HWND h; WNDPROC old; char key[160]; char *initial; int cancel, enter; fe_fn fn; fe_live_fn live; } FE;
static RECT FE_RECT;
static HBRUSH FE_BRUSH;
static int fe_active(const char *key) { return FE.h && !strcmp(FE.key, key); }
static void fe_mark(int x, int y, int w, int h) { FE_RECT.left = x; FE_RECT.top = y; FE_RECT.right = x + w; FE_RECT.bottom = y + h; }

static LRESULT CALLBACK fe_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && wp == VK_RETURN) { FE.enter = 1; SetFocus(GetParent(h)); return 0; }
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) { FE.cancel = 1; SetFocus(GetParent(h)); return 0; }
    if (m == WM_CHAR && (wp == '\r' || wp == 27)) return 0;
    if (m == WM_KILLFOCUS) PostMessageW(GetParent(h), WM_KBEDIT, 0, 0);
    return CallWindowProcW(FE.old, h, m, wp, lp);
}
static void fe_finish(void) {
    if (!FE.h) return;
    HWND e = FE.h;
    FE.h = NULL;
    int len = GetWindowTextLengthW(e);
    wchar_t *buf = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
    if (buf) GetWindowTextW(e, buf, len + 1);
    DestroyWindow(e);
    char key[160];
    snprintf(key, sizeof key, "%s", FE.key);
    fe_fn fn = FE.fn;
    int enter = FE.enter, cancel = FE.cancel;
    char *initial = FE.initial;
    FE.initial = NULL;
    FE.cancel = FE.enter = 0;
    if (!cancel && buf && fn) {
        char *u = w_to_utf8(buf, wcslen(buf));
        if (u) {
            size_t a, b, c0, c1;
            js_trim(u, strlen(u), &a, &b);
            const char *in = initial ? initial : "";
            js_trim(in, strlen(in), &c0, &c1);
            int changed = (b - a) != (c1 - c0) || memcmp(u + a, in + c0, b - a);
            fn(key, u, enter, changed);
            free(u);
        }
    }
    free(initial);
    free(buf);
    if (W) InvalidateRect(W, NULL, FALSE);
}
static void fe_start(const char *key, const char *text, int maxlen, int password, fe_fn fn) {
    fe_finish();
    snprintf(FE.key, sizeof FE.key, "%s", key);
    FE.initial = _strdup(text ? text : "");
    FE.fn = fn;
    FE.live = NULL;
    FE.cancel = FE.enter = 0;
    //  자리를 알려면 한 번 그려야 한다 — 먼저 만들고 그 자리로 옮긴다
    FE.h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | (password ? ES_PASSWORD : 0), 0, 0, 10, 10, W, NULL, INST, NULL);
    if (!FE.h) return;
    FE.old = (WNDPROC)SetWindowLongPtrW(FE.h, GWLP_WNDPROC, (LONG_PTR)fe_proc);
    SendMessageW(FE.h, WM_SETFONT, (WPARAM)f_small, 0);
    if (maxlen > 0) SendMessageW(FE.h, EM_LIMITTEXT, (WPARAM)maxlen, 0);   //  maxLength (UTF-16)
    wchar_t *wl = utf8_to_w(text ? text : "", strlen(text ? text : ""));
    if (wl) { SetWindowTextW(FE.h, wl); free(wl); }
    FE_RECT.right = 0;
    RedrawWindow(W, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    if (FE_RECT.right > FE_RECT.left) {
        int pad = D(9), eh = D(18);
        MoveWindow(FE.h, FE_RECT.left + pad, FE_RECT.top + (FE_RECT.bottom - FE_RECT.top - eh) / 2, FE_RECT.right - FE_RECT.left - 2 * pad, eh, TRUE);
        ShowWindow(FE.h, SW_SHOW);
        SetFocus(FE.h);
        SendMessageW(FE.h, EM_SETSEL, 0, -1);
    } else { DestroyWindow(FE.h); FE.h = NULL; free(FE.initial); FE.initial = NULL; }
}
static void fe_start_live(const char *key, const char *text, int maxlen, fe_fn fn, fe_live_fn live) {
    fe_start(key, text, maxlen, 0, fn);
    if (FE.h) FE.live = live;   //  처음 넣은 글로는 부르지 않는다
}
//  칸 그리기 — 지금 쓰는 칸이면 자리만 적어 둔다 (진짜 입력 칸이 그 위에 있다)
static void w_field(int id, const char *key, int x, int y, int w, int h, const char *text, const char *ph, int password, uint32_t fg) {
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(7), hover == id ? C_FIELD_HI : C_FIELD, 1);
    cv_round_border(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(7), S > 1 ? S : 1, fe_active(key) ? C_ACC : C_BORDER);
    if (fe_active(key)) fe_mark(x, y, w, h);
    else if (text && *text) {
        if (password) {   //  주소는 가린다 (지금 앱 type=password)
            char dots[200] = "";
            size_t n = utf16_len(text, strlen(text));
            for (size_t i = 0; i < n && i < 60; i++) strcat(dots, "\xE2\x97\x8F");
            cv_text(&CV, f_small, dots, x + D(10), y, w - D(20), h, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else cv_text(&CV, f_small, text, x + D(10), y, w - D(20), h, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (ph) cv_text(&CV, f_small, ph, x + D(10), y, w - D(20), h, 0x5b5f6b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    add_hit(id, x, y, w, h);
}

//  키 설정 이름 칸 (kbRow commit)
static void kb_name_done(const char *key, const char *text, int enter, int changed) {
    (void)enter;
    if (!changed) return;
    char site[16] = "", id[32] = "";
    const char *p = key + 4, *c = strchr(p, ':');
    if (!c) return;
    snprintf(site, sizeof site, "%.*s", (int)(c - p), p);
    snprintf(id, sizeof id, "%s", c + 1);
    jval lab; memset(&lab, 0, sizeof lab);
    lab.t = J_STR; lab.s = (char *)text; lab.slen = strlen(text);
    kb_res r = app_kb_edit(1, site, id, &lab, 0);
    char mk[56];
    snprintf(mk, sizeof mk, "%s:%s", site, id);
    kb_say(mk, r.ok ? T(S_kbRenamed) : kb_err_text(r.error), r.ok ? 1 : 2);
}
static void kb_edit_start(const char *site, const char *id, const char *label) {
    char key[64];
    snprintf(key, sizeof key, "kbn:%s:%s", site, id);
    fe_start(key, label, 30, 0, kb_name_done);
}

//  키 받기 (kbStartKey) — 한 번 더 누르면 그만
static void kb_start_key(const char *site, const char *id, int keep_msg) {
    if (kb_cap.on && !strcmp(kb_cap.site, site) && !strcmp(kb_cap.id, id)) { kb_cap.on = 0; return; }
    kb_cap.on = 1;
    snprintf(kb_cap.site, sizeof kb_cap.site, "%s", site);
    snprintf(kb_cap.id, sizeof kb_cap.id, "%s", id);
    char key[56];
    snprintf(key, sizeof key, "%s:%s", site, id);
    if (!keep_msg) kb_say(key, T(S_kbKeyNow), 1);
    SetFocus(W);
}

//  Windows 키 → KeyboardEvent.code (물리 위치 기준, Chromium 표와 같은 이름)
static const char *vk_code(UINT vk, LPARAM lp) {
    UINT sc = (lp >> 16) & 0xFF;
    int ext = (lp >> 24) & 1;
    switch (vk) {   //  스캔 코드로 갈리지 않는 것 먼저
    case VK_PAUSE: return "Pause";
    case VK_NUMLOCK: return "NumLock";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_LWIN: return "MetaLeft";
    case VK_RWIN: return "MetaRight";
    case VK_APPS: return "ContextMenu";
    case VK_MEDIA_PLAY_PAUSE: return "MediaPlayPause";
    case VK_MEDIA_NEXT_TRACK: return "MediaTrackNext";
    case VK_MEDIA_PREV_TRACK: return "MediaTrackPrevious";
    case VK_MEDIA_STOP: return "MediaStop";
    case VK_VOLUME_MUTE: return "AudioVolumeMute";
    case VK_VOLUME_UP: return "AudioVolumeUp";
    case VK_VOLUME_DOWN: return "AudioVolumeDown";
    }
    if (ext) {
        switch (sc) {
        case 0x1C: return "NumpadEnter"; case 0x1D: return "ControlRight"; case 0x35: return "NumpadDivide"; case 0x38: return "AltRight";
        case 0x47: return "Home"; case 0x48: return "ArrowUp"; case 0x49: return "PageUp"; case 0x4B: return "ArrowLeft"; case 0x4D: return "ArrowRight";
        case 0x4F: return "End"; case 0x50: return "ArrowDown"; case 0x51: return "PageDown"; case 0x52: return "Insert"; case 0x53: return "Delete";
        }
        return NULL;
    }
    static const char *const T1[0x59] = {
        NULL, "Escape", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal", "Backspace", "Tab",
        "KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO", "KeyP", "BracketLeft", "BracketRight", "Enter", "ControlLeft", "KeyA", "KeyS",
        "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL", "Semicolon", "Quote", "Backquote", "ShiftLeft", "Backslash", "KeyZ", "KeyX", "KeyC", "KeyV",
        "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash", "ShiftRight", "NumpadMultiply", "AltLeft", "Space", "CapsLock", "F1", "F2", "F3", "F4", "F5",
        "F6", "F7", "F8", "F9", "F10", "Pause", "ScrollLock", "Numpad7", "Numpad8", "Numpad9", "NumpadSubtract", "Numpad4", "Numpad5", "Numpad6", "NumpadAdd", "Numpad1",
        "Numpad2", "Numpad3", "Numpad0", "NumpadDecimal", NULL, NULL, "IntlBackslash", "F11", "F12" };
    if (sc < 0x59) return T1[sc];
    if (sc >= 0x64 && sc <= 0x6E) { static char f[8]; snprintf(f, sizeof f, "F%u", 13 + (sc - 0x64)); return f; }
    if (sc == 0x76) return "F24";
    if (sc == 0x70) return "KanaMode";
    if (sc == 0x73) return "IntlRo";
    if (sc == 0x79) return "Convert";
    if (sc == 0x7B) return "NonConvert";
    if (sc == 0x7D) return "IntlYen";
    return NULL;
}

//  키 받는 중 누른 키 — 1 이면 여기서 처리함
static int kb_key(UINT vk, LPARAM lp) {
    if (kb_pick.on && tab == 3 && vk == VK_ESCAPE && !kb_cap.on) { app_kb_cancel(); return 1; }
    if (!kb_cap.on) return 0;
    char key[56];
    snprintf(key, sizeof key, "%s:%s", kb_cap.site, kb_cap.id);
    if (vk == VK_ESCAPE) { kb_cap.on = 0; kb_say(key, T(S_kbCanceled), 0); InvalidateRect(W, NULL, FALSE); return 1; }
    if (vk == VK_PROCESSKEY) return 1;   // 한글 입력기가 잡은 키 — 기다린다 (isComposing)
    const char *code = vk_code(vk, lp);
    if (!code) return 1;
    if (!strncmp(code, "Shift", 5) || !strncmp(code, "Control", 7) || !strncmp(code, "Alt", 3) || !strncmp(code, "Meta", 4)) return 1;   // 보조 키만 누른 동안은 기다린다
    jval *c = json_new(J_OBJ);
    json_obj_set(c, "code", json_new_str(code));
    json_obj_set(c, "ctrl", json_new_bool(GetKeyState(VK_CONTROL) < 0));
    json_obj_set(c, "alt", json_new_bool(GetKeyState(VK_MENU) < 0));
    json_obj_set(c, "shift", json_new_bool(GetKeyState(VK_SHIFT) < 0));
    json_obj_set(c, "meta", json_new_bool(GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0));
    kb_cap.on = 0;
    kb_res r = app_kb_edit(0, kb_cap.site, kb_cap.id, c, 0);
    if (r.ok) {
        char kn[64], t1[300], t2[300];
        kb_key_name(kn, sizeof kn, c);
        if (r.has_moved) { fill(t1, sizeof t1, T(S_kbMoved), "{key}", kn); fill(t2, sizeof t2, t1, "{from}", *r.moved ? r.moved : T(S_kbUnnamed)); }
        else fill(t2, sizeof t2, T(S_kbSaved), "{key}", kn);
        kb_say(key, t2, 1);
    } else kb_say(key, kb_err_text(r.error), 2);
    json_free(c);
    InvalidateRect(W, NULL, FALSE);
    return 1;
}

//  버튼 고르기 결과 (main.c 가 부름)
void win_kb_pick_result(const char *site, const char *id, int ok, const char *err) {
    int was_new = kb_pick.on && !kb_pick.id[0];
    char at[56];
    if (kb_pick.on && kb_pick.id[0]) snprintf(at, sizeof at, "%s:%s", site, kb_pick.id); else snprintf(at, sizeof at, "%s", site);
    kb_pick.on = 0;
    if (ok) {
        char key[56];
        snprintf(key, sizeof key, "%s:%s", site, id ? id : "");
        kb_say(key, was_new ? T(S_kbPicked) : T(S_kbRepicked), 1);
        if (was_new && id && *id) kb_start_key(site, id, 1);   // 새 줄은 바로 키를 받는다
    } else if (err && !strcmp(err, "canceled")) kb_say(at, T(S_kbCanceled), 0);
    else {
        char t1[300];
        int s = kb_site_index(site);
        fill(t1, sizeof t1, kb_err_text(err), "{site}", s >= 0 ? kb_site_name(s) : site);
        kb_say(at, t1, 2);
    }
    if (W) InvalidateRect(W, NULL, FALSE);
}

static void kb_click(int id) {
    int s = (id - ID_KB0) / 1000, rest = (id - ID_KB0) % 1000;
    if (s < 0 || s > 5) return;
    const char *site = SITE_IDS[s];
    if (rest == 900) {   // 켜고 끄기
        jval *on = kb_on();
        int now_on = !json_get(on, site);
        json_free(on);
        kb_res r = app_kb_edit(3, site, "", NULL, !now_on);
        kb_say(site, r.ok ? (!now_on ? T(S_kbOnMsg) : T(S_kbOffTip)) : kb_err_text(r.error), r.ok ? 1 : 2);
        return;
    }
    if (rest == 901) {   // 버튼 고르기 / 취소
        if (kb_pick.on) { if (!strcmp(kb_pick.site, site) && !kb_pick.id[0]) app_kb_cancel(); return; }
        kb_cap.on = 0;
        kb_unsay(site);
        kb_pick.on = 1;
        snprintf(kb_pick.site, sizeof kb_pick.site, "%s", site);
        kb_pick.id[0] = 0;
        const char *e = app_kb_pick(site, "");
        if (e) win_kb_pick_result(site, "", 0, e);
        return;
    }
    int r = rest / 10, part = rest % 10;
    jval *all = kb_all();
    const jval *list = json_get(all, site);
    const jval *b = (list && (size_t)r < list->count) ? list->items[r] : NULL;
    char bid[32] = "", lab[128] = "";
    if (b) { snprintf(bid, sizeof bid, "%s", json_get_str(b, "id", "")); snprintf(lab, sizeof lab, "%s", json_get_str(b, "label", "")); }
    json_free(all);
    if (!b) return;
    char key[56];
    snprintf(key, sizeof key, "%s:%s", site, bid);
    if (part == 0) kb_edit_start(site, bid, lab);
    else if (part == 1) kb_start_key(site, bid, 0);
    else if (part == 2) {
        if (kb_pick.on) { if (!strcmp(kb_pick.site, site) && !strcmp(kb_pick.id, bid)) app_kb_cancel(); return; }
        kb_cap.on = 0;
        kb_unsay(site);
        kb_unsay(key);
        kb_pick.on = 1;
        snprintf(kb_pick.site, sizeof kb_pick.site, "%s", site);
        snprintf(kb_pick.id, sizeof kb_pick.id, "%s", bid);
        const char *e = app_kb_pick(site, bid);
        if (e) win_kb_pick_result(site, bid, 0, e);
    } else if (part == 3) {
        if (!(kb_armed.key[0] && !strcmp(kb_armed.key, key) && GetTickCount() - kb_armed.at < 3000)) {
            snprintf(kb_armed.key, sizeof kb_armed.key, "%s", key);
            kb_armed.at = GetTickCount();
            SetTimer(W, 3, 3100, NULL);   // 3초 뒤 '삭제' 로 되돌려 그린다
            return;
        }
        kb_armed.key[0] = 0;
        kb_unsay(key);
        kb_res rr = app_kb_edit(2, site, bid, NULL, 0);
        kb_say(site, rr.ok ? T(S_kbRemoved) : kb_err_text(rr.error), rr.ok ? 1 : 2);
    }
}

// ── 탭: 웹훅 (rpc-window.js renderHooks · renderHookChars · 프로필 사진 조절 창) ──
//  클릭할 곳은 그릴 때마다 표(HKA)에 적고 id = ID_HK0 + 표 번호
#define ID_HK0 50000
enum { HA_F_TITLE = 1, HA_F_URL, HA_F_NAME, HA_ADD, HA_FOLDER, HA_ICON, HA_ICONCLR, HA_ADDHERE, HA_H_NAME, HA_H_TEST, HA_H_DEL,
       HA_C_SW, HA_C_FACE, HA_C_NAME, HA_C_URL, HA_C_PICK, HA_C_TEST, HA_C_DEL, HA_CA_NAME, HA_CA_ADD,
       HA_AV_STAGE, HA_AV_ZOOM, HA_AV_RESET, HA_AV_OTHER, HA_AV_APPLY, HA_AV_CANCEL, HA_AV_BG };
typedef struct { int kind; char *title; char id[32]; char *text; int n; } hk_act;
static hk_act *HKA;
static int nHKA, capHKA;
static void hka_reset(void) { for (int i = 0; i < nHKA; i++) { free(HKA[i].title); free(HKA[i].text); } nHKA = 0; }
static int hka(int kind, const char *title, const char *id, const char *text, int n) {
    if (nHKA >= capHKA) { int nc = capHKA ? capHKA * 2 : 128; hk_act *na = (hk_act *)realloc(HKA, sizeof *na * (size_t)nc); if (!na) return ID_NONE; HKA = na; capHKA = nc; }
    hk_act *a = &HKA[nHKA];
    a->kind = kind;
    a->title = _strdup(title ? title : "");
    snprintf(a->id, sizeof a->id, "%s", id ? id : "");
    a->text = _strdup(text ? text : "");
    a->n = n;
    return ID_HK0 + nHKA++;
}

static char *HD_TITLE, *HD_URL, *HD_NAME;                    // 추가 칸에 쓰던 글 (저장 전)
static struct { char *title, *text; } HD_CADD[64];           // 폴더별 '캐릭터 추가' 칸
static int nHD_CADD;
static char *HK_OPEN[64];                                     // 펼친 폴더 (작품명)
static int nHK_OPEN;
static struct { char key[340]; char text[300]; int cls; } HK_MSG[64];
static char HK_GMSG[300];
static int HK_GCLS;
static struct { char key[340]; DWORD at; } HK_ARMED;
static struct { int tag; char key[340]; } HK_TESTS[16];
static int HK_TAG;
static char HK_ED_TITLE[340], HK_ED_ID[32];                   // 지금 쓰는 칸이 가리키는 폴더·항목

static void set_str(char **p, const char *v) { free(*p); *p = _strdup(v ? v : ""); }
static const char *str0(const char *s) { return s ? s : ""; }
static int hk_is_open(const char *t) { for (int i = 0; i < nHK_OPEN; i++) if (!strcmp(HK_OPEN[i], t)) return 1; return 0; }
static void hk_set_open(const char *t, int on) {
    for (int i = 0; i < nHK_OPEN; i++) if (!strcmp(HK_OPEN[i], t)) { if (!on) { free(HK_OPEN[i]); HK_OPEN[i] = HK_OPEN[--nHK_OPEN]; } return; }
    if (on && nHK_OPEN < 64) HK_OPEN[nHK_OPEN++] = _strdup(t);
}
static char **hd_cadd(const char *title) {
    for (int i = 0; i < nHD_CADD; i++) if (!strcmp(HD_CADD[i].title, title)) return &HD_CADD[i].text;
    if (nHD_CADD >= 64) return &HD_CADD[63].text;
    HD_CADD[nHD_CADD].title = _strdup(title);
    HD_CADD[nHD_CADD].text = _strdup("");
    return &HD_CADD[nHD_CADD++].text;
}
static void hk_say(const char *key, const char *text, int cls) {
    int slot = -1;
    for (int i = 0; i < 64; i++) if (!strcmp(HK_MSG[i].key, key)) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < 64; i++) if (!HK_MSG[i].key[0]) { slot = i; break; }
    if (slot < 0) slot = 0;
    snprintf(HK_MSG[slot].key, sizeof HK_MSG[slot].key, "%s", key);
    snprintf(HK_MSG[slot].text, sizeof HK_MSG[slot].text, "%s", text);
    HK_MSG[slot].cls = cls;
}
static const char *hk_msg(const char *key, int *cls) {
    for (int i = 0; i < 64; i++) if (HK_MSG[i].key[0] && !strcmp(HK_MSG[i].key, key)) { *cls = HK_MSG[i].cls; return HK_MSG[i].text; }
    return NULL;
}
static void hk_unsay(const char *key) { for (int i = 0; i < 64; i++) if (!strcmp(HK_MSG[i].key, key)) HK_MSG[i].key[0] = 0; }
static void hk_gsay(const char *text, int cls) { snprintf(HK_GMSG, sizeof HK_GMSG, "%s", text ? text : ""); HK_GCLS = cls; }
static int hk_armed(const char *key) { return HK_ARMED.key[0] && !strcmp(HK_ARMED.key, key) && GetTickCount() - HK_ARMED.at < 4000; }
//  HOOK_ERR
static const char *hk_err(const char *e, int dflt) {
    static const struct { const char *e; int s; } M[] = { { "title", S_hookErrTitle }, { "url", S_hookErrUrl }, { "dup", S_hookErrDup }, { "full", S_hookErrFull },
        { "badfile", S_hookErrFile }, { "missing", S_hookErrMissing }, { "icon", S_hookErrIcon }, { "charname", S_hookErrCharName }, { "charbad", S_hookErrCharBad },
        { "charfull", S_hookErrCharFull }, { "avatarurl", S_hookErrAvatarUrl }, { "upload", S_hookErrUpload }, { "avatarload", S_hookErrAvatarLoad }, { "stale", S_hookErrAvatarStale } };
    for (size_t i = 0; e && i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i].e, e)) return T(M[i].s);
    return T(dflt);
}
static uint32_t cls_color(int cls) { return cls == 2 ? C_RED : cls == 1 ? C_GREEN : C_DIM; }
static void first_letter(char *out, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) { strcpy(out, "?"); return; }
    size_t k = p[0] < 0x80 ? 1 : p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : 2;
    memcpy(out, s, k);
    out[k] = 0;
}

// ── 프로필 사진 조절 창 (avatarEd) ──
static struct {
    int open, busy, drag, slider;
    char title[340], id[32], name[200], mime[24];
    unsigned char *buf; size_t n;          // 고른 원본 바이트 (적용하면 보관)
    uint32_t *px; int pw, ph;              // 화면용으로 줄인 그림
    int w, h;                              // 원본 크기
    double zoom, ox, oy, D;                // ox·oy: 틀 왼쪽 위 기준 그림 위치 (화면 px)
    int sx, sy; double sox, soy;           // 끌기 시작
    char msg[300]; int mcls;
    int tag;
    RECT stage, zoom_r;
} AV;
#define AV_ZOOM_MAX 5.0
static double av_scale(void) { return AV.D / (AV.w < AV.h ? AV.w : AV.h) * AV.zoom; }
static void av_clamp(void) {
    double k = av_scale();
    AV.ox = fmin(0, fmax(AV.D - AV.w * k, AV.ox));
    AV.oy = fmin(0, fmax(AV.D - AV.h * k, AV.oy));
}
static void av_center(void) {
    AV.zoom = 1;
    double k = av_scale();
    AV.ox = (AV.D - AV.w * k) / 2;
    AV.oy = (AV.D - AV.h * k) / 2;
}
static void av_zoom_at(double z, double px, double py) {
    double before = av_scale();
    AV.zoom = fmin(AV_ZOOM_MAX, fmax(1, z));
    double after = av_scale();
    AV.ox = px - (px - AV.ox) * (after / before);
    AV.oy = py - (py - AV.oy) * (after / before);
    av_clamp();
}
static void av_free_img(void) { free(AV.buf); AV.buf = NULL; AV.n = 0; free(AV.px); AV.px = NULL; }
static void av_close(void) { av_free_img(); AV.open = 0; AV.busy = 0; AV.drag = AV.slider = 0; if (W) InvalidateRect(W, NULL, FALSE); }
//  그림 싣기 — 못 풀면 0 (지금 그림은 그대로)
static int av_load(unsigned char *buf, size_t n, const char *mime) {
    int w, h, ow, oh;
    uint32_t *px = iu_decode(buf, n, 1600, &w, &h, &ow, &oh);
    if (!px) { free(buf); return 0; }
    av_free_img();
    AV.buf = buf; AV.n = n; snprintf(AV.mime, sizeof AV.mime, "%s", mime);
    AV.px = px; AV.pw = w; AV.ph = h; AV.w = ow; AV.h = oh;
    AV.D = D(252);
    av_center();
    return 1;
}
//  PC 에서 그림 파일 고르기 (pickImageBytes) → 1 = 고름, 0 = 취소, -1 = 쓸 수 없는 파일
static int pick_image(int title_id, unsigned char **buf, size_t *n, const char **mime) {
    wchar_t file[MAX_PATH] = L"";
    wchar_t *title = utf8_to_w(T(title_id), strlen(T(title_id)));
    wchar_t *fname = utf8_to_w(T(S_imgDialogFilter), strlen(T(S_imgDialogFilter)));
    wchar_t filter[400];
    _snwprintf(filter, 398, L"%ls%lc*.png;*.jpg;*.jpeg;*.jfif;*.webp;*.gif;*.bmp;*.ico;*.avif%lc", fname ? fname : L"Images", 0, 0);
    filter[398] = filter[399] = 0;
    OPENFILENAMEW of;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = W;
    of.lpstrFilter = filter;
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    int r = GetOpenFileNameW(&of);
    free(title);
    free(fname);
    if (!r) return 0;
    int rr = iu_read_file(file, 20 * 1024 * 1024, buf, n);
    if (rr != 1) return -1;
    *mime = hk_sniff_mime(*buf, *n);
    if (!*mime) { free(*buf); *buf = NULL; return -1; }
    return 1;
}

//  '지금 사진' 을 받아 오는 스레드 (원본이 없고 주소만 있을 때)
#define WM_AV_FETCHED (WM_APP + 8)
typedef struct { int tag; char *url; unsigned char *buf; size_t n; int ok; } av_fetch;
static DWORD WINAPI av_fetch_thread(LPVOID p) {
    av_fetch *f = (av_fetch *)p;
    sbuf b; sb_init(&b, 0);
    int st = 0;
    char err[64];
    if (http_get(f->url, NULL, 15000, 8 * 1024 * 1024, &st, &b, err, sizeof err) && st >= 200 && st < 300 && b.len) {
        f->buf = (unsigned char *)malloc(b.len);
        if (f->buf) { memcpy(f->buf, b.p, b.len); f->n = b.len; f->ok = 1; }
    }
    sb_free(&b);
    if (!PostMessageW(W, WM_AV_FETCHED, 0, (LPARAM)f)) { free(f->buf); free(f->url); free(f); }
    return 0;
}

//  조절 창 열기 (avatarEditor) — mode 'file' 이면 파일 고르기, 'current' 면 보관한 원본 → 없으면 지금 사진 주소에서
static void av_open(const char *title, const char *id, const char *name, const char *avatar, int current) {
    if (AV.open || AV.busy) return;
    char key[64];
    snprintf(key, sizeof key, "c:%s", id);
    snprintf(AV.title, sizeof AV.title, "%s", title);
    snprintf(AV.id, sizeof AV.id, "%s", id);
    snprintf(AV.name, sizeof AV.name, "%s", name);
    AV.msg[0] = 0;
    if (current) {
        unsigned char *b; size_t n;
        if (hk_avatar_orig_read(id, &b, &n)) {
            const char *m = hk_sniff_mime(b, n);
            if (m && av_load(b, n, m)) { hk_unsay(key); AV.open = 1; return; }
            if (!m) free(b);
        }
        if (strncmp(avatar, "https://", 8)) { hk_say(key, T(S_hookErrAvatarLoad), 2); return; }
        hk_say(key, T(S_avatarLoading), 0);
        av_fetch *f = (av_fetch *)calloc(1, sizeof *f);
        if (!f) return;
        f->tag = ++AV.tag;
        f->url = _strdup(avatar);
        AV.busy = 1;
        HANDLE h = CreateThread(NULL, 0, av_fetch_thread, f, 0, NULL);
        if (h) CloseHandle(h); else { AV.busy = 0; free(f->url); free(f); }
        return;
    }
    unsigned char *b; size_t n; const char *m;
    int r = pick_image(S_hookCharDialog, &b, &n, &m);
    if (r == 0) { hk_unsay(key); return; }
    if (r < 0) { hk_say(key, T(S_hookErrIcon), 2); return; }
    if (!av_load(b, n, m)) { hk_say(key, T(S_hookErrIcon), 2); return; }
    hk_unsay(key);
    AV.open = 1;
}
static void av_fetched(av_fetch *f) {
    if (f->tag == AV.tag && AV.busy && !AV.open) {
        AV.busy = 0;
        char key[64];
        snprintf(key, sizeof key, "c:%s", AV.id);
        const char *m = f->ok ? hk_sniff_mime(f->buf, f->n) : NULL;
        if (!f->ok || !m) { app_log(1, "[Binge] 캐릭터 사진 불러오기 실패"); hk_say(key, T(S_hookErrAvatarLoad), 2); free(f->buf); }
        else if (!av_load(f->buf, f->n, m)) hk_say(key, T(S_hookErrAvatarLoad), 2);
        else { hk_unsay(key); AV.open = 1; }
        f->buf = NULL;
    }
    free(f->buf);
    free(f->url);
    free(f);
    InvalidateRect(W, NULL, FALSE);
}
static void av_apply(void) {
    if (!AV.open || AV.busy) return;
    if (!hk_char_get(AV.title, AV.id)) { snprintf(AV.msg, sizeof AV.msg, "%s", T(S_hookErrMissing)); AV.mcls = 2; return; }
    double k = av_scale();
    size_t n = 0;
    unsigned char *png = iu_square_png(AV.buf, AV.n, -AV.ox / k, -AV.oy / k, AV.D / k, 256, &n);
    if (!png) { snprintf(AV.msg, sizeof AV.msg, "%s", T(S_hookErrIcon)); AV.mcls = 2; return; }
    AV.busy = 1;
    snprintf(AV.msg, sizeof AV.msg, "%s", T(S_hookCharUploading));
    AV.mcls = 0;
    hk_upload_avatar(W, ++AV.tag, png, n);
}
static void av_uploaded(hk_upload_res *r) {
    if (r->tag != AV.tag || !AV.open) { hk_upload_res_free(r); return; }
    AV.busy = 0;
    if (!r->ok) {
        char m[300];
        snprintf(m, sizeof m, "[Binge] 캐릭터 사진 올리기 실패: %s", r->err);
        app_log(1, m);
        snprintf(AV.msg, sizeof AV.msg, "%s", T(S_hookErrUpload));
        AV.mcls = 2;
        hk_upload_res_free(r);
        return;
    }
    char m[200];
    snprintf(m, sizeof m, "[Binge] 캐릭터 사진 올림 (%s, 원본 %s)", r->host, AV.mime);
    app_log(0, m);
    const char *e = hk_char_set_avatar(AV.title, AV.id, r->url, r->until);
    char key[64];
    snprintf(key, sizeof key, "c:%s", AV.id);
    if (!e) { hk_avatar_orig_write(AV.id, AV.buf, AV.n, AV.mime); hk_say(key, T(S_hookCharUploaded), 1); av_close(); }
    else { snprintf(AV.msg, sizeof AV.msg, "%s", hk_err(e, S_hookErrUpload)); AV.mcls = 2; }
    hk_upload_res_free(r);
}

static void av_paint(int ww, int wh) {
    nhits = 0;   //  창 뒤의 것은 못 누르게
    cv_clip(&CV, 0, 0, 0, 0);
    cv_round(&CV, 0, 0, (float)ww, (float)wh, 0, 0x000000, 0.62f);
    add_hit(ID_HK0 - 1, 0, 0, ww, wh);   //  바탕 (아무 일도 안 함)
    int dw = ww - D(40) < D(640) ? ww - D(40) : D(640), dh = D(470);
    int x = (ww - dw) / 2, y = (wh - dh) / 2;
    if (y < D(10)) y = D(10);
    card(x, y, dw, dh);
    cv_text(&CV, f_bold, T(S_avatarTitle), x + D(22), y + D(16), dw - D(44), D(24), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_text(&CV, f_small, T(S_avatarHint), x + D(22), y + D(42), dw - D(44), D(36), C_DIM, DT_LEFT | DT_WORDBREAK);
    //  틀 (둥근 자리 바깥은 어둡게)
    int ring = D(14), sd = (int)AV.D + 2 * ring, stx = x + D(22), sty = y + D(84);
    AV.stage.left = stx; AV.stage.top = sty; AV.stage.right = stx + sd; AV.stage.bottom = sty + sd;
    cv_round(&CV, (float)stx, (float)sty, (float)sd, (float)sd, (float)D(8), 0x0b0b0d, 1);
    double k = av_scale(), f = (double)AV.pw / AV.w;   //  f: 원본 → 화면용 그림
    cv_blit(&CV, AV.px, AV.pw, AV.ph, (float)(stx + ring + AV.ox), (float)(sty + ring + AV.oy), (float)(k / f), stx, sty, sd, sd, 0);
    //  원 밖 어둡게: 칸 전체를 어둡게 칠한 뒤 원 안만 다시 그린다
    cv_round(&CV, (float)stx, (float)sty, (float)sd, (float)sd, 0, 0x000000, 0.55f);
    cv_blit(&CV, AV.px, AV.pw, AV.ph, (float)(stx + ring + AV.ox), (float)(sty + ring + AV.oy), (float)(k / f), stx + ring, sty + ring, (int)AV.D, (int)AV.D, 1);
    cv_round_border(&CV, (float)(stx + ring), (float)(sty + ring), (float)AV.D, (float)AV.D, (float)(AV.D / 2), S > 1 ? S : 1, 0xffffff);
    add_hit(hka(HA_AV_STAGE, NULL, NULL, NULL, 0), stx, sty, sd, sd);
    //  확대 막대
    int zy = sty + sd + D(14), zx = stx, zw = sd;
    cv_text(&CV, f_small, T(S_avatarZoom), zx, zy, D(60), D(20), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int tx0 = zx + D(56), tw = zw - D(56) - D(52);
    AV.zoom_r.left = tx0; AV.zoom_r.right = tx0 + tw; AV.zoom_r.top = zy; AV.zoom_r.bottom = zy + D(20);
    cv_round(&CV, (float)tx0, (float)(zy + D(8)), (float)tw, (float)D(4), (float)D(2), C_TRACK, 1);
    float kx = (float)(tx0 + tw * (AV.zoom - 1) / (AV_ZOOM_MAX - 1));
    cv_round(&CV, (float)tx0, (float)(zy + D(8)), kx - tx0, (float)D(4), (float)D(2), C_ACC, 1);
    cv_circle(&CV, kx, (float)(zy + D(10)), (float)D(7), 0xffffff);
    add_hit(hka(HA_AV_ZOOM, NULL, NULL, NULL, 0), tx0 - D(6), zy, tw + D(12), D(20));
    char zp[16];
    snprintf(zp, sizeof zp, "%d%%", (int)js_round(AV.zoom * 100));
    cv_text(&CV, f_small, zp, tx0 + tw + D(8), zy, D(44), D(20), C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int by = zy + D(30);
    int bw = kb_btn(hka(HA_AV_RESET, NULL, NULL, NULL, 0), zx, by, D(30), T(S_avatarReset), C_TEXT2, AV.busy);
    kb_btn(hka(HA_AV_OTHER, NULL, NULL, NULL, 0), zx + bw + D(8), by, D(30), T(S_avatarOther), C_TEXT2, AV.busy);
    //  미리보기 (Discord 메시지처럼)
    int px0 = stx + sd + D(24), pw = x + dw - D(22) - px0;
    cv_text(&CV, f_title, T(S_avatarPreview), px0, sty, pw, D(20), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    double rx = -AV.ox / k, ry = -AV.oy / k, rs = AV.D / k;   //  원본 픽셀 자리
    int big = D(96), sm = D(36);
    int bx = px0, byy = sty + D(28);
    double sc = big / (rs * f);
    cv_blit(&CV, AV.px, AV.pw, AV.ph, (float)(bx - rx * f * sc), (float)(byy - ry * f * sc), (float)sc, bx, byy, big, big, 1);
    cv_text(&CV, f_bold, AV.name, bx, byy + big + D(8), pw, D(22), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int my = byy + big + D(44);
    sc = sm / (rs * f);
    cv_blit(&CV, AV.px, AV.pw, AV.ph, (float)(px0 - rx * f * sc), (float)(my - ry * f * sc), (float)sc, px0, my, sm, sm, 1);
    cv_text(&CV, f_bold, AV.name, px0 + sm + D(10), my, pw - sm - D(10), D(18), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_text(&CV, f_small, T(S_avatarMsgSample), px0 + sm + D(10), my + D(18), pw - sm - D(10), D(18), C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    //  아래: 알림 · 취소 · 적용
    int fy = y + dh - D(50);
    int aw = cv_text_w(&CV, f_small, T(S_avatarApply)) + D(22), cw2 = cv_text_w(&CV, f_small, T(S_avatarCancel)) + D(22);
    int ax = x + dw - D(22) - aw;
    if (!AV.busy) {
        cv_round(&CV, (float)ax, (float)fy, (float)aw, (float)D(32), (float)D(7), hover == ID_HK0 + nHKA ? C_ACCHI : C_ACC, 1);
        cv_text(&CV, f_small, T(S_avatarApply), ax, fy, aw, D(32), 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        add_hit(hka(HA_AV_APPLY, NULL, NULL, NULL, 0), ax, fy, aw, D(32));
    } else kb_btn(ID_NONE, ax, fy, D(32), T(S_avatarApply), C_DIM, 1);
    kb_btn(hka(HA_AV_CANCEL, NULL, NULL, NULL, 0), ax - D(8) - cw2, fy, D(32), T(S_avatarCancel), C_TEXT2, 0);
    if (AV.msg[0]) cv_text(&CV, f_small, AV.msg, x + D(22), fy, ax - D(16) - cw2 - x - D(22), D(32), cls_color(AV.mcls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

//  조절 창 마우스 — 1 이면 여기서 처리함
static int av_mouse(UINT m, int x, int y, int wheel) {
    if (!AV.open) return 0;
    int ring = D(14);
    if (m == WM_LBUTTONDOWN) {
        POINT pt = { x, y };
        if (PtInRect(&AV.stage, pt) && !AV.busy) { AV.drag = 1; AV.sx = x; AV.sy = y; AV.sox = AV.ox; AV.soy = AV.oy; SetCapture(W); return 1; }
        RECT zr = AV.zoom_r; InflateRect(&zr, D(6), D(4));
        if (PtInRect(&zr, pt) && !AV.busy) { AV.slider = 1; SetCapture(W); }
        else return 0;
    }
    if ((m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN) && AV.slider) {
        double t = (double)(x - AV.zoom_r.left) / (AV.zoom_r.right - AV.zoom_r.left);
        av_zoom_at(1 + fmin(1, fmax(0, t)) * (AV_ZOOM_MAX - 1), AV.D / 2, AV.D / 2);
        InvalidateRect(W, NULL, FALSE);
        return 1;
    }
    if (m == WM_MOUSEMOVE && AV.drag) {
        AV.ox = AV.sox + (x - AV.sx);
        AV.oy = AV.soy + (y - AV.sy);
        av_clamp();
        InvalidateRect(W, NULL, FALSE);
        return 1;
    }
    if (m == WM_LBUTTONUP && (AV.drag || AV.slider)) { AV.drag = AV.slider = 0; ReleaseCapture(); return 1; }
    if (m == WM_MOUSEWHEEL) {
        POINT pt = { x, y };
        if (PtInRect(&AV.stage, pt) && !AV.busy) {
            double dy = -(double)wheel / WHEEL_DELTA * 100;   //  CSS deltaY (한 칸 100)
            av_zoom_at(AV.zoom * exp(-dy * 0.0015), x - AV.stage.left - ring, y - AV.stage.top - ring);
            InvalidateRect(W, NULL, FALSE);
        }
        return 1;   //  창이 열려 있으면 뒤 화면은 안 움직인다
    }
    return m == WM_MOUSEMOVE ? 0 : 0;
}
static int av_key(UINT vk) {
    if (!AV.open) return 0;
    if (vk == VK_ESCAPE) { av_close(); return 1; }
    if (AV.busy) return 1;
    double step = GetKeyState(VK_SHIFT) < 0 ? D(24) : D(6), mid = AV.D / 2;
    if (vk == VK_LEFT) AV.ox += step;
    else if (vk == VK_RIGHT) AV.ox -= step;
    else if (vk == VK_UP) AV.oy += step;
    else if (vk == VK_DOWN) AV.oy -= step;
    else if (vk == VK_OEM_PLUS || vk == VK_ADD) av_zoom_at(AV.zoom * 1.1, mid, mid);
    else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) av_zoom_at(AV.zoom / 1.1, mid, mid);
    else return 1;
    av_clamp();
    InvalidateRect(W, NULL, FALSE);
    return 1;
}

// ── 페이지 ──
static void hk_draft_done(const char *key, const char *text, int enter, int changed);
static void hk_do_add(void) {
    jval t, u, n;
    memset(&t, 0, sizeof t); memset(&u, 0, sizeof u); memset(&n, 0, sizeof n);
    t.t = u.t = n.t = J_STR;
    t.s = (char *)str0(HD_TITLE); t.slen = strlen(t.s);
    u.s = (char *)str0(HD_URL); u.slen = strlen(u.s);
    n.s = (char *)str0(HD_NAME); n.slen = strlen(n.s);
    const char *e = hk_add(&t, &u, &n);
    if (!e) {
        //  주소·이름은 바로 지운다 (작품명은 같은 폴더에 더 넣기 쉽게 남긴다). 넣은 폴더는 펼친다
        set_str(&HD_URL, "");
        set_str(&HD_NAME, "");
        char *added = binge_clean_ws(t.s, t.slen, 100000);
        if (added && *added) hk_set_open(added, 1);
        free(added);
        hk_gsay(T(S_hookAdded), 1);
    } else hk_gsay(hk_err(e, S_hookErrUrl), 2);
}
static void hk_draft_done(const char *key, const char *text, int enter, int changed) {
    (void)changed;
    if (!strcmp(key, "hkf:title")) set_str(&HD_TITLE, text);
    else if (!strcmp(key, "hkf:url")) set_str(&HD_URL, text);
    else if (!strcmp(key, "hkf:name")) set_str(&HD_NAME, text);
    if (enter) hk_do_add();
}
static void hk_hname_done(const char *key, const char *text, int enter, int changed) {
    (void)enter;
    if (!changed) return;
    jval n; memset(&n, 0, sizeof n); n.t = J_STR; n.s = (char *)text; n.slen = strlen(text);
    const char *e = hk_rename(key + 4, &n);
    hk_gsay(e ? hk_err(e, S_hookErrFile) : T(S_hookRenamed), e ? 2 : 1);
}
static void hk_char_field_done(const char *key, const char *text, int enter, int changed) {
    (void)enter;
    if (!changed) return;
    jval v; memset(&v, 0, sizeof v); v.t = J_STR; v.s = (char *)text; v.slen = strlen(text);
    const char *e = !strncmp(key, "hcn:", 4) ? hk_char_rename(HK_ED_TITLE, HK_ED_ID, &v) : hk_char_avatar_url(HK_ED_TITLE, HK_ED_ID, &v);
    char mk[64];
    snprintf(mk, sizeof mk, "c:%s", HK_ED_ID);
    hk_say(mk, e ? hk_err(e, S_hookErrFile) : T(S_hookCharSaved), e ? 2 : 1);
}
static void hk_char_add_now(const char *title, int refocus);
static void hk_cadd_done(const char *key, const char *text, int enter, int changed) {
    (void)changed;
    set_str(hd_cadd(HK_ED_TITLE), text);
    if (enter) hk_char_add_now(HK_ED_TITLE, 1);
}
static void hk_char_add_now(const char *title, int refocus) {
    char **d = hd_cadd(title);
    jval n; memset(&n, 0, sizeof n); n.t = J_STR; n.s = (char *)str0(*d); n.slen = strlen(n.s);
    char id[16];
    char mk[340];
    snprintf(mk, sizeof mk, "cadd:%s", title);
    const char *e = hk_char_add(title, &n, id);
    if (!e) {
        hk_unsay(mk);
        set_str(d, "");
        if (refocus) {   //  이어서 다음 캐릭터를 바로 적을 수 있게
            snprintf(HK_ED_TITLE, sizeof HK_ED_TITLE, "%s", title);
            fe_start(mk, "", 80, 0, hk_cadd_done);
        }
    } else hk_say(mk, hk_err(e, S_hookErrFile), 2);
}
static void hk_test_send(const char *msgkey, const char *url, const char *title, const jval *character) {
    int slot = -1;
    for (int i = 0; i < 16; i++) if (!HK_TESTS[i].tag) { slot = i; break; }
    if (slot < 0) slot = 0;
    HK_TESTS[slot].tag = ++HK_TAG;
    snprintf(HK_TESTS[slot].key, sizeof HK_TESTS[slot].key, "%s", msgkey);
    hk_say(msgkey, T(S_hookSending), 0);
    binge_send_test(url, title, character, HK_TAG);
}
static int hk_testing(const char *msgkey) { for (int i = 0; i < 16; i++) if (HK_TESTS[i].tag && !strcmp(HK_TESTS[i].key, msgkey)) return 1; return 0; }

static int hk_char_rows_h(const hk_char *cs, int n) {
    int h = 0;
    for (int i = 0; i < n; i++) {
        char mk[64]; int cls;
        snprintf(mk, sizeof mk, "c:%s", cs[i].id);
        h += D(84) + ((cs[i].until || hk_msg(mk, &cls)) ? D(20) : 0);
    }
    return h;
}

static int page_hook(int x, int y, int w) {
    if (w > D(900)) w = D(900);
    hka_reset();
    int yy = y;
    section_title(x, yy, w, T(S_hookTitle));
    yy += D(28);
    int ih = text_h(f_small, T(S_hookIntro), w);
    cv_text(&CV, f_small, T(S_hookIntro), x, yy, w, ih, C_DIM, DT_LEFT | DT_WORDBREAK);
    yy += ih + D(14);
    //  추가 칸
    int lw = D(130), fx = x + D(20) + lw, fw = w - D(40) - lw, rh = D(52);
    int nh = text_h(f_small, T(S_hookNameHint), fw);
    int ch = D(14) + rh + nh + D(6) + rh + rh + D(52);
    card(x, yy, w, ch);
    int ry = yy + D(14);
    cv_text(&CV, f_body, T(S_hookName), x + D(20), ry, lw, D(36), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    w_field(hka(HA_F_TITLE, NULL, NULL, HD_TITLE, 0), "hkf:title", fx, ry, fw, D(36), str0(HD_TITLE), T(S_hookNamePh), 0, C_TEXT);
    cv_text(&CV, f_small, T(S_hookNameHint), fx, ry + D(40), fw, nh, C_DIM, DT_LEFT | DT_WORDBREAK);
    ry += rh + nh + D(6) - D(8);
    cv_text(&CV, f_body, T(S_hookUrl), x + D(20), ry, lw, D(36), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    w_field(hka(HA_F_URL, NULL, NULL, HD_URL, 0), "hkf:url", fx, ry, fw, D(36), str0(HD_URL), T(S_hookUrlPh), 1, C_TEXT);
    ry += rh;
    cv_text(&CV, f_body, T(S_hookWebhookName), x + D(20), ry, lw, D(36), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    w_field(hka(HA_F_NAME, NULL, NULL, HD_NAME, 0), "hkf:name", fx, ry, fw, D(36), str0(HD_NAME), T(S_hookWebhookNamePh), 0, C_TEXT);
    ry += rh;
    {   //  알림 + 추가 (주 버튼)
        int aw = cv_text_w(&CV, f_body, T(S_hookAdd)) + D(32), ax = x + w - D(20) - aw, aid = hka(HA_ADD, NULL, NULL, NULL, 0);
        cv_round(&CV, (float)ax, (float)ry, (float)aw, (float)D(36), (float)D(8), hover == aid ? C_ACCHI : C_ACC, 1);
        cv_text(&CV, f_body, T(S_hookAdd), ax, ry, aw, D(36), 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        add_hit(aid, ax, ry, aw, D(36));
        if (HK_GMSG[0]) cv_text(&CV, f_small, HK_GMSG, x + D(20), ry, ax - x - D(36), D(36), cls_color(HK_GCLS), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    yy += ch + D(22);
    //  목록 머리
    section_title(x, yy, w / 2, T(S_hookList));
    cv_text(&CV, f_small, T(hk_has_default() ? S_hookDefaultOn : S_hookDefaultOff), x + w / 2, yy, w / 2, D(20), C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    yy += D(28);
    hk_item *l;
    int n = hk_list(&l);
    if (!n) {
        card(x, yy, w, D(70));
        cv_text(&CV, f_small, T(S_hookEmpty), x + D(20), yy, w - D(40), D(70), C_DIM, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        yy += D(70) + D(14);
        hk_list_free(l, n);
        return yy - y + D(6);
    }
    //  작품명별 폴더 (넣은 순서)
    for (int gi = 0; gi < n; gi++) {
        int seen = 0;
        for (int k = 0; k < gi; k++) if (!strcmp(l[k].title, l[gi].title)) seen = 1;
        if (seen) continue;
        const char *title = l[gi].title;
        int cnt = 0;
        for (int k = gi; k < n; k++) cnt += !strcmp(l[k].title, title);
        int open = hk_is_open(title);
        hk_char *cs = NULL;
        int nc = open ? hk_chars(title, &cs) : 0;
        //  높이 먼저
        int fh = D(56);
        if (open) {
            fh += D(46);
            for (int k = gi; k < n; k++) if (!strcmp(l[k].title, title)) { int cls; fh += D(48) + (hk_msg(l[k].id, &cls) ? D(20) : 0); }
            fh += D(64) + hk_char_rows_h(cs, nc);
            char mk[340]; int cls;
            snprintf(mk, sizeof mk, "cadd:%s", title);
            fh += D(52) + (hk_msg(mk, &cls) ? D(20) : 0) + D(8);
        }
        card(x, yy, w, fh);
        //  머리 (누르면 펼치기·접기)
        int hid = hka(HA_FOLDER, title, NULL, NULL, 0);
        add_hit(hid, x, yy, w, D(56));
        if (hover == hid) cv_round(&CV, (float)x + 1, (float)yy + 1, (float)w - 2, (float)D(56) - 2, (float)D(10), C_FIELD_HI, 1);
        const char *ic = hk_icon(title);
        const uint32_t *ipx = NULL;
        int isz = D(32), ix = x + D(16), iy = yy + (D(56) - isz) / 2;
        if (ic && img_get(ic, &ipx) == 1 && ipx) cv_image(&CV, ipx, IMG_SIZE, (float)ix, (float)iy, (float)isz, (float)D(8));
        else {
            char fl[8];
            first_letter(fl, title);
            cv_round(&CV, (float)ix, (float)iy, (float)isz, (float)isz, (float)D(8), C_THUMB, 1);
            cv_text(&CV, f_bold, fl, ix, iy, isz, isz, C_TEXT2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        char cn[16], ctext[48];
        snprintf(cn, sizeof cn, "%d", cnt);
        fill(ctext, sizeof ctext, T(S_hookCount), "{n}", cn);
        int tw = cv_text_w(&CV, f_bold, title), tmax = w - D(16) - isz - D(14) - D(110);
        if (tw > tmax) tw = tmax;
        cv_text(&CV, f_bold, title, ix + isz + D(14), yy, tw, D(56), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        cv_text(&CV, f_small, ctext, ix + isz + D(14) + tw + D(10), yy, D(80), D(56), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cv_text(&CV, f_small, open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", x + w - D(40), yy, D(20), D(56), C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        int by = yy + D(56);
        if (open) {
            cv_fill(&CV, x + D(20), by, w - D(40), S > 1 ? (int)S : 1, C_DIV);
            //  도구: 아이콘 · 아이콘 지우기 · 이 폴더에 추가
            int bx = x + D(20), bh = D(30), ty = by + D(8);
            bx += kb_btn(hka(HA_ICON, title, NULL, NULL, 0), bx, ty, bh, T(S_hookIcon), C_TEXT2, 0) + D(8);
            if (ic) bx += kb_btn(hka(HA_ICONCLR, title, NULL, NULL, 0), bx, ty, bh, T(S_hookIconClear), C_TEXT2, 0) + D(8);
            kb_btn(hka(HA_ADDHERE, title, NULL, NULL, 0), bx, ty, bh, T(S_hookAddHere), C_TEXT2, 0);
            by += D(46);
            //  웹훅 줄
            int num = 0;
            for (int k = gi; k < n; k++) {
                if (strcmp(l[k].title, title)) continue;
                num++;
                const hk_item *hi = &l[k];
                int cy = by + D(8);
                char key[40], ph[64], nn[16];
                snprintf(key, sizeof key, "hkn:%s", hi->id);
                snprintf(nn, sizeof nn, "%d", num);
                fill(ph, sizeof ph, T(S_hookUnnamed), "{n}", nn);
                int nw = w / 3 > D(220) ? D(220) : w / 3;
                w_field(hka(HA_H_NAME, title, hi->id, hi->name, 0), key, x + D(20), cy, nw, D(32), hi->name, ph, 0, C_TEXT);
                cv_text(&CV, f_mono, hi->label, x + D(20) + nw + D(12), cy, D(90), D(32), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                int armed = hk_armed(hi->id);
                const char *dl = armed ? T(S_hookRemoveConfirm) : T(S_hookRemove);
                int dw2 = cv_text_w(&CV, f_small, dl) + D(22), ex = x + w - D(20);
                int did = hka(HA_H_DEL, title, hi->id, NULL, 0);
                kb_btn(did, ex - dw2, cy, D(32), dl, armed ? 0xffffff : C_RED, 0);
                if (armed) { cv_round(&CV, (float)(ex - dw2), (float)cy, (float)dw2, (float)D(32), (float)D(7), C_RED, 1); cv_text(&CV, f_small, dl, ex - dw2, cy, dw2, D(32), 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
                int tw2 = cv_text_w(&CV, f_small, T(S_hookTest)) + D(22);
                kb_btn(hka(HA_H_TEST, title, hi->id, NULL, 0), ex - dw2 - D(8) - tw2, cy, D(32), T(S_hookTest), C_TEXT2, hk_testing(hi->id));
                int cls = 0;
                const char *m = hk_msg(hi->id, &cls);
                by += D(48);
                if (m) { cv_text(&CV, f_small, m, x + D(20), by - D(4), w - D(40), D(20), cls_color(cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); by += D(20); }
            }
            //  캐릭터
            cv_fill(&CV, x + D(20), by + D(4), w - D(40), S > 1 ? (int)S : 1, C_DIV);
            int on = 0;
            for (int k = 0; k < nc; k++) on += !cs[k].off;
            char head[120];
            snprintf(head, sizeof head, "%s", T(S_hookChars));
            int hw = cv_text_w(&CV, f_title, head);
            cv_text(&CV, f_title, head, x + D(20), by + D(12), hw, D(20), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (on < nc) {
                char a1[16], a2[16], t1[96], t2[96];
                snprintf(a1, sizeof a1, "%d", on);
                snprintf(a2, sizeof a2, "%d", nc);
                fill(t1, sizeof t1, on ? T(S_hookCharsCount) : T(S_hookCharsNone), "{on}", a1);
                fill(t2, sizeof t2, t1, "{all}", a2);
                cv_text(&CV, f_small, t2, x + D(20) + hw + D(8), by + D(12), D(200), D(20), C_AMBER, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            cv_text(&CV, f_small, T(S_hookCharsHint), x + D(20), by + D(34), w - D(40), D(20), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            by += D(64);
            for (int k = 0; k < nc; k++) {
                const hk_char *c = &cs[k];
                char mk[64];
                snprintf(mk, sizeof mk, "c:%s", c->id);
                int cy = by;
                uint32_t tc = c->off ? C_DIM : C_TEXT;
                //  1줄: 켜기·끄기 · 얼굴 · 이름 · 사진 주소
                w_switch(hka(HA_C_SW, title, c->id, NULL, !c->off), x + D(20), cy + D(7), !c->off);
                int fx2 = x + D(20) + D(46) + D(10), fsz = D(34);
                int fid = hka(HA_C_FACE, title, c->id, c->avatar, 0);
                const uint32_t *apx = NULL;
                if (!strncmp(c->avatar, "https://", 8) && img_get(c->avatar, &apx) == 1 && apx) cv_image(&CV, apx, IMG_SIZE, (float)fx2, (float)cy + 3, (float)fsz, fsz / 2.0f);
                else {
                    char fl[8];
                    first_letter(fl, c->name);
                    cv_circle(&CV, fx2 + fsz / 2.0f, cy + 3 + fsz / 2.0f, fsz / 2.0f, C_THUMB);
                    cv_text(&CV, f_body, fl, fx2, cy + 3, fsz, fsz, C_TEXT2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                if (hover == fid) cv_round_border(&CV, (float)fx2, (float)cy + 3, (float)fsz, (float)fsz, fsz / 2.0f, S > 1 ? S : 1, C_ACC);
                add_hit(fid, fx2, cy + 3, fsz, fsz);
                int nx = fx2 + fsz + D(10), rest = x + w - D(20) - nx, nw = rest * 2 / 5;
                char nkey[64], ukey[64];
                snprintf(nkey, sizeof nkey, "hcn:%s", c->id);
                snprintf(ukey, sizeof ukey, "hcu:%s", c->id);
                w_field(hka(HA_C_NAME, title, c->id, c->name, 0), nkey, nx, cy + 2, nw, D(36), c->name, T(S_hookCharNamePh), 0, tc);
                w_field(hka(HA_C_URL, title, c->id, c->avatar, 0), ukey, nx + nw + D(8), cy + 2, rest - nw - D(8), D(36), c->avatar, T(S_hookCharAvatarPh), 0, C_TEXT2);
                //  2줄: 사진 고르기 · 시험 · 삭제 · 알림
                int by2 = cy + D(46), cbx = nx;
                cbx += kb_btn(hka(HA_C_PICK, title, c->id, c->avatar, 0), cbx, by2, D(30), T(S_hookCharPick), C_TEXT2, AV.busy) + D(8);
                cbx += kb_btn(hka(HA_C_TEST, title, c->id, NULL, 0), cbx, by2, D(30), T(S_hookTest), C_TEXT2, hk_testing(mk)) + D(8);
                int armed = hk_armed(mk);
                const char *dl = armed ? T(S_hookRemoveConfirm) : T(S_hookRemove);
                int dw2 = kb_btn(hka(HA_C_DEL, title, c->id, NULL, 0), cbx, by2, D(30), dl, armed ? 0xffffff : C_RED, 0);
                if (armed) { cv_round(&CV, (float)cbx, (float)by2, (float)dw2, (float)D(30), (float)D(7), C_RED, 1); cv_text(&CV, f_small, dl, cbx, by2, dw2, D(30), 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
                cbx += dw2 + D(12);
                int ly = cy + D(84);
                if (c->until) {   //  임시 저장소(72시간) 사진이면 언제까지인지
                    char note[120];
                    int64_t nowm = (int64_t)time(NULL) * 1000;
                    if ((double)nowm > c->until) snprintf(note, sizeof note, "%s", T(S_hookCharExpired));
                    else {
                        FILETIME ft, lt; SYSTEMTIME st2;
                        unsigned long long t100 = (unsigned long long)(c->until + 11644473600000.0) * 10000ULL;
                        ft.dwLowDateTime = (DWORD)t100; ft.dwHighDateTime = (DWORD)(t100 >> 32);
                        FileTimeToLocalFileTime(&ft, &lt); FileTimeToSystemTime(&lt, &st2);
                        char dt[32];
                        snprintf(dt, sizeof dt, "%u/%02u %02u:%02u", st2.wMonth, st2.wDay, st2.wHour, st2.wMinute);
                        fill(note, sizeof note, T(S_hookCharTemp), "{date}", dt);
                    }
                    cv_text(&CV, f_small, note, cbx, by2, x + w - D(20) - cbx, D(30), (double)nowm > c->until ? C_RED : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
                int cls = 0;
                const char *m = hk_msg(mk, &cls);
                if (m) cv_text(&CV, f_small, m, nx, ly - D(4), x + w - D(20) - nx, D(20), cls_color(cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                by += D(84) + ((c->until || m) ? D(20) : 0);
            }
            //  캐릭터 추가 칸
            char akey[340];
            snprintf(akey, sizeof akey, "cadd:%s", title);
            char **dr = hd_cadd(title);
            int aw2 = cv_text_w(&CV, f_small, T(S_hookCharAdd)) + D(22);
            w_field(hka(HA_CA_NAME, title, NULL, *dr, 0), akey, x + D(20), by + D(8), w - D(40) - aw2 - D(8), D(34), str0(*dr), T(S_hookCharNamePh), 0, C_TEXT);
            kb_btn(hka(HA_CA_ADD, title, NULL, NULL, 0), x + w - D(20) - aw2, by + D(10), D(30), T(S_hookCharAdd), C_ACCHI, 0);
            int cls = 0;
            const char *m = hk_msg(akey, &cls);
            by += D(52);
            if (m) cv_text(&CV, f_small, m, x + D(20), by - D(6), w - D(40), D(20), cls_color(cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        hk_chars_free(cs, nc);
        yy += fh + D(14);
    }
    hk_list_free(l, n);
    return yy - y + D(6);
}

static void hk_click(int idn) {
    int i = idn - ID_HK0;
    if (i < 0 || i >= nHKA) return;
    hk_act a = HKA[i];   //  아래 호출이 다시 그리며 표를 바꿀 수 있다 → 필요한 값은 복사
    char title[340], id[32], text[2100];
    snprintf(title, sizeof title, "%s", str0(a.title));
    snprintf(id, sizeof id, "%s", a.id);
    snprintf(text, sizeof text, "%s", str0(a.text));
    int nval = a.n;
    switch (a.kind) {
    case HA_F_TITLE: fe_start("hkf:title", str0(HD_TITLE), 100, 0, hk_draft_done); break;
    case HA_F_URL: fe_start("hkf:url", str0(HD_URL), 0, 1, hk_draft_done); break;
    case HA_F_NAME: fe_start("hkf:name", str0(HD_NAME), 40, 0, hk_draft_done); break;
    case HA_ADD: hk_do_add(); break;
    case HA_FOLDER: hk_set_open(title, !hk_is_open(title)); break;
    case HA_ICON: {
        if (!hk_has_title(title)) { hk_gsay(T(S_hookErrMissing), 2); break; }
        unsigned char *b; size_t bn; const char *m;
        int r = pick_image(S_hookIconDialog, &b, &bn, &m);
        if (r == 0) break;
        if (r < 0) { hk_gsay(T(S_hookErrIcon), 2); break; }
        //  가운데를 정사각으로 잘라 64px PNG
        int w2 = 0, h2 = 0, ow = 0, oh = 0;
        uint32_t *px = iu_decode(b, bn, 64, &w2, &h2, &ow, &oh);   //  풀 수 있는지 · 원래 크기
        size_t pn = 0;
        unsigned char *png = NULL;
        if (px && ow > 0 && oh > 0) {
            int side = ow < oh ? ow : oh;
            png = iu_square_png(b, bn, (ow - side) / 2, (oh - side) / 2, side, 64, &pn);
        }
        free(px);
        free(b);
        if (!png) { hk_gsay(T(S_hookErrIcon), 2); break; }
        char *b64 = iu_base64(png, pn);
        free(png);
        if (!b64) break;
        char *url = (char *)malloc(strlen(b64) + 32);
        if (url) { sprintf(url, "data:image/png;base64,%s", b64); const char *e = hk_set_icon(title, url); if (e) hk_gsay(hk_err(e, S_hookErrIcon), 2); free(url); }
        free(b64);
        break;
    }
    case HA_ICONCLR: hk_clear_icon(title); break;
    case HA_ADDHERE:
        set_str(&HD_TITLE, title);
        scroll_[tab] = 0;
        fe_start("hkf:url", str0(HD_URL), 0, 1, hk_draft_done);
        break;
    case HA_H_NAME: { char key[40]; snprintf(key, sizeof key, "hkn:%s", id); fe_start(key, text, 40, 0, hk_hname_done); break; }
    case HA_H_TEST: {
        char *u = hk_url(id);
        if (!u) { char t2[120]; fill(t2, sizeof t2, T(S_hookTestFail), "{status}", "\xE2\x80\x94"); hk_say(id, t2, 2); break; }
        hk_test_send(id, u, title, NULL);
        free(u);
        break;
    }
    case HA_H_DEL:
        if (!hk_armed(id)) { snprintf(HK_ARMED.key, sizeof HK_ARMED.key, "%s", id); HK_ARMED.at = GetTickCount(); SetTimer(W, 3, 4100, NULL); break; }
        HK_ARMED.key[0] = 0;
        { const char *e = hk_remove(id); hk_unsay(id); hk_gsay(e ? hk_err(e, S_hookErrFile) : T(S_hookRemoved), e ? 2 : 1); }
        break;
    case HA_C_SW: {
        char mk[64];
        snprintf(mk, sizeof mk, "c:%s", id);
        int want = !nval;
        const char *e = hk_char_toggle(title, id, want);
        hk_say(mk, e ? hk_err(e, S_hookErrFile) : T(want ? S_hookCharOnMsg : S_hookCharOffMsg), e ? 2 : want ? 1 : 0);
        break;
    }
    case HA_C_FACE: { jval *c = hk_char_get(title, id); if (c) { av_open(title, id, json_get_str(c, "name", ""), text, *text != 0); json_free(c); } break; }
    case HA_C_PICK: { jval *c = hk_char_get(title, id); if (c) { av_open(title, id, json_get_str(c, "name", ""), text, 0); json_free(c); } break; }
    case HA_C_NAME: case HA_C_URL: {
        snprintf(HK_ED_TITLE, sizeof HK_ED_TITLE, "%s", title);
        snprintf(HK_ED_ID, sizeof HK_ED_ID, "%s", id);
        char key[64];
        snprintf(key, sizeof key, "%s%s", a.kind == HA_C_NAME ? "hcn:" : "hcu:", id);
        fe_start(key, text, a.kind == HA_C_NAME ? 80 : 2000, 0, hk_char_field_done);
        break;
    }
    case HA_C_TEST: {
        char mk[64];
        snprintf(mk, sizeof mk, "c:%s", id);
        jval *c = hk_char_get(title, id);
        char *u = c ? hk_test_url_for_char(title) : NULL;
        if (!c || !u || !*u) { char t2[120]; fill(t2, sizeof t2, T(S_hookTestFail), "{status}", "\xE2\x80\x94"); hk_say(mk, t2, 2); }
        else hk_test_send(mk, u, title, c);
        free(u);
        json_free(c);
        break;
    }
    case HA_C_DEL: {
        char mk[64];
        snprintf(mk, sizeof mk, "c:%s", id);
        if (!hk_armed(mk)) { snprintf(HK_ARMED.key, sizeof HK_ARMED.key, "%s", mk); HK_ARMED.at = GetTickCount(); SetTimer(W, 3, 4100, NULL); break; }
        HK_ARMED.key[0] = 0;
        const char *e = hk_char_remove(title, id);
        hk_unsay(mk);
        if (e) hk_gsay(hk_err(e, S_hookErrFile), 2);
        break;
    }
    case HA_CA_NAME: {
        snprintf(HK_ED_TITLE, sizeof HK_ED_TITLE, "%s", title);
        char key[360];
        snprintf(key, sizeof key, "cadd:%s", title);
        fe_start(key, text, 80, 0, hk_cadd_done);
        break;
    }
    case HA_CA_ADD: hk_char_add_now(title, 0); break;
    case HA_AV_RESET: av_center(); break;
    case HA_AV_OTHER: {
        unsigned char *b; size_t bn; const char *m;
        int r = pick_image(S_hookCharDialog, &b, &bn, &m);
        if (r == 0) break;
        if (r < 0 || !av_load(b, bn, m)) { snprintf(AV.msg, sizeof AV.msg, "%s", T(S_hookErrIcon)); AV.mcls = 2; break; }
        AV.msg[0] = 0;
        break;
    }
    case HA_AV_APPLY: av_apply(); break;
    case HA_AV_CANCEL: av_close(); break;
    }
    if (W) InvalidateRect(W, NULL, FALSE);
}

//  시험 보내기 결과 (binge.c → main → 여기)
void win_binge_test_result(int tag, LPARAM r) {
    for (int i = 0; i < 16; i++) {
        if (HK_TESTS[i].tag != tag) continue;
        int ok = (int)((r >> 16) & 1), st = (int)(r & 0xFFFF);
        char t2[120], s2[16];
        if (st) snprintf(s2, sizeof s2, "%d", st); else snprintf(s2, sizeof s2, "\xE2\x80\x94");
        fill(t2, sizeof t2, T(S_hookTestFail), "{status}", s2);
        hk_say(HK_TESTS[i].key, ok ? T(S_hookTestOk) : t2, ok ? 1 : 2);
        HK_TESTS[i].tag = 0;
    }
    if (W) InvalidateRect(W, NULL, FALSE);
}

static int popup(int id, const char *const *labels, int n, int cur);
// ── 배경 꾸미기 (rpc-window.js 배경 부분 + rpc-window.html .bg-layer CSS) ──
//  저장된 상태(BGS)와 편집 중(BGD)을 따로 둔다. 이 탭에 있는 동안만 편집값을 창 바탕에 보이고, 다른 탭에선 저장된 배경
#define ID_BG0 60000
enum { BA_PREVIEW = 1, BA_PICK, BA_RESET, BA_ENABLE, BA_FIT, BA_CENTER, BA_ZOOM, BA_OPACITY, BA_DIM, BA_BLUR, BA_CANCEL, BA_APPLY };
#define BG_ID(k) (ID_BG0 + (k))
typedef struct { uint32_t *px; int w, h; char key[64]; } bg_img;
static bg_set BGS;                 // 저장된 것 (bgSaved.settings)
static int BGS_URL;                // 저장된 그림을 보일 수 있나 (bgSaved.url)
static bg_set BGD;                 // 편집 중 (bgDraft)
static char BGD_TOKEN[24];         // 새로 고르고 적용 안 한 사본
static int BGD_URL;                // 편집 중 그림이 있나
static int BG_BUSY, BG_LOADED;
static int BG_NOTICE, BG_NOTICE_ERR;   // 문구 번호 (0 = 없음)
static bg_img BGI_S, BGI_D;        // 풀어 둔 그림 (저장된 것 · 고른 사본)
static struct { uint32_t *px; int w, h; char key[200]; } BGC;   // 창 바탕 (계산해 둔 것)
static RECT BG_PREV_R;             // 미리보기 자리
static struct { int on, kind; int x0, y0, px, py; double rx, ry; RECT r; int lo, hi, step; } BG_DRAG;
static const bg_set BG_FRESH = { 1, "cover", 500, 500, 100, 100, 60, 0, "", "", 0, 0 };

static int bg_clampi(double v, int lo, int hi, int dflt) { double n = js_round(v); return (n == n && n > -1e300 && n < 1e300) ? (int)fmin(hi, fmax(lo, n)) : dflt; }
static void bg_img_free(bg_img *g) { free(g->px); memset(g, 0, sizeof *g); }
static int bg_img_load(bg_img *g, const char *file) {   //  관리 파일을 풀어 둔다 (긴 변 2560)
    if (g->px && !strcmp(g->key, file)) return 1;
    bg_img_free(g);
    wchar_t p[MAX_PATH];
    unsigned char *b; size_t n;
    if (!bg_file(file, p) || iu_read_file(p, 64 * 1024 * 1024, &b, &n) != 1) return 0;
    int ow, oh;
    g->px = iu_decode(b, n, 2560, &g->w, &g->h, &ow, &oh);
    free(b);
    if (!g->px) return 0;
    snprintf(g->key, sizeof g->key, "%s", file);
    return 1;
}
static void bg_from_saved(void) { BGD = BGS; BGD.enabled = BGS.enabled && BGS_URL; BGD_URL = BGS_URL; BGD_TOKEN[0] = 0; }
static int bg_dirty(void) {
    if (BGD_TOKEN[0]) return 1;
    bg_set s = BGS;
    s.enabled = BGS.enabled && BGS_URL;
    return s.enabled != BGD.enabled || strcmp(s.fit, BGD.fit) || s.posX != BGD.posX || s.posY != BGD.posY || s.zoom != BGD.zoom || s.opacity != BGD.opacity || s.dim != BGD.dim || s.blur != BGD.blur;
}
static void bg_notice(int id, int err) { BG_NOTICE = id; BG_NOTICE_ERR = err; }
static int bg_err_id(const char *e) {
    static const struct { const char *e; int s; } M[] = { { "tooLarge", S_bgErr_tooLarge }, { "tooBig", S_bgErr_tooBig }, { "format", S_bgErr_format }, { "animated", S_bgErr_animated },
        { "corrupt", S_bgErr_corrupt }, { "read", S_bgErr_read }, { "copy", S_bgErr_copy }, { "save", S_bgErr_save }, { "busy", S_bgErr_busy }, { "stale", S_bgErr_stale },
        { "invalid", S_bgErr_invalid }, { "noImage", S_bgErr_noImage } };
    for (size_t i = 0; e && i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i].e, e)) return M[i].s;
    return S_bgErr_save;
}
//  저장된 그림을 그리지 못하면 기본 바탕으로 (reportBroken)
static void bg_saved_broken(void) {
    const char *nt = bg_report_broken(BGS.image, &BGS);
    BGS_URL = BGS.image[0] && strcmp(nt, "unavailable") && strcmp(nt, "stale") ? 1 : 0;
    if (!strcmp(nt, "unavailable")) BGS_URL = 0;
    if (!BGD_TOKEN[0]) bg_from_saved();
    bg_notice(!strcmp(nt, "unavailable") ? S_bgUnavailable : S_bgMissing, 1);
}
static void bg_load_state(void) {   //  loadBackground
    const char *nt = bg_get(&BGS);
    BGS_URL = BGS.image[0] && !(nt && !strcmp(nt, "unavailable"));
    if (BGS_URL && !bg_img_load(&BGI_S, BGS.image)) bg_saved_broken();
    else { bg_from_saved(); if (nt) bg_notice(!strcmp(nt, "unavailable") ? S_bgUnavailable : S_bgMissing, 1); }
    BG_LOADED = 1;
}

//  그림 한 장을 (W×H) 바탕에 CSS 처럼: object-fit · object-position · scale(zoom) (기준점 = 위치) · blur · opacity · 어둡게
//  out 은 W×H (검정 바탕). 흐림은 가우스(σ = blur)를 상자 흐림 세 번으로
static void bg_box(uint32_t *a, uint32_t *t, int CW, int CH, int r, int horiz) {
    if (r < 1) return;
    int n = horiz ? CW : CH, lines = horiz ? CH : CW;
    for (int l = 0; l < lines; l++) {
        long long sr = 0, sg = 0, sb = 0;
        int cnt = 0;
        #define AT(i) (horiz ? a[l * CW + (i)] : a[(i) * CW + l])
        for (int i = -r; i <= r; i++) { int k = i < 0 ? 0 : i >= n ? n - 1 : i; uint32_t p = AT(k); sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255; cnt++; }
        for (int i = 0; i < n; i++) {
            uint32_t v = (uint32_t)(sr / cnt) << 16 | (uint32_t)(sg / cnt) << 8 | (uint32_t)(sb / cnt);
            if (horiz) t[l * CW + i] = v; else t[i * CW + l] = v;
            int ka = i - r < 0 ? 0 : i - r, kb = i + r + 1 >= n ? n - 1 : i + r + 1;
            uint32_t pa = AT(ka), pb = AT(kb);
            sr += (int)((pb >> 16) & 255) - (int)((pa >> 16) & 255);
            sg += (int)((pb >> 8) & 255) - (int)((pa >> 8) & 255);
            sb += (int)(pb & 255) - (int)(pa & 255);
        }
        #undef AT
    }
    memcpy(a, t, sizeof(uint32_t) * (size_t)CW * CH);
}
static void bg_compose(const bg_set *d, const bg_img *g, int CW, int CH, float px_scale, uint32_t *out) {
    float b = d->blur * px_scale;
    float x0 = -2 * b, y0 = -2 * b, EW = CW + 4 * b, EH = CH + 4 * b;
    float s = !strcmp(d->fit, "contain") ? fminf(EW / g->w, EH / g->h) : fmaxf(EW / g->w, EH / g->h);
    float px = d->posX / 1000.0f, py = d->posY / 1000.0f, z = d->zoom / 100.0f;
    float left = x0 + (EW - g->w * s) * px, top = y0 + (EH - g->h * s) * py;
    float ox = x0 + EW * px, oy = y0 + EH * py;
    float L = ox + (left - ox) * z, Tp = oy + (top - oy) * z, sc = s * z;
    canvas tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.px = out; tmp.w = CW; tmp.h = CH;
    tmp.clip.left = 0; tmp.clip.top = 0; tmp.clip.right = CW; tmp.clip.bottom = CH;
    memset(out, 0, sizeof(uint32_t) * (size_t)CW * CH);
    cv_blit(&tmp, g->px, g->w, g->h, L, Tp, sc, 0, 0, CW, CH, 0);
    if (b >= 0.5f) {   //  σ = b → 상자 3번 (Kovesi)
        double wi = sqrt(12.0 * b * b / 3 + 1);
        int wl = (int)floor(wi);
        if (wl % 2 == 0) wl--;
        int wu = wl + 2;
        int m = (int)js_round((12.0 * b * b - 3.0 * wl * wl - 12.0 * wl - 9) / (-4.0 * wl - 4));
        uint32_t *t = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)CW * CH);
        if (t) for (int i = 0; i < 3; i++) { int r = ((i < m ? wl : wu) - 1) / 2; bg_box(out, t, CW, CH, r, 1); bg_box(out, t, CW, CH, r, 0); }
        free(t);
    }
    float k = d->opacity / 100.0f * (1 - d->dim / 100.0f);   //  불투명도 · 그 위 검은 막
    for (size_t i = 0; i < (size_t)CW * CH; i++) {
        uint32_t p = out[i];
        out[i] = (uint32_t)(((p >> 16) & 255) * k) << 16 | (uint32_t)(((p >> 8) & 255) * k) << 8 | (uint32_t)((p & 255) * k);
    }
}
//  창 바탕 — 이 탭이면 편집값, 아니면 저장된 것. 깔았으면 1
static int bg_paint_window(int CW, int CH) {
    const bg_set *d = tab == 4 ? &BGD : &BGS;
    const bg_img *g = tab == 4 ? (BGD_TOKEN[0] ? &BGI_D : &BGI_S) : &BGI_S;
    int on = tab == 4 ? (BGD.enabled && BGD_URL) : (BGS.enabled && BGS_URL);
    if (!on || !g->px) return 0;
    char key[200];
    snprintf(key, sizeof key, "%s|%dx%d|%s|%d,%d,%d,%d,%d,%d|%g", g->key, CW, CH, d->fit, d->posX, d->posY, d->zoom, d->opacity, d->dim, d->blur, S);
    if (!BGC.px || BGC.w != CW || BGC.h != CH || strcmp(BGC.key, key)) {
        free(BGC.px);
        BGC.px = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)CW * CH);
        if (!BGC.px) return 0;
        BGC.w = CW; BGC.h = CH;
        bg_compose(d, g, CW, CH, S, BGC.px);
        snprintf(BGC.key, sizeof BGC.key, "%s", key);
    }
    memcpy(CV.px, BGC.px, sizeof(uint32_t) * (size_t)CW * CH);
    return 1;
}

//  끌기 1px 당 위치 변화 (bgDragRate)
static int bg_drag_rate(double *rx, double *ry) {
    int bw = BG_PREV_R.right - BG_PREV_R.left, bh = BG_PREV_R.bottom - BG_PREV_R.top;
    double iw = BGD.width, ih = BGD.height;
    if (!(bw > 0 && bh > 0 && iw > 0 && ih > 0)) return 0;
    double sc = (!strcmp(BGD.fit, "contain") ? fmin(bw / iw, bh / ih) : fmax(bw / iw, bh / ih)) * (BGD.zoom / 100.0);
    double ox = iw * sc - bw, oy = ih * sc - bh;
    *rx = fabs(ox) >= 1 ? -1000 / ox : 0;
    *ry = fabs(oy) >= 1 ? -1000 / oy : 0;
    return 1;
}
static int bg_can_edit(void) { return BGD_URL && !BG_BUSY; }
static void bg_center_view(void) { if (!bg_can_edit()) return; BGD.posX = 500; BGD.posY = 500; BGD.zoom = 100; }
static void bg_set_zoom(int z) { BGD.zoom = bg_clampi(z, 100, 400, BGD.zoom); }

static void bg_do_pick(void) {
    if (BG_BUSY) return;
    BG_BUSY = 1;
    bg_notice(0, 0);
    char token[24], name[512], fmt[8];
    int w = 0, h = 0;
    const char *e = bg_pick(NULL, token, name, &w, &h, fmt);
    BG_BUSY = 0;
    if (e) { if (strcmp(e, "canceled")) bg_notice(bg_err_id(e), 1); return; }
    //  새로 고른 사진이 실제로 그려지는지 (bgDecode)
    char file[48];
    snprintf(file, sizeof file, "tmp-%s.%s", token, fmt);
    bg_img g;
    memset(&g, 0, sizeof g);
    if (!bg_img_load(&g, file)) { bg_discard(token); bg_notice(S_bgErr_corrupt, 1); return; }
    if (BGD_TOKEN[0] && strcmp(BGD_TOKEN, token)) bg_discard(BGD_TOKEN);
    bg_img_free(&BGI_D);
    BGI_D = g;
    BGD = BG_FRESH;
    snprintf(BGD.name, sizeof BGD.name, "%s", name);
    BGD.width = w; BGD.height = h;
    snprintf(BGD_TOKEN, sizeof BGD_TOKEN, "%s", token);
    BGD_URL = 1;
}
static void bg_do_apply(void) {
    if (BG_BUSY || !bg_dirty()) return;
    if (BGD.enabled && !BGD_URL) { bg_notice(S_bgNeedImage, 1); return; }
    BG_BUSY = 1;
    bg_set saved;
    const char *e = bg_apply(&BGD, BGD_TOKEN[0] ? BGD_TOKEN : NULL, &saved);
    BG_BUSY = 0;
    if (!e) {
        BGS = saved;
        BGS_URL = BGS.image[0] != 0;
        if (BGD_TOKEN[0]) { bg_img_free(&BGI_S); BGI_S = BGI_D; memset(&BGI_D, 0, sizeof BGI_D); snprintf(BGI_S.key, sizeof BGI_S.key, "%s", BGS.image); }
        if (BGS_URL && !bg_img_load(&BGI_S, BGS.image)) { bg_saved_broken(); return; }
        bg_from_saved();
        bg_notice(S_bgApplied, 0);
    } else if (!strcmp(e, "busy")) bg_notice(S_bgErr_busy, 1);
    else {
        if (BGD_TOKEN[0]) bg_discard(BGD_TOKEN);
        bg_from_saved();
        bg_notice(bg_err_id(e), 1);
    }
}
static void bg_do_cancel(void) {
    if (BG_BUSY) return;
    if (BGD_TOKEN[0]) bg_discard(BGD_TOKEN);
    bg_from_saved();
    bg_notice(0, 0);
}
static void bg_do_reset(void) {
    if (BG_BUSY) return;
    wchar_t *q = utf8_to_w(T(S_bgResetConfirm), strlen(T(S_bgResetConfirm)));
    int yes = MessageBoxW(W, q ? q : L"?", L"TOKU RPC", MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
    free(q);
    if (!yes) return;
    BG_BUSY = 1;
    bg_set saved;
    const char *e = bg_reset(&saved);
    BG_BUSY = 0;
    if (!e) { BGS = saved; BGS_URL = 0; bg_img_free(&BGI_S); bg_img_free(&BGI_D); bg_from_saved(); bg_notice(S_bgResetDone, 0); }
    else bg_notice(bg_err_id(e), 1);
}

//  가로 막대 (슬라이더) — 끌거나 눌러서
static void w_slider(int id, int x, int y, int w, int v, int lo, int hi, int disabled) {
    cv_round(&CV, (float)x, (float)(y + D(8)), (float)w, (float)D(4), (float)D(2), C_TRACK, 1);
    float k = (float)(x + (double)w * (v - lo) / (hi - lo));
    if (!disabled) cv_round(&CV, (float)x, (float)(y + D(8)), k - x, (float)D(4), (float)D(2), C_ACC, 1);
    cv_circle(&CV, k, (float)(y + D(10)), (float)D(7), disabled ? 0x6b6e78 : 0xffffff);
    if (!disabled) add_hit(id, x - D(6), y, w + D(12), D(20));
}

static int page_bg(int x, int y, int w) {
    if (!BG_LOADED) bg_load_state();
    if (w > D(900)) w = D(900);
    int yy = y, dirty = bg_dirty(), has = BGD_URL;
    section_title(x, yy, w / 2, T(S_bgTitle));
    if (dirty) {
        int tw = cv_text_w(&CV, f_title, T(S_bgTitle));
        cv_circle(&CV, (float)(x + tw + D(12)), (float)(yy + D(10)), (float)D(4), C_AMBER);
        cv_text(&CV, f_small, T(S_bgUnsaved), x + tw + D(22), yy, D(200), D(20), C_AMBER, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    yy += D(28);
    int ih = text_h(f_small, T(S_bgIntro), w);
    cv_text(&CV, f_small, T(S_bgIntro), x, yy, w, ih, C_DIM, DT_LEFT | DT_WORDBREAK);
    yy += ih + D(14);
    //  미리보기 (비율 = 지금 창 비율 — 끌어서 맞춘 자리가 실제 창에서도 같은 자리)
    RECT cr;
    GetClientRect(W, &cr);
    int pw = w * 46 / 100 > D(440) ? D(440) : w * 46 / 100;
    if (pw < D(220)) pw = D(220);
    int ph = cr.right > 0 ? pw * cr.bottom / cr.right : pw * 3 / 4;
    int pvx = x, pvy = yy;
    BG_PREV_R.left = pvx; BG_PREV_R.top = pvy; BG_PREV_R.right = pvx + pw; BG_PREV_R.bottom = pvy + ph;
    cv_round(&CV, (float)pvx, (float)pvy, (float)pw, (float)ph, (float)D(8), 0x000000, 1);
    const bg_img *g = BGD_TOKEN[0] ? &BGI_D : &BGI_S;
    if (has && g->px) {
        uint32_t *buf = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)pw * ph);
        if (buf) {
            //  미리보기는 창보다 작다 — 흐림도 같은 비율로 (paintPreview)
            float ratio = cr.right > 0 ? fminf(1, (float)pw / cr.right) : 0.4f;
            bg_set d = BGD;
            d.dim = 0; d.opacity = 100;
            bg_compose(&d, g, pw, ph, S * ratio, buf);
            float k = BGD.opacity / 100.0f;   //  미리보기: 불투명도 → 그 위 어둡게 층 (꺼 둬도 보인다)
            float dimk = 1 - BGD.dim / 100.0f;
            for (int py = 0; py < ph; py++) {
                if (pvy + py < CV.clip.top || pvy + py >= CV.clip.bottom) continue;
                for (int px = 0; px < pw; px++) {
                    if (pvx + px < CV.clip.left || pvx + px >= CV.clip.right) continue;
                    float m = 1;
                    uint32_t p = buf[py * pw + px];
                    uint32_t r = (uint32_t)(((p >> 16) & 255) * k * dimk), gg = (uint32_t)(((p >> 8) & 255) * k * dimk), b2 = (uint32_t)((p & 255) * k * dimk);
                    (void)m;
                    CV.px[(pvy + py) * CV.w + pvx + px] = r << 16 | gg << 8 | b2;
                }
            }
            free(buf);
        }
        //  보기 카드 (TOKU RPC · 글 · 진행 막대)
        int sw = pw * 45 / 100, sh = D(56), sx = pvx + D(12), sy = pvy + ph - sh - D(12);
        cv_round(&CV, (float)sx, (float)sy, (float)sw, (float)sh, (float)D(6), C_CARD, 0.86f);
        cv_text(&CV, f_title, "TOKU RPC", sx + D(10), sy + D(6), sw - D(20), D(18), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cv_text(&CV, f_small, T(S_bgSample), sx + D(10), sy + D(24), sw - D(20), D(16), C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        cv_round(&CV, (float)(sx + D(10)), (float)(sy + D(44)), (float)((sw - D(20)) * 0.6f), (float)D(4), (float)D(2), C_ACC, 1);
    } else cv_text(&CV, f_small, T(S_bgNoImage), pvx, pvy, pw, ph, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    cv_round_border(&CV, (float)pvx, (float)pvy, (float)pw, (float)ph, (float)D(8), S > 1 ? S : 1, C_BORDER);
    if (bg_can_edit()) add_hit(BG_ID(BA_PREVIEW), pvx, pvy, pw, ph);
    //  오른쪽: 파일 · 고르기 · 복원 · 알림
    int rx = pvx + pw + D(18), rw = x + w - rx;
    if (rw < D(160)) { rx = x; rw = w; yy += ph + D(14); } else yy += 0;
    int ry = rx == x ? yy : pvy;
    if (has) {
        cv_text(&CV, f_bold, BGD.name, rx, ry, rw, D(22), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (BGD.width) {
            char a1[16], a2[16], t1[64], t2[64];
            snprintf(a1, sizeof a1, "%d", BGD.width); snprintf(a2, sizeof a2, "%d", BGD.height);
            fill(t1, sizeof t1, T(S_bgMeta), "{w}", a1);
            fill(t2, sizeof t2, t1, "{h}", a2);
            cv_text(&CV, f_small, t2, rx, ry + D(22), rw, D(18), C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        ry += D(48);
    }
    int bw1 = kb_btn(BG_BUSY ? ID_NONE : BG_ID(BA_PICK), rx, ry, D(32), T(has ? S_bgChange : S_bgPick), C_ACCHI, BG_BUSY);
    bg_set sv = BGS;
    sv.enabled = BGS.enabled && BGS_URL;
    int saved_default = !BGS_URL && !sv.enabled && !strcmp(sv.fit, "cover") && sv.posX == 500 && sv.posY == 500 && sv.zoom == 100 && sv.opacity == 100 && sv.dim == 60 && sv.blur == 0;
    kb_btn(BG_ID(BA_RESET), rx + bw1 + D(8), ry, D(32), T(S_bgReset), C_TEXT2, BG_BUSY || (saved_default && !BGD_TOKEN[0]));
    ry += D(42);
    if (BG_NOTICE) {
        int nh = text_h(f_small, T(BG_NOTICE), rw);
        cv_text(&CV, f_small, T(BG_NOTICE), rx, ry, rw, nh, BG_NOTICE_ERR ? C_RED : C_GREEN, DT_LEFT | DT_WORDBREAK);
        ry += nh;
    }
    yy = (rx == x ? ry : pvy + ph) + D(18);
    if (rx != x && ry > yy - D(18)) yy = ry + D(18);
    //  옵션
    int rh = D(56), dis = BG_BUSY || !has;
    int ch2 = rh * 7;
    card(x, yy, w, ch2);
    int cx = row(x, yy, w, rh, T(S_bgEnable), T(S_bgEnableHint), 0, D(46));
    w_switch(dis ? ID_NONE : BG_ID(BA_ENABLE), cx, yy + D(15), BGD.enabled);
    cx = row(x, yy + rh, w, rh, T(S_bgFit), NULL, 0, D(200));
    w_select(dis ? ID_NONE : BG_ID(BA_FIT), cx, yy + rh + D(11), D(200), !strcmp(BGD.fit, "contain") ? T(S_bgFitContain) : T(S_bgFitCover));
    int cw3 = cv_text_w(&CV, f_small, T(S_bgCenter)) + D(22);
    cx = row(x, yy + 2 * rh, w, rh, T(S_bgPosition), T(S_bgDragHint), 0, cw3);
    kb_btn(BG_ID(BA_CENTER), cx, yy + 2 * rh + D(12), D(32), T(S_bgCenter), C_TEXT2, dis || (BGD.posX == 500 && BGD.posY == 500 && BGD.zoom == 100));
    struct { int s, k, v, lo, hi; const char *unit; } SL[4] = { { S_bgZoom, BA_ZOOM, BGD.zoom, 100, 400, "%" }, { S_bgOpacity, BA_OPACITY, BGD.opacity, 0, 100, "%" },
        { S_bgDim, BA_DIM, BGD.dim, 0, 90, "%" }, { S_bgBlur, BA_BLUR, BGD.blur, 0, 20, "px" } };
    for (int i = 0; i < 4; i++) {
        int sy2 = yy + (3 + i) * rh;
        int slw = w / 2 > D(320) ? D(320) : w / 2;
        cx = row(x, sy2, w, rh, T(SL[i].s), NULL, i == 3, slw + D(60));
        w_slider(dis ? ID_NONE : BG_ID(SL[i].k), cx, sy2 + D(18), slw, SL[i].v, SL[i].lo, SL[i].hi, dis);
        char vt[16];
        snprintf(vt, sizeof vt, "%d%s", SL[i].v, SL[i].unit);
        cv_text(&CV, f_small, vt, cx + slw + D(12), sy2, D(48), rh, C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    yy += ch2 + D(14);
    //  아래: 취소 · 적용
    int aw = cv_text_w(&CV, f_body, T(S_bgApply)) + D(32), ax = x + w - aw;
    int can = !BG_BUSY && dirty;
    cv_round(&CV, (float)ax, (float)yy, (float)aw, (float)D(36), (float)D(8), can ? (hover == BG_ID(BA_APPLY) ? C_ACCHI : C_ACC) : C_THUMB, 1);
    cv_text(&CV, f_body, T(S_bgApply), ax, yy, aw, D(36), can ? 0xffffff : C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (can) add_hit(BG_ID(BA_APPLY), ax, yy, aw, D(36));
    int cw4 = cv_text_w(&CV, f_small, T(S_bgCancel)) + D(22);
    kb_btn(BG_ID(BA_CANCEL), ax - D(8) - cw4, yy + D(2), D(32), T(S_bgCancel), C_TEXT2, !can);
    yy += D(36) + D(14);
    return yy - y + D(6);
}

static void bg_click(int id) {
    switch (id - ID_BG0) {
    case BA_PICK: bg_do_pick(); break;
    case BA_RESET: bg_do_reset(); break;
    case BA_APPLY: bg_do_apply(); break;
    case BA_CANCEL: bg_do_cancel(); break;
    case BA_CENTER: bg_center_view(); break;
    case BA_ENABLE:
        if (!BGD_URL) { bg_notice(S_bgNeedImage, 1); break; }
        BGD.enabled = !BGD.enabled;
        break;
    case BA_FIT: {
        const char *L2[2] = { T(S_bgFitCover), T(S_bgFitContain) };
        int k = popup(id, L2, 2, !strcmp(BGD.fit, "contain"));
        if (k >= 0) snprintf(BGD.fit, sizeof BGD.fit, "%s", k ? "contain" : "cover");
        break;
    }
    }
    InvalidateRect(W, NULL, FALSE);
}
//  배경 탭 마우스 (미리보기 끌기 · 막대 끌기 · 휠 확대 · 두 번 눌러 가운데) — 1 이면 여기서 처리함
static int bg_mouse(UINT m, int x, int y, int wheel, int hid) {
    if (tab != 4) return 0;
    POINT pt = { x, y };
    if (m == WM_LBUTTONDOWN) {
        int k = hid - ID_BG0;
        if (hid == BG_ID(BA_PREVIEW) && bg_can_edit()) {
            double rx, ry;
            if (!bg_drag_rate(&rx, &ry)) return 0;
            BG_DRAG.on = 1; BG_DRAG.kind = BA_PREVIEW; BG_DRAG.x0 = x; BG_DRAG.y0 = y; BG_DRAG.px = BGD.posX; BG_DRAG.py = BGD.posY; BG_DRAG.rx = rx; BG_DRAG.ry = ry;
            SetCapture(W);
            return 1;
        }
        if (hid >= BG_ID(BA_ZOOM) && hid <= BG_ID(BA_BLUR) && bg_can_edit()) {
            for (int i = 0; i < nhits; i++) if (hits[i].id == hid) BG_DRAG.r = hits[i].r;
            BG_DRAG.on = 1; BG_DRAG.kind = k;
            BG_DRAG.lo = k == BA_ZOOM ? 100 : 0; BG_DRAG.hi = k == BA_ZOOM ? 400 : k == BA_OPACITY ? 100 : k == BA_DIM ? 90 : 20; BG_DRAG.step = k == BA_ZOOM ? 5 : 1;
            SetCapture(W);
            m = WM_MOUSEMOVE;   //  누른 자리 값으로 바로
        } else return 0;
    }
    if (m == WM_MOUSEMOVE && BG_DRAG.on) {
        if (!bg_can_edit()) { BG_DRAG.on = 0; ReleaseCapture(); return 1; }
        if (BG_DRAG.kind == BA_PREVIEW) {
            //  시작점 기준으로 (조금씩 더하면 반올림 오차가 쌓인다)
            BGD.posX = bg_clampi(BG_DRAG.px + (x - BG_DRAG.x0) * BG_DRAG.rx, 0, 1000, BGD.posX);
            BGD.posY = bg_clampi(BG_DRAG.py + (y - BG_DRAG.y0) * BG_DRAG.ry, 0, 1000, BGD.posY);
        } else {
            int x0 = BG_DRAG.r.left + D(6), x1 = BG_DRAG.r.right - D(6);
            double t = x1 > x0 ? (double)(x - x0) / (x1 - x0) : 0;
            t = fmin(1, fmax(0, t));
            double v = BG_DRAG.lo + t * (BG_DRAG.hi - BG_DRAG.lo);
            v = js_round(v / BG_DRAG.step) * BG_DRAG.step;   //  input range step
            int iv = bg_clampi(v, BG_DRAG.lo, BG_DRAG.hi, 0);
            if (BG_DRAG.kind == BA_ZOOM) BGD.zoom = iv; else if (BG_DRAG.kind == BA_OPACITY) BGD.opacity = iv; else if (BG_DRAG.kind == BA_DIM) BGD.dim = iv; else BGD.blur = iv;
        }
        InvalidateRect(W, NULL, FALSE);
        return 1;
    }
    if (m == WM_LBUTTONUP && BG_DRAG.on) { BG_DRAG.on = 0; ReleaseCapture(); return 1; }
    if (m == WM_LBUTTONDBLCLK && PtInRect(&BG_PREV_R, pt)) { bg_center_view(); InvalidateRect(W, NULL, FALSE); return 1; }
    if (m == WM_MOUSEWHEEL && PtInRect(&BG_PREV_R, pt) && bg_can_edit()) {
        if (wheel) bg_set_zoom(BGD.zoom + (wheel > 0 ? 10 : -10));
        InvalidateRect(W, NULL, FALSE);
        return 1;
    }
    return 0;
}
static int bg_key(UINT vk) {
    if (tab != 4 || !bg_can_edit() || FE.h || AV.open) return 0;
    int step = GetKeyState(VK_SHIFT) < 0 ? 100 : 10;
    if (vk == VK_LEFT) BGD.posX = bg_clampi(BGD.posX - step, 0, 1000, BGD.posX);
    else if (vk == VK_RIGHT) BGD.posX = bg_clampi(BGD.posX + step, 0, 1000, BGD.posX);
    else if (vk == VK_UP) BGD.posY = bg_clampi(BGD.posY - step, 0, 1000, BGD.posY);
    else if (vk == VK_DOWN) BGD.posY = bg_clampi(BGD.posY + step, 0, 1000, BGD.posY);
    else if (vk == VK_OEM_PLUS || vk == VK_ADD) bg_set_zoom(BGD.zoom + 10);
    else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) bg_set_zoom(BGD.zoom - 10);
    else if (vk == '0' || vk == VK_HOME) bg_center_view();
    else return 0;
    InvalidateRect(W, NULL, FALSE);
    return 1;
}

static int page_soon(int x, int y, int w) {
    int h = D(140);
    card(x, y, w, h);
    cv_text(&CV, f_bold, T(S_NA_soon), x, y + D(44), w, D(24), C_TEXT2, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cv_text(&CV, f_small, T(S_NA_soonHint), x + D(20), y + D(76), w - D(40), D(40), C_DIM, DT_CENTER | DT_WORDBREAK);
    return h + D(20);
}

// ════════════════════════════════════════
//  보충 탭 (rpc-window.js bf*) — 모바일로 본 화를 정주행 웹훅에 나중에 남긴다
//  링크 → 확장이 작품·화·썸네일을 읽어 옴 → 고칠 수 있는 칸 + 미리보기 → 본 시각을 정해 보내기
//  직접 입력(11/8): TTFC·IMAGINATION 에 없는 DVD 특전·극장 한정 영상 등 — 작품 폴더 고르기·화·시각·출처·이미지(링크/파일)
//  ⚠ 본 시각은 글 칸 (YYYY-MM-DD HH:MM) — 지금 앱은 datetime-local 고르기
// ════════════════════════════════════════
#define ID_BF0 70000
#define BF_ID(k) (ID_BF0 + (k))
enum { BA_URL = 1, BA_LOAD, BA_SERIES, BA_EPNUM, BA_EPTITLE, BA_WHEN, BA_NOW, BA_SEND,
       BA_MODE_LINK, BA_MODE_MANUAL,
       BA_M_PICK, BA_M_SERIES, BA_M_EPNUM, BA_M_EPTITLE, BA_M_WHEN, BA_M_NOW, BA_M_SRC, BA_M_IMG, BA_M_FILE, BA_M_CLEAR, BA_M_SEND };
static int BF_MODE;     // 0 링크로 불러오기 · 1 직접 입력
static int BF_TAGS;     // 보내기 표 (두 모드가 같이 센다 — 결과가 어느 쪽 것인지 가린다)
static struct {
    char *url, *last_url;
    int loading, sending, tag;
    int has;
    char token[17], site[16];
    char *series, *epno, *eptitle, *when;
    int accent;                      // -1 = 없음 → 사이트 색
    uint32_t *thumb; int tw, th;
    int tg_has, tg_hook, tg_chars; char *tg_folder;
    char msg[1200], smsg[1200];
    int msg_cls, smsg_cls;           // 0 보통 · 1 good · 2 err
} BF = { .accent = -1 };
//  직접 입력 (TTFC·IMAGINATION 에 없는 DVD 특전·극장 한정 영상 등) — 링크 모드와 칸을 나눈다
static struct {
    char *series, *epno, *eptitle, *when, *src, *img;
    unsigned char *fbuf; size_t fn; char ftype[32]; char fname[260];   // 고른 그림 파일
    uint32_t *fpx; int fw, fh; int faccent;                             // 그 미리보기 · 카드 색 (-1 = 없음)
    int sending, tag;
    int tg_has, tg_hook, tg_chars; char *tg_folder;
    char smsg[1200]; int smsg_cls;
} MN = { .faccent = -1 };

static void bf_say(int send_line, const char *text, int cls) {
    if (send_line) { snprintf(BF.smsg, sizeof BF.smsg, "%s", text ? text : ""); BF.smsg_cls = cls; }
    else { snprintf(BF.msg, sizeof BF.msg, "%s", text ? text : ""); BF.msg_cls = cls; }
}
static void mn_say(const char *text, int cls) { snprintf(MN.smsg, sizeof MN.smsg, "%s", text ? text : ""); MN.smsg_cls = cls; }
static int bf_err(const char *e) {   //  BF_ERR (+ 직접 입력의 이미지 링크)
    static const char *const K[] = { "noext", "oldext", "badurl", "blocked", "empty", "fetch", "stale", "series", "when", "nohook", "imgurl" };
    static const int V[] = { S_bfErrNoext, S_bfErrOldext, S_bfErrBadurl, S_bfErrBlocked, S_bfErrEmpty, S_bfErrFetch, S_bfErrStale, S_bfErrSeries, S_bfErrWhen, S_bfErrNohook, S_NA_bfErrImgUrl };
    for (int i = 0; i < 11; i++) if (e && !strcmp(e, K[i])) return V[i];
    return -1;
}
//  칸 글 (앞뒤 빈칸 뗌 — bfFields)
static char *bf_trim(const char *s) {
    s = s ? s : "";
    size_t a, b;
    js_trim(s, strlen(s), &a, &b);
    char *o = (char *)malloc(b - a + 1);
    if (o) { memcpy(o, s + a, b - a); o[b - a] = 0; }
    return o;
}
static void bf_clear(void) {
    BF.has = 0;
    BF.token[0] = BF.site[0] = 0;
    set_str(&BF.series, ""); set_str(&BF.epno, ""); set_str(&BF.eptitle, ""); set_str(&BF.when, "");
    free(BF.thumb); BF.thumb = NULL; BF.tw = BF.th = 0;
    BF.accent = -1;
    BF.tg_has = 0;
}
//  bfRefreshTarget — 작품명을 고치면 보낼 폴더가 바뀔 수 있다
static void bf_target_of(const char *series, int *has, int *hook, int *chars, char **folder) {
    char *sn = bf_trim(series);
    free(*folder);
    *folder = NULL;
    app_bf_target(sn ? sn : "", folder, hook, chars);
    *has = 1;
    free(sn);
}
static void bf_refresh_target(void) { bf_target_of(BF.series, &BF.tg_has, &BF.tg_hook, &BF.tg_chars, &BF.tg_folder); }
static void mn_refresh_target(void) { bf_target_of(MN.series, &MN.tg_has, &MN.tg_hook, &MN.tg_chars, &MN.tg_folder); }
static void bf_load(void) {
    char *url = bf_trim(BF.url);
    if (!url || !*url || BF.loading) { free(url); return; }
    BF.loading = 1;
    set_str(&BF.last_url, url);
    bf_clear();   //  앞 링크의 결과는 지운다 — 새 링크를 못 읽었을 때 옛 내용이 그 링크 것처럼 남지 않게
    bf_say(0, T(S_bfLoading), 0);
    bf_say(1, "", 0);
    const char *e = app_bf_resolve(url);
    free(url);
    if (e) { BF.loading = 0; int k = bf_err(e); bf_say(0, T(k >= 0 ? k : S_bfErrFetch), 2); }
    if (W) InvalidateRect(W, NULL, FALSE);
}
static void bf_now_into(char **dst) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char v[40];
    bf_fmt_when(v, sizeof v, &t);
    set_str(dst, v);
}
static void bf_now(void) { bf_now_into(&BF.when); }
static void mn_file_clear(void) {
    free(MN.fbuf); MN.fbuf = NULL; MN.fn = 0; MN.ftype[0] = 0; MN.fname[0] = 0;
    free(MN.fpx); MN.fpx = NULL; MN.fw = MN.fh = 0; MN.faccent = -1;
}
static void bf_field_done(const char *key, const char *text, int enter, int changed);
static void bf_field_live(const char *key, const char *text) {
    if (!strcmp(key, "bf:url")) {
        set_str(&BF.url, text);
        //  링크를 붙여 넣으면 바로 불러온다 (같은 링크를 또 읽지는 않는다)
        char *v = bf_trim(text);
        if (v && bf_link(v, strlen(v)) && strcmp(v, str0(BF.last_url))) bf_load();
        free(v);
    } else if (!strcmp(key, "bf:series")) { set_str(&BF.series, text); bf_refresh_target(); }
    else if (!strcmp(key, "bf:epno")) set_str(&BF.epno, text);
    else if (!strcmp(key, "bf:eptitle")) set_str(&BF.eptitle, text);
    else if (!strcmp(key, "bf:when")) set_str(&BF.when, text);
    else if (!strcmp(key, "mn:series")) { set_str(&MN.series, text); mn_refresh_target(); }
    else if (!strcmp(key, "mn:epno")) set_str(&MN.epno, text);
    else if (!strcmp(key, "mn:eptitle")) set_str(&MN.eptitle, text);
    else if (!strcmp(key, "mn:when")) set_str(&MN.when, text);
    else if (!strcmp(key, "mn:src")) set_str(&MN.src, text);
    else if (!strcmp(key, "mn:img")) { set_str(&MN.img, text); if (*text) mn_file_clear(); }   //  링크를 적으면 고른 파일은 뺀다 (둘 중 하나)
}
static void bf_field_done(const char *key, const char *text, int enter, int changed) {
    (void)changed;
    bf_field_live(key, text);
    if (!strcmp(key, "bf:url") && enter) bf_load();
}
static void bf_edit(int k) {
    switch (k) {
    case BA_URL: fe_start_live("bf:url", str0(BF.url), 0, bf_field_done, bf_field_live); break;
    case BA_SERIES: fe_start_live("bf:series", str0(BF.series), 200, bf_field_done, bf_field_live); break;
    case BA_EPNUM: fe_start_live("bf:epno", str0(BF.epno), 60, bf_field_done, bf_field_live); break;
    case BA_EPTITLE: fe_start_live("bf:eptitle", str0(BF.eptitle), 200, bf_field_done, bf_field_live); break;
    case BA_WHEN: fe_start_live("bf:when", str0(BF.when), 40, bf_field_done, bf_field_live); break;
    case BA_M_SERIES: fe_start_live("mn:series", str0(MN.series), 200, bf_field_done, bf_field_live); break;
    case BA_M_EPNUM: fe_start_live("mn:epno", str0(MN.epno), 60, bf_field_done, bf_field_live); break;
    case BA_M_EPTITLE: fe_start_live("mn:eptitle", str0(MN.eptitle), 200, bf_field_done, bf_field_live); break;
    case BA_M_WHEN: fe_start_live("mn:when", str0(MN.when), 40, bf_field_done, bf_field_live); break;
    case BA_M_SRC: fe_start_live("mn:src", str0(MN.src), 80, bf_field_done, bf_field_live); break;
    case BA_M_IMG: fe_start_live("mn:img", str0(MN.img), 2000, bf_field_done, bf_field_live); break;
    }
}
static int bf_when(SYSTEMTIME *t) { return BF.when && bf_parse_when(BF.when, t); }
static int mn_when(SYSTEMTIME *t) { return MN.when && bf_parse_when(MN.when, t); }
static int bf_can_send(void) {
    SYSTEMTIME t;
    char *sn = bf_trim(BF.series);
    int ok = !BF.sending && sn && *sn && bf_when(&t) && BF.tg_has && BF.tg_hook;
    free(sn);
    return ok;
}
static int mn_can_send(void) {
    SYSTEMTIME t;
    char *sn = bf_trim(MN.series), *im = bf_trim(MN.img);
    int ok = !MN.sending && sn && *sn && mn_when(&t) && MN.tg_has && MN.tg_hook && (MN.fbuf || app_bf_img_url_ok(im ? im : ""));
    free(sn); free(im);
    return ok;
}
static void bf_fail_text(char *m, size_t n, const char *e) {
    int k = bf_err(e);
    if (k >= 0) snprintf(m, n, "%s", T(k));
    else fill(m, n, T(S_bfFail), "{status}", "\xE2\x80\x94");
}
static void bf_send(void) {
    if (!BF.has || BF.sending) return;
    SYSTEMTIME w;
    if (!bf_when(&w)) { bf_say(1, T(S_bfErrWhen), 2); return; }
    BF.sending = 1;
    BF.tag = ++BF_TAGS;
    bf_say(1, T(S_bfSending), 0);
    char *sn = bf_trim(BF.series), *en = bf_trim(BF.epno), *et = bf_trim(BF.eptitle);
    const char *e = app_bf_send(BF.token, str0(sn), str0(en), str0(et), &w, BF.tag);
    free(sn); free(en); free(et);
    if (e) { BF.sending = 0; char m[600]; bf_fail_text(m, sizeof m, e); bf_say(1, m, 2); }
}
static void mn_send(void) {
    if (MN.sending) return;
    SYSTEMTIME w;
    if (!mn_when(&w)) { mn_say(T(S_bfErrWhen), 2); return; }
    MN.sending = 1;
    MN.tag = ++BF_TAGS;
    mn_say(T(S_bfSending), 0);
    char *sn = bf_trim(MN.series), *en = bf_trim(MN.epno), *et = bf_trim(MN.eptitle), *src = bf_trim(MN.src), *im = bf_trim(MN.img);
    const char *e = app_bf_send_manual(str0(sn), str0(en), str0(et), str0(src), MN.fbuf ? "" : str0(im), MN.fbuf, MN.fn, MN.ftype, &w, MN.tag);
    free(sn); free(en); free(et); free(src); free(im);
    if (e) { MN.sending = 0; char m[600]; bf_fail_text(m, sizeof m, e); mn_say(m, 2); }
}
void win_bf_resolved(const jval *r) {
    BF.loading = 0;
    if (!json_get_bool(r, "ok", 0)) {
        int k = bf_err(json_get_str(r, "error", ""));
        bf_say(0, T(k >= 0 ? k : S_bfErrFetch), 2);
        if (W) InvalidateRect(W, NULL, FALSE);
        return;
    }
    bf_clear();
    BF.has = 1;
    snprintf(BF.token, sizeof BF.token, "%s", json_get_str(r, "token", ""));
    snprintf(BF.site, sizeof BF.site, "%s", json_get_str(r, "site", ""));
    set_str(&BF.series, json_get_str(r, "seriesName", ""));
    set_str(&BF.epno, json_get_str(r, "episodeNumber", ""));
    set_str(&BF.eptitle, json_get_str(r, "episodeTitle", ""));
    const jval *ac = json_get(r, "accent");
    BF.accent = (ac && ac->t == J_NUM && ac->n >= 0 && ac->n <= 0xffffff && ac->n == floor(ac->n)) ? (int)ac->n : -1;
    size_t tn;
    const unsigned char *tb = app_bf_thumb(BF.token, &tn);
    if (tb) { int ow, oh; BF.thumb = iu_decode(tb, tn, 320, &BF.tw, &BF.th, &ow, &oh); }
    free(BF.tg_folder);
    BF.tg_folder = _strdup(json_get_str(r, "folder", ""));
    BF.tg_hook = json_get_bool(r, "hasHook", 0);
    BF.tg_chars = (int)json_get_num(r, "chars", 0);
    BF.tg_has = 1;
    bf_now();   //  처음엔 지금 시각 — 본 시각으로 고친다
    bf_say(0, T(S_bfLoaded), 1);
    if (W && IsWindowVisible(W) && tab == 6 && BF_MODE == 0) {
        InvalidateRect(W, NULL, FALSE);
        bf_edit(*BF.series ? BA_WHEN : BA_SERIES);
    }
    if (W) InvalidateRect(W, NULL, FALSE);
}
static void bf_sent_name(char *out, size_t n, const char *series, const char *epno) {
    char *sn = bf_trim(series), *en = bf_trim(epno);
    char name[900];
    snprintf(name, sizeof name, "%s%s%s", str0(sn), (sn && *sn && en && *en) ? " " : "", str0(en));
    fill(out, n, T(S_bfSent), "{name}", name);
    free(sn); free(en);
}
void win_bf_sent(int tag, int ok, int status) {
    char st[16], m[1100];
    if (status) snprintf(st, sizeof st, "%d", status); else snprintf(st, sizeof st, "%s", "\xE2\x80\x94");
    if (tag == BF.tag && BF.sending) {
        BF.sending = 0;
        if (ok) {
            //  한 링크는 한 번 — 다음 화 링크를 바로 넣을 수 있게 비운다
            bf_sent_name(m, sizeof m, BF.series, BF.epno);
            bf_say(1, "", 0);
            bf_say(0, m, 1);
            bf_clear();
            set_str(&BF.url, "");
            set_str(&BF.last_url, "");
            if (W && IsWindowVisible(W) && tab == 6 && BF_MODE == 0) bf_edit(BA_URL);
        } else { fill(m, sizeof m, T(S_bfFail), "{status}", st); bf_say(1, m, 2); }
    } else if (tag == MN.tag && MN.sending) {
        MN.sending = 0;
        if (ok) {
            //  같은 작품의 다음 특전을 바로 적을 수 있게 작품명·출처는 남기고 화·그림만 비운다
            bf_sent_name(m, sizeof m, MN.series, MN.epno);
            mn_say(m, 1);
            set_str(&MN.epno, ""); set_str(&MN.eptitle, ""); set_str(&MN.img, "");
            mn_file_clear();
            bf_now_into(&MN.when);
        } else { fill(m, sizeof m, T(S_bfFail), "{status}", st); mn_say(m, 2); }
    }
    if (W) InvalidateRect(W, NULL, FALSE);
}
static int popup(int id, const char *const *labels, int n, int cur);
//  작품 고르기 — 웹훅 탭에 만든 작품 폴더 (넣은 순서, 같은 작품은 한 번)
static void mn_pick_title(void) {
    hk_item *l;
    int n = hk_list(&l);
    const char *labels[64];
    int k = 0;
    for (int i = 0; i < n && k < 64; i++) {
        int dup = 0;
        for (int j = 0; j < k; j++) if (!strcmp(labels[j], l[i].title)) dup = 1;
        if (!dup) labels[k++] = l[i].title;
    }
    if (!k) { mn_say(T(S_NA_bfNoFolders), 2); hk_list_free(l, n); return; }
    int cur = -1;
    for (int j = 0; j < k; j++) if (MN.series && !strcmp(MN.series, labels[j])) cur = j;
    int pick = popup(BF_ID(BA_M_PICK), labels, k, cur);
    if (pick >= 0) { set_str(&MN.series, labels[pick]); mn_refresh_target(); mn_say("", 0); }
    hk_list_free(l, n);
}
//  그림 파일 고르기 — Discord 첨부가 되는 png·jpeg·gif·webp, 8MB 까지
static void mn_pick_file(void) {
    unsigned char *buf = NULL;
    size_t n = 0;
    const char *mime = NULL;
    int r = pick_image(S_NA_bfImage, &buf, &n, &mime);
    if (r == 0) return;
    int okfmt = r == 1 && mime && (!strcmp(mime, "image/png") || !strcmp(mime, "image/jpeg") || !strcmp(mime, "image/gif") || !strcmp(mime, "image/webp"));
    if (!okfmt || n > 8 * 1024 * 1024) { free(buf); mn_say(T(S_NA_bfErrImgFile), 2); return; }
    int w2, h2, ow, oh;
    uint32_t *px = iu_decode(buf, n, 320, &w2, &h2, &ow, &oh);
    if (!px) { free(buf); mn_say(T(S_NA_bfErrImgFile), 2); return; }
    mn_file_clear();
    MN.fbuf = buf; MN.fn = n;
    snprintf(MN.ftype, sizeof MN.ftype, "%s", mime);
    MN.fpx = px; MN.fw = w2; MN.fh = h2;
    int c = 0;
    if (accent_from_buffer(buf, n, &c)) MN.faccent = c;
    set_str(&MN.img, "");
    snprintf(MN.fname, sizeof MN.fname, "%s %zuKB", mime + 6, (n + 1023) / 1024);
    mn_say("", 0);
}
static void bf_set_mode(int m) {
    fe_finish();
    BF_MODE = m;
    if (m == 1) { if (!MN.when || !*MN.when) bf_now_into(&MN.when); mn_refresh_target(); }
}
static void bf_click(int id) {
    int k = id - ID_BF0;
    if (k == BA_LOAD) { fe_finish(); bf_load(); }
    else if (k == BA_NOW) { if (fe_active("bf:when")) fe_finish(); bf_now(); }
    else if (k == BA_SEND) { fe_finish(); if (bf_can_send()) bf_send(); }
    else if (k == BA_MODE_LINK) bf_set_mode(0);
    else if (k == BA_MODE_MANUAL) bf_set_mode(1);
    else if (k == BA_M_PICK) { fe_finish(); mn_pick_title(); }
    else if (k == BA_M_NOW) { if (fe_active("mn:when")) fe_finish(); bf_now_into(&MN.when); }
    else if (k == BA_M_FILE) { fe_finish(); mn_pick_file(); }
    else if (k == BA_M_CLEAR) { fe_finish(); mn_file_clear(); set_str(&MN.img, ""); }
    else if (k == BA_M_SEND) { fe_finish(); if (mn_can_send()) mn_send(); }
    else bf_edit(k);
}
//  시험 모드 그림용: 직접 입력 칸을 채운다 (main.c TEST_BF_FILL)
void win_bf_test_fill(const jval *m) {
    BF_MODE = 1;
    set_str(&MN.series, json_get_str(m, "seriesName", ""));
    set_str(&MN.epno, json_get_str(m, "episodeNumber", ""));
    set_str(&MN.eptitle, json_get_str(m, "episodeTitle", ""));
    set_str(&MN.src, json_get_str(m, "source", ""));
    set_str(&MN.img, json_get_str(m, "imageUrl", ""));
    set_str(&MN.when, json_get_str(m, "when", ""));
    mn_refresh_target();
}
//  주 버튼 (보라) · 작은 버튼
static int bf_primary(int id, int x, int y, int h, const char *label, int disabled) {
    int w = cv_text_w(&CV, f_body, label) + D(32);
    cv_round(&CV, (float)x, (float)y, (float)w, (float)h, (float)D(8), disabled ? C_TRACK : hover == id ? C_ACCHI : C_ACC, 1);
    cv_text(&CV, f_body, label, x, y, w, h, disabled ? C_DIM : 0xffffff, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!disabled) add_hit(id, x, y, w, h);
    return w;
}
static int bf_label(int x, int y, int w, const char *t) {
    cv_text(&CV, f_body, t, x, y, w, D(22), C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return D(26);
}
//  미리보기 카드 (Discord 웹훅 카드 모양) — 높이를 돌려준다. draw = 0 이면 재기만
//  그림: px(w×h, 가운데 잘라 채우기) 또는 sq(IMG_SIZE 정사각 — 링크 그림)
typedef struct { const char *site, *series, *epno, *eptitle, *when; uint32_t bar; const uint32_t *px; int pw, ph; const uint32_t *sq; } bf_view;
static int bf_card(int x, int y, int w, int draw, const bf_view *v) {
    char *sn = bf_trim(v->series), *en = bf_trim(v->epno), *et = bf_trim(v->eptitle);
    char ep[600], date[64];
    if (en && *en && et && *et) snprintf(ep, sizeof ep, "%s / %s", en, et);
    else snprintf(ep, sizeof ep, "%s", (en && *en) ? en : (et && *et) ? et : T(S_hook_episode));
    SYSTEMTIME t;
    if (v->when && bf_parse_when(v->when, &t)) bf_card_date(date, sizeof date, &t); else snprintf(date, sizeof date, "%s", "\xE2\x80\x94");
    const char *title = (sn && *sn) ? sn : "\xE2\x80\x94";
    int img = v->px || v->sq;
    int pad = D(12), tx = x + D(4) + pad, tw = w - D(4) - pad * 2 - (img ? D(80) + D(12) : 0);
    if (tw < D(40)) tw = D(40);
    int h1 = (v->site && *v->site) ? D(16) : 0, h2 = text_h(f_bf14b, title, tw), h3 = text_h(f_bf13b, ep, tw), h4 = D(18);
    int texth = (h1 ? h1 + D(3) : 0) + h2 + D(3) + h3 + D(3) + h4;
    int ch = (texth > (img ? D(80) : 0) ? texth : D(80)) + D(20);
    if (draw) {
        cv_round(&CV, (float)x, (float)y, (float)w, (float)ch, (float)D(6), 0x2b2d31, 1);
        cv_round(&CV, (float)x, (float)y, (float)D(8), (float)ch, (float)D(6), v->bar, 1);
        cv_fill(&CV, x + D(4), y, D(4), ch, 0x2b2d31);
        int ty = y + D(10);
        if (h1) { cv_text(&CV, f_bf12b, v->site, tx, ty, tw, h1, 0xf2f3f5, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); ty += h1 + D(3); }
        cv_text(&CV, f_bf14b, title, tx, ty, tw, h2, 0x00a8fc, DT_LEFT | DT_WORDBREAK);
        ty += h2 + D(3);
        cv_text(&CV, f_bf13b, ep, tx, ty, tw, h3, 0xf2f3f5, DT_LEFT | DT_WORDBREAK);
        ty += h3 + D(3);
        cv_text(&CV, f_bf13, date, tx, ty, tw, h4, 0xdbdee1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        int bx = x + w - pad - D(80), by = y + D(10), bs = D(80);
        if (v->px) {   //  80×80 object-fit: cover
            float sc = fmaxf((float)bs / v->pw, (float)bs / v->ph);
            cv_blit(&CV, v->px, v->pw, v->ph, bx + (bs - v->pw * sc) / 2, by + (bs - v->ph * sc) / 2, sc, bx, by, bs, bs, 0);
        } else if (v->sq) cv_image(&CV, v->sq, IMG_SIZE, (float)bx, (float)by, (float)bs, (float)D(4));
    }
    free(sn); free(en); free(et);
    return ch;
}
static void bf_target_text(char *tgt, size_t n, int *cls, int has, int hook, int chars, const char *folder) {
    tgt[0] = 0;
    *cls = 0;
    if (!has) return;
    if (!hook) { snprintf(tgt, n, "%s", T(S_bfNoHook)); *cls = 2; }
    else if (folder && *folder) {
        char nn[16], a[700];
        snprintf(nn, sizeof nn, "%d", chars);
        fill(a, sizeof a, T(chars ? S_bfToFolderChars : S_bfToFolder), "{folder}", folder);
        fill(tgt, n, a, "{n}", nn);
    } else snprintf(tgt, n, "%s", T(S_bfToDefault));
}
//  화 번호 0.6 : 화 제목 1.4 줄
static void bf_ep_row(int ix, int *ly, int lw, int id_no, int id_title, const char *k_no, const char *k_title, const char *no, const char *title) {
    int gw = lw - D(10), nw = gw * 6 / 20;
    if (nw < D(90)) nw = D(90);
    int tw2 = gw - nw;
    bf_label(ix, *ly, nw, T(S_bfEpNum));
    bf_label(ix + nw + D(10), *ly, tw2, T(S_bfEpTitle));
    *ly += D(26);
    w_field(id_no, k_no, ix, *ly, nw, D(36), no, NULL, 0, C_TEXT);
    w_field(id_title, k_title, ix + nw + D(10), *ly, tw2, D(36), title, NULL, 0, C_TEXT);
    *ly += D(36) + D(12);
}
//  본 시각 + 지금 버튼 + 안내
static void bf_when_row(int ix, int *ly, int lw, int id, int id_now, const char *key, const char *val, int wh) {
    *ly += bf_label(ix, *ly, lw, T(S_bfWhen));
    int nbw = cv_text_w(&CV, f_small, T(S_bfNow)) + D(22);
    SYSTEMTIME tt;
    int bad = val && *val && !bf_parse_when(val, &tt);
    w_field(id, key, ix, *ly, lw - nbw - D(8), D(36), str0(val), T(S_NA_bfWhenPh), 0, bad ? C_RED : C_TEXT);
    kb_btn(id_now, ix + lw - nbw, *ly, D(36), T(S_bfNow), C_TEXT2, 0);
    *ly += D(36) + D(6);
    cv_text(&CV, f_small, T(S_bfWhenHint), ix, *ly, lw, wh, C_DIM, DT_LEFT | DT_WORDBREAK);
    *ly += wh;
}
//  [링크로 불러오기 | 직접 입력]
static int bf_mode_bar(int x, int y) {
    const char *a = T(S_NA_bfModeLink), *b = T(S_NA_bfModeManual);
    int wa = cv_text_w(&CV, f_body, a) + D(32), wb = cv_text_w(&CV, f_body, b) + D(32), h = D(36);
    cv_round(&CV, (float)x, (float)y, (float)(wa + wb + D(8)), (float)h, (float)D(9), C_FIELD, BG_ON ? 0.86f : 1);
    cv_round_border(&CV, (float)x, (float)y, (float)(wa + wb + D(8)), (float)h, (float)D(9), S > 1 ? S : 1, C_BORDER);
    for (int i = 0; i < 2; i++) {
        int sx = x + D(4) + (i ? wa : 0), sw = i ? wb : wa, on = BF_MODE == i, id = BF_ID(i ? BA_MODE_MANUAL : BA_MODE_LINK);
        if (on) cv_round(&CV, (float)sx, (float)(y + D(4)), (float)sw, (float)(h - D(8)), (float)D(7), C_ACC_SOFT, 1);
        cv_text(&CV, f_body, i ? b : a, sx, y, sw, h, on ? C_ACCHI : (hover == id ? C_TEXT : C_DIM), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!on) add_hit(id, sx, y, sw, h);
    }
    return h;
}
static int page_manual(int x, int y, int w) {
    int yy = y;
    int iw = w - D(40), ix = x + D(20);
    int two = iw >= D(620);
    int rw = two ? D(300) : iw, lw = two ? iw - rw - D(18) : iw;
    int wh = text_h(f_small, T(S_bfWhenHint), lw);
    //  이미지 칸 아래 한 줄 (파일 이름 · 받는 중 · 링크 오류 · 그림 없음)
    char *im = bf_trim(MN.img);
    const uint32_t *sq = NULL;
    char istat[300] = "";
    int icls = 0;
    if (MN.fbuf) snprintf(istat, sizeof istat, "%s", MN.fname);
    else if (im && *im) {
        if (!app_bf_img_url_ok(im)) { snprintf(istat, sizeof istat, "%s", T(S_NA_bfErrImgUrl)); icls = 2; }
        else {
            int g = img_get(im, &sq);
            if (g != 1) sq = NULL;
            if (g == 0) snprintf(istat, sizeof istat, "%s", T(S_NA_bfImgLoading));
            else if (g < 0) { snprintf(istat, sizeof istat, "%s", T(S_NA_bfImgPreviewFail)); icls = 2; }
        }
    } else snprintf(istat, sizeof istat, "%s", T(S_NA_bfNoImage));
    int sh = text_h(f_small, istat, lw);
    int left_h = (D(26) + D(36)) * 5 + D(12) * 4 + D(6) + wh + D(6) + sh;
    char *src = bf_trim(MN.src);
    const char *site = !strcmp(str0(src), "ttfc") ? T(S_site_ttfc) : !strcmp(str0(src), "imagination") ? T(S_site_imagination) : str0(src);
    bf_view v = { site, MN.series, MN.epno, MN.eptitle, MN.when,
                  MN.faccent >= 0 ? (uint32_t)MN.faccent : !strcmp(str0(src), "imagination") ? 0xd32f5a : 0x2ea6ff,
                  MN.fpx, MN.fw, MN.fh, MN.fpx ? NULL : sq };
    int card_h = bf_card(0, 0, rw, 0, &v);
    int right_h = D(26) + card_h;
    int body_h = two ? (left_h > right_h ? left_h : right_h) : left_h + D(18) + right_h;
    char tgt[700];
    int tcls;
    bf_target_text(tgt, sizeof tgt, &tcls, MN.tg_has, MN.tg_hook, MN.tg_chars, MN.tg_folder);
    int th2 = tgt[0] ? text_h(f_small, tgt, iw) : 0;
    int ch2 = D(18) + body_h + D(12) + th2 + D(12) + D(36) + D(18);
    card(x, yy, w, ch2);
    int top = yy + D(18), ly = top;
    //  작품명 + 작품 고르기
    ly += bf_label(ix, ly, lw, T(S_bfSeries));
    {
        char pk[160];
        snprintf(pk, sizeof pk, "%s \xE2\x96\xBE", T(S_NA_bfPickTitle));
        int pbw = cv_text_w(&CV, f_small, pk) + D(22);
        w_field(BF_ID(BA_M_SERIES), "mn:series", ix, ly, lw - pbw - D(8), D(36), str0(MN.series), NULL, 0, C_TEXT);
        kb_btn(BF_ID(BA_M_PICK), ix + lw - pbw, ly, D(36), pk, C_ACCHI, 0);
        ly += D(36) + D(12);
    }
    bf_ep_row(ix, &ly, lw, BF_ID(BA_M_EPNUM), BF_ID(BA_M_EPTITLE), "mn:epno", "mn:eptitle", str0(MN.epno), str0(MN.eptitle));
    bf_when_row(ix, &ly, lw, BF_ID(BA_M_WHEN), BF_ID(BA_M_NOW), "mn:when", MN.when, wh);
    ly += D(12);
    ly += bf_label(ix, ly, lw, T(S_NA_bfSource));
    w_field(BF_ID(BA_M_SRC), "mn:src", ix, ly, lw, D(36), str0(MN.src), T(S_NA_bfSourcePh), 0, C_TEXT);
    ly += D(36) + D(12);
    ly += bf_label(ix, ly, lw, T(S_NA_bfImage));
    {
        int fbw = cv_text_w(&CV, f_small, T(S_NA_bfImageFile)) + D(22);
        int cbw = (MN.fbuf || (im && *im)) ? cv_text_w(&CV, f_small, T(S_NA_bfImageClear)) + D(22) : 0;
        int fw2 = lw - fbw - D(8) - (cbw ? cbw + D(8) : 0);
        w_field(BF_ID(BA_M_IMG), "mn:img", ix, ly, fw2, D(36), MN.fbuf ? "" : str0(MN.img), MN.fbuf ? MN.fname : T(S_NA_bfImagePh), 0, C_TEXT);
        kb_btn(BF_ID(BA_M_FILE), ix + fw2 + D(8), ly, D(36), T(S_NA_bfImageFile), C_TEXT2, 0);
        if (cbw) kb_btn(BF_ID(BA_M_CLEAR), ix + lw - cbw, ly, D(36), T(S_NA_bfImageClear), C_TEXT2, 0);
        ly += D(36) + D(6);
        cv_text(&CV, f_small, istat, ix, ly, lw, sh, icls == 2 ? C_RED : C_DIM, DT_LEFT | DT_WORDBREAK);
    }
    int rx = two ? ix + lw + D(18) : ix, ry = two ? top : top + left_h + D(18);
    ry += bf_label(rx, ry, rw, T(S_bfPreview));
    bf_card(rx, ry, rw, 1, &v);
    int by = top + body_h + D(12);
    if (tgt[0]) cv_text(&CV, f_small, tgt, ix, by, iw, th2, tcls == 2 ? C_RED : C_TEXT2, DT_LEFT | DT_WORDBREAK);
    by += th2 + D(12);
    {
        int bw = cv_text_w(&CV, f_body, T(S_bfSend)) + D(32);
        bf_primary(BF_ID(BA_M_SEND), ix + iw - bw, by, D(36), T(S_bfSend), !mn_can_send());
        if (MN.smsg[0]) cv_text(&CV, f_small, MN.smsg, ix, by, iw - bw - D(16), D(36), cls_color(MN.smsg_cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    free(im); free(src);
    yy += ch2 + D(16);
    return yy - y;
}
static int page_backfill(int x, int y, int w) {
    if (w > D(900)) w = D(900);
    int yy = y;
    section_title(x, yy, w, T(S_bfTitle));
    yy += D(28);
    const char *intro = BF_MODE ? T(S_NA_bfManualIntro) : T(S_bfIntro);
    int ih = text_h(f_small, intro, w);
    cv_text(&CV, f_small, intro, x, yy, w, ih, C_DIM, DT_LEFT | DT_WORDBREAK);
    yy += ih + D(12);
    yy += bf_mode_bar(x, yy) + D(14);
    if (BF_MODE == 1) return yy - y + page_manual(x, yy, w);
    //  카드 1: 링크 · 불러오기
    int iw = w - D(40), ix = x + D(20);
    int hh = text_h(f_small, T(S_bfUrlHint), iw);
    int ch = D(18) + D(26) + D(36) + D(6) + hh + D(12) + D(36) + D(18);
    card(x, yy, w, ch);
    int cy = yy + D(18);
    cy += bf_label(ix, cy, iw, T(S_bfUrl));
    w_field(BF_ID(BA_URL), "bf:url", ix, cy, iw, D(36), str0(BF.url), T(S_bfUrlPh), 0, C_TEXT);
    cy += D(36) + D(6);
    cv_text(&CV, f_small, T(S_bfUrlHint), ix, cy, iw, hh, C_DIM, DT_LEFT | DT_WORDBREAK);
    cy += hh + D(12);
    {
        int bw = cv_text_w(&CV, f_body, T(S_bfLoad)) + D(32);
        bf_primary(BF_ID(BA_LOAD), ix + iw - bw, cy, D(36), T(S_bfLoad), BF.loading);
        if (BF.msg[0]) cv_text(&CV, f_small, BF.msg, ix, cy, iw - bw - D(16), D(36), cls_color(BF.msg_cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    yy += ch + D(16);
    if (!BF.has) return yy - y;
    //  카드 2: 칸 + 미리보기 (넓으면 두 칸, 좁으면 위아래 — .bf-grid)
    int two = iw >= D(620);
    int rw = two ? D(300) : iw, lw = two ? iw - rw - D(18) : iw;
    int wh = text_h(f_small, T(S_bfWhenHint), lw);
    int left_h = (D(26) + D(36)) * 3 + D(12) * 2 + D(6) + wh;
    bf_view v = { T(!strcmp(BF.site, "imagination") ? S_site_imagination : S_site_ttfc), BF.series, BF.epno, BF.eptitle, BF.when,
                  BF.accent >= 0 ? (uint32_t)BF.accent : !strcmp(BF.site, "imagination") ? 0xd32f5a : 0x2ea6ff, BF.thumb, BF.tw, BF.th, NULL };
    int card_h = bf_card(0, 0, rw, 0, &v);
    int nothumb = !BF.thumb;
    int right_h = D(26) + card_h + (nothumb ? D(8) + text_h(f_small, T(S_bfNoThumb), rw) : 0);
    int body_h = two ? (left_h > right_h ? left_h : right_h) : left_h + D(18) + right_h;
    char tgt[700];
    int tcls;
    bf_target_text(tgt, sizeof tgt, &tcls, BF.tg_has, BF.tg_hook, BF.tg_chars, BF.tg_folder);
    int th2 = tgt[0] ? text_h(f_small, tgt, iw) : 0;
    int ch2 = D(18) + body_h + D(12) + th2 + D(12) + D(36) + D(18);
    card(x, yy, w, ch2);
    int top = yy + D(18), ly = top;
    ly += bf_label(ix, ly, lw, T(S_bfSeries));
    w_field(BF_ID(BA_SERIES), "bf:series", ix, ly, lw, D(36), str0(BF.series), NULL, 0, C_TEXT);
    ly += D(36) + D(12);
    bf_ep_row(ix, &ly, lw, BF_ID(BA_EPNUM), BF_ID(BA_EPTITLE), "bf:epno", "bf:eptitle", str0(BF.epno), str0(BF.eptitle));
    bf_when_row(ix, &ly, lw, BF_ID(BA_WHEN), BF_ID(BA_NOW), "bf:when", BF.when, wh);
    int rx = two ? ix + lw + D(18) : ix, ry = two ? top : top + left_h + D(18);
    ry += bf_label(rx, ry, rw, T(S_bfPreview));
    bf_card(rx, ry, rw, 1, &v);
    ry += card_h;
    if (nothumb) cv_text(&CV, f_small, T(S_bfNoThumb), rx, ry + D(8), rw, text_h(f_small, T(S_bfNoThumb), rw), C_DIM, DT_LEFT | DT_WORDBREAK);
    int by = top + body_h + D(12);
    if (tgt[0]) cv_text(&CV, f_small, tgt, ix, by, iw, th2, tcls == 2 ? C_RED : C_TEXT2, DT_LEFT | DT_WORDBREAK);
    by += th2 + D(12);
    {
        int bw = cv_text_w(&CV, f_body, T(S_bfSend)) + D(32);
        bf_primary(BF_ID(BA_SEND), ix + iw - bw, by, D(36), T(S_bfSend), !bf_can_send());
        if (BF.smsg[0]) cv_text(&CV, f_small, BF.smsg, ix, by, iw - bw - D(16), D(36), cls_color(BF.smsg_cls), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    yy += ch2 + D(16);
    return yy - y;
}

// ── 전체 그리기 ──
static void status_of(const char **hdr, const char **foot, uint32_t *dot) {
    int dc = core_discord_connected(app_core()), ext = app_ext_count();
    if (!app_rpc_on()) { *hdr = T(S_rpcOff); *foot = T(S_stOff); *dot = C_DIM; }
    else if (!app_port_ready()) { *hdr = T(S_hdrNoExt); *foot = T(S_NA_portWait); *dot = C_AMBER; }
    else if (!dc) { *hdr = T(S_hdrNoDiscord); *foot = T(S_stNoDiscord); *dot = C_RED; }
    else if (!ext) { *hdr = T(S_hdrNoExt); *foot = T(S_stNoExt); *dot = C_AMBER; }
    else { *hdr = T(S_hdrOk); *foot = T(S_stOk); *dot = C_GREEN; }
}

static void paint(HDC hdc) {
    RECT rc;
    GetClientRect(W, &rc);
    int w = rc.right, h = rc.bottom;
    cv_resize(&CV, hdc, w, h);
    nhits = 0;
    cv_clip(&CV, 0, 0, 0, 0);
    if (!BG_LOADED) bg_load_state();   //  창을 처음 그릴 때 배경 설정을 읽는다 (어느 탭이든)
    BG_ON = bg_paint_window(w, h);   //  배경 꾸미기 (꺼져 있으면 기본 바탕)
    if (!BG_ON) cv_fill(&CV, 0, 0, w, h, C_BG);
    //  머리줄
    int hh = D(52);
    if (BG_ON) cv_round(&CV, 0, 0, (float)w, (float)hh, 0, C_BAR, 0.86f); else cv_fill(&CV, 0, 0, w, hh, C_BAR);
    if (icon_small) DrawIconEx(CV.dc, D(18), (hh - D(22)) / 2, icon_small, D(22), D(22), 0, NULL, DI_NORMAL);
    cv_text(&CV, f_brand, "TOKU RPC", D(50), 0, D(120), hh, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int bw = cv_text_w(&CV, f_brand, "TOKU RPC");
    int pw = cv_text_w(&CV, f_small, T(CH_LABEL)) + D(14);
    cv_round(&CV, (float)(D(50) + bw + D(10)), (float)((hh - D(20)) / 2), (float)pw, (float)D(20), (float)D(10), C_ACC_SOFT, 1);
    cv_text(&CV, f_small, T(CH_LABEL), D(50) + bw + D(10), (hh - D(20)) / 2, pw, D(20), C_ACCHI, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const char *hdr, *foot;
    uint32_t dot;
    status_of(&hdr, &foot, &dot);
    //  창 버튼 (최소화·최대화·닫기) — Windows 제목 줄을 없애고 이 줄에 합쳤다. 글리프는 Windows 가 쓰는 것과 같은 아이콘 글꼴
    int cbw = D(46), cbx = w - 3 * cbw;
    static const int CB_ID[3] = { ID_MIN, ID_MAX, ID_CLOSE };
    const wchar_t *glyph[3] = { L"\xE921", IsZoomed(W) ? L"\xE923" : L"\xE922", L"\xE8BB" };
    for (int i = 0; i < 3; i++) {
        int bx = cbx + i * cbw, hot = hover == CB_ID[i], down = hot && press == CB_ID[i];
        if (hot) cv_fill(&CV, bx, 0, cbw, hh, i == 2 ? (down ? 0x94141f : 0xc42b1c) : (down ? 0x26272c : 0x2d2e33));
        HFONT o = (HFONT)SelectObject(CV.dc, f_icon);
        SetTextColor(CV.dc, (hot && i == 2) ? RGB(255, 255, 255) : RGB(0xc9, 0xce, 0xdb));
        RECT r = { bx, 0, bx + cbw, hh };
        DrawTextW(CV.dc, glyph[i], 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(CV.dc, o);
        add_hit(CB_ID[i], bx, 0, cbw, hh);
    }
    int sw2 = cv_text_w(&CV, f_small, hdr);
    cv_circle(&CV, (float)(cbx - D(16) - sw2 - D(12)), (float)(hh / 2), (float)D(4), dot);
    cv_text(&CV, f_small, hdr, cbx - D(16) - sw2, 0, sw2, hh, C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    caption_h = hh;
    //  탭
    int ty = hh, th = D(44);
    if (BG_ON) cv_round(&CV, 0, (float)ty, (float)w, (float)th, 0, C_BAR, 0.86f);   //  탭 줄 (has-bg)
    cv_fill(&CV, 0, ty + th - 1, w, S > 1 ? (int)S : 1, C_DIV);
    int tx = D(14);
    static const int TABS[7] = { S_tabActivity, S_tabDisplay, S_tabApp, S_tabKeys, S_tabBg, S_tabHook, S_tabBackfill };
    for (int i = 0; i < 7; i++) {
        const char *t = T(TABS[i]);
        int tw = cv_text_w(&CV, f_tab, t) + D(24);
        uint32_t col = i == tab ? C_ACCHI : (hover == ID_TAB0 + i ? C_TEXT : C_DIM);
        cv_text(&CV, f_tab, t, tx, ty, tw, th, col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (i == tab) cv_fill(&CV, tx + D(10), ty + th - D(3), tw - D(20), D(2), C_ACC);
        add_hit(ID_TAB0 + i, tx, ty, tw, th);
        tx += tw;
    }
    //  아래 줄
    int fh = D(34);
    if (BG_ON) cv_round(&CV, 0, (float)(h - fh), (float)w, (float)fh, 0, C_BAR, 0.86f); else cv_fill(&CV, 0, h - fh, w, fh, C_BAR);
    cv_fill(&CV, 0, h - fh, w, S > 1 ? (int)S : 1, C_DIV);
    cv_circle(&CV, (float)D(22), (float)(h - fh / 2), (float)D(4), dot);
    cv_text(&CV, f_small, foot, D(34), h - fh, w / 2, fh, C_TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    char ver[64];
    snprintf(ver, sizeof ver, "%s %s", T(CH_LABEL), VERSION);
    cv_text(&CV, f_mono, ver, w / 2, h - fh, w / 2 - D(20), fh, C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    //  내용 (스크롤)
    int top = ty + th, bottom = h - fh;
    view_h = bottom - top;
    cv_clip(&CV, 0, top, w, view_h);
    int pad = D(22);
    int y0 = top + pad - scroll_[tab];
    int cw = w - 2 * pad;
    int used = 0;
    switch (tab) {
    case 0: used = page_activity(pad, y0, cw); break;
    case 1: used = page_display(pad, y0, cw); break;
    case 2: used = page_app(pad, y0, cw); break;
    case 3: used = page_keys(pad, y0, cw); break;
    case 4: used = page_bg(pad, y0, cw); break;
    case 5: used = page_hook(pad, y0, cw); break;
    case 6: used = page_backfill(pad, y0, cw); break;
    default: used = page_soon(pad, y0, cw > D(620) ? D(620) : cw); break;
    }
    content_h[tab] = used + pad;
    int maxs = content_h[tab] - view_h;
    if (maxs < 0) maxs = 0;
    if (scroll_[tab] > maxs) { scroll_[tab] = maxs; }
    //  스크롤 막대
    if (maxs > 0) {
        float frac = (float)view_h / content_h[tab];
        float bh = view_h * frac, by = top + (view_h - bh) * ((float)scroll_[tab] / maxs);
        cv_round(&CV, (float)(w - D(8)), by + D(2), (float)D(4), bh - D(4), (float)D(2), C_TRACK, 1);
    }
    cv_clip(&CV, 0, 0, 0, 0);
    if (AV.open) av_paint(w, h);   //  프로필 사진 조절 창 (위에 덮는 창)
    BitBlt(hdc, 0, 0, w, h, CV.dc, 0, 0, SRCCOPY);
}

// ── 고르기 (팝업 메뉴) ──
static int popup(int id, const char *const *labels, int n, int cur) {
    RECT r = { 0 };
    for (int i = 0; i < nhits; i++) if (hits[i].id == id) r = hits[i].r;
    POINT pt = { r.left, r.bottom + D(4) };
    ClientToScreen(W, &pt);
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < n; i++) {
        wchar_t *ws = utf8_to_w(labels[i], strlen(labels[i]));
        AppendMenuW(m, MF_STRING | (i == cur ? MF_CHECKED : 0), (UINT_PTR)(i + 1), ws ? ws : L"");
        free(ws);
    }
    int r2 = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, W, NULL);
    DestroyMenu(m);
    return r2 - 1;
}

static void click(int id) {
    if (id >= ID_BG0 && id < ID_BG0 + 100) { bg_click(id); return; }   //  배경 탭
    if (id >= ID_BF0 && id < ID_BF0 + 100) { bf_click(id); InvalidateRect(W, NULL, FALSE); return; }   //  보충 탭
    if (id >= ID_HK0 - 1) { if (id >= ID_HK0) hk_click(id); InvalidateRect(W, NULL, FALSE); return; }   //  웹훅 탭 (ID_HK0-1 = 조절 창 바탕)
    if (id >= ID_KB0) { kb_click(id); InvalidateRect(W, NULL, FALSE); return; }
    if (id >= ID_TAB0 && id <= ID_TAB6) {
        fe_finish();
        if (id - ID_TAB0 != 3) kb_cap.on = 0;
        tab = id - ID_TAB0;
        if (tab == 0) SetTimer(W, 1, 1000, NULL); else KillTimer(W, 1);
    } else if (id == ID_BINGE) {
        //  binge-set: 확장에 바꾸라고 보내고 3초까지 확장이 알려 오는 값을 기다린다
        int want = binge_state() != 1;
        if (!app_set_binge(want)) { binge_ui.err_id = S_bingeNoExt; binge_ui.err_until = GetTickCount() + 4000; }
        else { binge_ui.busy = 1; binge_ui.want = want; binge_ui.deadline = GetTickCount() + 3000; SetTimer(W, 4, 100, NULL); }
    } else if (id == ID_RPC) app_set_rpc(!app_rpc_on());
    else if (id == ID_RECONNECT) app_reconnect();
    else if (id == ID_VIEWALL || id == ID_OPENLOGS) app_open_logs();
    else if (id >= ID_OPEN0 && id <= ID_OPEN5) {
        //  ⚠ 그릴 때 받은 문자열은 그 뒤 엔진이 지웠을 수 있다 → 지금 다시 읽고 같은 사이트 카드를 찾는다
        const char *site = cards[id - ID_OPEN0].site;   // 사이트 표의 고정 문자열이라 안전
        int nn = core_cards(app_core(), cards, 6);
        const core_card *cc = NULL;
        for (int k = 0; k < nn; k++) if (cards[k].site == site) cc = &cards[k];
        if (!cc) { InvalidateRect(W, NULL, FALSE); return; }
        const char *u = (cc->btn_url && *cc->btn_url) ? cc->btn_url : cc->url;   // 버튼이 있으면 Discord 에 보낸 그 주소
        if (u && (!strncmp(u, "https://", 8) || !strncmp(u, "http://", 7))) {
            wchar_t *wu = utf8_to_w(u, strlen(u));
            if (wu) { ShellExecuteW(NULL, L"open", wu, NULL, NULL, SW_SHOWNORMAL); free(wu); }
        }
    } else if (id == ID_AUTOSTART) app_set_autostart(!app_autostart());
    else if (id == ID_FASTSTART) app_set_faststart(!app_faststart());
    else if (id == ID_TESTCH) app_set_test_channel(!app_test_channel());
    else if (id == ID_SHOWSERIES) app_set_rpc_flag("showSeries", !flag("showSeries"));
    else if (id == ID_SHOWEPISODE) app_set_rpc_flag("showEpisode", !flag("showEpisode"));
    else if (id == ID_SHOWTHUMB) app_set_rpc_flag("showThumbnail", !flag("showThumbnail"));
    else if (id == ID_SHOWBUTTONS) app_set_rpc_flag("showButtons", !flag("showButtons"));
    else if (id == ID_TIMEMODE) {
        static const char *V[] = { "progress", "remaining", "none" };
        const char *L[] = { T(S_timeProgress), T(S_timeRemaining), T(S_timeNone) };
        int cur = !strcmp(time_mode(), "remaining") ? 1 : !strcmp(time_mode(), "none") ? 2 : 0;
        int k = popup(id, L, 3, cur);
        if (k >= 0 && k != cur) app_set_time_mode(V[k]);
    } else if (id == ID_ACTTYPE) {
        static const int V[] = { 3, 2, 0 };
        const char *L[] = { T(S_actWatching), T(S_actListening), T(S_actPlaying) };
        int t = act_type(), cur = t == 2 ? 1 : t == 0 ? 2 : 0;
        int k = popup(id, L, 3, cur);
        if (k >= 0 && k != cur) app_set_activity_type(V[k]);
    } else if (id == ID_IDLE) {
        static const double V[] = { 0, 1, 3, 5, 10, 30, 60 };
        char buf[7][64];
        const char *L[7];
        int cur = -1;
        for (int i = 0; i < 7; i++) { idle_label(buf[i], sizeof buf[i], V[i]); L[i] = buf[i]; if (V[i] == idle_min()) cur = i; }
        int k = popup(id, L, 7, cur);
        if (k >= 0 && k != cur) app_set_idle_minutes(V[k]);
    } else if (id == ID_LEVEL) {
        static const char *V[] = { "max", "high", "normal", "low" };
        const char *L[] = { T(S_NA_levelMax), T(S_NA_levelHigh), T(S_NA_levelNormal), T(S_NA_levelLow) };
        int cur = 2;
        for (int i = 0; i < 4; i++) if (!strcmp(V[i], level_of())) cur = i;
        int k = popup(id, L, 4, cur);
        if (k >= 0 && k != cur) app_set_level(V[k]);
    } else if (id >= ID_SD0 && id <= ID_SD5) {
        const char *site = SITE_IDS[id - ID_SD0];
        static const int V[] = { 2, 1, 0 };
        const char *L[] = { T(S_sdSeries), T(S_sdEpisode), T(S_sdAppName) };
        int v = sd_of(site), cur = v == 1 ? 1 : v == 0 ? 2 : 0;
        int k = popup(id, L, 3, cur);
        if (k >= 0 && k != cur) app_set_status_display(site, V[k]);
    } else if (id == ID_LANG) {
        static const char *V[] = { "auto", "ko", "en", "ja" };
        const char *L[] = { T(S_langAuto), T(S_NA_langKo), T(S_NA_langEn), T(S_NA_langJa) };
        int cur = 0;
        for (int i = 0; i < 4; i++) if (!strcmp(V[i], lang_setting())) cur = i;
        int k = popup(id, L, 4, cur);
        if (k >= 0 && k != cur) app_set_lang_setting(V[k]);
    }
    InvalidateRect(W, NULL, FALSE);
}

static int hit_at(int x, int y) {
    for (int i = nhits - 1; i >= 0; i--) {
        RECT r = hits[i].r;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return hits[i].id;
    }
    return 0;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:   //  입력 칸 글이 바뀜 → 바로 알려 줄 칸이면
        if (FE.h && (HWND)lp == FE.h && HIWORD(wp) == EN_CHANGE && FE.live) {
            int len = GetWindowTextLengthW(FE.h);
            wchar_t *buf = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
            if (buf) {
                GetWindowTextW(FE.h, buf, len + 1);
                char *u = w_to_utf8(buf, wcslen(buf));
                char key[160];
                snprintf(key, sizeof key, "%s", FE.key);
                if (u) FE.live(key, u);
                free(u);
                free(buf);
            }
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    //  ── 제목 줄을 없애고 머리줄로 대신 (테두리·그림자·스냅은 Windows 것 그대로) ──
    case WM_NCCALCSIZE:
        if (wp && (GetWindowLongW(h, GWL_STYLE) & WS_THICKFRAME)) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
            UINT dpi = GetDpiForWindow(h);
            int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + pad, fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + pad;
            RECT *r = &p->rgrc[0];
            r->left += fx;
            r->right -= fx;
            r->bottom -= fy;
            if (IsZoomed(h)) r->top += fy;   // 최대화면 화면 밖으로 나간 테두리만큼 들인다
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, wp, lp);
        if (r != HTCLIENT || !(GetWindowLongW(h, GWL_STYLE) & WS_THICKFRAME)) return r;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        if (!IsZoomed(h) && pt.y < D(5)) {   // 맨 위 가장자리 = 크기 조절
            RECT rc;
            GetClientRect(h, &rc);
            return pt.x < D(12) ? HTTOPLEFT : pt.x >= rc.right - D(12) ? HTTOPRIGHT : HTTOP;
        }
        int id = hit_at(pt.x, pt.y);
        if (id == ID_MIN) return HTMINBUTTON;
        if (id == ID_MAX) return HTMAXBUTTON;   // Windows 11 스냅 레이아웃이 뜬다
        if (id == ID_CLOSE) return HTCLOSE;
        if (pt.y < caption_h && !id) return HTCAPTION;   // 끌기·두 번 눌러 최대화·우클릭 메뉴
        return HTCLIENT;
    }
    case WM_NCMOUSEMOVE: {
        int id = wp == HTMINBUTTON ? ID_MIN : wp == HTMAXBUTTON ? ID_MAX : wp == HTCLOSE ? ID_CLOSE : 0;
        if (id != hover) { hover = id; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE | TME_NONCLIENT, h, 0 };
        TrackMouseEvent(&t);
        if (id) return 0;
        break;
    }
    case WM_NCMOUSELEAVE:
        if (hover == ID_MIN || hover == ID_MAX || hover == ID_CLOSE) { hover = 0; press = 0; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        //  창 버튼은 직접 처리 (기본 처리에 맡기면 옛 모양 버튼을 그린다)
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            press = wp == HTMINBUTTON ? ID_MIN : wp == HTMAXBUTTON ? ID_MAX : ID_CLOSE;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            int id = wp == HTMINBUTTON ? ID_MIN : wp == HTMAXBUTTON ? ID_MAX : ID_CLOSE, p = press;
            press = 0;
            hover = 0;
            InvalidateRect(h, NULL, FALSE);
            if (id != p) return 0;
            if (id == ID_MIN) ShowWindow(h, SW_MINIMIZE);
            else if (id == ID_MAX) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
            else PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_IMG:   // 카드 그림이 도착 (wp=1 이면 실패 이유 — 같은 이유는 10분에 한 번만 로그)
        if (wp == 1 && lp) {
            static char last[240];
            static DWORD last_at;
            const char *why = (const char *)lp;
            if (strcmp(last, why) || GetTickCount() - last_at > 600000) {
                char line[300];
                snprintf(line, sizeof line, "[Img] 카드 그림 못 받음: %s", why);
                app_log(1, line);
                snprintf(last, sizeof last, "%s", why);
                last_at = GetTickCount();
            }
            free((void *)lp);
            return 0;
        }
        if (IsWindowVisible(h)) InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SIZE: fe_finish(); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_TIMER:
        if (wp == 3) { KillTimer(h, 3); InvalidateRect(h, NULL, FALSE); return 0; }
        if (wp == 4) {   //  정주행 바꾸기 결과 기다림
            if (binge_state() == binge_ui.want) { binge_ui.busy = 0; KillTimer(h, 4); }
            else if (GetTickCount() >= binge_ui.deadline) {
                binge_ui.busy = 0;
                KillTimer(h, 4);
                binge_ui.err_id = binge_state() < 0 ? S_bingeOldExt : S_bingeFail;
                binge_ui.err_until = GetTickCount() + 4000;
                SetTimer(h, 5, 4100, NULL);
            }
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if (wp == 5) { KillTimer(h, 5); InvalidateRect(h, NULL, FALSE); return 0; }   // '정말 삭제' 3초 끝
        if (IsWindowVisible(h) && tab == 0) InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KBEDIT: fe_finish(); return 0;
    case WM_AV_FETCHED: av_fetched((av_fetch *)lp); return 0;          //  프로필 지금 사진 받음
    case WM_HK_UPLOADED: av_uploaded((hk_upload_res *)lp); return 0;   //  프로필 사진 올림
    case WM_CTLCOLOREDIT:
        if ((HWND)lp == FE.h) {
            if (!FE_BRUSH) FE_BRUSH = CreateSolidBrush(RGB(0x1b, 0x1b, 0x1f));
            SetTextColor((HDC)wp, RGB(0xf4, 0xf5, 0xf8));
            SetBkColor((HDC)wp, RGB(0x1b, 0x1b, 0x1f));
            return (LRESULT)FE_BRUSH;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (av_key((UINT)wp)) return 0;   //  프로필 사진 조절 창
        if (m == WM_KEYDOWN && bg_key((UINT)wp)) return 0;   //  배경 미리보기 방향키
        if (kb_key((UINT)wp, lp)) return 0;
        break;
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        if (kb_cap.on || wp == VK_MENU) return 0;   // 키 받는 중 Alt 가 창 메뉴를 열지 않게
        break;
    case WM_MOUSEMOVE: {
        if (av_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0)) return 0;
        if (bg_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, 0)) return 0;
        int id = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (id != hover) { hover = id; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (hover) { hover = 0; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(NULL, hover ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_LBUTTONDBLCLK:
        if (!AV.open && bg_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, 0)) return 0;
        m = WM_LBUTTONDOWN;   //  그 밖에선 누르기와 같게
        /* fall through */
    case WM_LBUTTONDOWN:
        if (av_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0)) return 0;   //  조절 창: 끌기·확대 막대
        if (bg_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)))) return 0;   //  배경 미리보기·막대
        press = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (FE.h) SetFocus(h);   // 입력 칸 밖을 누르면 저장
        //  키 받는 중 다른 곳(키 칸이 아닌 곳)을 누르면 그만
        if (kb_cap.on && !(press >= ID_KB0 && press < ID_HK0 - 1 && (press - ID_KB0) % 1000 < 900 && (press - ID_KB0) % 10 == 1)) { kb_cap.on = 0; InvalidateRect(h, NULL, FALSE); }
        SetCapture(h);
        return 0;
    case WM_LBUTTONUP: {
        if (av_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0)) { press = 0; return 0; }
        if (bg_mouse(m, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, 0)) { press = 0; return 0; }
        ReleaseCapture();
        int id = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        int p = press;
        press = 0;
        if (id && id == p) click(id);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        {   POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(h, &pt);
            if (av_mouse(m, pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wp))) return 0;
            if (bg_mouse(m, pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wp), 0)) return 0; }
        fe_finish();
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        scroll_[tab] -= d * D(48) / WHEEL_DELTA;
        if (scroll_[tab] < 0) scroll_[tab] = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mi = (MINMAXINFO *)lp;
        mi->ptMinTrackSize.x = D(640);
        mi->ptMinTrackSize.y = D(520);
        return 0;
    }
    case WM_DPICHANGED: {
        S = HIWORD(wp) / 96.0f;
        make_fonts();
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE: ShowWindow(h, SW_HIDE); KillTimer(h, 1); return 0;   // 닫으면 트레이로
    }
    return DefWindowProcW(h, m, wp, lp);
}

void win_init(HINSTANCE inst) {
    bg_cleanup();   //  지난번에 남은 배경 임시 사본·쓰지 않는 관리 파일 정리
    INST = inst;
    //  다크 메뉴 (Windows 10 1903+ uxtheme 순번 135/136 — 없으면 밝은 메뉴로 그냥 둔다)
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef int (WINAPI *SetMode)(int);
        typedef void (WINAPI *Flush)(void);
        SetMode sm = (SetMode)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        Flush fl = (Flush)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (sm) sm(2);
        if (fl) fl();
    }
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_DBLCLKS;   //  배경 미리보기 두 번 누르기 (그 밖에선 누르기와 같게)
    wc.lpszClassName = L"TokuRpcNativeWindow";
    RegisterClassExW(&wc);
}

void win_show(void) {
    if (!W) {
        UINT dpi = GetDpiForSystem();
        S = dpi / 96.0f;
        RECT r = { 0, 0, D(980), D(740) };
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        W = CreateWindowExW(0, L"TokuRpcNativeWindow", L"TOKU RPC", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, NULL, NULL, INST, NULL);
        if (!W) return;
        S = GetDpiForWindow(W) / 96.0f;
        make_fonts();
        SetWindowPos(W, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);   // 제목 줄 없앤 크기로 다시 계산
        img_init(W, WM_IMG);
        ImmAssociateContextEx(W, NULL, 0);   // 이 창은 글을 받지 않는다 — 한글 입력기가 키 받기를 가로채지 않게 (이름 칸은 따로)
        //  다크 제목 줄 + 제목 줄 색을 앱 머리줄 색으로 (Windows 11)
        BOOL dark = TRUE;
        DwmSetWindowAttribute(W, 20, &dark, sizeof dark);
        COLORREF cap = RGB(0x12, 0x13, 0x15), txt = RGB(0xf4, 0xf5, 0xf8);
        DwmSetWindowAttribute(W, 35, &cap, sizeof cap);
        DwmSetWindowAttribute(W, 36, &txt, sizeof txt);
    }
    ShowWindow(W, IsIconic(W) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(W);
    if (tab == 0) SetTimer(W, 1, 1000, NULL);
    InvalidateRect(W, NULL, FALSE);
}

void win_refresh(void) { if (W && IsWindowVisible(W)) InvalidateRect(W, NULL, FALSE); }
int win_visible(void) { return W && IsWindowVisible(W); }

// ── 시험용: 화면에 띄우지 않고 각 탭을 그려 BMP 로 저장 (시험 모드에서만 부름) ──
static int save_bmp(const wchar_t *file) {
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    ZeroMemory(&fh, sizeof fh);
    ZeroMemory(&ih, sizeof ih);
    DWORD bytes = (DWORD)CV.w * CV.h * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + bytes;
    ih.biSize = sizeof ih;
    ih.biWidth = CV.w;
    ih.biHeight = -CV.h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD n;
    WriteFile(f, &fh, sizeof fh, &n, NULL);
    WriteFile(f, &ih, sizeof ih, &n, NULL);
    WriteFile(f, CV.px, bytes, &n, NULL);
    CloseHandle(f);
    return 1;
}

int win_snapshot(const wchar_t *dir) {
    static const float SC[2] = { 1.0f, 1.5f };
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW, L"TokuRpcNativeWindow", L"snap", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, INST, NULL);
    if (!w) return 0;
    HWND keep = W;
    W = w;
    int saved = 0, keep_tab = tab;
    float keep_s = S;
    HDC sdc = CreateCompatibleDC(NULL);   // 메모리 DC — 화면에는 아무것도 안 그린다
    //  카드 그림을 먼저 요청하고 (최대 15초) 다 받을 때까지 기다린다
    img_init(NULL, 0);
    S = 1;
    make_fonts();
    SetWindowPos(w, NULL, 0, 0, D(980), D(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    tab = 0;
    paint(sdc);
    for (int t = 0; t < 150 && img_pending(); t++) Sleep(100);
    for (int k = 0; k < 2; k++) {
        S = SC[k];
        make_fonts();
        SetWindowPos(w, NULL, 0, 0, D(980), D(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        {   //  시험 그림: 웹훅 폴더는 다 펼쳐서 (이 함수는 시험 모드에서만 불린다)
            hk_item *hl; int hn = hk_list(&hl);
            for (int q = 0; q < hn; q++) hk_set_open(hl[q].title, 1);
            hk_list_free(hl, hn);
        }
        for (int t = 0; t < 7; t++) {
            tab = t;
            paint(sdc);
            //  다시 그려야 스크롤 길이가 맞는다 (첫 그림에서 content_h 를 잼)
            paint(sdc);
            wchar_t f[MAX_PATH];
            _snwprintf(f, MAX_PATH - 1, L"%ls\\tab%d@%d.bmp", dir, t, (int)(S * 100));
            f[MAX_PATH - 1] = 0;
            if (save_bmp(f)) saved++;
            wchar_t avp[MAX_PATH];
            if (t == 5 && GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_AVATAR", avp, MAX_PATH)) {   //  시험: 프로필 사진 조절 창을 연 그림
                unsigned char *ab; size_t an;
                if (iu_read_file(avp, 20 * 1024 * 1024, &ab, &an) == 1 && av_load(ab, an, "image/png")) {
                    snprintf(AV.name, sizeof AV.name, "%s", "\xE5\xA6\x82\xE6\x9C\x88\xE5\xBC\xA6\xE5\xA4\xAA\xE6\x9C\x97");
                    AV.open = 1;
                    av_zoom_at(1.6, AV.D / 2, AV.D / 2);
                    paint(sdc);
                    _snwprintf(f, MAX_PATH - 1, L"%ls\\avatar@%d.bmp", dir, (int)(S * 100));
                    f[MAX_PATH - 1] = 0;
                    save_bmp(f);
                    av_close();
                }
            }
            if (t == 5) {   //  웹훅 탭은 맨 아래까지 내린 그림도 (캐릭터 칸)
                scroll_[5] = 100000;
                paint(sdc);
                paint(sdc);
                _snwprintf(f, MAX_PATH - 1, L"%ls\\tab5b@%d.bmp", dir, (int)(S * 100));
                f[MAX_PATH - 1] = 0;
                save_bmp(f);
                scroll_[5] = 0;
            }
        }
    }
    DeleteDC(sdc);
    DestroyWindow(w);
    W = keep;
    tab = keep_tab;
    S = keep_s;
    make_fonts();
    return saved;
}

