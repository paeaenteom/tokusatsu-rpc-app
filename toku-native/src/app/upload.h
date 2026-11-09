// 썸네일 업로드 스레드 — discord-rpc.js uploadImage() 와 같은 순서·규칙 (앱이 멈추지 않게 따로 돈다)
#ifndef TOKU_UPLOAD_H
#define TOKU_UPLOAD_H

typedef struct {
    int job;
    int ok;
    char *url;     // 성공한 주소
    double keep;   // 이만큼 믿는다 (ms)
    char *err;     // 실패 사유
    char *log;     // 성공 로그 한 줄 ('[RPC] 업로드 성공 (…)')
} up_result;

// worker_url·token: secrets.json 의 avatarUpload (없으면 NULL) — 값은 로그에 남기지 않는다
int up_start(const char *worker_url, const char *worker_token);
void up_push(int job, const char *data_url);
int up_pop(up_result *out);          // 메인 스레드에서. 끝난 것이 있으면 1 (out 의 문자열은 up_result_free)
void up_result_free(up_result *r);
void up_stop(void);

#endif
