#include "traymenu.h"
#include <dwmapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "binge.h"
#include "canvas.h"
#include "strings.h"
#include "../base/utf.h"
#include "../core/js.h"

#define ID_QUIT_CMD 104   // main.c 의 ID_QUIT

//  색 (tray-menu.html :root 와 같은 값)
#define K_CARD 0x17171a
#define K_BORDER 0x2b2c31
#define K_DIV 0x232328
#define K_TEXT 0xf4f5f8
#define K_TEXT2 0xc9cedb
#define K_DIM 0x9aa0b0
#define K_ACC 0x7775fc
#define K_ACCHI 0x908eff
#define K_TRACK 0x3a3942
#define K_RED 0xf2555a
#define K_GREEN 0x3ae373
#define K_AMBER 0xf2b84b

static HWND MW, OWNER;
static HINSTANCE INST;
static canvas CVT;
static float S = 1;
static HFONT f_item, f_name, f_small, f_mono;
static HICON logo;
static int cur = -1, pressed = -1, SNAPPING, ACTIVE;

enum { IT_OPEN, IT_RPC, IT_RECONNECT, IT_BINGE, IT_AUTOSTART, IT_QUIT, IT_SEP };
typedef struct { int kind, toggle, on; const char *label, *icon; RECT r; } item;
static item ITEMS[12];
static int NITEMS;

static int D(float v) { return (int)lroundf(v * S); }
static const char *T(int id) {
    const char *l = app_lang();
    const char *const *t = !strcmp(l, "ja") ? STR_JA : !strcmp(l, "en") ? STR_EN : STR_KO;
    return t[id];
}
//  기본 메뉴 문구의 앞 기호(✓ ⬆ 🔁)는 스위치·아이콘이 대신한다 (plain)
static const char *plain(const char *s) {
    for (;;) {
        if (!strncmp(s, "\xE2\x9C\x93", 3) || !strncmp(s, "\xE2\xAC\x86", 3)) s += 3;
        else if (!strncmp(s, "\xF0\x9F\x94\x81", 4)) s += 4;
        else break;
        while (*s == ' ') s++;
    }
    return s;
}

// ── 아이콘: tray-menu.js 의 SVG (viewBox 24, 선 1.8, 둥근 끝) 를 그대로 선으로 그린다 ──
static const char *ICON_OPEN = "R3,4,18,16,2.5|M3 9h18";
static const char *ICON_RPC = "C12,12,2|M16.2 7.8a6 6 0 0 1 0 8.4M7.8 16.2a6 6 0 0 1 0-8.4M19 5a10 10 0 0 1 0 14M5 19A10 10 0 0 1 5 5";
static const char *ICON_RECONNECT = "M20 11a8 8 0 0 0-14.3-4.9L4 8|M4 3v5h5|M4 13a8 8 0 0 0 14.3 4.9L20 16|M20 21v-5h-5";
static const char *ICON_BINGE = "R3,5,18,14,2.5|M10 9.5v5l4.5-2.5z";
static const char *ICON_POWER = "M12 3v8|M6.4 6.6a8 8 0 1 0 11.2 0";
static const char *ICON_QUIT = "M6 6l12 12M18 6 6 18";

