#include "canvas.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../base/utf.h"

void cv_resize(canvas *c, HDC ref, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (c->dc && c->w == w && c->h == h) return;
    cv_free(c);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    c->dc = CreateCompatibleDC(ref);
    c->bmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, (void **)&c->px, NULL, 0);
    c->old = (HBITMAP)SelectObject(c->dc, c->bmp);
    c->w = w;
    c->h = h;
    SetBkMode(c->dc, TRANSPARENT);
    cv_clip(c, 0, 0, 0, 0);
}

void cv_free(canvas *c) {
    if (c->dc) { SelectObject(c->dc, c->old); DeleteObject(c->bmp); DeleteDC(c->dc); }
    c->dc = NULL;
    c->px = NULL;
}

void cv_clip(canvas *c, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) { c->clip.left = 0; c->clip.top = 0; c->clip.right = c->w; c->clip.bottom = c->h; }
    else { c->clip.left = x; c->clip.top = y; c->clip.right = x + w; c->clip.bottom = y + h; }
    if (c->clip.left < 0) c->clip.left = 0;
    if (c->clip.top < 0) c->clip.top = 0;
    if (c->clip.right > c->w) c->clip.right = c->w;
    if (c->clip.bottom > c->h) c->clip.bottom = c->h;
    SelectClipRgn(c->dc, NULL);
    HRGN r = CreateRectRgn(c->clip.left, c->clip.top, c->clip.right, c->clip.bottom);
    SelectClipRgn(c->dc, r);
    DeleteObject(r);
}

static inline void blend(uint32_t *p, uint32_t rgb, float a) {
    if (a <= 0) return;
    if (a >= 1) { *p = rgb; return; }
    uint32_t d = *p;
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * a + ((d >> 16) & 255) * (1 - a));
    uint32_t g = (uint32_t)(((rgb >> 8) & 255) * a + ((d >> 8) & 255) * (1 - a));
    uint32_t b = (uint32_t)((rgb & 255) * a + (d & 255) * (1 - a));
    *p = (r << 16) | (g << 8) | b;
}

void cv_fill(canvas *c, int x, int y, int w, int h, uint32_t rgb) {
    int x0 = x < c->clip.left ? c->clip.left : x, y0 = y < c->clip.top ? c->clip.top : y;
    int x1 = x + w > c->clip.right ? c->clip.right : x + w, y1 = y + h > c->clip.bottom ? c->clip.bottom : y + h;
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) c->px[yy * c->w + xx] = rgb;
}

// 둥근 사각형 안쪽 비율 (픽셀 가운데 기준, 가장자리는 0~1)
static float cover(float px, float py, float x, float y, float w, float h, float r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    float dx = 0, dy = 0;
    if (px < x + r) dx = x + r - px; else if (px > x + w - r) dx = px - (x + w - r);
    if (py < y + r) dy = y + r - py; else if (py > y + h - r) dy = py - (y + h - r);
    if (dx > 0 && dy > 0) {
        float d = sqrtf(dx * dx + dy * dy);
        float v = r - d + 0.5f;
        return v < 0 ? 0 : v > 1 ? 1 : v;
    }
    float e = px - x;
    if (x + w - px < e) e = x + w - px;
    if (py - y < e) e = py - y;
    if (y + h - py < e) e = y + h - py;
    e += 0.5f;
    return e < 0 ? 0 : e > 1 ? 1 : e;
}

//  이 줄(yy)에서 cover() = 1 이 확실한 칸 [*xa, *xb] — 가장자리에서 1칸 넘게 떨어진 곳만 (11/14 최적화)
//  그 밖은 예전처럼 한 칸씩 계산하므로 그림은 한 점도 안 바뀐다 (tests/cv_cli.c 가 예전 함수와 비교)
static int full_span(int yy, float x, float y, float w, float h, float r, int *xa, int *xb) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    float py = yy + 0.5f;
    if (py - y < 1.5f || y + h - py < 1.5f) return 0;
    float l, rr;
    if (py >= y + r + 1 && py <= y + h - r - 1) { l = x; rr = x + w; }   //  곧은 줄: 양 끝 말고 다
    else { l = x + r; rr = x + w - r; }                                    //  모서리 줄: 가운데만
    int a = (int)ceilf(l) + 1, b = (int)floorf(rr) - 2;
    if (a > b) return 0;
    *xa = a;
    *xb = b;
    return 1;
}

