#include "ws.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../base/crypto.h"
#include "../base/sbuf.h"
#include "../base/utf.h"

#define WS_MAX_CLIENTS 16
#define WS_MAX_HEADER 16384          // Node http 기본 헤더 한도와 같다 (넘으면 431)
#define WS_HANDSHAKE_MS 10000        // 연결 절차를 끝내지 않고 붙들고 있는 연결은 끊는다
#define WS_MAX_OUT (64u * 1024 * 1024)   // 보낼 데이터가 이만큼 밀리면(상대가 안 읽음) 끊는다
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum { ST_HANDSHAKE, ST_OPEN, ST_CLOSING };

struct ws_client {
    SOCKET s;
    int state;
    int id;
    ULONGLONG t0;
    sbuf in, out, msg;
    int msg_op;          // 이어 붙이는 중인 메시지 종류 (0 = 없음)
    int dead;            // 다 보내면 끊는다
    int notified_open;
    int close_code;
    ws_server *srv;
};

struct ws_server {
    SOCKET ls;
    WSAEVENT ev;   // 받을 것·연결·닫힘이 오면 켜진다 (메인 루프가 기다림)
    ws_handlers h;
    size_t max_payload;
    ws_client *c[WS_MAX_CLIENTS];
    int next_id;
};

static int g_wsa;

// ── 보내기 ──────────────────────────────────────────────────

static void flush_out(ws_client *c) {
    while (c->out.len) {
        int n = send(c->s, c->out.p, c->out.len > 0x7fffffff ? 0x7fffffff : (int)c->out.len, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) { c->dead = 2; c->out.len = 0; }
            return;
        }
        sb_consume(&c->out, (size_t)n);
    }
}

static int queue(ws_client *c, const void *p, size_t n) {
    if (c->dead == 2) return 0;
    if (c->out.len + n > WS_MAX_OUT) { c->dead = 2; return 0; }
    sb_putn(&c->out, p, n);
    if (c->out.err) { c->dead = 2; return 0; }
    flush_out(c);
    return 1;
}

static int send_frame(ws_client *c, int op, const void *data, size_t len) {
    uint8_t h[10];
    size_t hl;
    h[0] = (uint8_t)(0x80 | op);
    if (len < 126) { h[1] = (uint8_t)len; hl = 2; }
    else if (len <= 0xFFFF) { h[1] = 126; h[2] = (uint8_t)(len >> 8); h[3] = (uint8_t)len; hl = 4; }
    else {
        h[1] = 127;
        for (int i = 0; i < 8; i++) h[2 + i] = (uint8_t)((uint64_t)len >> (56 - 8 * i));
        hl = 10;
    }
    if (!queue(c, h, hl)) return 0;
    return len ? queue(c, data, len) : 1;
}

int ws_send_text(ws_client *c, const char *data, size_t len) {
    if (!c || c->state != ST_OPEN || c->dead) return 0;
    return send_frame(c, 1, data, len);
}

int ws_send_binary(ws_client *c, const void *data, size_t len) {
    if (!c || c->state != ST_OPEN || c->dead) return 0;
    return send_frame(c, 2, data, len);
}

int ws_ping(ws_client *c) {
    if (!c || c->state != ST_OPEN || c->dead) return 0;
    return send_frame(c, 9, NULL, 0);
}

// 닫기 프레임을 보내고, 다 보내지면 끊는다 (code 0 = 내용 없는 닫기)
static void fail(ws_client *c, int code) {
    if (c->state == ST_OPEN) {
        uint8_t b[2] = { (uint8_t)(code >> 8), (uint8_t)code };
        send_frame(c, 8, b, code ? 2 : 0);
        c->state = ST_CLOSING;
    }
    if (!c->close_code) c->close_code = code ? code : 1005;
    if (!c->dead) c->dead = 1;
}

void ws_close(ws_client *c, int code) {
    if (c && !c->dead) fail(c, code ? code : 1000);
}

// ── 연결 절차 ───────────────────────────────────────────────

static const char *status_text(int code) {
    switch (code) {
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 405: return "Method Not Allowed";
    case 426: return "Upgrade Required";
    case 431: return "Request Header Fields Too Large";
    default: return "Error";
    }
}

