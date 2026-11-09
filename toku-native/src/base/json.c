#include "json.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── 만들기·해제 ─────────────────────────────────────────────

jval *json_new(jtype t) {
    jval *v = (jval *)calloc(1, sizeof(jval));
    if (v) v->t = t;
    return v;
}

jval *json_new_strn(const char *s, size_t n) {
    jval *v = json_new(J_STR);
    if (!v) return NULL;
    v->s = (char *)malloc(n + 1);
    if (!v->s) { free(v); return NULL; }
    if (n) memcpy(v->s, s, n);
    v->s[n] = '\0';
    v->slen = n;
    return v;
}

jval *json_new_str(const char *s) { return json_new_strn(s, strlen(s)); }

jval *json_new_num(double n) {
    jval *v = json_new(J_NUM);
    if (v) v->n = n;
    return v;
}

jval *json_new_bool(int b) {
    jval *v = json_new(J_BOOL);
    if (v) v->b = b ? 1 : 0;
    return v;
}

void json_free(jval *v) {
    if (!v) return;
    free(v->s);
    for (size_t i = 0; i < v->count; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->klens);
    free(v);
}

static int grow(jval *c) {
    if (c->count < c->cap) return 1;
    size_t cap = c->cap ? c->cap * 2 : 4;
    if (cap > ((size_t)-1) / sizeof(void *)) return 0;
    jval **ni = (jval **)realloc(c->items, cap * sizeof(jval *));
    if (!ni) return 0;
    c->items = ni;
    if (c->t == J_OBJ) {
        char **nk = (char **)realloc(c->keys, cap * sizeof(char *));
        if (!nk) return 0;
        c->keys = nk;
        size_t *nl = (size_t *)realloc(c->klens, cap * sizeof(size_t));
        if (!nl) return 0;
        c->klens = nl;
    }
    c->cap = cap;
    return 1;
}

int json_arr_push(jval *arr, jval *v) {
    if (!arr || arr->t != J_ARR || !v || !grow(arr)) { json_free(v); return 0; }
    arr->items[arr->count++] = v;
    return 1;
}

static int obj_setn(jval *obj, const char *key, size_t klen, jval *v) {
    if (!obj || obj->t != J_OBJ || !v) { json_free(v); return 0; }
    for (size_t i = 0; i < obj->count; i++) {
        if (obj->klens[i] == klen && memcmp(obj->keys[i], key, klen) == 0) {
            json_free(obj->items[i]);
            obj->items[i] = v;
            return 1;
        }
    }
    if (!grow(obj)) { json_free(v); return 0; }
    char *k = (char *)malloc(klen + 1);
    if (!k) { json_free(v); return 0; }
    if (klen) memcpy(k, key, klen);
    k[klen] = '\0';
    obj->keys[obj->count] = k;
    obj->klens[obj->count] = klen;
    obj->items[obj->count] = v;
    obj->count++;
    return 1;
}

int json_obj_set(jval *obj, const char *key, jval *v) {
    return obj_setn(obj, key, strlen(key), v);
}

// ── 읽기 도우미 ─────────────────────────────────────────────

const jval *json_get(const jval *obj, const char *key) {
    if (!obj || obj->t != J_OBJ || !key) return NULL;
    size_t kl = strlen(key);
    for (size_t i = 0; i < obj->count; i++)
        if (obj->klens[i] == kl && memcmp(obj->keys[i], key, kl) == 0) return obj->items[i];
    return NULL;
}
const char *json_str(const jval *v, const char *dflt) { return v && v->t == J_STR ? v->s : dflt; }
double json_num(const jval *v, double dflt) { return v && v->t == J_NUM ? v->n : dflt; }
int json_bool(const jval *v, int dflt) { return v && v->t == J_BOOL ? v->b : dflt; }
const char *json_get_str(const jval *o, const char *k, const char *d) { return json_str(json_get(o, k), d); }
double json_get_num(const jval *o, const char *k, double d) { return json_num(json_get(o, k), d); }
int json_get_bool(const jval *o, const char *k, int d) { return json_bool(json_get(o, k), d); }

// ── 읽기 ────────────────────────────────────────────────────

typedef struct {
    const unsigned char *s;
    size_t len;
    size_t i;
    int fail;
} P;