typedef struct { float x0, y0, x1, y1; } seg;
static seg SEGS[600];
static int NSEG;
static void add_seg(float a, float b, float c, float d) { if (NSEG < 600) { SEGS[NSEG].x0 = a; SEGS[NSEG].y0 = b; SEGS[NSEG].x1 = c; SEGS[NSEG].y1 = d; NSEG++; } }
//  SVG 호 (끝점 표기 → 가운데 표기, SVG 1.1 부록 F.6.5) 를 짧은 선으로
static void add_arc(float x1, float y1, float rx, float ry, int large, int sweep, float x2, float y2) {
    float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    float lam = dx * dx / (rx * rx) + dy * dy / (ry * ry);
    if (lam > 1) { rx *= sqrtf(lam); ry *= sqrtf(lam); }
    float num = rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx, den = rx * rx * dy * dy + ry * ry * dx * dx;
    float co = den > 0 ? sqrtf(fmaxf(0, num / den)) : 0;
    if (large == sweep) co = -co;
    float cxp = co * rx * dy / ry, cyp = -co * ry * dx / rx;
    float cx = cxp + (x1 + x2) / 2, cy = cyp + (y1 + y2) / 2;
    float t1 = atan2f((dy - cyp) / ry, (dx - cxp) / rx);
    float t2 = atan2f((-dy - cyp) / ry, (-dx - cxp) / rx);
    float dt = t2 - t1;
    if (sweep && dt < 0) dt += 2 * 3.14159265f;
    if (!sweep && dt > 0) dt -= 2 * 3.14159265f;
    int n = 28;
    float px = x1, py = y1;
    for (int i = 1; i <= n; i++) {
        float t = t1 + dt * i / n;
        float nx = cx + rx * cosf(t), ny = cy + ry * sinf(t);
        add_seg(px, py, nx, ny);
        px = nx; py = ny;
    }
}
static const char *num(const char *p, float *v) {
    while (*p == ' ' || *p == ',') p++;
    char *e;
    *v = strtof(p, &e);
    return e;
}
static void build_icon(const char *spec) {
    NSEG = 0;
    char buf[400];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *part = strtok(buf, "|"); part; part = strtok(NULL, "|")) {
        if (part[0] == 'R') {   //  둥근 사각형 x,y,w,h,r
            float x, y, w, h, r;
            const char *p = part + 1;
            p = num(p, &x); p = num(p, &y); p = num(p, &w); p = num(p, &h); p = num(p, &r);
            add_seg(x + r, y, x + w - r, y); add_seg(x + w, y + r, x + w, y + h - r);
            add_seg(x + w - r, y + h, x + r, y + h); add_seg(x, y + h - r, x, y + r);
            add_arc(x + w - r, y, r, r, 0, 1, x + w, y + r); add_arc(x + w, y + h - r, r, r, 0, 1, x + w - r, y + h);
            add_arc(x + r, y + h, r, r, 0, 1, x, y + h - r); add_arc(x, y + r, r, r, 0, 1, x + r, y);
            continue;
        }
        if (part[0] == 'C') {   //  원 cx,cy,r
            float cx, cy, r;
            const char *p = part + 1;
            p = num(p, &cx); p = num(p, &cy); p = num(p, &r);
            add_arc(cx + r, cy, r, r, 0, 1, cx - r, cy);
            add_arc(cx - r, cy, r, r, 0, 1, cx + r, cy);
            continue;
        }
        //  path: M m L l H h V v A a Z (뒤에 숫자가 이어지면 같은 명령 반복)
        const char *p = part;
        float x = 0, y = 0, sx = 0, sy = 0;
        char cmd = 0;
        while (*p) {
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            if (strchr("MmLlHhVvAaZz", *p)) cmd = *p++;
            else if (cmd == 'M') cmd = 'L';
            else if (cmd == 'm') cmd = 'l';
            float a, b, c, d, e, f, g;
            switch (cmd) {
            case 'M': p = num(p, &a); p = num(p, &b); x = sx = a; y = sy = b; break;
            case 'm': p = num(p, &a); p = num(p, &b); x += a; y += b; sx = x; sy = y; break;
            case 'L': p = num(p, &a); p = num(p, &b); add_seg(x, y, a, b); x = a; y = b; break;
            case 'l': p = num(p, &a); p = num(p, &b); add_seg(x, y, x + a, y + b); x += a; y += b; break;
            case 'H': p = num(p, &a); add_seg(x, y, a, y); x = a; break;
            case 'h': p = num(p, &a); add_seg(x, y, x + a, y); x += a; break;
            case 'V': p = num(p, &a); add_seg(x, y, x, a); y = a; break;
            case 'v': p = num(p, &a); add_seg(x, y, x, y + a); y += a; break;
            case 'A': case 'a':
                p = num(p, &a); p = num(p, &b); p = num(p, &c); p = num(p, &d); p = num(p, &e); p = num(p, &f); p = num(p, &g);
                if (cmd == 'a') { f += x; g += y; }
                add_arc(x, y, a, b, d != 0, e != 0, f, g);
                x = f; y = g;
                break;
            case 'Z': case 'z': add_seg(x, y, sx, sy); x = sx; y = sy; break;
            default: p++;
            }
        }
    }
}
//  선(굵기 1.8, 둥근 끝)을 거리로 칠한다 — 가장자리는 부드럽게
static void draw_icon(const char *spec, int x, int y, int size, uint32_t rgb) {
    build_icon(spec);
    float k = size / 24.0f, hw = 0.9f * k;
    for (int py = y; py < y + size; py++) {
        if (py < CVT.clip.top || py >= CVT.clip.bottom) continue;
        for (int px = x; px < x + size; px++) {
            if (px < CVT.clip.left || px >= CVT.clip.right) continue;
            float ux = (px + 0.5f - x) / k, uy = (py + 0.5f - y) / k, best = 1e9f;
            for (int i = 0; i < NSEG; i++) {
                seg *s = &SEGS[i];
                float vx = s->x1 - s->x0, vy = s->y1 - s->y0, wx = ux - s->x0, wy = uy - s->y0;
                float L = vx * vx + vy * vy, t = L > 0 ? (wx * vx + wy * vy) / L : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                float ddx = wx - t * vx, ddy = wy - t * vy, dd = ddx * ddx + ddy * ddy;
                if (dd < best) best = dd;
            }
            float cov = hw - sqrtf(best) * k + 0.5f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            uint32_t *d = &CVT.px[py * CVT.w + px], dv = *d;
            uint32_t r = (uint32_t)(((rgb >> 16) & 255) * cov + ((dv >> 16) & 255) * (1 - cov));
            uint32_t g = (uint32_t)(((rgb >> 8) & 255) * cov + ((dv >> 8) & 255) * (1 - cov));
            uint32_t b = (uint32_t)((rgb & 255) * cov + (dv & 255) * (1 - cov));
            *d = r << 16 | g << 8 | b;
        }
    }
}

