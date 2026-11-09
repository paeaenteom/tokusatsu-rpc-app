// 영상 버튼 키 설정 — 지금 앱 keybinds.js (KeybindStore) 를 그대로 옮김. 저장은 알파 설정(settings.c)의 keybinds·keybindsOn
//  확장에서 온 값은 모양·길이를 다시 거른다 (페이지가 만든 글자가 섞여 있다)
#ifndef TOKU_KEYBINDS_H
#define TOKU_KEYBINDS_H
#include "../base/json.h"
#include "../base/sbuf.h"

#define KB_MAX_PER_SITE 30
extern const char *const KB_SITES[6];

typedef struct {
    int ok;
    const char *error;      // site|target|full|missing|key|same
    char id[32];            // add 가 만든 id
    int has_moved;          // setKey: 같은 키를 쓰던 줄에서 뗐다
    char moved[128];        //   그 줄 이름 (없으면 "")
} kb_res;

jval *kb_clean_all(const jval *raw);   // 새 객체 (호출한 쪽이 json_free)
jval *kb_clean_on(const jval *raw);
jval *kb_all(void);                    // 저장된 것을 거른 것 (json_free)
jval *kb_on(void);
void kb_payload(sbuf *b);              // {"type":"KEYBINDS","keybinds":…,"on":…}

kb_res kb_add(const char *site, const jval *target, const jval *label);
kb_res kb_set_target(const char *site, const char *id, const jval *target);
kb_res kb_set_key(const char *site, const char *id, const jval *combo);
kb_res kb_rename(const char *site, const char *id, const jval *label);
kb_res kb_remove(const char *site, const char *id);
kb_res kb_set_site_on(const char *site, int on);
kb_res kb_mark_fs(const char *site, const char *id);
int kb_adopt_local(const jval *keybinds, const jval *on);

//  시험용: add 의 무작위 id 대신 (NULL 이면 BCryptGenRandom 5바이트)
extern void (*kb_random5)(unsigned char out[5]);

#endif