static void ws(P *p) {
    while (p->i < p->len) {
        unsigned char c = p->s[p->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->i++;
        else break;
    }
}

static int lit(P *p, const char *w) {
    size_t n = strlen(w);
    if (p->len - p->i < n || memcmp(p->s + p->i, w, n) != 0) return 0;
    p->i += n;
    return 1;
}

// UTF-8 한 글자 검사: 올바르면 바이트 수, 아니면 0 (긴 표현·서로게이트·U+10FFFF 초과 거절)
static size_t utf8_ok(const unsigned char *s, size_t avail) {
    unsigned char c = s[0];
    if (c < 0x80) return 1;
    size_t n;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) { n = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 3; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 4; cp = c & 0x07; }
    else return 0;
    if (avail < n) return 0;
    for (size_t k = 1; k < n; k++) {
        if ((s[k] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (s[k] & 0x3F);
    }
    if (n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
    if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
    return n;
}

static void put_utf8(sbuf *b, uint32_t cp) {
    char t[4];
    if (cp < 0x80) { t[0] = (char)cp; sb_putn(b, t, 1); }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); sb_putn(b, t, 2); }
    else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[2] = (char)(0x80 | (cp & 0x3F));
        sb_putn(b, t, 3);
    } else {
        t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (char)(0x80 | (cp & 0x3F));
        sb_putn(b, t, 4);
    }
}

static int hex4(P *p, uint32_t *out) {
    if (p->len - p->i < 4) return 0;
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        unsigned char c = p->s[p->i + k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return 0;
    }
    p->i += 4;
    *out = v;
    return 1;
}

// 따옴표 다음부터 읽어 b 에 UTF-8 로 담는다. 짝 없는 \uD800~\uDFFF 는 U+FFFD 로 바꾼다
//  (JS 문자열은 깨진 서로게이트를 담을 수 있다 — 메시지 전체를 버리는 대신 그 글자만 바꾼다)
static int parse_string(P *p, sbuf *b) {
    p->i++;   // 여는 따옴표
    for (;;) {
        if (p->i >= p->len) return 0;
        unsigned char c = p->s[p->i];
        if (c == '"') { p->i++; return !b->err; }
        if (c < 0x20) return 0;
        if (c == '\\') {
            p->i++;
            if (p->i >= p->len) return 0;
            unsigned char e = p->s[p->i++];
            switch (e) {
            case '"': sb_putc(b, '"'); break;
            case '\\': sb_putc(b, '\\'); break;
            case '/': sb_putc(b, '/'); break;
            case 'b': sb_putc(b, '\b'); break;
            case 'f': sb_putc(b, '\f'); break;
            case 'n': sb_putc(b, '\n'); break;
            case 'r': sb_putc(b, '\r'); break;
            case 't': sb_putc(b, '\t'); break;
            case 'u': {
                uint32_t u;
                if (!hex4(p, &u)) return 0;
                if (u >= 0xD800 && u <= 0xDBFF) {
                    uint32_t lo;
                    size_t save = p->i;
                    if (p->len - p->i >= 6 && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                        p->i += 2;
                        if (!hex4(p, &lo)) return 0;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            put_utf8(b, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                            break;
                        }
                        p->i = save;   // 다음 \u 는 따로 다시 읽는다
                    }
                    put_utf8(b, 0xFFFD);
                } else if (u >= 0xDC00 && u <= 0xDFFF) {
                    put_utf8(b, 0xFFFD);
                } else {
                    put_utf8(b, u);
                }
                break;
            }
            default: return 0;
            }
            continue;
        }
        size_t n = utf8_ok(p->s + p->i, p->len - p->i);
        if (!n) return 0;
        sb_putn(b, p->s + p->i, n);
        p->i += n;
    }
}

static jval *parse_value(P *p, int depth);

