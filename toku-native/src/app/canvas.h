// 그리기 — 32비트 DIB 에 직접 칠한다. 둥근 사각형·원은 가장자리를 부드럽게(안티에일리어싱), 글자는 GDI(ClearType)
//  색은 0xRRGGBB
#ifndef TOKU_CANVAS_H
#define TOKU_CANVAS_H
#include <windows.h>
#include <stdint.h>

typedef struct {
    HDC dc;
    HBITMAP bmp, old;
    uint32_t *px;
    int w, h;
    RECT clip;   // 칠할 수 있는 영역 (스크롤 영역 자르기용)
} canvas;

void cv_resize(canvas *c, HDC ref, int w, int h);
void cv_free(canvas *c);
void cv_clip(canvas *c, int x, int y, int w, int h);   // w<=0 이면 전체
void cv_fill(canvas *c, int x, int y, int w, int h, uint32_t rgb);
void cv_round(canvas *c, float x, float y, float w, float h, float r, uint32_t rgb, float alpha);
void cv_round_border(canvas *c, float x, float y, float w, float h, float r, float t, uint32_t rgb);
void cv_circle(canvas *c, float cx, float cy, float r, uint32_t rgb);
void cv_image(canvas *c, const uint32_t *src, int n, float x, float y, float size, float r);   // n×n 미리 곱한 BGRA 를 둥근 사각형(r=size/2 면 원)으로
//  sw×sh 그림을 (dx, dy) 에 scale 배로 — (cx, cy, cw, ch) 안만, circle 이면 그 칸에 꼭 맞는 원 안만 (양선형)
void cv_blit(canvas *c, const uint32_t *src, int sw, int sh, float dx, float dy, float scale, int cx, int cy, int cw, int ch, int circle);
// 글자 (UTF-8). fmt = DT_* (DT_SINGLELINE|DT_END_ELLIPSIS 등)
void cv_text(canvas *c, HFONT f, const char *s, int x, int y, int w, int h, uint32_t rgb, UINT fmt);
int cv_text_w(canvas *c, HFONT f, const char *s);
int cv_text_h(canvas *c, HFONT f, const char *s, int w);   // 여러 줄(DT_WORDBREAK) 높이 — 줄 나눔은 기억해 둔다

#endif
