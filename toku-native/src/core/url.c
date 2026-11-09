#include "url.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int is_hex(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hexv(int c) { return c <= '9' ? c - '0' : (lower(c) - 'a' + 10); }

void url_free(url_t *u) {
    sb_free(&u->host);
    sb_free(&u->path);
    sb_free(&u->query);
}

static void pct(sbuf *b, unsigned char c) {
    static const char H[] = "0123456789ABCDEF";
    char t[3] = { '%', H[c >> 4], H[c & 15] };
    sb_putn(b, t, 3);
}

// 경로 퍼센트 인코딩 집합: C0 제어·공백·" # < > ? ` { } 와 0x7E 초과
static int path_enc(unsigned char c) {
    return c < 0x21 || c > 0x7E || c == '"' || c == '#' || c == '<' || c == '>' || c == '?' || c == '`' || c == '{' || c == '}';
}
// 특수 스킴 쿼리 집합: C0 제어·공백·" # < > ' 와 0x7E 초과
static int query_enc(unsigned char c) {
    return c < 0x21 || c > 0x7E || c == '"' || c == '#' || c == '<' || c == '>' || c == '\'';
}

static int ieq_n(const char *a, size_t n, const char *b) {
    if (strlen(b) != n) return 0;
    for (size_t i = 0; i < n; i++) if (lower((unsigned char)a[i]) != b[i]) return 0;
    return 1;
}
static int single_dot(const char *s, size_t n) { return (n == 1 && s[0] == '.') || ieq_n(s, n, "%2e"); }
static int double_dot(const char *s, size_t n) {
    return (n == 2 && s[0] == '.' && s[1] == '.') || ieq_n(s, n, ".%2e") || ieq_n(s, n, "%2e.") || ieq_n(s, n, "%2e%2e");
}

// IPv4 숫자 한 칸 (0x 16진·0 8진·10진). 실패하면 0
static int ipv4_num(const char *s, size_t n, uint64_t *out) {
    int base = 10;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; n -= 2; }
    else if (n >= 2 && s[0] == '0') { base = 8; s++; n--; }
    if (n == 0) { *out = 0; return 1; }
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)s[i], d;
        if (base == 16 && is_hex(c)) d = hexv(c);
        else if (c >= '0' && c <= '9' && c - '0' < base) d = c - '0';
        else return 0;
        v = v * (uint64_t)base + (uint64_t)d;
        if (v > 0xFFFFFFFFFFull) v = 0xFFFFFFFFFFull;   // 넘침 막기 (어차피 범위 밖)
    }
    *out = v;
    return 1;
}

// 마지막 라벨이 숫자면 IPv4 로 읽는다. 0 = IPv4 아님, 1 = IPv4 로 바꿈, -1 = 실패
static int maybe_ipv4(sbuf *host) {
    char *h = host->p;
    size_t n = host->len;
    size_t end = n;
    if (end && h[end - 1] == '.') end--;
    size_t st = end;
    while (st > 0 && h[st - 1] != '.') st--;
    const char *last = h + st;
    size_t ll = end - st;
    if (ll == 0) return 0;
    int numeric = 1;
    for (size_t i = 0; i < ll; i++) if (last[i] < '0' || last[i] > '9') { numeric = 0; break; }
    if (!numeric) {
        if (!(ll >= 2 && last[0] == '0' && (last[1] == 'x' || last[1] == 'X'))) return 0;
        for (size_t i = 2; i < ll; i++) if (!is_hex((unsigned char)last[i])) return 0;
    }
    //  IPv4 해석
    uint64_t parts[5];
    size_t np = 0;
    size_t i = 0;
    size_t total = (n && h[n - 1] == '.') ? n - 1 : n;
    if (total == 0) return -1;
    while (i <= total) {
        size_t j = i;
        while (j < total && h[j] != '.') j++;
        if (np >= 4) return -1;
        if (j == i) return -1;
        if (!ipv4_num(h + i, j - i, &parts[np])) return -1;
        np++;
        i = j + 1;
    }
    for (size_t k = 0; k + 1 < np; k++) if (parts[k] > 255) return -1;
    uint64_t lim = 1;
    for (size_t k = 0; k < 5 - np; k++) lim *= 256;
    if (parts[np - 1] >= lim) return -1;
    uint64_t ip = parts[np - 1];
    for (size_t k = 0; k + 1 < np; k++) ip += parts[k] << (8 * (3 - k));
    sb_reset(host);
    sb_printf(host, "%u.%u.%u.%u", (unsigned)((ip >> 24) & 255), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255), (unsigned)(ip & 255));
    return 1;
}

