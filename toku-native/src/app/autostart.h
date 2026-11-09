// 로그온 시 자동 시작 (채널 이름 — channel.h. 테스트 알파는 등록 안 함, 베타가 맡음)
#ifndef TOKU_AUTOSTART_H
#define TOKU_AUTOSTART_H
#include <wchar.h>

int as_installed(void);     // %LOCALAPPDATA%\Programs\TOKU RPC Alpha\TOKU-RPC-alpha.exe 에서 돌고 있나 (아니면 등록하지 않는다)
int as_run_set(int on);     // Run 키 "TOKU RPC Alpha" (끄기: 없으면 성공)
int as_run_exists(void);
int as_task_set(int on);    // 작업 "TOKU RPC Alpha Fast Start" (끄기: 없으면 성공)
int as_task_exists(void);
int as_remove_named(const wchar_t *run_name, const wchar_t *task_name);   // 다른 이름의 Run 값·작업 지우기 (없으면 성공)

#endif
