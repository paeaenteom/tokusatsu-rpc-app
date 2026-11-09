#include "upload.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/crypto.h"
#include "../base/json.h"
#include "../base/sbuf.h"
#include "../net/http.h"

#define THUMB_TTL (2.0 * 60 * 60 * 1000)
#define WORKER_KEEP_MS (30.0 * 24 * 60 * 60 * 1000)
#define WORKER_TIMEOUT_MS 6000
#define HOST_TIMEOUT_MS 60000

typedef struct node { int job; char *data; up_result res; struct node *next; } node;

static CRITICAL_SECTION cs;
static HANDLE wake, thread;
static node *in_head, *in_tail, *out_head, *out_tail;
static volatile LONG stopping;
static char *w_url, *w_token;
static int preferred;   // _preferredHost — 최근 성공한 호스트

static char *dupz(const char *s) { if (!s) return NULL; size_t n = strlen(s); char *p = (char *)malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

// 응답 글자 앞뒤 공백 빼기 (JS trim — 응답은 ASCII 라 이걸로 충분)
static void trim_resp(sbuf *b) {
    size_t a = 0, e = b->len;
    while (a < e && (unsigned char)b->p[a] <= 0x20) a++;
    while (e > a && (unsigned char)b->p[e - 1] <= 0x20) e--;
    if (a) memmove(b->p, b->p + a, e - a);
    b->len = e - a;
    if (b->p) b->p[b->len] = '\0';
}

// Worker: PNG 바이트를 그대로 POST → {"url":"<origin>/a/<id>.png"}
static int up_worker(const unsigned char *buf, size_t n, char **url, char *err, size_t errn) {
    char hdr[1024];
    snprintf(hdr, sizeof hdr, "Content-Type: image/png\r\nAuthorization: Bearer %s\r\n", w_token);
    int st = 0;
    sbuf r; sb_init(&r, 0);
    char e[256] = "";
    if (!http_post(w_url, hdr, buf, n, WORKER_TIMEOUT_MS, &st, &r, e, sizeof e)) { snprintf(err, errn, "%s", e); sb_free(&r); return 0; }
    trim_resp(&r);
    if (st < 200 || st > 299) { snprintf(err, errn, "HTTP %d %.40s", st, r.p ? r.p : ""); sb_free(&r); return 0; }
    jval *j = r.p ? json_parse(r.p, r.len, NULL) : NULL;
    const char *u = json_get_str(j, "url", NULL);
    //  돌려받은 주소는 그 Worker 의 /a/<id>.png 여야 한다
    const char *slash = strstr(w_url, "://");
    size_t olen = 0;
    if (slash) { const char *p = strchr(slash + 3, '/'); olen = p ? (size_t)(p - w_url) : strlen(w_url); }
    int good = u && olen && !strncmp(u, w_url, olen) && !strncmp(u + olen, "/a/", 3) && !strncmp(u, "https://", 8);
    if (good) {
        size_t ul = strlen(u);
        good = ul > 12 && !strcmp(u + ul - 4, ".png");
        for (size_t i = 8; good && i < ul; i++) if ((unsigned char)u[i] <= 0x20 || strchr("\"'<>", u[i])) good = 0;
    }
    if (good) *url = dupz(u);
    else snprintf(err, errn, "bad response");
    json_free(j);
    sb_free(&r);
    return good;
}

typedef struct { const char *name; double keep; } host_t;
static const host_t HOSTS[] = { { "litterbox", 60.0 * 60 * 60 * 1000 }, { "uguu", 2.0 * 60 * 60 * 1000 }, { "catbox", 30.0 * 24 * 60 * 60 * 1000 } };

static int up_host(int i, const unsigned char *buf, size_t n, const char *type, const char *fname, char **url, char *err, size_t errn) {
    form_t f;
    form_init(&f);
    const char *endpoint;
    if (i == 0) { form_field(&f, "reqtype", "fileupload"); form_field(&f, "time", "72h"); form_file(&f, "fileToUpload", fname, type, buf, n); endpoint = "https://litterbox.catbox.moe/resources/internals/api.php"; }
    else if (i == 1) { form_file(&f, "files[]", fname, type, buf, n); endpoint = "https://uguu.se/upload?output=text"; }
    else { form_field(&f, "reqtype", "fileupload"); form_file(&f, "fileToUpload", fname, type, buf, n); endpoint = "https://catbox.moe/user/api.php"; }
    form_end(&f);
    char hdr[160];
    snprintf(hdr, sizeof hdr, "Content-Type: multipart/form-data; boundary=%s\r\nUser-Agent: curl/8.4.0\r\n", f.boundary);
    int st = 0;
    sbuf r; sb_init(&r, 0);
    char e[256] = "";
    int ok = http_post(endpoint, hdr, f.body.p, f.body.len, HOST_TIMEOUT_MS, &st, &r, e, sizeof e);
    form_free(&f);
    if (!ok) { snprintf(err, errn, "%s", e); sb_free(&r); return 0; }
    trim_resp(&r);
    const char *t = r.p ? r.p : "";
    if (st < 200 || st > 299 || !(!strncmp(t, "http://", 7) || !strncmp(t, "https://", 8))) {
        snprintf(err, errn, "HTTP %d %.50s", st, t);
        sb_free(&r);
        return 0;
    }
    *url = dupz(t);
    sb_free(&r);
    return 1;
}

static void do_upload(node *n) {
    up_result *res = &n->res;
    res->job = n->job;
    const char *d = n->data;
    //  /^data:([^;]+);base64,(.+)$/
    const char *semi = !strncmp(d, "data:", 5) ? strchr(d + 5, ';') : NULL;
    if (!semi || semi == d + 5 || strncmp(semi, ";base64,", 8) || !semi[8]) { res->err = dupz("\xEC\x9E\x98\xEB\xAA\xBB\xEB\x90\x9C dataURL"); return; }
    char type[64];
    snprintf(type, sizeof type, "%.*s", (int)(semi - d - 5), d + 5);
    sbuf buf; sb_init(&buf, 32u * 1024 * 1024);
    const char *b64 = semi + 8;
    if (!b64_decode(&buf, b64, strlen(b64))) { res->err = dupz("\xEC\x9E\x98\xEB\xAA\xBB\xEB\x90\x9C dataURL"); sb_free(&buf); return; }
    sbuf errs; sb_init(&errs, 0);
    char e[320];
    //  0순위: 내 Cloudflare Worker (PNG·2MB 까지)
    if (w_url && w_token && !_stricmp(type, "image/png") && buf.len <= 2u * 1024 * 1024) {
        ULONGLONG t0 = GetTickCount64();
        char *u = NULL;
        e[0] = 0;
        if (up_worker((const unsigned char *)buf.p, buf.len, &u, e, sizeof e)) {
            res->ok = 1; res->url = u; res->keep = WORKER_KEEP_MS;
            char l[96];
            snprintf(l, sizeof l, "[RPC] \xEC\x97\x85\xEB\xA1\x9C\xEB\x93\x9C \xEC\x84\xB1\xEA\xB3\xB5 (cloudflare, %llums)", (unsigned long long)(GetTickCount64() - t0));
            res->log = dupz(l);
            sb_free(&buf); sb_free(&errs);
            return;
        }
        sb_printf(&errs, "cloudflare: %s", e);
    }
    const char *ext = strstr(type, "png") || strstr(type, "PNG") ? "png" : (strstr(type, "webp") || strstr(type, "WEBP")) ? "webp" : "jpg";
    char fname[16];
    snprintf(fname, sizeof fname, "thumb.%s", ext);
    int order[4] = { preferred, 0, 1, 2 }, seen[3] = { 0, 0, 0 };
    for (int k = 0; k < 4; k++) {
        int i = order[k];
        if (seen[i]) continue;
        seen[i] = 1;
        char *u = NULL;
        e[0] = 0;
        if (up_host(i, (const unsigned char *)buf.p, buf.len, type, fname, &u, e, sizeof e)) {
            preferred = i;
            res->ok = 1; res->url = u; res->keep = HOSTS[i].keep;
            char l[96];
            snprintf(l, sizeof l, "[RPC] \xEC\x97\x85\xEB\xA1\x9C\xEB\x93\x9C \xEC\x84\xB1\xEA\xB3\xB5 (%s)", HOSTS[i].name);
            res->log = dupz(l);
            sb_free(&buf); sb_free(&errs);
            return;
        }
        if (errs.len) sb_puts(&errs, " / ");
        sb_printf(&errs, "%s: %s", HOSTS[i].name, e);
    }
    sbuf m; sb_init(&m, 0);
    sb_printf(&m, "\xEB\xAA\xA8\xEB\x93\xA0 \xED\x98\xB8\xEC\x8A\xA4\xED\x8A\xB8 \xEC\x8B\xA4\xED\x8C\xA8 \xE2\x80\x94 %s", errs.p ? errs.p : "");
    res->err = sb_detach(&m);
    sb_free(&buf); sb_free(&errs);
}

static DWORD WINAPI worker(LPVOID arg) {
    while (!stopping) {
        WaitForSingleObject(wake, INFINITE);
        for (;;) {
            EnterCriticalSection(&cs);
            node *n = in_head;
            if (n) { in_head = n->next; if (!in_head) in_tail = NULL; }
            LeaveCriticalSection(&cs);
            if (!n || stopping) break;
            do_upload(n);
            free(n->data); n->data = NULL;
            n->next = NULL;
            EnterCriticalSection(&cs);
            if (out_tail) out_tail->next = n; else out_head = n;
            out_tail = n;
            LeaveCriticalSection(&cs);
        }
    }
    return 0;
}

int up_start(const char *url, const char *token) {
    InitializeCriticalSection(&cs);
    w_url = dupz(url);
    w_token = dupz(token);
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return thread != NULL;
}

void up_push(int job, const char *data_url) {
    node *n = (node *)calloc(1, sizeof *n);
    if (!n) return;
    n->job = job;
    n->data = dupz(data_url);
    EnterCriticalSection(&cs);
    if (in_tail) in_tail->next = n; else in_head = n;
    in_tail = n;
    LeaveCriticalSection(&cs);
    SetEvent(wake);
}

int up_pop(up_result *out) {
    EnterCriticalSection(&cs);
    node *n = out_head;
    if (n) { out_head = n->next; if (!out_head) out_tail = NULL; }
    LeaveCriticalSection(&cs);
    if (!n) return 0;
    *out = n->res;
    free(n);
    return 1;
}

void up_result_free(up_result *r) { free(r->url); free(r->err); free(r->log); memset(r, 0, sizeof *r); }

void up_stop(void) {
    InterlockedExchange(&stopping, 1);
    if (wake) SetEvent(wake);
    if (thread) { WaitForSingleObject(thread, 3000); CloseHandle(thread); }
}