int url_parse(url_t *u, const char *s0, size_t len0) {
    memset(u, 0, sizeof *u);
    u->port = -1;
    sb_init(&u->host, 0);
    sb_init(&u->path, 0);
    sb_init(&u->query, 0);
    //  앞뒤 C0 제어·공백 자르기, 탭·줄바꿈 빼기
    size_t a = 0, b = len0;
    while (a < b && (unsigned char)s0[a] <= 0x20) a++;
    while (b > a && (unsigned char)s0[b - 1] <= 0x20) b--;
    sbuf in;
    sb_init(&in, 0);
    for (size_t i = a; i < b; i++) if (s0[i] != '\t' && s0[i] != '\n' && s0[i] != '\r') sb_putc(&in, s0[i]);
    if (in.err) { sb_free(&in); return 0; }
    const char *s = in.p ? in.p : "";
    size_t len = in.len, i = 0;
    //  스킴
    size_t c = 0;
    while (c < len && s[c] != ':') c++;
    if (c == len) { sb_free(&in); return 0; }
    if (ieq_n(s, c, "http")) strcpy(u->scheme, "http");
    else if (ieq_n(s, c, "https")) strcpy(u->scheme, "https");
    else { sb_free(&in); return 0; }   // 여기서 쓰는 곳은 늘 http(s) 다
    i = c + 1;
    while (i < len && (s[i] == '/' || s[i] == '\\')) i++;   // 특수 스킴은 빗금을 몇 개든 건너뛴다
    size_t as = i;
    while (i < len && s[i] != '/' && s[i] != '\\' && s[i] != '?' && s[i] != '#') i++;
    const char *auth = s + as;
    size_t al = i - as;
    //  사용자 정보 (@) 는 버린다
    for (size_t k = al; k > 0; k--) if (auth[k - 1] == '@') { auth += k; al -= k; break; }
    if (al == 0) { sb_free(&in); return 0; }
    if (auth[0] == '[') { sb_free(&in); return 0; }   // IPv6 — 지원하지 않음
    size_t hl = al;
    for (size_t k = 0; k < al; k++) if (auth[k] == ':') { hl = k; break; }
    if (hl < al) {
        const char *pp = auth + hl + 1;
        size_t pl = al - hl - 1;
        if (pl) {
            long port = 0;
            for (size_t k = 0; k < pl; k++) {
                if (pp[k] < '0' || pp[k] > '9') { sb_free(&in); return 0; }
                port = port * 10 + (pp[k] - '0');
                if (port > 65535) { sb_free(&in); return 0; }
            }
            int dflt = !strcmp(u->scheme, "http") ? 80 : 443;
            u->port = (port == dflt) ? -1 : (int)port;
        }
    }
    if (hl == 0) { sb_free(&in); return 0; }
    //  호스트: 퍼센트 디코드 → ASCII 만 (아니면 IDNA 라 지원 안 함) → 소문자 → 금지 글자 검사
    for (size_t k = 0; k < hl; k++) {
        unsigned char ch = (unsigned char)auth[k];
        if (ch == '%' && k + 2 < hl && is_hex((unsigned char)auth[k + 1]) && is_hex((unsigned char)auth[k + 2])) {
            ch = (unsigned char)(hexv((unsigned char)auth[k + 1]) * 16 + hexv((unsigned char)auth[k + 2]));
            k += 2;
        }
        if (ch >= 0x80) { sb_free(&in); return 0; }
        sb_putc(&u->host, (char)lower(ch));
    }
    {
        const char *h = u->host.p;
        for (size_t k = 0; k < u->host.len; k++) {
            unsigned char ch = (unsigned char)h[k];
            if (ch <= 0x20 || ch == 0x7F || strchr("#%/:<>?@[\\]^|", ch)) { sb_free(&in); return 0; }
        }
        //  xn-- 라벨은 IDNA 검사가 필요하다 — 지원하지 않음
        for (size_t k = 0; k + 4 <= u->host.len; k++)
            if ((k == 0 || h[k - 1] == '.') && !memcmp(h + k, "xn--", 4)) { sb_free(&in); return 0; }
        if (u->host.len == 0 || maybe_ipv4(&u->host) < 0) { sb_free(&in); return 0; }
    }
    //  경로
    typedef struct { size_t off, len; } seg;
    sbuf segs;   // 조각들을 이어 붙인 글자
    sb_init(&segs, 0);
    seg *list = NULL;
    size_t nseg = 0, capseg = 0;
    sbuf buf;
    sb_init(&buf, 0);
    if (i < len && (s[i] == '/' || s[i] == '\\')) i++;
    int ok = 1;
    for (;;) {
        int ch = i < len ? (unsigned char)s[i] : -1;
        if (ch == -1 || ch == '/' || ch == '\\' || ch == '?' || ch == '#') {
            const char *bp = buf.p ? buf.p : "";
            int slash = (ch == '/' || ch == '\\');
            int push = 0;
            if (double_dot(bp, buf.len)) {
                if (nseg) nseg--;
                if (!slash) { sb_reset(&buf); push = 1; }
            } else if (single_dot(bp, buf.len)) {
                if (!slash) { sb_reset(&buf); push = 1; }
            } else push = 1;
            if (push) {
                if (nseg == capseg) {
                    capseg = capseg ? capseg * 2 : 8;
                    seg *nl = (seg *)realloc(list, capseg * sizeof *nl);
                    if (!nl) { ok = 0; break; }
                    list = nl;
                }
                list[nseg].off = segs.len;
                list[nseg].len = buf.len;
                sb_putn(&segs, buf.p ? buf.p : "", buf.len);
                nseg++;
            }
            sb_reset(&buf);
            if (ch == '/' || ch == '\\') { i++; continue; }
            break;
        }
        if (path_enc((unsigned char)ch)) pct(&buf, (unsigned char)ch);
        else sb_putc(&buf, (char)ch);
        i++;
    }
    if (ok) for (size_t k = 0; k < nseg; k++) { sb_putc(&u->path, '/'); sb_putn(&u->path, segs.p + list[k].off, list[k].len); }
    if (ok && nseg == 0) sb_putc(&u->path, '/');
    free(list);
    sb_free(&segs);
    sb_free(&buf);
    if (!ok) { sb_free(&in); return 0; }
    //  쿼리 (# 앞까지)
    if (i < len && s[i] == '?') {
        u->has_query = 1;
        i++;
        while (i < len && s[i] != '#') {
            unsigned char ch = (unsigned char)s[i++];
            if (query_enc(ch)) pct(&u->query, ch);
            else sb_putc(&u->query, (char)ch);
        }
    }
    int good = !in.err && !u->host.err && !u->path.err && !u->query.err;
    sb_free(&in);
    return good;
}

