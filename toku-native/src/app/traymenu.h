// 트레이 메뉴 — 지금 앱 tray-menu.html 과 같은 모양으로 직접 그린 작은 창 (Windows 기본 메뉴 대신)
//  머리(로고·이름·버전) · 지연 시간 줄(확장·Discord) · 항목(아이콘·스위치) · 종료
#ifndef TOKU_TRAYMENU_H
#define TOKU_TRAYMENU_H
#include <windows.h>

int trm_init(HINSTANCE inst, HWND owner);   // owner = 본체 창 (종료는 owner 에 WM_COMMAND)
int trm_show(POINT at);                      // 0 이면 못 띄움 (부르는 쪽이 기본 메뉴로)
void trm_hide(void);
int trm_visible(void);
int trm_snapshot(const wchar_t *dir);       // 시험 모드 전용: tray@100·tray@150.bmp 저장, 저장한 수

#endif
