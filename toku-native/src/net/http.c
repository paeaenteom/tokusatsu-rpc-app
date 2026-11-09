#include "http.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/utf.h"

static void set_err(char *err, size_t n, const char *what, DWORD code) {
    if (err && n) snprintf(err, n, "%s (%lu)", what, (unsigned long)code);
}

//  cap: 응답 본문 최대 바이트. strict=1 이면 넘을 때 실패(0, "too large"), 0 이면 거기서 자르고 성공 (기존 POST 동작)
static int http_req(const wchar_t *method, const char *url, const char *headers, const void *body, size_t len, int timeout_ms,
                    size_t cap, int strict, int *status, sbuf *resp, char *err, size_t errn) {
    int ok = 0, over = 0;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    wchar_t *wurl = utf8_to_w(url, strlen(url));
    wchar_t *whdr = headers ? utf8_to_w(headers, strlen(headers)) : NULL;
    if (!wurl || len > 0xFFFFFFFFu) { set_err(err, errn, "bad url", 0); goto done; }
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[2048];
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { set_err(err, errn, "bad url", GetLastError()); goto done; }
    wchar_t full[4096];
    _snwprintf(full, 4095, L"%ls%ls", path, extra);
    full[4095] = 0;
    ses = WinHttpOpen(L"curl/8.4.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { set_err(err, errn, "open", GetLastError()); goto done; }
    WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) { set_err(err, errn, "connect", GetLastError()); goto done; }
    req = WinHttpOpenRequest(con, method, full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { set_err(err, errn, "request", GetLastError()); goto done; }
    if (!WinHttpSendRequest(req, whdr ? whdr : WINHTTP_NO_ADDITIONAL_HEADERS, whdr ? (DWORD)-1L : 0,
                            (LPVOID)body, (DWORD)len, (DWORD)len, 0)) {
        DWORD e = GetLastError();
        set_err(err, errn, e == ERROR_WINHTTP_TIMEOUT ? "The operation was aborted due to timeout" : "send", e);
        goto done;
    }
    if (!WinHttpReceiveResponse(req, NULL)) {
        DWORD e = GetLastError();
        set_err(err, errn, e == ERROR_WINHTTP_TIMEOUT ? "The operation was aborted due to timeout" : "receive", e);
        goto done;
    }
    DWORD st = 0, sl = sizeof st;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX);
    *status = (int)st;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
        if (resp->len + avail > cap) { over = 1; break; }
        if (!sb_reserve(resp, avail)) break;
        DWORD got = 0;
        if (!WinHttpReadData(req, resp->p + resp->len, avail, &got) || !got) break;
        resp->len += got;
        resp->p[resp->len] = '\0';
    }
    ok = 1;
    if (over && strict) { ok = 0; set_err(err, errn, "too large", 0); }
done:
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    free(wurl);
    free(whdr);
    return ok;
}

int http_post(const char *url, const char *headers, const void *body, size_t len, int timeout_ms,
              int *status, sbuf *resp, char *err, size_t errn) {
    return http_req(L"POST", url, headers, body, len, timeout_ms, 1024 * 1024, 0, status, resp, err, errn);
}

int http_get(const char *url, const char *headers, int timeout_ms, size_t max_bytes,
             int *status, sbuf *resp, char *err, size_t errn) {
    return http_req(L"GET", url, headers, NULL, 0, timeout_ms, max_bytes, 1, status, resp, err, errn);
}

void form_init(form_t *f) {
    sb_init(&f->body, 0);
    unsigned char r[12];
    BCryptGenRandom(NULL, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char *p = f->boundary;
    p += sprintf(p, "----toku");
    for (int i = 0; i < 12; i++) p += sprintf(p, "%02x", r[i]);
}
void form_field(form_t *f, const char *name, const char *value) {
    sb_printf(&f->body, "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", f->boundary, name, value);
}
void form_file(form_t *f, const char *name, const char *filename, const char *type, const void *data, size_t len) {
    sb_printf(&f->body, "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
              f->boundary, name, filename, type);
    sb_putn(&f->body, data, len);
    sb_puts(&f->body, "\r\n");
}
void form_end(form_t *f) { sb_printf(&f->body, "--%s--\r\n", f->boundary); }
void form_free(form_t *f) { sb_free(&f->body); }
