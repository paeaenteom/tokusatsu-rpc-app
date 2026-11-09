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

void cv_round(canvas *c, float x, float y, float w, float h, float r, uint32_t rgb, float alpha) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float a = cover(xx + 0.5f, yy + 0.5f, x, y, w, h, r);
            if (a > 0) blend(&c->px[yy * c->w + xx], rgb, a * alpha);
        }
}

void cv_round_border(canvas *c, float x, float y, float w, float h, float r, float t, uint32_t rgb) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < c->clip.left) x0 = c->clip.left;
    if (y0 < c->clip.top) y0 = c->clip.top;
    if (x1 > c->clip.right) x1 = c->clip.right;
    if (y1 > c->clip.bottom) y1 = c->clip.bottom;
    float ri = r - t < 0 ? 0 : r - t;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float a = cover(xx + 0.5f, yy + 0.5f, x, y, w, h, r) - cover(xx + 0.5f, yy + 0.5f, x + t, y + t, w - 2 * t, h - 2 * t, ri);
            if (a > 0) blend(&c->px[yy * c->w + xx], rgb, a);
        }
}

void cv_circle(canvas *c, float cx, float cy, float r, uint32_t rgb) { cv_round(c, cx - r, cy - r, 2 * r, 2 * r, r, rgb, 1); }

void cv_text(canvas *c, HFONT f, const char *s, int x, int y, int w, int h, uint32_t rgb, UINT fmt) {
    if (!s || !*s) return;
    wchar_t *ws = utf8_to_w(s, strlen(s));
    if (!ws) return;
    HFONT o = (HFONT)SelectObject(c->dc, f);
    SetTextColor(c->dc, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
    RECT r = { x, y, x + w, y + h };
    DrawTextW(c->dc, ws, -1, &r, fmt | DT_NOPREFIX);
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
