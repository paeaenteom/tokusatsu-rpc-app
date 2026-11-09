// imgutil.c 점검 — iu_cli <입력> <출력.png> x y size n  → 잘라 n×n PNG 로 쓴다. 성공하면 0
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/app/imgutil.h"

int wmain(int argc, wchar_t **argv) {
    if (argc < 7) return 2;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    unsigned char *b; size_t n;
    if (iu_read_file(argv[1], 64 * 1024 * 1024, &b, &n) != 1) return 3;
    int w, h, ow, oh;
    uint32_t *px = iu_decode(b, n, 64, &w, &h, &ow, &oh);
    if (!px) return 4;
    printf("orig %d %d small %d %d\n", ow, oh, w, h);
    free(px);
    size_t pn = 0;
    unsigned char *png = iu_square_png(b, n, _wtof(argv[3]), _wtof(argv[4]), _wtof(argv[5]), _wtoi(argv[6]), &pn);
    if (!png) return 5;
    FILE *f = _wfopen(argv[2], L"wb");
    if (!f) return 6;
    fwrite(png, 1, pn, f);
    fclose(f);
    char *b64 = iu_base64(png, pn);
    printf("png %u b64 %u\n", (unsigned)pn, (unsigned)(b64 ? strlen(b64) : 0));
    free(b64);
    return 0;
}