static HFONT mkfont(float px, int weight, const wchar_t *face) {
    return CreateFontW(-D(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
static void fonts(void) {
    HFONT *all[] = { &f_item, &f_name, &f_small, &f_mono };
    for (int i = 0; i < 4; i++) if (*all[i]) { DeleteObject(*all[i]); *all[i] = NULL; }
    f_item = mkfont(13.5f, FW_NORMAL, L"Segoe UI");
    f_name = mkfont(14, FW_SEMIBOLD, L"Segoe UI");
    f_small = mkfont(12, FW_NORMAL, L"Segoe UI");
    f_mono = mkfont(11.5f, FW_NORMAL, L"Consolas");
    if (logo) DestroyIcon(logo);
    logo = (HICON)LoadImageW(INST, MAKEINTRESOURCEW(1), IMAGE_ICON, D(20), D(20), 0);
}

//  항목 (main.js trayMenuItems — 업데이트 항목은 알파에 업데이트 확인이 생기면)
static void build_items(void) {
    NITEMS = 0;
    item *it;
    #define ADD(k, tg, o, lb, ic) do { it = &ITEMS[NITEMS++]; it->kind = k; it->toggle = tg; it->on = o; it->label = lb; it->icon = ic; } while (0)
    ADD(IT_OPEN, 0, 0, T(S_tray_open), ICON_OPEN);
    ADD(IT_RPC, 1, app_rpc_on(), T(S_tray_rpc), ICON_RPC);
    ADD(IT_RECONNECT, 0, 0, plain(T(S_tray_reconnect)), ICON_RECONNECT);
    if (binge_state() >= 0) ADD(IT_BINGE, 1, binge_state() == 1, plain(T(S_tray_bingeOff)), ICON_BINGE);
    ADD(IT_SEP, 0, 0, "", NULL);
    ADD(IT_AUTOSTART, 1, app_autostart(), plain(T(S_tray_autoStartOff)), ICON_POWER);
    ADD(IT_SEP, 0, 0, "", NULL);
    ADD(IT_QUIT, 0, 0, T(S_tray_quit), ICON_QUIT);
    #undef ADD
}

//  지연 시간 문구 (measureTrayLatency)
static void fmt_ms(char *o, size_t n, double ms) {
    if (ms < 10) { sbuf b; sb_init(&b, 0); js_to_fixed(&b, ms, 1); snprintf(o, n, "%sms", b.p ? b.p : "0"); sb_free(&b); }
    else snprintf(o, n, "%dms", (int)js_round(ms));
}
static void fillv(char *out, size_t n, const char *tpl, const char *val) {
    const char *k = strstr(tpl, "{ms}");
    if (!k) { snprintf(out, n, "%s", tpl); return; }
    snprintf(out, n, "%.*s%s%s", (int)(k - tpl), tpl, val, k + 4);
}
static int LAT_HAVE;
static char LAT_EXT[96], LAT_DC[96];
static uint32_t LAT_EXT_C = K_TRACK, LAT_DC_C = K_TRACK;
static int LAT_EXT_LIT, LAT_DC_LIT;
static DWORD LAT_NEXT;
static void latency_update(void) {
    int clients; double ms;
    if (app_ping_get(&clients, &ms)) {
        char t[32];
        if (!clients) { snprintf(LAT_EXT, sizeof LAT_EXT, "%s", T(S_tray_latExtNone)); LAT_EXT_C = K_TRACK; LAT_EXT_LIT = 0; }
        else if (ms < 0) { snprintf(LAT_EXT, sizeof LAT_EXT, "%s", T(S_tray_latExtSlow)); LAT_EXT_C = K_RED; LAT_EXT_LIT = 0; }
        else { fmt_ms(t, sizeof t, ms); fillv(LAT_EXT, sizeof LAT_EXT, T(S_tray_latExt), t); LAT_EXT_C = ms < 20 ? K_GREEN : ms < 100 ? K_AMBER : K_RED; LAT_EXT_LIT = ms < 100; }
        LAT_HAVE = 1;
    }
    int conn; double dms;
    app_discord_latency(&conn, &dms);
    char t[32];
    if (!app_rpc_on()) { snprintf(LAT_DC, sizeof LAT_DC, "%s", T(S_tray_latRpcOff)); LAT_DC_C = K_TRACK; LAT_DC_LIT = 0; }
    else if (!conn) { snprintf(LAT_DC, sizeof LAT_DC, "%s", T(S_tray_latDiscordNone)); LAT_DC_C = K_RED; LAT_DC_LIT = 0; }
    else if (dms < 0) { fillv(LAT_DC, sizeof LAT_DC, T(S_tray_latDiscord), "\xE2\x80\x94"); LAT_DC_C = K_TRACK; LAT_DC_LIT = 0; }
    else { fmt_ms(t, sizeof t, dms); fillv(LAT_DC, sizeof LAT_DC, T(S_tray_latDiscord), t); LAT_DC_C = dms < 100 ? K_GREEN : dms < 500 ? K_AMBER : K_RED; LAT_DC_LIT = dms < 500; }
}

//  크기를 재고(dry) 또는 그린다 — 너비는 가장 긴 글에 맞춘다 (236~360)
static void layout(int *ow, int *oh) {
    HDC dc = GetDC(NULL);
    HFONT o = (HFONT)SelectObject(dc, f_item);
    int maxw = D(236);
    for (int i = 0; i < NITEMS; i++) {
        if (ITEMS[i].kind == IT_SEP) continue;
        wchar_t *w = utf8_to_w(ITEMS[i].label, strlen(ITEMS[i].label));
        SIZE sz = { 0, 0 };
        if (w) { GetTextExtentPoint32W(dc, w, (int)wcslen(w), &sz); free(w); }
        int need = D(6) * 2 + D(10) * 2 + D(16) + D(10) + sz.cx + (ITEMS[i].toggle ? D(14) + D(32) : 0);
        if (need > maxw) maxw = need;
    }
    SelectObject(dc, o);
    ReleaseDC(NULL, dc);
    if (maxw > D(360)) maxw = D(360);
    int h = D(6) + D(36) + D(26) + D(11);   //  안쪽 여백 · 머리 · 지연 줄 · 구분선
    for (int i = 0; i < NITEMS; i++) h += ITEMS[i].kind == IT_SEP ? D(11) : D(34);
    h += D(6);
    *ow = maxw;
    *oh = h;
}

static void draw(HDC hdc, int w, int h) {
    cv_resize(&CVT, hdc, w, h);
    cv_clip(&CVT, 0, 0, 0, 0);
    cv_fill(&CVT, 0, 0, w, h, K_CARD);
    int x = D(6), y = D(6), iw = w - D(12);
    //  머리: 로고 · TOKU RPC · 버전
    if (logo) DrawIconEx(CVT.dc, x + D(8), y + D(8), logo, D(20), D(20), 0, NULL, DI_NORMAL);
    cv_text(&CVT, f_name, "TOKU RPC", x + D(37), y, iw / 2, D(36), K_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    char ver[48];
    snprintf(ver, sizeof ver, "%s %s", T(CH_LABEL), APP_VERSION);
    cv_text(&CVT, f_mono, ver, x + iw / 2, y, iw / 2 - D(8), D(36), K_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += D(36);
    //  지연 시간 줄 (점 · 글)
    int lx = x + D(37) - D(8) + D(8);
    {
        const char *t1 = LAT_HAVE ? LAT_EXT : "\xE2\x80\xA6";
        cv_circle(&CVT, (float)(lx + D(3.5f)), (float)(y + D(9)), D(3.5f), LAT_HAVE ? LAT_EXT_C : K_TRACK);
        int t1w = cv_text_w(&CVT, f_small, t1);
        int avail = (iw - (lx - x) - D(8)) / 2 - D(12);
        if (t1w > avail) t1w = avail;
        cv_text(&CVT, f_small, t1, lx + D(13), y, t1w, D(18), LAT_EXT_LIT ? K_TEXT2 : K_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        int lx2 = lx + D(13) + t1w + D(12);
        cv_circle(&CVT, (float)(lx2 + D(3.5f)), (float)(y + D(9)), D(3.5f), LAT_DC_C);
        cv_text(&CVT, f_small, LAT_DC, lx2 + D(13), y, x + iw - D(8) - lx2 - D(13), D(18), LAT_DC_LIT ? K_TEXT2 : K_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    y += D(26);
    cv_fill(&CVT, x + D(6), y + D(5), iw - D(12), S > 1 ? (int)S : 1, K_DIV);
    y += D(11);
    for (int i = 0; i < NITEMS; i++) {
        item *it = &ITEMS[i];
        if (it->kind == IT_SEP) {
            cv_fill(&CVT, x + D(6), y + D(5), iw - D(12), S > 1 ? (int)S : 1, K_DIV);
            it->r.left = it->r.right = 0;
            y += D(11);
            continue;
        }
        it->r.left = x; it->r.top = y; it->r.right = x + iw; it->r.bottom = y + D(34);
        int hot = i == cur, danger = it->kind == IT_QUIT;
        if (hot) cv_round(&CVT, (float)x, (float)y, (float)iw, (float)D(34), (float)D(8), danger ? K_RED : K_ACC, i == pressed ? 0.24f : danger ? 0.14f : 0.16f);
        uint32_t fg = hot ? (danger ? K_RED : K_TEXT) : K_TEXT2, ic = hot ? (danger ? K_RED : K_ACCHI) : K_DIM;
        draw_icon(it->icon, x + D(10), y + (D(34) - D(16)) / 2, D(16), ic);
        int sw = it->toggle ? D(32) + D(14) : 0;
        cv_text(&CVT, f_item, it->label, x + D(36), y, iw - D(46) - sw, D(34), fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (it->toggle) {   //  앱 스위치를 메뉴 크기로 (32×18)
            float sx = (float)(x + iw - D(10) - D(32)), sy = (float)(y + (D(34) - D(18)) / 2);
            cv_round(&CVT, sx, sy, (float)D(32), (float)D(18), (float)D(9), it->on ? K_ACC : K_TRACK, 1);
            cv_circle(&CVT, sx + (it->on ? D(23) : D(9)), sy + D(9), (float)D(6), 0xffffff);
        }
        y += D(34);
    }
    //  테두리·둥근 모서리는 Windows 11 이 창에 그려 준다 (trm_show 의 DWM 설정). 시험 그림에만 비슷하게 그린다
    if (SNAPPING) cv_round_border(&CVT, 0.5f, 0.5f, (float)w - 1, (float)h - 1, (float)D(8), 1, K_BORDER);
}
static void paint(HDC hdc) {
    RECT rc;
    GetClientRect(MW, &rc);
    draw(hdc, rc.right, rc.bottom);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, CVT.dc, 0, 0, SRCCOPY);
}

static int item_at(int x, int y) {
    POINT pt = { x, y };
    for (int i = 0; i < NITEMS; i++) if (ITEMS[i].kind != IT_SEP && PtInRect(&ITEMS[i].r, pt)) return i;
    return -1;
}
//  누름 — 스위치는 메뉴를 닫지 않는다 (TRAY_MENU_STAY)
static void act(int i) {
    if (i < 0 || i >= NITEMS) return;
    int k = ITEMS[i].kind;
    if (k == IT_RPC) app_set_rpc(!app_rpc_on());
    else if (k == IT_BINGE) { if (binge_state() >= 0) app_set_binge(binge_state() != 1); }
    else if (k == IT_AUTOSTART) app_set_autostart(!app_autostart());
    else {
        trm_hide();
        if (k == IT_OPEN) win_show();
        else if (k == IT_RECONNECT) app_reconnect();
        else if (k == IT_QUIT) PostMessageW(OWNER, WM_COMMAND, ID_QUIT_CMD, 0);
        return;
    }
    build_items();
    latency_update();
    InvalidateRect(MW, NULL, FALSE);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        int i = item_at((short)LOWORD(lp), (short)HIWORD(lp));
        if (i != cur) { cur = i; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (cur >= 0) { cur = -1; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_LBUTTONDOWN: pressed = item_at((short)LOWORD(lp), (short)HIWORD(lp)); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONUP: {
        int i = item_at((short)LOWORD(lp), (short)HIWORD(lp)), p = pressed;
        pressed = -1;
        if (i >= 0 && i == p) act(i); else InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {   //  ↑ ↓ 로 고르고 Enter·Space 로 누른다, Esc 로 닫기
        if (wp == VK_ESCAPE) { trm_hide(); return 0; }
        if (wp == VK_UP || wp == VK_DOWN) {
            int d = wp == VK_UP ? -1 : 1, i = cur;
            for (int n = 0; n < NITEMS; n++) { i = (i + d + NITEMS) % NITEMS; if (i < 0) i = NITEMS - 1; if (ITEMS[i].kind != IT_SEP) break; }
            cur = i;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if ((wp == VK_RETURN || wp == VK_SPACE) && cur >= 0) { act(cur); return 0; }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) trm_hide();
        else ACTIVE = 1;
        return 0;
    case WM_TIMER:
        if (!ACTIVE) {   //  포커스를 못 받았으면 (다른 창이 앞을 지킬 때) 바깥을 누르면 닫는다
            POINT p; RECT r;
            GetCursorPos(&p);
            GetWindowRect(h, &r);
            if (!PtInRect(&r, p) && (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0)) { trm_hide(); return 0; }
        }
        if (GetTickCount() >= LAT_NEXT) { app_ping_begin(); LAT_NEXT = GetTickCount() + 1000; }   //  1초마다 다시 잰다
        latency_update();
        build_items();   //  바깥에서 바뀐 것(정주행 상태 등)도 따라간다
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int trm_init(HINSTANCE inst, HWND owner) {
    INST = inst;
    OWNER = owner;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"TokuRpcNativeTrayMenu";
    return RegisterClassExW(&wc) != 0;
}

int trm_visible(void) { return MW && IsWindowVisible(MW); }
void trm_hide(void) {
    if (!MW || !IsWindowVisible(MW)) return;
    KillTimer(MW, 1);
    ShowWindow(MW, SW_HIDE);
    cur = pressed = -1;
}

int trm_show(POINT at) {
    if (!MW) {
        MW = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"TokuRpcNativeTrayMenu", L"TOKU RPC", WS_POPUP, 0, 0, 10, 10, NULL, NULL, INST, NULL);
        if (!MW) return 0;
        //  Windows 11: 둥근 모서리 + 테두리 색 (없는 Windows 면 그냥 네모)
        int corner = 2;   // DWMWCP_ROUND
        DwmSetWindowAttribute(MW, 33, &corner, sizeof corner);
        COLORREF bc = RGB(0x2b, 0x2c, 0x31);
        DwmSetWindowAttribute(MW, 34, &bc, sizeof bc);
    }
    //  모니터 배율 (메뉴가 뜰 화면)
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    UINT dpi = GetDpiForWindow(MW);
    {
        typedef HRESULT (WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT *, UINT *);
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        GetDpiForMonitorFn fn = sh ? (GetDpiForMonitorFn)(void *)GetProcAddress(sh, "GetDpiForMonitor") : NULL;
        UINT dx, dy;
        if (fn && fn(mon, 0, &dx, &dy) == S_OK) dpi = dx;
    }
    S = dpi / 96.0f;
    fonts();
    build_items();
    cur = pressed = -1;
    ACTIVE = 0;
    LAT_HAVE = 0;
    app_ping_begin();
    LAT_NEXT = GetTickCount() + 1000;
    latency_update();
    int w, h;
    layout(&w, &h);
    //  커서에서 위·오른쪽으로 (작업 영역 밖으로 나가면 반대쪽으로)
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    int x = at.x, y = at.y - h;
    if (x + w > wa.right) x = at.x - w;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = at.y;
    if (y + h > wa.bottom) y = wa.bottom - h;
    SetWindowPos(MW, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    ShowWindow(MW, SW_SHOW);
    SetForegroundWindow(MW);
    SetFocus(MW);
    SetTimer(MW, 1, 100, NULL);
    InvalidateRect(MW, NULL, FALSE);
    return 1;
}

// ── 시험용 (시험 모드에서만 부름): 화면에 띄우지 않고 메뉴를 그려 BMP 로. 배율 1 은 '창 열기', 1.5 는 '종료' 에 마우스를 올린 그림 ──
int trm_snapshot(const wchar_t *dir) {
    static const float SC[2] = { 1.0f, 1.5f };
    HDC sdc = CreateCompatibleDC(NULL);
    int saved = 0;
    SNAPPING = 1;
    for (int k = 0; k < 2; k++) {
        S = SC[k];
        fonts();
        build_items();
        latency_update();
        cur = k == 0 ? 0 : NITEMS - 1;
        int w, h;
        layout(&w, &h);
        draw(sdc, w, h);
        draw(sdc, w, h);   //  항목 칸(r)은 첫 그림에서 정해진다
        wchar_t f[MAX_PATH];
        _snwprintf(f, MAX_PATH - 1, L"%ls\\tray@%d.bmp", dir, (int)(S * 100));
        f[MAX_PATH - 1] = 0;
        BITMAPFILEHEADER fh;
        BITMAPINFOHEADER ih;
        ZeroMemory(&fh, sizeof fh);
        ZeroMemory(&ih, sizeof ih);
        DWORD bytes = (DWORD)CVT.w * CVT.h * 4;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof fh + sizeof ih;
        fh.bfSize = fh.bfOffBits + bytes;
        ih.biSize = sizeof ih;
        ih.biWidth = CVT.w;
        ih.biHeight = -CVT.h;
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        ih.biCompression = BI_RGB;
        HANDLE fl = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fl != INVALID_HANDLE_VALUE) {
            DWORD n;
            WriteFile(fl, &fh, sizeof fh, &n, NULL);
            WriteFile(fl, &ih, sizeof ih, &n, NULL);
            WriteFile(fl, CVT.px, bytes, &n, NULL);
            CloseHandle(fl);
            saved++;
        }
    }
    SNAPPING = 0;
    cur = -1;
    DeleteDC(sdc);
    return saved;
}
