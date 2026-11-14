//  canvas 빠른 길(11/14) 점검 — 새 cv_round·cv_round_border 가 예전(한 칸씩 전부 계산) 것과 픽셀까지 똑같은지 + 걸린 시간
//  사용: cv_cli.exe [경우 수]   (화면에 아무것도 안 띄움 — 메모리 DC)
#include "../src/app/canvas.c"
#include <stdio.h>
#include "../src/app/strings.h"
#include "../src/base/utf.h"

//  예전 그대로 (canvas.c 11/14 이전)
static void old_round(canvas *c, float x, float y, float w, float h, float r, uint32_t rgb, float alpha) {
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
static void old_border(canvas *c, float x, float y, float w, float h, float r, float t, uint32_t rgb) {
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

static unsigned long long seed = 12345;
static unsigned rnd(void) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return (unsigned)(seed >> 33); }
static float frand(float lo, float hi) { return lo + (hi - lo) * (rnd() % 100000) / 100000.0f; }

int main(int argc, char **argv) {
    int cases = argc > 1 ? atoi(argv[1]) : 20000;
    HDC ref = CreateCompatibleDC(NULL);
    canvas A = { 0 }, B = { 0 };
    const int W = 400, H = 300;
    cv_resize(&A, ref, W, H);
    cv_resize(&B, ref, W, H);
    int bad = 0;
    for (int i = 0; i < cases && bad < 5; i++) {
        for (int k = 0; k < W * H; k++) A.px[k] = B.px[k] = rnd() & 0xffffff;   //  얼룩 바탕 (섞기 확인)
        if (rnd() % 3 == 0) { int cx = (int)(rnd() % W), cy = (int)(rnd() % H); cv_clip(&A, cx, cy, (int)(rnd() % W) + 1, (int)(rnd() % H) + 1); cv_clip(&B, cx, cy, A.clip.right - A.clip.left, A.clip.bottom - A.clip.top); }
        else { cv_clip(&A, 0, 0, 0, 0); cv_clip(&B, 0, 0, 0, 0); }
        //  크기: 아주 작은 것 · 알약(r 이 큼) · 화면 밖으로 넘침 · 소수점 위치
        float x = frand(-60, W), y = frand(-60, H), w = rnd() % 4 == 0 ? frand(0, 6) : frand(0, 450), h = rnd() % 4 == 0 ? frand(0, 6) : frand(0, 350);
        float r = rnd() % 5 == 0 ? frand(0, 400) : frand(0, 30), t = frand(0.5f, 4), al = rnd() % 3 == 0 ? 1.0f : frand(0, 1.2f);
        uint32_t col = rnd() & 0xffffff;
        int kind = (int)(rnd() % 3);
        if (kind == 0) { old_round(&A, x, y, w, h, r, col, al); cv_round(&B, x, y, w, h, r, col, al); }
        else if (kind == 1) { old_border(&A, x, y, w, h, r, t, col); cv_round_border(&B, x, y, w, h, r, t, col); }
        else { float cr = frand(0, 60); float cx = x + cr, cy = y + cr; old_round(&A, cx - cr, cy - cr, 2 * cr, 2 * cr, cr, col, 1); cv_circle(&B, cx, cy, cr, col);   /* cv_circle 과 같은 계산 순서 */ }
        if (memcmp(A.px, B.px, (size_t)W * H * 4)) {
            int at = 0;
            while (A.px[at] == B.px[at]) at++;
            printf("X 다름 #%d kind=%d x=%.3f y=%.3f w=%.3f h=%.3f r=%.3f t=%.3f a=%.3f 첫 차이 (%d,%d) %06x vs %06x\n", i, kind, x, y, w, h, r, t, al, at % W, at / W, A.px[at], B.px[at]);
            bad++;
        }
    }
    //  여러 줄 글: 화면 문구 전부(ko·en·ja) × 너비 × 글꼴 크기 — 예전 DrawText(DT_WORDBREAK) 와 높이·픽셀 비교
    {
        const char *const *TB[3] = { STR_KO, STR_EN, STR_JA };
        static const int WS[] = { 90, 140, 200, 260, 330, 420, 560, 700, 900 };
        static const int PX[] = { 13, 15, 19 };
        cv_resize(&A, ref, 1000, 600);
        cv_resize(&B, ref, 1000, 600);
        int tot = 0, hdiff = 0, pdiff = 0;
        LARGE_INTEGER tf, t0, t1, t2;
        QueryPerformanceFrequency(&tf);
        double old_ms = 0, new_ms = 0;
        for (int fi = 0; fi < 3; fi++) {
            HFONT fo = CreateFontW(-PX[fi], 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            for (int L = 0; L < 3; L++)
                for (int si = 0; si < S_COUNT; si++)
                    for (size_t wi = 0; wi < sizeof WS / sizeof WS[0]; wi++) {
                        const char *str = TB[L][si];
                        if (!str || !*str) continue;
                        int ww = WS[wi];
                        tot++;
                        wchar_t *wsx = utf8_to_w(str, strlen(str));
                        HFONT o2 = (HFONT)SelectObject(A.dc, fo);
                        RECT rc = { 0, 0, ww, 10000 };
                        DrawTextW(A.dc, wsx, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                        SelectObject(A.dc, o2);
                        int hn = cv_text_h(&B, fo, str, ww);
                        if (rc.bottom != hn) { if (hdiff < 6) { char sh[61]; snprintf(sh, sizeof sh, "%s", str); printf("  높이 다름 L%d #%d w=%d px=%d: %ld vs %d  \"%s\"\n", L, si, ww, PX[fi], rc.bottom, hn, sh); } hdiff++; }
                        for (int k = 0; k < 1000 * 600; k++) A.px[k] = B.px[k] = 0x1b1d22;
                        cv_clip(&A, 0, 0, 0, 0); cv_clip(&B, 0, 0, 0, 0);
                        QueryPerformanceCounter(&t0);
                        o2 = (HFONT)SelectObject(A.dc, fo);
                        SetTextColor(A.dc, RGB(0xc9, 0xce, 0xdb));
                        static const UINT FM[3] = { DT_LEFT | DT_WORDBREAK, DT_CENTER | DT_WORDBREAK, DT_LEFT | DT_VCENTER | DT_WORDBREAK };   /* 정렬 · 세로 가운데(여러 줄이면 무시) */
                        UINT fm = FM[(si + (int)wi) % 3];
                        int rh = (si + fi) % 2 ? 500 : 40;   /* 낮은 칸 = 아래 줄이 잘림 */
                        RECT ra = { 30, 20, 30 + ww, 20 + rh };
                        DrawTextW(A.dc, wsx, -1, &ra, fm | DT_NOPREFIX);
                        SelectObject(A.dc, o2);
                        QueryPerformanceCounter(&t1);
                        cv_text(&B, fo, str, 30, 20, ww, rh, 0xc9cedb, fm);
                        QueryPerformanceCounter(&t2);
                        old_ms += (t1.QuadPart - t0.QuadPart) * 1000.0 / tf.QuadPart;
                        new_ms += (t2.QuadPart - t1.QuadPart) * 1000.0 / tf.QuadPart;
                        GdiFlush();
                        if (memcmp(A.px, B.px, (size_t)1000 * 600 * 4)) { if (pdiff < 6) { char sh[61]; snprintf(sh, sizeof sh, "%s", str); printf("  그림 다름 L%d #%d w=%d px=%d \"%s\"\n", L, si, ww, PX[fi], sh); } pdiff++; }
                        free(wsx);
                    }
            DeleteObject(fo);
        }
        printf("%s 여러 줄 글 %d가지: 높이 다름 %d · 그림 다름 %d · 그리기 합계 %.0fms → %.0fms\n", hdiff || pdiff ? "X" : "OK", tot, hdiff, pdiff, old_ms, new_ms);
        if (hdiff || pdiff) bad++;
    }
    //  시간: 웹훅 탭 크기의 카드 (900×330 · 테두리) 200번
    cv_resize(&A, ref, 1000, 400);
    cv_clip(&A, 0, 0, 0, 0);
    LARGE_INTEGER f, q0, q1, q2;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    for (int k = 0; k < 200; k++) { old_round(&A, 20.3f, 10.6f, 900, 330, 10, 0x202020, 0.86f); old_border(&A, 20.3f, 10.6f, 900, 330, 10, 1, 0x404040); }
    QueryPerformanceCounter(&q1);
    for (int k = 0; k < 200; k++) { cv_round(&A, 20.3f, 10.6f, 900, 330, 10, 0x202020, 0.86f); cv_round_border(&A, 20.3f, 10.6f, 900, 330, 10, 1, 0x404040); }
    QueryPerformanceCounter(&q2);
    double o = (q1.QuadPart - q0.QuadPart) * 1000.0 / f.QuadPart / 200, n = (q2.QuadPart - q1.QuadPart) * 1000.0 / f.QuadPart / 200;
    printf("%s canvas %d가지 픽셀 비교 · 큰 카드 한 장 %.3fms → %.3fms (%.1f배)\n", bad ? "X" : "OK", cases, o, n, o / n);
    return bad ? 1 : 0;
}