void url_origin(sbuf *out, const url_t *u) {
    sb_puts(out, u->scheme);
    sb_puts(out, "://");
    sb_putn(out, u->host.p ? u->host.p : "", u->host.len);
    if (u->port >= 0) sb_printf(out, ":%d", u->port);
}

// WHATWG UTF-8 디코드 (깨진 부분마다 U+FFFD 하나)
static void utf8_decode_lossy(sbuf *out, const unsigned char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) { sb_putc(out, (char)c); i++; continue; }
        size_t need;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else { sb_puts(out, "\xEF\xBF\xBD"); i++; continue; }
        size_t k = 1;
        for (; k <= need; k++) {
            if (i + k >= n) break;
            unsigned char d = p[i + k];
            if (d < lo || d > hi) break;
            lo = 0x80; hi = 0xBF;
        }
        if (k == need + 1) { sb_putn(out, p + i, need + 1); i += need + 1; }
        else { sb_puts(out, "\xEF\xBF\xBD"); i += k; }
    }
}

// application/x-www-form-urlencoded 의 한 조각 디코드 (+ → 공백, %XX → 바이트, 그다음 UTF-8)
static void form_decode(sbuf *out, const char *s, size_t n) {
    sbuf bytes;
    sb_init(&bytes, 0);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '+') c = ' ';
        else if (c == '%' && i + 2 < n && is_hex((unsigned char)s[i + 1]) && is_hex((unsigned char)s[i + 2])) {
            c = (unsigned char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]));
            i += 2;
        }
        sb_putc(&bytes, (char)c);
    }
    utf8_decode_lossy(out, (const unsigned char *)(bytes.p ? bytes.p : ""), bytes.len);
    sb_free(&bytes);
}