static jval *parse_number(P *p) {
    size_t st = p->i;
    if (p->i < p->len && p->s[p->i] == '-') p->i++;
    if (p->i >= p->len) return NULL;
    if (p->s[p->i] == '0') p->i++;
    else if (p->s[p->i] >= '1' && p->s[p->i] <= '9') { while (p->i < p->len && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++; }
    else return NULL;
    if (p->i < p->len && p->s[p->i] == '.') {
        p->i++;
        size_t d = p->i;
        while (p->i < p->len && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
        if (p->i == d) return NULL;
    }
    if (p->i < p->len && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        p->i++;
        if (p->i < p->len && (p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
        size_t d = p->i;
        while (p->i < p->len && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
        if (p->i == d) return NULL;
    }
    //  입력은 '\0' 로 끝나지 않을 수 있다 — strtod 가 넘어 읽지 않게 잘라서 넘긴다
    size_t n = p->i - st;
    char small[64];
    char *tmp = n < sizeof small ? small : (char *)malloc(n + 1);
    if (!tmp) return NULL;
    memcpy(tmp, p->s + st, n);
    tmp[n] = '\0';
    double d = strtod(tmp, NULL);
    if (tmp != small) free(tmp);
    return json_new_num(d);   // 너무 큰 값은 JS 처럼 Infinity 가 된다 (쓸 때 null)
}

static jval *parse_value(P *p, int depth) {
    ws(p);
    if (p->i >= p->len) return NULL;
    unsigned char c = p->s[p->i];
    //  깊이 = 겹친 괄호 수. 64 겹까지 받는다 (밖에서 온 입력이 재귀로 스택을 넘기지 못하게)
    if ((c == '{' || c == '[') && depth >= JSON_MAX_DEPTH) return NULL;
    if (c == '{') {
        jval *o = json_new(J_OBJ);
        if (!o) return NULL;
        p->i++;
        ws(p);
        if (p->i < p->len && p->s[p->i] == '}') { p->i++; return o; }
        for (;;) {
            ws(p);
            if (p->i >= p->len || p->s[p->i] != '"') break;
            sbuf k;
            sb_init(&k, 0);
            if (!parse_string(p, &k)) { sb_free(&k); break; }
            ws(p);
            if (p->i >= p->len || p->s[p->i] != ':') { sb_free(&k); break; }
            p->i++;
            jval *v = parse_value(p, depth + 1);
            if (!v) { sb_free(&k); break; }
            int okset = obj_setn(o, k.p ? k.p : "", k.len, v);
            sb_free(&k);
            if (!okset) break;
            ws(p);
            if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
            if (p->i < p->len && p->s[p->i] == '}') { p->i++; return o; }
            break;
        }
        json_free(o);
        return NULL;
    }
    if (c == '[') {
        jval *a = json_new(J_ARR);
        if (!a) return NULL;
        p->i++;
        ws(p);
        if (p->i < p->len && p->s[p->i] == ']') { p->i++; return a; }
        for (;;) {
            jval *v = parse_value(p, depth + 1);
            if (!v || !json_arr_push(a, v)) break;
            ws(p);
            if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
            if (p->i < p->len && p->s[p->i] == ']') { p->i++; return a; }
            break;
        }
        json_free(a);
        return NULL;
    }
    if (c == '"') {
        sbuf b;
        sb_init(&b, 0);
        if (!parse_string(p, &b)) { sb_free(&b); return NULL; }
        jval *v = json_new(J_STR);
        if (!v) { sb_free(&b); return NULL; }
        v->slen = b.len;
        v->s = sb_detach(&b);
        if (!v->s) { free(v); return NULL; }
        return v;
    }
    if (c == 't') return lit(p, "true") ? json_new_bool(1) : NULL;
    if (c == 'f') return lit(p, "false") ? json_new_bool(0) : NULL;
    if (c == 'n') return lit(p, "null") ? json_new(J_NULL) : NULL;
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);
    return NULL;
}

jval *json_parse(const char *s, size_t len, size_t *err_pos) {
    P p = { (const unsigned char *)s, len, 0, 0 };
    jval *v = s ? parse_value(&p, 0) : NULL;
    if (v) {
        ws(&p);
        if (p.i != p.len) { json_free(v); v = NULL; }   // 값 뒤에 쓰레기
    }
    if (!v && err_pos) *err_pos = p.i;
    return v;
}

// ── 쓰기 ────────────────────────────────────────────────────

void json_write_str(sbuf *b, const char *s, size_t len) {
    static const char hx[] = "0123456789abcdef";
    sb_putc(b, '"');
    size_t run = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u[6];
        if (c == '"') esc = "\\\"";
        else if (c == '\\') esc = "\\\\";
        else if (c == '\n') esc = "\\n";
        else if (c == '\r') esc = "\\r";
        else if (c == '\t') esc = "\\t";
        else if (c == '\b') esc = "\\b";
        else if (c == '\f') esc = "\\f";
        else if (c < 0x20) { u[0] = '\\'; u[1] = 'u'; u[2] = '0'; u[3] = '0'; u[4] = hx[c >> 4]; u[5] = hx[c & 15]; }
        else { run++; continue; }
        sb_putn(b, s + i - run, run);
        run = 0;
        if (esc) sb_puts(b, esc);
        else sb_putn(b, u, 6);
    }
    sb_putn(b, s + len - run, run);
    sb_putc(b, '"');
}

// JS 의 Number → 문자열(ECMAScript Number::toString)과 똑같이 쓴다 — 지금 앱(JSON.stringify)과 한 글자도 다르면 안 된다
//  1) 되읽어 같은 값이 되는 가장 짧은 유효 숫자 d1..dk 와 지수 n (값 = 0.d1..dk × 10^n) 을 구한다
//  2) k≤n≤21: 숫자 뒤 0 채움 · 0<n≤21: 소수점 · -6<n≤0: 0.000.. · 그 밖: 지수 표기(1.5e+21, 1e-7)
void js_number_str(sbuf *b, double v) {
    if (isnan(v)) { sb_puts(b, "NaN"); return; }
    if (isinf(v)) { sb_puts(b, v > 0 ? "Infinity" : "-Infinity"); return; }
    if (v == 0) { sb_puts(b, "0"); return; }
    //  유한한 값은 JSON 쓰기(write_num)와 같은 규칙이다
    jval tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.t = J_NUM;
    tmp.n = v;
    json_write(b, &tmp);
}

static void write_num(sbuf *b, double v) {
    if (!isfinite(v)) { sb_puts(b, "null"); return; }
    if (v == 0) { sb_puts(b, "0"); return; }   // -0 도 "0"
    if (v < 0) { sb_putc(b, '-'); v = -v; }
    char t[48];
    int prec;
    for (prec = 1; prec <= 17; prec++) {
        snprintf(t, sizeof t, "%.*e", prec - 1, v);
        if (strtod(t, NULL) == v) break;
    }
    if (prec > 17) prec = 17;
    //  t = "d.ddddde±XX" → 숫자열과 지수
    char digits[24];
    int k = 0;
    const char *q = t;
    for (; *q && *q != 'e'; q++)
        if (*q >= '0' && *q <= '9' && k < (int)sizeof digits - 1) digits[k++] = *q;
    digits[k] = '\0';
    while (k > 1 && digits[k - 1] == '0') digits[--k] = '\0';   // 끝의 0 은 유효 숫자가 아니다
    int e10 = *q == 'e' ? atoi(q + 1) : 0;
    int n = e10 + 1;
    if (k <= n && n <= 21) {
        sb_putn(b, digits, (size_t)k);
        for (int i = 0; i < n - k; i++) sb_putc(b, '0');
    } else if (0 < n && n <= 21) {
        sb_putn(b, digits, (size_t)n);
        sb_putc(b, '.');
        sb_putn(b, digits + n, (size_t)(k - n));
    } else if (-6 < n && n <= 0) {
        sb_puts(b, "0.");
        for (int i = 0; i < -n; i++) sb_putc(b, '0');
        sb_putn(b, digits, (size_t)k);
    } else {
        sb_putc(b, digits[0]);
        if (k > 1) { sb_putc(b, '.'); sb_putn(b, digits + 1, (size_t)(k - 1)); }
        sb_printf(b, "e%c%d", n - 1 >= 0 ? '+' : '-', abs(n - 1));
    }
}

void json_write(sbuf *b, const jval *v) {
    if (!v) { sb_puts(b, "null"); return; }
    switch (v->t) {
    case J_NULL: sb_puts(b, "null"); break;
    case J_BOOL: sb_puts(b, v->b ? "true" : "false"); break;
    case J_NUM: write_num(b, v->n); break;
    case J_STR: json_write_str(b, v->s, v->slen); break;
    case J_ARR:
        sb_putc(b, '[');
        for (size_t i = 0; i < v->count; i++) {
            if (i) sb_putc(b, ',');
            json_write(b, v->items[i]);
        }
        sb_putc(b, ']');
        break;
    case J_OBJ:
        sb_putc(b, '{');
        for (size_t i = 0; i < v->count; i++) {
            if (i) sb_putc(b, ',');
            json_write_str(b, v->keys[i], v->klens[i]);
            sb_putc(b, ':');
            json_write(b, v->items[i]);
        }
        sb_putc(b, '}');
        break;
    }
}

char *json_stringify(const jval *v) {
    sbuf b;
    sb_init(&b, 0);
    json_write(&b, v);
    if (b.err) { sb_free(&b); return NULL; }
    return sb_detach(&b);
}
