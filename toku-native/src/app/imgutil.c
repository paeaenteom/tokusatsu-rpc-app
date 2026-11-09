#define COBJMACROS
#include "imgutil.h"
#include <windows.h>
#include <wincodec.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static IWICImagingFactory *factory(void) {
    IWICImagingFactory *f = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f))) return NULL;
    return f;
}
static IWICBitmapFrameDecode *open_frame(IWICImagingFactory *f, const unsigned char *data, size_t len, IWICStream **st, IWICBitmapDecoder **dec) {
    IWICBitmapFrameDecode *fr = NULL;
    *st = NULL; *dec = NULL;
    if (!data || !len || len > 0x7FFFFFFF) return NULL;
    if (FAILED(IWICImagingFactory_CreateStream(f, st)) || FAILED(IWICStream_InitializeFromMemory(*st, (BYTE *)data, (DWORD)len))) return NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)*st, NULL, WICDecodeMetadataCacheOnDemand, dec))) return NULL;
    if (FAILED(IWICBitmapDecoder_GetFrame(*dec, 0, &fr))) return NULL;   //  GIF 는 첫 장면
    return fr;
}

uint32_t *iu_decode(const unsigned char *data, size_t len, int max_side, int *w, int *h, int *ow, int *oh) {
    uint32_t *out = NULL;
    IWICImagingFactory *f = factory();
    if (!f) return NULL;
    IWICStream *st;
    IWICBitmapDecoder *dec;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    IWICBitmapFrameDecode *fr = open_frame(f, data, len, &st, &dec);
    UINT W = 0, H = 0;
    if (!fr || FAILED(IWICBitmapFrameDecode_GetSize(fr, &W, &H)) || !W || !H || W > 30000 || H > 30000) goto done;
    *ow = (int)W; *oh = (int)H;
    UINT tw = W, th = H;
    if ((int)W > max_side || (int)H > max_side) {
        double k = (double)max_side / (W > H ? W : H);
        tw = (UINT)fmax(1, floor(W * k + 0.5)); th = (UINT)fmax(1, floor(H * k + 0.5));
    }
    IWICBitmapSource *src = (IWICBitmapSource *)fr;
    if (tw != W || th != H) {
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) || FAILED(IWICBitmapScaler_Initialize(sc, src, tw, th, WICBitmapInterpolationModeFant))) goto done;
        src = (IWICBitmapSource *)sc;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, src, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    out = (uint32_t *)malloc((size_t)tw * th * 4);
    if (out && FAILED(IWICFormatConverter_CopyPixels(cv, NULL, tw * 4, tw * th * 4, (BYTE *)out))) { free(out); out = NULL; }
    if (out) { *w = (int)tw; *h = (int)th; }
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (st) IWICStream_Release(st);
    IWICImagingFactory_Release(f);
    return out;
}

unsigned char *iu_square_png(const unsigned char *data, size_t len, double x, double y, double size, int n, size_t *out_len) {
    unsigned char *res = NULL;
    *out_len = 0;
    IWICImagingFactory *f = factory();
    if (!f) return NULL;
    IWICStream *st, *ost = NULL;
    IWICBitmapDecoder *dec;
    IWICBitmapClipper *cl = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *fe = NULL;
    IStream *mem = NULL;
    IWICBitmapFrameDecode *fr = open_frame(f, data, len, &st, &dec);
    UINT W = 0, H = 0;
    if (!fr || FAILED(IWICBitmapFrameDecode_GetSize(fr, &W, &H)) || !W || !H) goto done;
    //  자리를 그림 안으로 (정수 픽셀)
    int s = (int)floor(size + 0.5);
    if (s < 1) s = 1;
    if (s > (int)W) s = (int)W;
    if (s > (int)H) s = (int)H;
    int x0 = (int)floor(x + 0.5), y0 = (int)floor(y + 0.5);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + s > (int)W) x0 = (int)W - s;
    if (y0 + s > (int)H) y0 = (int)H - s;
    WICRect r = { x0, y0, s, s };
    if (FAILED(IWICImagingFactory_CreateBitmapClipper(f, &cl)) || FAILED(IWICBitmapClipper_Initialize(cl, (IWICBitmapSource *)fr, &r))) goto done;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) || FAILED(IWICBitmapScaler_Initialize(sc, (IWICBitmapSource *)cl, (UINT)n, (UINT)n, WICBitmapInterpolationModeFant))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)sc, &GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &mem))) goto done;
    if (FAILED(IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &enc)) || FAILED(IWICBitmapEncoder_Initialize(enc, mem, WICBitmapEncoderNoCache))) goto done;
    if (FAILED(IWICBitmapEncoder_CreateNewFrame(enc, &fe, NULL)) || FAILED(IWICBitmapFrameEncode_Initialize(fe, NULL))) goto done;
    if (FAILED(IWICBitmapFrameEncode_SetSize(fe, (UINT)n, (UINT)n))) goto done;
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(IWICBitmapFrameEncode_SetPixelFormat(fe, &pf))) goto done;
    if (FAILED(IWICBitmapFrameEncode_WriteSource(fe, (IWICBitmapSource *)cv, NULL)) || FAILED(IWICBitmapFrameEncode_Commit(fe)) || FAILED(IWICBitmapEncoder_Commit(enc))) goto done;
    {
        HGLOBAL hg = NULL;
        if (SUCCEEDED(GetHGlobalFromStream(mem, &hg))) {
            STATSTG ss;
            if (SUCCEEDED(IStream_Stat(mem, &ss, STATFLAG_NONAME))) {
                size_t sz = (size_t)ss.cbSize.QuadPart;
                void *p = GlobalLock(hg);
                if (p && sz) { res = (unsigned char *)malloc(sz); if (res) { memcpy(res, p, sz); *out_len = sz; } }
                if (p) GlobalUnlock(hg);
            }
        }
    }
done:
    if (fe) IWICBitmapFrameEncode_Release(fe);
    if (enc) IWICBitmapEncoder_Release(enc);
    if (mem) IStream_Release(mem);
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (cl) IWICBitmapClipper_Release(cl);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (st) IWICStream_Release(st);
    if (ost) IWICStream_Release(ost);
    IWICImagingFactory_Release(f);
    return res;
}

char *iu_base64(const unsigned char *b, size_t n) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *o = (char *)malloc((n + 2) / 3 * 4 + 1);
    if (!o) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)b[i] << 16 | (i + 1 < n ? (unsigned)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
        o[k++] = T[v >> 18 & 63];
        o[k++] = T[v >> 12 & 63];
        o[k++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        o[k++] = i + 2 < n ? T[v & 63] : '=';
    }
    o[k] = 0;
    return o;
}

int iu_read_file(const wchar_t *path, size_t max, unsigned char **buf, size_t *n) {
    *buf = NULL; *n = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    int r = 0;
    if (GetFileSizeEx(h, &sz)) {
        if ((unsigned long long)sz.QuadPart > max) r = -1;
        else if (sz.QuadPart > 0) {
            *buf = (unsigned char *)malloc((size_t)sz.QuadPart);
            DWORD got = 0;
            if (*buf && ReadFile(h, *buf, (DWORD)sz.QuadPart, &got, NULL) && got == sz.QuadPart) { *n = got; r = 1; }
            else { free(*buf); *buf = NULL; }
        }
    }
    CloseHandle(h);
    return r;
}
