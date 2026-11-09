// URL — discord-rpc.js 의 '사이트에서 보기' 버튼 주소 정리용 (new URL() 의 WHATWG 규칙을 http/https 에 한해 따른다)
//  ⚠ 일부러 지원하지 않는 것: 한글 등 비ASCII 도메인(IDNA)·xn-- 라벨·IPv6 주소 → 해석 실패로 보고 사이트 홈으로 보낸다
//    (확장이 보내는 YouTube·치지직·dアニメストア 페이지 주소에는 나오지 않는다)
#ifndef TOKU_URL_H
#define TOKU_URL_H
#include <stddef.h>
#include "../base/sbuf.h"

typedef struct {
    char scheme[8];     // "http" | "https"
    sbuf host;
    int port;           // -1 = 없음(기본 포트 포함)
    sbuf path;          // "/a/b"
    sbuf query;         // ? 뒤 (인코딩된 그대로)
    int has_query;
} url_t;

int url_parse(url_t *u, const char *s, size_t len);   // 1 = 성공. 실패해도 url_free 는 불러도 된다
void url_free(url_t *u);
void url_origin(sbuf *out, const url_t *u);
// searchParams.get(name) — 있으면 1 (값은 디코드된 UTF-8)
int url_param(const url_t *u, const char *name, sbuf *out);
void encode_uri_component(sbuf *out, const char *s, size_t len);

// discord-rpc.js 의 ytButtonUrl · chzzkButtonUrl · danimeButtonUrl
void yt_button_url(sbuf *out, const char *raw, size_t len, const char *home);
void chzzk_button_url(sbuf *out, const char *raw, size_t len, const char *home);
void danime_button_url(sbuf *out, const char *raw, size_t len, const char *home);

#endif
