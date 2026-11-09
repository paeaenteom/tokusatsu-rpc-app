// 썸네일 → 정주행 기록 카드 왼쪽 줄 색 (지금 앱 thumb-color.js 와 같은 계산)
//  ⚠ 일부러 다른 점: 지금 앱은 Electron nativeImage(Skia)로 풀고 줄이고, 여기선 WIC(Fant)로 — 줄인 픽셀이 조금 달라
//    고르는 색이 아주 조금 다를 수 있다 (같은 칸·비슷한 색). 계산식은 같다.
//  부르는 스레드에서 COM 이 초기화돼 있어야 한다
#define COBJMACROS
#include "accent.h"
#include <windows.h>
#include <wincodec.h>
#include <math.h>
#include <stdlib.h>
#include "../core/js.h"

#define SAMPLE_W 48
#define HUE_BINS 24
#define MIN_SAT 0.28
#define MIN_VAL 0.22
#define MIN_SHARE 0.03

static void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l) {
    r /= 255; g /= 255; b /= 255;
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    *l = (mx + mn) / 2;
    if (mx == mn) { *h = 0; *s = 0; return; }
    double d = mx - mn;
    *s = *l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    double hh = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    *h = hh / 6;
}
static double hue_f(double p, double q, double t) {
    t = fmod(t + 1, 1);
    return t < 1.0 / 6 ? p + (q - p) * 6 * t : t < 1.0 / 2 ? q : t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6 : p;
}
static int c255(double v) { double x = js_round(v * 255); return x < 0 ? 0 : x > 255 ? 255 : (int)x; }
static int hsl_to_int(double h, double s, double l) {
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    return (c255(hue_f(p, q, h + 1.0 / 3)) << 16) | (c255(hue_f(p, q, h)) << 8) | c255(hue_f(p, q, h - 1.0 / 3));
}

//  px: B G R A 순서 4바이트씩
int accent_from_pixels(const unsigned char *px, size_t n, int *out) {
    struct { double w, r, g, b; long n; } bins[HUE_BINS];
    memset(bins, 0, sizeof bins);
    long counted = 0;
    for (size_t i = 0; i < n; i++) {
        const unsigned char *k = px + i * 4;
        if (k[3] < 128) continue;
        counted++;
        int r = k[2], g = k[1], b = k[0];
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        double v = mx / 255.0;
        double sat = mx ? (double)(mx - mn) / mx : 0;
        if (sat < MIN_SAT || v < MIN_VAL) continue;
        double d = mx - mn, h;
        if (mx == r) h = fmod((g - b) / d + 6, 6); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
        int bi = (int)floor((h / 6) * HUE_BINS);
        if (bi > HUE_BINS - 1) bi = HUE_BINS - 1;
        double w = sat * v;
        bins[bi].w += w; bins[bi].r += r * w; bins[bi].g += g * w; bins[bi].b += b * w; bins[bi].n++;
    }
    if (!counted) return 0;
    int best = -1;
    double best_score = 0;
    for (int k = 0; k < HUE_BINS; k++) {
        double score = bins[k].w + 0.5 * (bins[(k + HUE_BINS - 1) % HUE_BINS].w + bins[(k + 1) % HUE_BINS].w);
        if (bins[k].n && score > best_score) { best_score = score; best = k; }
    }
    if (best < 0 || (double)bins[best].n / counted < MIN_SHARE) return 0;
    double h, s, l;
    rgb_to_hsl(bins[best].r / bins[best].w, bins[best].g / bins[best].w, bins[best].b / bins[best].w, &h, &s, &l);
    *out = hsl_to_int(h, fmax(s, 0.45), fmin(0.68, fmax(0.45, l)));
    return 1;
}

int accent_from_buffer(const unsigned char *data, size_t len, int *out) {
    int ok = 0;
    IWICImagingFactory *f = NULL;
    IWICStream *st = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *fr = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    unsigned char *px = NULL;
    UINT w = 0, h = 0;
    if (!data || !len || len > 0xFFFFFFFFu) return 0;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f))) return 0;
    if (FAILED(IWICImagingFactory_CreateStream(f, &st)) || FAILED(IWICStream_InitializeFromMemory(st, (BYTE *)data, (DWORD)len))) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)st, NULL, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &fr)) || FAILED(IWICBitmapFrameDecode_GetSize(fr, &w, &h)) || !w || !h || w > 16384 || h > 16384) goto done;
    //  가로가 48 보다 크면 가로 48 로 (비율 유지 — nativeImage.resize({ width }))
    UINT tw = w, th = h;
    if (w > SAMPLE_W) { tw = SAMPLE_W; th = (UINT)js_round((double)h * SAMPLE_W / w); if (th < 1) th = 1; }
    IWICBitmapSource *src = (IWICBitmapSource *)fr;
    if (tw != w) {
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) || FAILED(IWICBitmapScaler_Initialize(sc, src, tw, th, WICBitmapInterpolationModeFant))) goto done;
        src = (IWICBitmapSource *)sc;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(cv, src, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    px = (unsigned char *)malloc((size_t)tw * th * 4);
    if (!px || FAILED(IWICFormatConverter_CopyPixels(cv, NULL, tw * 4, tw * th * 4, px))) goto done;
    ok = accent_from_pixels(px, (size_t)tw * th, out);
done:
    free(px);
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (st) IWICStream_Release(st);
    IWICImagingFactory_Release(f);
    return ok;
}
