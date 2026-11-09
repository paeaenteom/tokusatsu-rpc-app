// 웹훅 탭 데이터 — 지금 앱 secrets.js (작품별 웹훅) + main.js (폴더 아이콘·캐릭터) 를 그대로
//  ⚠ 웹훅 주소는 비밀: 화면에는 이름표(웹훅 번호 끝 6자리)만. 로그에도 안 남긴다
//  저장: 작품별 웹훅은 알파 전용 %APPDATA%\toku-rpc-native\secrets.json (처음 고칠 때 지금 앱 것을 복사 — 지금 앱 파일은 안 건드림)
//        폴더 아이콘·캐릭터는 알파 설정(settings.c)의 hookFolders
#ifndef TOKU_HOOKS_H
#define TOKU_HOOKS_H
#include "../base/json.h"

typedef struct { char id[16]; char *title, *name; char label[16]; } hk_item;
int hk_list(hk_item **out);            // 화면용 목록 (넣은 순서) — hk_list_free
void hk_list_free(hk_item *l, int n);
int hk_has_default(void);
const char *hk_add(const jval *title, const jval *url, const jval *name);   // NULL = 됨, 아니면 title|url|dup|full|badfile
const char *hk_rename(const char *id, const jval *name);
const char *hk_remove(const char *id);
char *hk_url(const char *id);          // 시험 보내기용 (free) — 없으면 NULL

int hk_has_title(const char *title);
const char *hk_icon(const char *title);              // data:image/png;base64,… 또는 NULL (지금 있는 폴더만)
const char *hk_set_icon(const char *title, const char *png_data_url);   // NULL = 됨
void hk_clear_icon(const char *title);

typedef struct { char *id, *name, *avatar; double until; int off; } hk_char;
int hk_chars(const char *title, hk_char **out);      // hookCharMap 한 폴더 — hk_chars_free
void hk_chars_free(hk_char *l, int n);
const char *hk_char_add(const char *title, const jval *name, char id_out[16]);
const char *hk_char_rename(const char *title, const char *id, const jval *name);
const char *hk_char_remove(const char *title, const char *id);
const char *hk_char_toggle(const char *title, const char *id, int on);
const char *hk_char_avatar_url(const char *title, const char *id, const jval *url);
const char *hk_char_set_avatar(const char *title, const char *id, const char *url, double until);
jval *hk_char_get(const char *title, const char *id);   // 시험 보내기·조절 창용 복사 (json_free) — 없으면 NULL
char *hk_test_url_for_char(const char *title);       // 그 폴더 웹훅 중 무작위 하나, 없으면 기본 웹훅 (free, 없으면 "")

//  캐릭터 원본 사진 보관 (알파: toku-rpc-native\hook-avatars, 없으면 지금 앱 폴더에서 읽기만)
int hk_avatar_orig_read(const char *id, unsigned char **buf, size_t *n);
void hk_avatar_orig_write(const char *id, const unsigned char *buf, size_t n, const char *mime);
void hk_avatar_orig_drop(const char *id);

//  캐릭터 사진 올리기 (hook-avatar.js): 내 Worker → catbox → litterbox(72시간) — 따로 도는 스레드, 끝나면 PostMessage(notify, WM_HK_UPLOADED, 0, hk_upload_res*)
#include <windows.h>
#define WM_HK_UPLOADED (WM_APP + 7)
typedef struct { int tag, ok; char *url; double until; char host[16]; char err[200]; } hk_upload_res;
void hk_upload_avatar(HWND notify, int tag, unsigned char *png, size_t n);   // png 은 가져간다
void hk_upload_res_free(hk_upload_res *r);

extern void (*hk_random)(unsigned char *out, int n);   // 시험용

const char *hk_sniff_mime(const unsigned char *b, size_t n);   // 바이트로 형식 (확장자는 믿지 않음) — 모르면 NULL

#endif
