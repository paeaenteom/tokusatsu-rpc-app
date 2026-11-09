#include "ipc.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../base/sbuf.h"

#define DIPC_RBUF 65536
struct dipc {
    HANDLE h;
    int index;
    int dead;
    sbuf rb;
    OVERLAPPED rov, wov;   // 읽기 (늘 하나 걸어 둠) · 쓰기
    int reading;           // 읽기가 걸려 있다
    unsigned char *rbuf;
};

void dipc_uuid(char out[37]) {
    uint8_t r[16];
    if (BCryptGenRandom(NULL, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        //  거의 일어나지 않는다 — 그래도 같은 nonce 가 겹치지 않게 시간·카운터로
        static uint32_t ctr;
        uint64_t t = GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32);
        for (int i = 0; i < 16; i++) r[i] = (uint8_t)((t >> ((i % 8) * 8)) ^ (++ctr * 131u));
    }
    r[6] = (uint8_t)((r[6] & 0x0F) | 0x40);
    r[8] = (uint8_t)((r[8] & 0x3F) | 0x80);
    static const char hx[] = "0123456789abcdef";
    int k = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[k++] = '-';
        out[k++] = hx[r[i] >> 4];
        out[k++] = hx[r[i] & 15];
    }
    out[k] = '\0';
}

//  쓰기는 예전처럼 끝날 때까지 기다린다 (겹친 핸들이라 OVERLAPPED 로 내고 결과를 기다림)
static int write_all(dipc *d, const void *p, size_t n) {
    const char *c = (const char *)p;
    while (n) {
        DWORD w = 0;
        DWORD want = n > 0x40000000 ? 0x40000000 : (DWORD)n;
        ResetEvent(d->wov.hEvent);
        if (!WriteFile(d->h, c, want, &w, &d->wov)) {
            if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(d->h, &d->wov, &w, TRUE)) return 0;
        } else if (!GetOverlappedResult(d->h, &d->wov, &w, TRUE)) return 0;
        if (w == 0) return 0;
        c += w;
        n -= w;
    }
    return 1;
}

int dipc_send_json(dipc *d, int op, const char *json, size_t len) {
    if (!d || d->dead || len > DIPC_MAX_FRAME) return 0;
    uint8_t hd[8];
    uint32_t o = (uint32_t)op, l = (uint32_t)len;
    for (int i = 0; i < 4; i++) { hd[i] = (uint8_t)(o >> (8 * i)); hd[4 + i] = (uint8_t)(l >> (8 * i)); }
    //  머리와 본문을 한 번에 쓴다 (상대가 반쪽만 읽는 일이 없게)
    sbuf b;
    sb_init(&b, 0);
    sb_putn(&b, hd, 8);
    sb_putn(&b, json, len);
    int r = !b.err && write_all(d, b.p, b.len);
    sb_free(&b);
    if (!r) d->dead = 1;
    return r;
}

dipc *dipc_open(const wchar_t *prefix, const char *client_id, int *err) {
    DWORD last = ERROR_FILE_NOT_FOUND;
    for (int i = 0; i < 10; i++) {
        wchar_t path[256];
        _snwprintf(path, 255, L"\\\\?\\pipe\\%ls%d", prefix, i);
        path[255] = 0;
        HANDLE h = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 2; attempt++) {
            h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h != INVALID_HANDLE_VALUE) break;
            last = GetLastError();
            //  모든 인스턴스가 쓰이는 중이면 잠깐 기다렸다 한 번 더 (그래도 안 되면 다음 번호)
            if (last != ERROR_PIPE_BUSY || !WaitNamedPipeW(path, 1000)) break;
        }
        if (h == INVALID_HANDLE_VALUE) continue;
        dipc *d = (dipc *)calloc(1, sizeof *d);
        if (!d) { CloseHandle(h); last = ERROR_NOT_ENOUGH_MEMORY; break; }
        d->h = h;
        d->index = i;
        sb_init(&d->rb, DIPC_MAX_FRAME + 8);
        d->rov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        d->wov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        d->rbuf = (unsigned char *)malloc(DIPC_RBUF);
        if (!d->rov.hEvent || !d->wov.hEvent || !d->rbuf) { last = ERROR_NOT_ENOUGH_MEMORY; d->dead = 1; dipc_close(d); break; }
        //  HANDSHAKE — discord-rpc: encode(0, { v: 1, client_id })
        sbuf j;
        sb_init(&j, 0);
        sb_puts(&j, "{\"v\":1,\"client_id\":");
        json_write_str(&j, client_id, strlen(client_id));
        sb_putc(&j, '}');
        int ok = !j.err && dipc_send_json(d, DIPC_HANDSHAKE, j.p, j.len);
        sb_free(&j);
        if (!ok) { last = GetLastError(); dipc_close(d); continue; }
        return d;
    }
    if (err) *err = (int)last;
    return NULL;
}