void cv_round(canvas *c, float x, float y, float w, float h, float r, uint32_t rgb, float alpha) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    for (int yy = y0; yy < y1; yy++) {
        int xa = 0, xb = -1;
        if (full_span(yy, x, y, w, h, r, &xa, &xb)) {
            if (xa < x0) xa = x0;
            if (xb > x1 - 1) xb = x1 - 1;
        }
        uint32_t *row = &c->px[yy * c->w];
        for (int xx = x0; xx < x1; xx++) {
            if (xx == xa && xa <= xb) {   //  안쪽은 그대로 칠한다 (cover = 1)
                if (alpha >= 1) for (int k = xa; k <= xb; k++) row[k] = rgb;
                else for (int k = xa; k <= xb; k++) blend(&row[k], rgb, alpha);
                xx = xb;
                continue;
            }
            float a = cover(xx + 0.5f, yy + 0.5f, x, y, w, h, r);
            if (a > 0) blend(&row[xx], rgb, a * alpha);
        }
    }
}

void cv_round_border(canvas *c, float x, float y, float w, float h, float r, float t, uint32_t rgb) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    float ri = r - t < 0 ? 0 : r - t;
    for (int yy = y0; yy < y1; yy++) {
        //  안쪽 사각형이 꽉 찬 칸은 바깥도 꽉 차서 테두리 = 0 → 건너뛴다
        int xa = 0, xb = -1;
        if (full_span(yy, x + t, y + t, w - 2 * t, h - 2 * t, ri, &xa, &xb)) {
            if (xa < x0) xa = x0;
            if (xb > x1 - 1) xb = x1 - 1;
        }
        for (int xx = x0; xx < x1; xx++) {
            if (xx == xa && xa <= xb) { xx = xb; continue; }
            float a = cover(xx + 0.5f, yy + 0.5f, x, y, w, h, r) - cover(xx + 0.5f, yy + 0.5f, x + t, y + t, w - 2 * t, h - 2 * t, ri);
            if (a > 0) blend(&c->px[yy * c->w + xx], rgb, a);
        }
    }
}

void cv_circle(canvas *c, float cx, float cy, float r, uint32_t rgb) { cv_round(c, cx - r, cy - r, 2 * r, 2 * r, r, rgb, 1); }

