// 그림 도구 (WIC) — 파일 바이트 풀기 · 정사각 자리를 잘라 n×n PNG 로 · base64
//  부르는 스레드에서 COM 이 초기화돼 있어야 한다 (창 스레드는 main 이 OleInitialize/CoInitialize)
#ifndef TOKU_IMGUTIL_H
#define TOKU_IMGUTIL_H
#include <stddef.h>
#include <stdint.h>

//  BGRA(미리 곱한 알파) 로 풀기 — 큰 그림은 긴 변 max_side 로 줄인다. 원래 크기는 *ow·*oh 로
uint32_t *iu_decode(const unsigned char *data, size_t len, int max_side, int *w, int *h, int *ow, int *oh);
//  원본 바이트에서 (x, y, size) 정사각 자리(원본 픽셀)를 잘라 n×n PNG 로 — 성공하면 malloc 한 바이트
unsigned char *iu_square_png(const unsigned char *data, size_t len, double x, double y, double size, int n, size_t *out_len);
char *iu_base64(const unsigned char *b, size_t n);   // malloc
int iu_read_file(const wchar_t *path, size_t max, unsigned char **buf, size_t *n);   // 1 = 읽음, -1 = 너무 큼, 0 = 못 읽음

#endif