// ws 의 abortHandshake 와 같은 모양으로 답하고 끊는다
static void reject(ws_client *c, int code, const char *why, const char *extra) {
    ws_server *s = c->srv;
    sbuf r;
    sb_init(&r, 0);
    const char *msg = status_text(code);
    if (code == 426) {
        sb_printf(&r, "HTTP/1.1 426 Upgrade Required\r\nContent-Length: %u\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s",
                  (unsigned)strlen(msg), msg);
    } else {
        sb_printf(&r, "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: text/html\r\nContent-Length: %u\r\n%s\r\n%s",
                  code, msg, (unsigned)strlen(msg), extra ? extra : "", msg);
    }
    if (!r.err) queue(c, r.p, r.len);
    sb_free(&r);
    c->dead = 1;
    if (s->h.on_reject) s->h.on_reject(s, code, why, s->h.user);
}

static int ieq(const char *a, size_t al, const char *b) {
    size_t bl = strlen(b);
    if (al != bl) return 0;
    for (size_t i = 0; i < al; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return 1;
}

static void trim(const char **p, size_t *n) {
    while (*n && (**p == ' ' || **p == '\t')) { (*p)++; (*n)--; }
    while (*n && ((*p)[*n - 1] == ' ' || (*p)[*n - 1] == '\t')) (*n)--;
}

// 쉼표로 나뉜 목록에 tok 이 있는지 (Connection: keep-alive, Upgrade)
static int has_token(const char *v, size_t n, const char *tok) {
    size_t i = 0;
    while (i <= n) {
        size_t j = i;
        while (j < n && v[j] != ',') j++;
        const char *t = v + i;
        size_t tl = j - i;
        trim(&t, &tl);
        if (ieq(t, tl, tok)) return 1;
        i = j + 1;
    }
    return 0;
}

typedef struct { const char *p; size_t n; int seen; } hval;

// [p, lim) 안에서 CRLF 찾기 — 요청에 0 바이트가 섞여도 범위를 넘어 읽지 않는다 (strstr 쓰지 않음)
static const char *find_crlf(const char *p, const char *lim) {
    for (; p + 1 < lim; p++)
        if (p[0] == '\r' && p[1] == '\n') return p;
    return NULL;
}

static int origin_ok(const char *o, size_t n) {
    static const char pre[] = "chrome-extension://";
    size_t pl = sizeof pre - 1;
    if (n != pl + 32 || memcmp(o, pre, pl) != 0) return 0;
    for (size_t i = pl; i < n; i++)
        if (o[i] < 'a' || o[i] > 'p') return 0;
    return 1;
}

static int key_ok(const char *k, size_t n) {
    if (n != 24 || k[22] != '=' || k[23] != '=') return 0;
    for (size_t i = 0; i < 22; i++) {
        char ch = k[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/')) return 0;
    }
    return 1;
}

// 0 = 아직 덜 받음, 1 = 처리함
static int handshake(ws_client *c) {
    char *end = NULL;
    for (size_t i = 0; i + 3 < c->in.len; i++)
        if (c->in.p[i] == '\r' && c->in.p[i + 1] == '\n' && c->in.p[i + 2] == '\r' && c->in.p[i + 3] == '\n') { end = c->in.p + i; break; }
    if (!end) {
        if (c->in.len > WS_MAX_HEADER) reject(c, 431, "header too large", NULL);
        return c->in.len > WS_MAX_HEADER;
    }
    size_t hlen = (size_t)(end - c->in.p) + 4;
    if (hlen > WS_MAX_HEADER) { reject(c, 431, "header too large", NULL); return 1; }
    const char *p = c->in.p;
    const char *eol = find_crlf(p, end + 2);
    //  요청 줄: METHOD SP PATH SP HTTP/1.x
    const char *sp1 = memchr(p, ' ', (size_t)(eol - p));
    if (!sp1) { reject(c, 400, "bad request line", NULL); return 1; }
    const char *sp2 = memchr(sp1 + 1, ' ', (size_t)(eol - sp1 - 1));
    if (!sp2 || (size_t)(eol - sp2 - 1) != 8 || memcmp(sp2 + 1, "HTTP/1.", 7) != 0) { reject(c, 400, "bad request line", NULL); return 1; }
    size_t mlen = (size_t)(sp1 - p);
    hval upgrade = {0}, connection = {0}, key = {0}, version = {0}, origin = {0}, origin8 = {0};
    const char *q = eol + 2;
    while (q < end) {
        const char *le = find_crlf(q, end + 2);
        if (!le) break;
        const char *colon = memchr(q, ':', (size_t)(le - q));
        if (!colon) { reject(c, 400, "bad header", NULL); return 1; }
        const char *name = q;
        size_t nl = (size_t)(colon - q);
        const char *v = colon + 1;
        size_t vl = (size_t)(le - v);
        trim(&v, &vl);
        hval *slot = NULL;
        if (ieq(name, nl, "upgrade")) slot = &upgrade;
        else if (ieq(name, nl, "connection")) slot = &connection;
        else if (ieq(name, nl, "sec-websocket-key")) slot = &key;
        else if (ieq(name, nl, "sec-websocket-version")) slot = &version;
        else if (ieq(name, nl, "origin")) slot = &origin;
        else if (ieq(name, nl, "sec-websocket-origin")) slot = &origin8;
        if (slot) {
            //  같은 머리가 두 번 오면 Node 는 ", " 로 이어 붙인다 → Upgrade·Key·Version 은 모양이 틀려 400,
            //  Origin 은 확장 주소 모양이 아니게 되어 401. Connection 은 이어 붙인 목록에서 upgrade 를 찾는다
            if (slot->seen && (slot == &origin || slot == &origin8)) { slot->p = "dup"; slot->n = 3; }
            else if (slot->seen && slot == &connection) { if (!has_token(slot->p, slot->n, "upgrade")) { slot->p = v; slot->n = vl; } }
            else if (slot->seen) { reject(c, 400, "duplicate header", NULL); return 1; }
            else { slot->p = v; slot->n = vl; }
            slot->seen = 1;
        }
        q = le + 2;
    }
    //  Node http 는 Connection 에 upgrade 가 있고 Upgrade 머리가 있을 때만 업그레이드로 넘긴다 — 아니면 426
    if (!upgrade.seen || !connection.seen || !has_token(connection.p, connection.n, "upgrade")) { reject(c, 426, "not an upgrade request", NULL); return 1; }
    if (!(mlen == 3 && memcmp(p, "GET", 3) == 0)) { reject(c, 405, "Invalid HTTP method", NULL); return 1; }
    if (!ieq(upgrade.p, upgrade.n, "websocket")) { reject(c, 400, "Invalid Upgrade header", NULL); return 1; }
    if (!key.seen || !key_ok(key.p, key.n)) { reject(c, 400, "Missing or invalid Sec-WebSocket-Key header", NULL); return 1; }
    int ver = 0;
    if (version.seen) {
        //  JS 의 +"13" 처럼 — 앞뒤 공백만 허용한 정수
        const char *vp = version.p;
        size_t vn = version.n;
        while (vn > 1 && *vp == '0') { vp++; vn--; }   // JS 의 +"013" === 13
        if (vn == 2 && vp[0] == '1' && vp[1] == '3') ver = 13;
        else if (vn == 1 && vp[0] == '8') ver = 8;
    }
    if (ver != 13 && ver != 8) { reject(c, 400, "Missing or invalid Sec-WebSocket-Version header", "Sec-WebSocket-Version: 13, 8\r\n"); return 1; }
    hval *o = ver == 8 ? &origin8 : &origin;
    if (o->seen && !origin_ok(o->p, o->n)) { reject(c, 401, "origin not allowed", NULL); return 1; }

    //  101 — Sec-WebSocket-Accept = base64(sha1(key + GUID))
    char kg[24 + sizeof WS_GUID];
    memcpy(kg, key.p, 24);
    memcpy(kg + 24, WS_GUID, sizeof WS_GUID - 1);
    uint8_t dig[20];
    sha1(kg, 24 + sizeof WS_GUID - 1, dig);
    sbuf r;
    sb_init(&r, 0);
    sb_puts(&r, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    b64_encode(&r, dig, 20);
    sb_puts(&r, "\r\n\r\n");
    int okq = !r.err && queue(c, r.p, r.len);
    sb_free(&r);
    if (!okq) { c->dead = 2; return 1; }
    sb_consume(&c->in, hlen);
    c->state = ST_OPEN;
    return 1;
}

// ── 프레임 읽기 ─────────────────────────────────────────────

static void deliver(ws_client *c, int op, const char *data, size_t len) {
    ws_server *s = c->srv;
    if (op == 1 && !utf8_valid(data, len)) { fail(c, 1007); return; }
    if (s->h.on_message) s->h.on_message(s, c, data, len, op == 2, s->h.user);
}

// 받은 바이트에서 완성된 프레임을 처리한다. 더 받아야 하면 멈춘다
static void frames(ws_client *c) {
    while (c->state == ST_OPEN && !c->dead && c->in.len >= 2) {
        const uint8_t *b = (const uint8_t *)c->in.p;
        int fin = b[0] & 0x80, rsv = b[0] & 0x70, op = b[0] & 0x0F;
        int masked = b[1] & 0x80;
        uint64_t len = b[1] & 0x7F;
        size_t hl = 2;
        if (rsv) { fail(c, 1002); return; }
        if (!(op == 0 || op == 1 || op == 2 || op == 8 || op == 9 || op == 10)) { fail(c, 1002); return; }
        if (op >= 8 && (!fin || len > 125)) { fail(c, 1002); return; }
        if (!masked) { fail(c, 1002); return; }
        if (len == 126) {
            if (c->in.len < 4) return;
            len = ((uint64_t)b[2] << 8) | b[3];
            hl = 4;
        } else if (len == 127) {
            if (c->in.len < 10) return;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | b[2 + i];
            hl = 10;
            if (len > 0x1FFFFFFFFFFFFFull) { fail(c, 1009); return; }   // 2^53-1 초과 (ws 와 같게)
        }
        //  메시지(이어 붙인 것 포함) 크기 한도 — 다 받기 전에 끊는다
        if (op < 8) {
            uint64_t total = (uint64_t)(op == 0 ? c->msg.len : 0) + len;
            if (total > c->srv->max_payload) { fail(c, 1009); return; }
        }
        if (c->in.len < hl + 4 + len) return;
        const uint8_t *mk = b + hl;
        char *pl = c->in.p + hl + 4;
        for (uint64_t i = 0; i < len; i++) pl[i] = (char)(pl[i] ^ mk[i & 3]);
        size_t flen = hl + 4 + (size_t)len;
        if (op == 8) {
            if (len == 1) { fail(c, 1002); sb_consume(&c->in, flen); return; }
            int code = len >= 2 ? (((uint8_t)pl[0] << 8) | (uint8_t)pl[1]) : 1005;
            if (len >= 2 && (code < 1000 || code == 1004 || code == 1005 || code == 1006 || (code > 1015 && code < 3000) || code > 4999)) { fail(c, 1002); return; }
            if (len > 2 && !utf8_valid(pl + 2, (size_t)len - 2)) { fail(c, 1007); return; }
            c->close_code = code;
            fail(c, len >= 2 ? code : 0);   // 받은 코드 그대로 돌려준다
            sb_consume(&c->in, flen);
            return;
        }
        if (op == 9) { send_frame(c, 10, pl, (size_t)len); sb_consume(&c->in, flen); continue; }
        if (op == 10) {
            ws_server *s = c->srv;
            if (s->h.on_pong) s->h.on_pong(s, c, s->h.user);
            sb_consume(&c->in, flen);
            continue;
        }
        if (op == 0 && !c->msg_op) { fail(c, 1002); return; }
        if (op != 0 && c->msg_op) { fail(c, 1002); return; }
        if (fin && op != 0) {
            deliver(c, op, pl, (size_t)len);
        } else {
            if (op != 0) c->msg_op = op;
            sb_putn(&c->msg, pl, (size_t)len);
            if (c->msg.err) { fail(c, 1009); return; }
            if (fin) {
                int mop = c->msg_op;
                c->msg_op = 0;
                deliver(c, mop, c->msg.p ? c->msg.p : "", c->msg.len);
                sb_reset(&c->msg);
            }
        }
        sb_consume(&c->in, flen);
    }
}

// ── 서버 ────────────────────────────────────────────────────

ws_server *ws_listen(unsigned short port, const ws_handlers *h, size_t max_payload, int *err) {
    if (!g_wsa) {
        WSADATA w;
        int r = WSAStartup(MAKEWORD(2, 2), &w);
        if (r) { if (err) *err = r; return NULL; }
    }
    g_wsa++;
    ws_server *s = (ws_server *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = WSA_NOT_ENOUGH_MEMORY; g_wsa--; return NULL; }
    s->h = *h;
    s->max_payload = max_payload;
    s->ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->ls == INVALID_SOCKET) goto fail;
    BOOL one = TRUE;
    setsockopt(s->ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->ls, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR) goto fail;
    if (listen(s->ls, SOMAXCONN) == SOCKET_ERROR) goto fail;
    u_long nb = 1;
    ioctlsocket(s->ls, FIONBIO, &nb);
    //  연결이 오면 켜지는 이벤트 (못 만들면 예전처럼 메인 루프가 시간마다 들여다본다)
    s->ev = WSACreateEvent();
    if (s->ev == WSA_INVALID_EVENT || WSAEventSelect(s->ls, s->ev, FD_ACCEPT) == SOCKET_ERROR) {
        if (s->ev != WSA_INVALID_EVENT) WSACloseEvent(s->ev);
        s->ev = NULL;
    }
    return s;
fail:
    if (err) *err = WSAGetLastError();
    if (s->ls != INVALID_SOCKET) closesocket(s->ls);
    free(s);
    if (--g_wsa == 0) WSACleanup();
    return NULL;
}

static void drop(ws_server *s, int i) {
    ws_client *c = s->c[i];
    s->c[i] = NULL;
    if (c->notified_open && s->h.on_close) s->h.on_close(s, c, c->close_code ? c->close_code : 1006, s->h.user);
    closesocket(c->s);
    sb_free(&c->in);
    sb_free(&c->out);
    sb_free(&c->msg);
    free(c);
}

static void accept_new(ws_server *s) {
    for (;;) {
        SOCKET ns = accept(s->ls, NULL, NULL);
        if (ns == INVALID_SOCKET) return;
        int slot = -1;
        for (int i = 0; i < WS_MAX_CLIENTS; i++) if (!s->c[i]) { slot = i; break; }
        ws_client *c = slot >= 0 ? (ws_client *)calloc(1, sizeof *c) : NULL;
        if (!c) { closesocket(ns); continue; }
        u_long nb = 1;
        ioctlsocket(ns, FIONBIO, &nb);
        BOOL nd = TRUE;
        setsockopt(ns, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof nd);
        if (s->ev) WSAEventSelect(ns, s->ev, FD_READ | FD_WRITE | FD_CLOSE);   //  받을 것이 오면 바로 깨게
        c->s = ns;
        c->srv = s;
        c->id = ++s->next_id;
        c->t0 = GetTickCount64();
        sb_init(&c->in, s->max_payload + WS_MAX_HEADER + 64);
        sb_init(&c->out, 0);
        sb_init(&c->msg, s->max_payload);
        s->c[slot] = c;
    }
}

static void read_client(ws_client *c) {
    char tmp[65536];
    for (;;) {
        int n = recv(c->s, tmp, sizeof tmp, 0);
        if (n == 0) { c->dead = 2; if (!c->close_code) c->close_code = 1006; return; }
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) { c->dead = 2; if (!c->close_code) c->close_code = 1006; }
            return;
        }
        if (c->dead) continue;   // 끊기로 한 연결 — 버린다
        sb_putn(&c->in, tmp, (size_t)n);
        if (c->in.err) { if (c->state == ST_OPEN) fail(c, 1009); else c->dead = 2; return; }
        if (c->state == ST_HANDSHAKE) {
            if (!handshake(c)) continue;
            if (c->state == ST_OPEN && !c->dead) {
                c->notified_open = 1;
                ws_server *s = c->srv;
                if (s->h.on_open) s->h.on_open(s, c, s->h.user);
            }
        }
        if (c->state == ST_OPEN) frames(c);
    }
}