int dipc_request(dipc *d, const char *cmd, const jval *args, char nonce_out[37]) {
    char nonce[37];
    dipc_uuid(nonce);
    sbuf j;
    sb_init(&j, 0);
    sb_puts(&j, "{\"cmd\":");
    json_write_str(&j, cmd, strlen(cmd));
    if (args) { sb_puts(&j, ",\"args\":"); json_write(&j, args); }
    sb_puts(&j, ",\"nonce\":\"");
    sb_puts(&j, nonce);
    sb_puts(&j, "\"}");
    int ok = !j.err && dipc_send_json(d, DIPC_FRAME, j.p, j.len);
    sb_free(&j);
    if (ok && nonce_out) memcpy(nonce_out, nonce, 37);
    return ok;
}

int dipc_poll(dipc *d, dipc_cb cb, void *user) {
    if (!d || d->dead) return -1;
    //  걸어 둔 읽기가 끝났으면 받고, 다시 건다 — 바로 끝나는 동안은 계속 (쌓인 것을 다 읽음)
    for (int guard = 0; guard < 4096; guard++) {
        DWORD got = 0;
        if (d->reading) {
            if (!GetOverlappedResult(d->h, &d->rov, &got, FALSE)) {
                DWORD e = GetLastError();
                if (e == ERROR_IO_INCOMPLETE) break;              // 아직 안 옴
                if (e != ERROR_MORE_DATA) { d->reading = 0; d->dead = 1; return -1; }   // 끊김 (ERROR_BROKEN_PIPE 등)
            }
            d->reading = 0;
        } else {
            if (!ReadFile(d->h, d->rbuf, DIPC_RBUF, NULL, &d->rov)) {
                DWORD e = GetLastError();
                if (e == ERROR_IO_PENDING) { d->reading = 1; continue; }   //  바로 끝났는지 한 번 더 본다
                if (e != ERROR_MORE_DATA) { d->dead = 1; return -1; }
            }
            if (!GetOverlappedResult(d->h, &d->rov, &got, FALSE) && GetLastError() != ERROR_MORE_DATA) { d->dead = 1; return -1; }
        }
        if (!got) continue;
        if (!sb_reserve(&d->rb, got)) { d->dead = 1; return -1; }   // 한도를 넘는 프레임
        memcpy(d->rb.p + d->rb.len, d->rbuf, got);
        d->rb.len += got;
        d->rb.p[d->rb.len] = '\0';
    }
    //  완성된 프레임마다 처리
    while (d->rb.len >= 8) {
        const uint8_t *p = (const uint8_t *)d->rb.p;
        int32_t op = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
        int32_t len = (int32_t)((uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24));
        if (op < 0 || op > 4 || len < 0 || (uint32_t)len > DIPC_MAX_FRAME) { d->dead = 1; return -1; }
        if (d->rb.len < 8 + (size_t)len) break;
        jval *msg = json_parse(d->rb.p + 8, (size_t)len, NULL);
        if (!msg) { d->dead = 1; return -1; }
        if (op == DIPC_PING) {
            //  discord-rpc: this.send(data, PONG) — 받은 값을 다시 JSON 으로 써서 돌려준다
            char *s = json_stringify(msg);
            if (s) { dipc_send_json(d, DIPC_PONG, s, strlen(s)); free(s); }
        }
        if (cb) cb(d, op, msg, user);
        json_free(msg);
        sb_consume(&d->rb, 8 + (size_t)len);
        if (op == DIPC_CLOSE) { d->dead = 1; return -1; }
        if (d->dead) return -1;
    }
    return 0;
}

void dipc_close(dipc *d) {
    if (!d) return;
    if (!d->dead && d->wov.hEvent) dipc_send_json(d, DIPC_CLOSE, "{}", 2);
    if (d->reading) {   //  걸어 둔 읽기를 거두고 끝날 때까지 기다린 뒤에 버퍼를 놓는다
        DWORD got;
        CancelIoEx(d->h, &d->rov);
        GetOverlappedResult(d->h, &d->rov, &got, TRUE);
        d->reading = 0;
    }
    CloseHandle(d->h);
    if (d->rov.hEvent) CloseHandle(d->rov.hEvent);
    if (d->wov.hEvent) CloseHandle(d->wov.hEvent);
    free(d->rbuf);
    sb_free(&d->rb);
    free(d);
}

int dipc_index(const dipc *d) { return d ? d->index : -1; }
void *dipc_event(const dipc *d) { return d ? d->rov.hEvent : NULL; }