// ── 여러 줄 글 (DT_WORDBREAK) — 줄 나눔을 한 번만 재고 기억한다 (11/14: DrawText 줄바꿈이 한 번에 7~10ms 라 웹훅 탭이 버벅임)
//  나누는 법: DrawText 처럼 띄어쓰기에서 (\n 은 늘 줄바꿈), 띄어쓰기 없이 넘치면 넘치는 자리에서. 줄마다 DrawText 한 줄로 그린다
//  키 = 글꼴(핸들 + 글자 높이·평균 폭) · 너비 · 글 내용. 창 스레드 하나에서만 쓴다
#define WRAP_N 96
typedef struct { HFONT f; LONG th, tw; int w, nlines; wchar_t *s; int *ls; int lh; DWORD used; int exact, h; } wrapent;   //  exact=0 → 예전처럼 DrawText 로 (h = 그때 높이)
static wrapent WR[WRAP_N];
static DWORD WR_TICK;
static int is_space(wchar_t ch) { return ch == L' ' || ch == L'\t' || ch == 0x3000; }
static int is_cjk(wchar_t ch) {
    return (ch >= 0xAC00 && ch <= 0xD7A3) || (ch >= 0x1100 && ch <= 0x11FF) || (ch >= 0x3130 && ch <= 0x318F)   //  한글
        || (ch >= 0x3040 && ch <= 0x30FF) || (ch >= 0x31F0 && ch <= 0x31FF)                                   //  가나
        || (ch >= 0x3400 && ch <= 0x4DBF) || (ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF); //  한자
}
static const wrapent *wrap_get(canvas *c, const wchar_t *ws, int w) {
    TEXTMETRICW tm;
    GetTextMetricsW(c->dc, &tm);
    HFONT f = (HFONT)GetCurrentObject(c->dc, OBJ_FONT);
    WR_TICK++;
    for (int i = 0; i < WRAP_N; i++)
        if (WR[i].s && WR[i].f == f && WR[i].th == tm.tmHeight && WR[i].tw == tm.tmAveCharWidth && WR[i].w == w && !wcscmp(WR[i].s, ws)) { WR[i].used = WR_TICK; return &WR[i]; }
    //  비었거나 가장 오래 안 쓴 칸
    int k = 0;
    for (int i = 0; i < WRAP_N; i++) { if (!WR[i].s) { k = i; break; } if (WR[i].used < WR[k].used) k = i; }
    wrapent *e = &WR[k];
    free(e->s);
    free(e->ls);
    memset(e, 0, sizeof *e);
    size_t n = wcslen(ws);
    e->s = _wcsdup(ws);
    e->ls = (int *)malloc(sizeof(int) * 2 * (n + 1));
    if (!e->s || !e->ls) { free(e->s); free(e->ls); memset(e, 0, sizeof *e); return NULL; }
    e->f = f; e->th = tm.tmHeight; e->tw = tm.tmAveCharWidth; e->w = w; e->lh = tm.tmHeight; e->used = WR_TICK;
    //  줄 나눔은 DrawText 에게 직접 묻는다 — 한 줄 높이 칸에 그리게 하면 uiLengthDrawn = 다음 줄이 시작하는 글자 (처음 한 번만)
    //  그리는 곳은 1×1 메모리 DC (화면·캔버스에 안 그림). 같은 글꼴
    static HDC mdc;
    static HBITMAP mbm;
    if (!mdc) { mdc = CreateCompatibleDC(c->dc); mbm = CreateCompatibleBitmap(c->dc, 1, 1); SelectObject(mdc, mbm); SetBkMode(mdc, TRANSPARENT); }
    HFONT mo = (HFONT)SelectObject(mdc, f);
    int nl = 0;
    size_t a = 0;
    while (a < n || (a == n && nl == 0)) {
        DRAWTEXTPARAMS dp;
        memset(&dp, 0, sizeof dp);
        dp.cbSize = sizeof dp;
        RECT lr = { 0, 0, w > 0 ? w : 1, tm.tmHeight };
        wchar_t *seg = e->s + a;   //  DrawTextEx 가 글을 고칠 수 있어 사본(e->s)으로
        if (!DrawTextExW(mdc, seg, (int)(n - a), &lr, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX, &dp)) break;   //  EDITCONTROL = 칸 밖 줄은 처리 안 함 → 첫 줄 길이만
        size_t used = dp.uiLengthDrawn;
        if (used == 0 || a + used > n) used = n - a;   //  이상하면 남은 것 전부 (무한 반복 방지)
        if (used > n - a) used = n - a;
        //  이 줄에 그릴 글 = [a, a+used) 에서 줄 끝 \r\n 은 빼고 (DrawText 도 안 그림)
        size_t len = used;
        while (len && (seg[len - 1] == L'\n' || seg[len - 1] == L'\r' || is_space(seg[len - 1]))) len--;   //  줄 끝 띄어쓰기도 (가운데 정렬이 DrawText 와 같게)
        e->ls[nl * 2] = (int)a;
        e->ls[nl * 2 + 1] = (int)len;
        nl++;
        a += used;
        if (used == 0) break;
    }
    //  긴 영어 단어를 칸 너비에서 자른 줄이 있으면(EDITCONTROL 만 하는 일) 예전 DrawText 와 달라진다 → 이 글은 예전 방식으로
    e->exact = 1;
    for (int i = 0; i + 1 < nl; i++) {
        size_t at = (size_t)e->ls[(i + 1) * 2];
        if (!at || at >= n) continue;
        wchar_t c1 = ws[at - 1], c2 = ws[at];
        if (!is_space(c1) && !is_space(c2) && c1 != L'\n' && c1 != L'\r' && !is_cjk(c1) && !is_cjk(c2)) { e->exact = 0; break; }
    }
    if (!e->exact) {
        RECT r = { 0, 0, w, 100000 };
        DrawTextW(mdc, ws, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        e->h = r.bottom;
    } else e->h = nl * e->lh;
    SelectObject(mdc, mo);
    e->nlines = nl;
    return e;
}
//  여러 줄 글 높이 (DrawText DT_CALCRECT|DT_WORDBREAK 대신)
int cv_text_h(canvas *c, HFONT f, const char *s, int w) {
    if (!s || !*s) return 0;
    wchar_t *ws = utf8_to_w(s, strlen(s));
    if (!ws) return 0;
    HFONT o = (HFONT)SelectObject(c->dc, f);
    const wrapent *e = wrap_get(c, ws, w);
    int h = e ? e->h : 0;
    if (!e) { RECT r = { 0, 0, w, 10000 }; DrawTextW(c->dc, ws, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX); h = r.bottom; }
    SelectObject(c->dc, o);
    free(ws);
    return h;
}

void cv_text(canvas *c, HFONT f, const char *s, int x, int y, int w, int h, uint32_t rgb, UINT fmt) {
    if (!s || !*s) return;
    wchar_t *ws = utf8_to_w(s, strlen(s));
    if (!ws) return;
    HFONT o = (HFONT)SelectObject(c->dc, f);
    SetTextColor(c->dc, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
    RECT r = { x, y, x + w, y + h };
    const wrapent *e = (fmt & DT_WORDBREAK) && !(fmt & (DT_SINGLELINE | DT_CALCRECT)) ? wrap_get(c, ws, w) : NULL;
    if (e && e->exact) {   //  여러 줄: 위에서부터 한 줄씩 (DrawText 여러 줄과 같게 DT_VCENTER 는 안 씀), 칸 밖은 잘린다
        UINT lf = (fmt & (DT_CENTER | DT_RIGHT)) | DT_SINGLELINE | DT_NOPREFIX;
        for (int i = 0; i < e->nlines; i++) {
            RECT lr = { x, y + i * e->lh, x + w, y + (i + 1) * e->lh };
            if (lr.top >= r.bottom) break;
            if (lr.bottom > r.bottom) lr.bottom = r.bottom;
            if (e->ls[i * 2 + 1] > 0) DrawTextW(c->dc, e->s + e->ls[i * 2], e->ls[i * 2 + 1], &lr, lf);
        }
    } else DrawTextW(c->dc, ws, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(c->dc, o);
    free(ws);
}

int cv_text_w(canvas *c, HFONT f, const char *s) {
    if (!s || !*s) return 0;
    wchar_t *ws = utf8_to_w(s, strlen(s));
    if (!ws) return 0;
    HFONT o = (HFONT)SelectObject(c->dc, f);
    RECT r = { 0, 0, 10000, 1000 };
    DrawTextW(c->dc, ws, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(c->dc, o);
    free(ws);
    return r.right;
}

//  정사각 그림을 size 로 줄여 그린다 (칸 평균 — 늘 줄이는 쪽). 가장자리는 cover() 로 부드럽게
void cv_image(canvas *c, const uint32_t *src, int n, float x, float y, float size, float r) {
    if (!src || n <= 0 || size <= 0) return;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + size), y1 = (int)ceilf(y + size);
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    float k = n / size;
    for (int yy = y0; yy < y1; yy++) {
        int sy0 = (int)floorf((yy - y) * k), sy1 = (int)ceilf((yy + 1 - y) * k);
        if (sy0 < 0) sy0 = 0;
        if (sy1 > n) sy1 = n;
        if (sy1 <= sy0) sy1 = sy0 + 1 <= n ? sy0 + 1 : sy0;
        for (int xx = x0; xx < x1; xx++) {
            float m = cover(xx + 0.5f, yy + 0.5f, x, y, size, size, r);
            if (m <= 0) continue;
            int sx0 = (int)floorf((xx - x) * k), sx1 = (int)ceilf((xx + 1 - x) * k);
            if (sx0 < 0) sx0 = 0;
            if (sx1 > n) sx1 = n;
            if (sx1 <= sx0) sx1 = sx0 + 1 <= n ? sx0 + 1 : sx0;
            unsigned sa = 0, sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    uint32_t p = src[sy * n + sx];
                    sa += p >> 24; sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255; cnt++;
                }
            if (!cnt) continue;
            float a = (float)sa / cnt / 255.0f * m;   // 덮는 비율
            if (a <= 0) continue;
            uint32_t *d = &c->px[yy * c->w + xx], dv = *d;
            float inv = 1 - a;
            //  미리 곱한 색 × 가장자리 비율 + 바탕 × (1 - 덮는 비율)
            uint32_t rr = (uint32_t)fminf(255, (float)sr / cnt * m + ((dv >> 16) & 255) * inv);
            uint32_t gg = (uint32_t)fminf(255, (float)sg / cnt * m + ((dv >> 8) & 255) * inv);
            uint32_t bb = (uint32_t)fminf(255, (float)sb / cnt * m + (dv & 255) * inv);
            *d = (rr << 16) | (gg << 8) | bb;
        }
    }
}

void cv_blit(canvas *c, const uint32_t *src, int sw, int sh, float dx, float dy, float scale, int cx, int cy, int cw, int ch, int circle) {
    if (!src || sw <= 0 || sh <= 0 || scale <= 0) return;
    int x0 = cx, y0 = cy, x1 = cx + cw, y1 = cy + ch;
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    float r = cw < ch ? cw / 2.0f : ch / 2.0f;
    for (int yy = y0; yy < y1; yy++) {
        float v = (yy + 0.5f - dy) / scale - 0.5f;
        if (v < -0.5f || v > sh - 0.5f) continue;
        int vy = (int)floorf(v);
        float fy = v - vy;
        int ya = vy < 0 ? 0 : vy, yb = vy + 1 >= sh ? sh - 1 : vy + 1;
        for (int xx = x0; xx < x1; xx++) {
            float m = circle ? cover(xx + 0.5f, yy + 0.5f, (float)cx, (float)cy, (float)cw, (float)ch, r) : 1.0f;
            if (m <= 0) continue;
            float u = (xx + 0.5f - dx) / scale - 0.5f;
            if (u < -0.5f || u > sw - 0.5f) continue;
            int ux = (int)floorf(u);
            float fx = u - ux;
            int xa = ux < 0 ? 0 : ux, xb = ux + 1 >= sw ? sw - 1 : ux + 1;
            uint32_t p00 = src[ya * sw + xa], p01 = src[ya * sw + xb], p10 = src[yb * sw + xa], p11 = src[yb * sw + xb];
            float ch4[4];
            for (int k = 0; k < 4; k++) {
                int sh8 = k * 8;
                float a = ((p00 >> sh8) & 255) * (1 - fx) + ((p01 >> sh8) & 255) * fx;
                float b = ((p10 >> sh8) & 255) * (1 - fx) + ((p11 >> sh8) & 255) * fx;
                ch4[k] = a * (1 - fy) + b * fy;
            }
            float al = ch4[3] / 255.0f * m;
            if (al <= 0) continue;
            uint32_t *d = &c->px[yy * c->w + xx], dv = *d;
            float inv = 1 - al;
            uint32_t rr = (uint32_t)fminf(255, ch4[2] * m + ((dv >> 16) & 255) * inv);
            uint32_t gg = (uint32_t)fminf(255, ch4[1] * m + ((dv >> 8) & 255) * inv);
            uint32_t bb = (uint32_t)fminf(255, ch4[0] * m + (dv & 255) * inv);
            *d = (rr << 16) | (gg << 8) | bb;
        }
    }
}