void ws_poll(ws_server *s, int timeout_ms) {
    if (!s) return;
    fd_set rd, wr;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_SET(s->ls, &rd);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_client *c = s->c[i];
        if (!c) continue;
        FD_SET(c->s, &rd);
        if (c->out.len) FD_SET(c->s, &wr);
    }
    if (s->ev) WSAResetEvent(s->ev);   //  이 뒤에 온 것은 다시 이벤트를 켠다 (놓치지 않음)
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int n = select(0, &rd, &wr, NULL, &tv);
    if (n == SOCKET_ERROR) return;
    if (FD_ISSET(s->ls, &rd)) accept_new(s);
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_client *c = s->c[i];
        if (!c) continue;
        if (FD_ISSET(c->s, &rd)) read_client(c);
        if (c->out.len && c->dead != 2) flush_out(c);
        if (c->state == ST_HANDSHAKE && !c->dead && now - c->t0 > WS_HANDSHAKE_MS) c->dead = 2;
        if (c->dead == 2 || (c->dead == 1 && c->out.len == 0)) {
            if (c->dead == 1) shutdown(c->s, SD_SEND);
            drop(s, i);
        }
    }
}

void ws_server_close(ws_server *s) {
    if (!s) return;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_client *c = s->c[i];
        if (!c) continue;
        if (c->state == ST_OPEN) { fail(c, 1001); flush_out(c); }
        c->close_code = 1001;
        drop(s, i);
    }
    closesocket(s->ls);
    if (s->ev) WSACloseEvent(s->ev);
    free(s);
    if (--g_wsa == 0) WSACleanup();
}
void *ws_event(const ws_server *s) { return s ? s->ev : NULL; }

size_t ws_count(const ws_server *s) {
    size_t n = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s->c[i] && s->c[i]->state == ST_OPEN && !s->c[i]->dead) n++;
    return n;
}

ws_client *ws_at(ws_server *s, size_t k) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s->c[i] && s->c[i]->state == ST_OPEN && !s->c[i]->dead && k-- == 0) return s->c[i];
    return NULL;
}

int ws_id(const ws_client *c) { return c ? c->id : 0; }
