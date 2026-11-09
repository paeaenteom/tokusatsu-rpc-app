// 썸네일에서 카드 왼쪽 줄 색 고르기 (thumb-color.js) — 고르면 1 과 *out = 0xRRGGBB, 못 고르면 0
#ifndef TOKU_ACCENT_H
#define TOKU_ACCENT_H
#include <stddef.h>
#include <string.h>

int accent_from_pixels(const unsigned char *bgra, size_t n_pixels, int *out);
int accent_from_buffer(const unsigned char *data, size_t len, int *out);

#endif
