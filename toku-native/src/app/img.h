// 창 카드 그림 — 따로 도는 스레드에서 받고(WinHTTP) WIC 로 풀어 정사각형(가운데 잘라 채우기) BGRA 로 둔다
//  키: "https://..." (썸네일) 또는 "asset:<앱>:<이름>" (Discord 앱 에셋 — 로고·재생·일시정지)
//  ⚠ 에셋 키에는 App ID 가 들어간다 → 이 모듈은 키를 로그·파일 어디에도 남기지 않는다
#ifndef TOKU_IMG_H
#define TOKU_IMG_H
#include <windows.h>
#include <stdint.h>

#define IMG_SIZE 192   // 저장 크기 (가로 = 세로). 그릴 때 줄인다

void img_init(HWND notify, UINT msg);   // 하나 끝날 때마다 PostMessage(notify, msg)
void img_stop(void);
//  1 = 있음(*px: IMG_SIZE² BGRA, 미리 곱한 알파), 0 = 받는 중(이번에 요청을 넣었을 수도), -1 = 실패(30초 뒤 다시)
int img_get(const char *key, const uint32_t **px);
int img_pending(void);   // 받는 중인 수 (시험용 그림 저장이 기다릴 때)

#endif