int url_param(const url_t *u, const char *name, sbuf *out) {
    if (!u->has_query) return 0;
    const char *q = u->query.p ? u->query.p : "";
    size_t n = u->query.len, i = 0;
    while (i <= n) {
        size_t j = i;
        while (j < n && q[j] != '&') j++;
        if (j > i) {
            size_t eq = i;
            while (eq < j && q[eq] != '=') eq++;
            sbuf k;
            sb_init(&k, 0);
            form_decode(&k, q + i, eq - i);
            int match = k.len == strlen(name) && !memcmp(k.p ? k.p : "", name, k.len);
            sb_free(&k);
            if (match) {
                if (eq < j) form_decode(out, q + eq + 1, j - eq - 1);
                return 1;
            }
        }
        i = j + 1;
    }
    return 0;
}

void encode_uri_component(sbuf *out, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c && strchr("-_.!~*'()", c))) sb_putc(out, (char)c);
        else pct(out, c);
    }
}

// ── 버튼 주소 ───────────────────────────────────────────────

static int word_ch(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static int is_dig(int c) { return c >= '0' && c <= '9'; }

// /^\/(shorts\/[\w-]+|live\/[\w-]+|@[^/]+|channel\/[^/]+|c\/[^/]+)/ — 맞으면 m[1] 길이
static size_t yt_path_match(const char *p, size_t n) {
    if (n < 2 || p[0] != '/') return 0;
    const char *r = p + 1;
    size_t rn = n - 1;
    struct { const char *pre; int word; } alts[] = { { "shorts/", 1 }, { "live/", 1 }, { "@", 0 }, { "channel/", 0 }, { "c/", 0 } };
    for (size_t a = 0; a < 5; a++) {
        size_t pl = strlen(alts[a].pre);
        if (rn <= pl || memcmp(r, alts[a].pre, pl) != 0) continue;
        size_t k = pl;
        while (k < rn && (alts[a].word ? (word_ch((unsigned char)r[k]) || r[k] == '-') : r[k] != '/')) k++;
        if (k > pl) return k;
    }
    return 0;
}

void yt_button_url(sbuf *out, const char *raw, size_t len, const char *home) {
    url_t u;
    if (!url_parse(&u, raw, len)) { url_free(&u); sb_puts(out, home); return; }
    const char *p = u.path.p ? u.path.p : "";
    sbuf v, list;
    sb_init(&v, 0);
    sb_init(&list, 0);
    int hv = url_param(&u, "v", &v) && v.len;
    int hl = url_param(&u, "list", &list) && list.len;
    size_t m;
    if (!strcmp(p, "/watch") && hv) {
        sb_puts(out, "https://www.youtube.com/watch?v=");
        encode_uri_component(out, v.p, v.len);
    } else if ((m = yt_path_match(p, u.path.len)) != 0) {
        sb_puts(out, "https://www.youtube.com/");
        sb_putn(out, p + 1, m);
    } else if (!strcmp(p, "/playlist") && hl) {
        sb_puts(out, "https://www.youtube.com/playlist?list=");
        encode_uri_component(out, list.p, list.len);
    } else {
        url_origin(out, &u);
        sb_putn(out, p, u.path.len);
    }
    sb_free(&v);
    sb_free(&list);
    url_free(&u);
}

// /^\/(live\/[0-9a-fA-F]{32}|video\/\d+|clips\/[A-Za-z0-9_-]+|[0-9a-fA-F]{32})(?=\/|$)/
static size_t chzzk_path_match(const char *p, size_t n) {
    if (n < 2 || p[0] != '/') return 0;
    const char *r = p + 1;
    size_t rn = n - 1;
    #define END_OK(k) ((k) == rn || r[(k)] == '/')
    if (rn > 5 && !memcmp(r, "live/", 5)) {
        size_t k = 5;
        while (k < rn && k < 37 && is_hex((unsigned char)r[k])) k++;
        if (k == 37 && END_OK(k)) return k;
    }
    if (rn > 6 && !memcmp(r, "video/", 6)) {
        size_t k = 6;
        while (k < rn && is_dig((unsigned char)r[k])) k++;
        if (k > 6 && END_OK(k)) return k;
    }
    if (rn > 6 && !memcmp(r, "clips/", 6)) {
        size_t k = 6;
        while (k < rn && (word_ch((unsigned char)r[k]) || r[k] == '-')) k++;
        if (k > 6 && END_OK(k)) return k;
    }
    if (rn >= 32) {
        size_t k = 0;
        while (k < 32 && is_hex((unsigned char)r[k])) k++;
        if (k == 32 && END_OK(k)) return k;
    }
    #undef END_OK
    return 0;
}

void chzzk_button_url(sbuf *out, const char *raw, size_t len, const char *home) {
    url_t u;
    if (!url_parse(&u, raw, len)) { url_free(&u); sb_puts(out, home); return; }
    sbuf o;
    sb_init(&o, 0);
    url_origin(&o, &u);
    if (strcmp(o.p ? o.p : "", "https://chzzk.naver.com") != 0) { sb_puts(out, home); }
    else {
        const char *p = u.path.p ? u.path.p : "";
        size_t m = chzzk_path_match(p, u.path.len);
        if (m) { sb_puts(out, "https://chzzk.naver.com/"); sb_putn(out, p + 1, m); }
        else { sb_putn(out, o.p, o.len); sb_putn(out, p, u.path.len); }
    }
    sb_free(&o);
    url_free(&u);
}

static int all_digits(const sbuf *b, size_t n) {
    if (b->len != n) return 0;
    for (size_t i = 0; i < n; i++) if (!is_dig((unsigned char)b->p[i])) return 0;
    return 1;
}

void danime_button_url(sbuf *out, const char *raw, size_t len, const char *home) {
    url_t u;
    if (!url_parse(&u, raw, len)) { url_free(&u); sb_puts(out, home); return; }
    sbuf o, part, work;
    sb_init(&o, 0);
    sb_init(&part, 0);
    sb_init(&work, 0);
    url_origin(&o, &u);
    if (strcmp(o.p ? o.p : "", "https://animestore.docomo.ne.jp") != 0) { sb_puts(out, home); goto done; }
    url_param(&u, "partId", &part);
    if (!all_digits(&part, 8)) sb_reset(&part);
    url_param(&u, "workId", &work);
    if (!all_digits(&work, 5)) sb_reset(&work);
    if (!work.len && part.len) sb_putn(&work, part.p, 5);
    const char *p = u.path.p ? u.path.p : "";
    int pathok = !strcmp(p, "/animestore/ci_pc") || !strcmp(p, "/animestore/sc_d_pc") || !strcmp(p, "/animestore/sc_pc") || !strcmp(p, "/animestore/sc_pc_l");
    if (work.len && pathok) {
        sb_puts(out, "https://animestore.docomo.ne.jp/animestore/ci_pc?workId=");
        sb_putn(out, work.p, work.len);
        if (part.len && !memcmp(part.p, work.p, 5)) { sb_puts(out, "&partId="); sb_putn(out, part.p, part.len); }
    } else {
        sb_putn(out, o.p, o.len);
        sb_putn(out, p, u.path.len);
    }
done:
    sb_free(&o);
    sb_free(&part);
    sb_free(&work);
    url_free(&u);
}
