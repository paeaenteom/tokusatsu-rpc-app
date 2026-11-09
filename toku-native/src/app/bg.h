// 배경 꾸미기 (main 쪽) — 지금 앱 appearance.js 를 그대로
//  · 파일은 대화상자로만 고르고, 내용(형식·크기·잘림·움직임)을 확인한 사본만 쓴다 (원본은 읽기만)
//  · 설정(appearance)에는 관리 파일 이름(bg-<16hex>.<ext>)·표시용 이름·옵션만. 원본 경로는 저장·로그 안 함
//  · 폴더: %APPDATA%\toku-rpc-native\backgrounds (지금 앱 사진은 처음 쓸 때 읽어서 알파 폴더로 복사)
#ifndef TOKU_BG_H
#define TOKU_BG_H
#include <stddef.h>
#include <wchar.h>
#include "../base/json.h"

typedef struct {
    int enabled;
    char fit[8];
    int posX, posY, zoom, opacity, dim, blur;
    char image[40];
    char name[512];
    int width, height;
} bg_set;

void bg_sanitize(const jval *o, bg_set *s);          // sanitizeStored
int bg_validate(const jval *p, bg_set *opts);        // validateOptions — 1 이면 됨 (enabled·fit·숫자 6개)
const char *bg_sniff(const unsigned char *b, size_t n, char fmt[8], int *w, int *h);   // NULL = 됨, 아니면 corrupt|format|animated|tooBig
char *bg_clean_name(const char *n);                  // cleanName (malloc)

void bg_load(bg_set *s);                             // 저장된 것 (거른 것)
const char *bg_get(bg_set *s);                       // 창에 보일 상태 · 알림(missing|unavailable|NULL)
//  고르기: 대화상자(또는 시험 경로) → 임시 사본. 됨이면 NULL, token·이름·크기·형식
const char *bg_pick(const wchar_t *test_path, char token[24], char name[512], int *w, int *h, char fmt[8]);
int bg_discard(const char *token);                   // NULL = 들고 있는 사본 전부
//  적용: opts(enabled·fit·숫자) + token(NULL = 지금 그림 그대로)
const char *bg_apply(const bg_set *opts, const char *token, bg_set *saved);
const char *bg_reset(bg_set *saved);
const char *bg_report_broken(const char *image, bg_set *state);   // 그리지 못했다 → 알림
void bg_cleanup(void);
int bg_file(const char *name, wchar_t *out);         // 관리 파일 전체 경로 (이름 규칙에 맞을 때만 1)

#endif
