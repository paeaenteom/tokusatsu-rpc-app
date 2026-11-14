// img.c 단독 점검 — 주소마다 받아 보고 결과를 한 줄씩 (1 됨 + 평균 색 / -1 실패)
//  사용: img_cli.exe <주소>...
#include <windows.h>
#include <stdio.h>
#include "../src/app/img.h"

int main(int argc, char **argv) {
    wchar_t cd[MAX_PATH];   //  TOKU_IMG_CACHE = 디스크 캐시 폴더 (시험)
    if (GetEnvironmentVariableW(L"TOKU_IMG_CACHE", cd, MAX_PATH)) img_set_cache_dir(cd);
    img_init(NULL, 0);
    for (int i = 1; i < argc; i++) {
        const uint32_t *px = NULL;
        int r = img_get(argv[i], &px);
        for (int t = 0; t < 200 && r == 0; t++) { Sleep(100); r = img_get(argv[i], &px); }
        if (r == 1 && px) {
            unsigned long long sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = 0; k < IMG_SIZE * IMG_SIZE; k++) { sa += px[k] >> 24; sr += (px[k] >> 16) & 255; sg += (px[k] >> 8) & 255; sb += px[k] & 255; }
            int n = IMG_SIZE * IMG_SIZE;
            printf("%d ok a=%llu rgb=%llu,%llu,%llu\n", i, sa / n, sr / n, sg / n, sb / n);
        } else printf("%d fail %d\n", i, r);
    }
    return 0;
}
