// 알파 설정 — %APPDATA%\toku-rpc-native\config.json (처음 켤 때 기존 앱 config.json 을 복사. 기존 파일은 안 건드림)
#ifndef TOKU_SETTINGS_H
#define TOKU_SETTINGS_H
#include <wchar.h>
#include "../base/json.h"

int settings_init(const wchar_t *appdata, const wchar_t *dirname, int *copied);   // 1 = 읽음 (dirname: toku-rpc-native)
const jval *settings_get(const char *a, const char *b);   // b 가 NULL 이면 최상위 키
void settings_set(const char *a, const char *b, jval *v); // v 는 가져간다
int settings_save(void);                                  // 임시 파일에 쓰고 바꿔치기
const wchar_t *settings_path(void);
jval *settings_root(void);

#endif
