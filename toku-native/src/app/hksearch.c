//  웹훅 폴더 검색 — 글 그대로 + 한글 발음 키
//  1) 글 그대로: NFKC · 소문자 · 가타카나→히라가나 · 띄어쓰기·기호·장음(ー) 빼고 포함 여부 (響鬼 · ヒビキ · ひびき · hibiki 같은 영문 제목)
//  2) 발음 키: 일본어 읽기(가나)와 한글을 같은 '소리 토막'으로 바꿔 비교 — 표기 흔들림(ㄱ/ㅋ, ㅈ/ㅊ, 쥬/주, 장음, 받침 ㅅ)은 같은 것으로 본다
//     한자는 Windows 일본어 읽기 분석기(Windows.Globalization.JapanesePhoneticAnalyzer)로 읽는다 — 響鬼→ひびき
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "hksearch.h"
#include "../base/utf.h"

const char *(*hks_read_hook)(const char *s) = NULL;

// ── 정규화 ──
static int is_kanji(wchar_t c) { return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || c == 0x3005; }
static int is_hira(wchar_t c) { return c >= 0x3041 && c <= 0x3096; }
static int is_hangul(wchar_t c) { return c >= 0xAC00 && c <= 0xD7A3; }
static int is_jamo(wchar_t c) { return (c >= 0x3131 && c <= 0x318E) || (c >= 0x1100 && c <= 0x11FF); }   //  한글 입력 중인 낱자 (ㅎ · ㅣ — NFKC 뒤엔 U+1100 대)
static wchar_t *norm_w(const wchar_t *in) {
    int n = (int)wcslen(in);
    wchar_t *k = NULL;
    int cap = NormalizeString(NormalizationKC, in, n, NULL, 0);
    for (int tries = 0; cap > 0 && tries < 4; tries++) {
        free(k);
        k = (wchar_t *)malloc(((size_t)cap + 1) * sizeof(wchar_t));
        if (!k) return NULL;
        int got = NormalizeString(NormalizationKC, in, n, k, cap);
        if (got > 0) { k[got] = 0; break; }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) { cap = 0; break; }
        cap = -got > cap ? -got : cap * 2;
    }
    if (cap <= 0 || !k) {   //  정규화 실패 → 그대로
        free(k);
        k = _wcsdup(in);
        if (!k) return NULL;
    }
    size_t len = wcslen(k);
    if (len) CharLowerBuffW(k, (DWORD)len);
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        wchar_t c = k[i];
        if (c >= 0x30A1 && c <= 0x30F6) c = (wchar_t)(c - 0x60);   //  가타카나 → 히라가나
        if (c == 0x30FC || c == 0x30FB || c == 0x309B || c == 0x309C) continue;   //  ー ・ ゛ ゜
        int keep = is_hira(c) || is_kanji(c) || is_hangul(c) || is_jamo(c) || (c >= 0xD800 && c <= 0xDFFF) || (c >= 0x30F7 && c <= 0x30FA)
                   || (c < 0x80 ? ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) : IsCharAlphaNumericW(c));
        if (keep) k[o++] = c;
    }
    k[o] = 0;
    return k;
}

// ── 발음 키 ──
//  토막 = 첫소리 갈래(g s j d n h b m r 또는 없음) + 모음(a i u e o ya yu yo wa wi we wo) + 받침 n
typedef struct { char c; char v[3]; int n, lb; } tok;   //  lb = 받침 ㄹ 이 만든 토막
typedef struct { tok *t; int len, cap; int bad; } toks;
static void tk_push(toks *s, char c, const char *v, int n) {
    if (s->len >= s->cap) { int nc = s->cap ? s->cap * 2 : 32; tok *t = (tok *)realloc(s->t, sizeof *t * (size_t)nc); if (!t) { s->bad = 1; return; } s->t = t; s->cap = nc; }
    tok *t = &s->t[s->len++];
    t->c = c;
    snprintf(t->v, sizeof t->v, "%s", v);
    t->n = n;
    t->lb = 0;
}
//  히라가나 0x3041~0x3096 — S=작은 모음 · Y=작은 や행 · Q=っ · N=ん
static const char *KT[] = {
    "Sa", "a", "Si", "i", "Su", "u", "Se", "e", "So", "o",
    "ga", "ga", "gi", "gi", "gu", "gu", "ge", "ge", "go", "go",
    "sa", "ja", "si", "ji", "su", "ju", "se", "je", "so", "jo",
    "da", "da", "ji", "ji", "Q", "ju", "ju", "de", "de", "do", "do",
    "na", "ni", "nu", "ne", "no",
    "ha", "ba", "ba", "hi", "bi", "bi", "hu", "bu", "bu", "he", "be", "be", "ho", "bo", "bo",
    "ma", "mi", "mu", "me", "mo",
    "Yya", "ya", "Yyu", "yu", "Yyo", "yo",
    "ra", "ri", "ru", "re", "ro",
    "Swa", "wa", "i", "e", "o", "N", "bu", "ga", "ge",
};
static void kana_tok(toks *s, wchar_t c, int *prev_ok) {
    const char *k = KT[c - 0x3041];
    tok *p = (*prev_ok && s->len) ? &s->t[s->len - 1] : NULL;
    if (k[0] == 'Q') return;
    if (k[0] == 'N') { if (p) p->n = 1; else tk_push(s, 0, "", 1); *prev_ok = 1; return; }
    if (k[0] == 'S' && p && !p->n) {
        const char *v = k + 1;
        if (v[0] == 'w') v = "a";   //  ゎ
        if (!p->c && !strcmp(p->v, "u")) {   //  ウァ ウィ ウェ ウォ
            if (strcmp(v, "u")) { char w[3] = { 'w', v[0], 0 }; snprintf(p->v, sizeof p->v, "%s", w); }
        } else {
            if (p->c == 'h' && !strcmp(p->v, "u")) p->c = 'b';   //  ファ = 파
            snprintf(p->v, sizeof p->v, "%s", v);
        }
        return;
    }
    if (k[0] == 'Y' && p && !p->n) { snprintf(p->v, sizeof p->v, "%s", k + 1); return; }
    if (k[0] == 'S' || k[0] == 'Y') { tk_push(s, 0, k + 1, 0); *prev_ok = 1; return; }
    char cc = strchr("gsjdnhbmr", k[0]) ? k[0] : 0;
    tk_push(s, cc, cc ? k + 1 : k, 0);
    *prev_ok = 1;
}
static void hangul_tok(toks *s, wchar_t c) {
    static const char LC[19] = { 'g', 'g', 'n', 'd', 'd', 'r', 'm', 'b', 'b', 's', 's', 0, 'j', 'j', 'j', 'g', 'd', 'b', 'h' };
    static const char *VV[21] = { "a", "a", "ya", "ya", "a", "e", "ya", "e", "o", "wa", "we", "we", "yo", "u", "wo", "we", "wi", "yu", "u", "i", "i" };
    int x = c - 0xAC00, L = x / 588, V = (x % 588) / 28, T = x % 28;
    char cc = LC[L];
    if (L == 10 && V == 18) cc = 'j';   //  쓰 = ツ
    const char *vv = VV[V];
    if (cc == 'd' && V == 18) vv = "o";   //  트·드 = ト·ド (울트라 · 빌드 · 실드)
    tk_push(s, cc, vv, T == 4 || T == 5 || T == 6 || T == 16 || T == 21);
    //  받침 ㄱ·ㅂ·ㄹ 은 한 토막 더 (넥서스=ネクサス · 업=アップ · 메탈=メタル) · ㅅ·ㄷ 등(ッ)은 뺀다
    if (T == 1 || T == 2 || T == 3 || T == 9 || T == 24) tk_push(s, 'g', "u", 0);
    else if (T == 17 || T == 18 || T == 26 || T == 11 || T == 14) tk_push(s, 'b', "u", 0);
    else if (T == 8 || T == 10 || T == 12 || T == 13 || T == 15) { tk_push(s, 'r', "u", 0); if (!s->bad) s->t[s->len - 1].lb = 1; }
}
//  strict = 다른 글자가 섞이면 실패 (검색어) · 아니면 그 자리에서 끊는다 (제목)
static char *key_w(const wchar_t *w, int strict) {
    toks s = { 0 };
    int prev_ok = 0;
    for (size_t i = 0; w[i]; i++) {
        wchar_t c = w[i];
        if (is_hira(c)) kana_tok(&s, c, &prev_ok);
        else if (is_hangul(c)) { hangul_tok(&s, c); prev_ok = 1; }
        else if (strict) { free(s.t); return NULL; }
        else { if (!s.len || s.t[s.len - 1].c != '#') tk_push(&s, '#', "", 0); prev_ok = 0; }
    }
    if (s.bad) { free(s.t); return NULL; }
    //  다듬기 (검색어·제목 똑같이): ㅈ 뒤 y 빼기(쥬=주) · 받침 ㄹ+ㄹ 은 하나(블레=ブレ · 블루=ブルー)
    //  · 장음(おう · えい · 같은 모음) 빼기 — 빠지는 모음의 받침 n 은 앞 토막으로 (레인저=レンジャー)
    int wn = 0;
    for (int i = 0; i < s.len; i++) {
        tok t = s.t[i];
        if (t.c == 'j' && t.v[0] == 'y') memmove(t.v, t.v + 1, strlen(t.v));
        if (t.lb && i + 1 < s.len && s.t[i + 1].c == 'r') continue;
        tok *prev = (wn && s.t[wn - 1].c != '#') ? &s.t[wn - 1] : NULL;
        if (!t.c && prev && !prev->n && t.v[0] && !t.v[1] && prev->v[0]) {
            char last = prev->v[strlen(prev->v) - 1];
            if ((t.v[0] == 'u' && (last == 'o' || last == 'u')) || (t.v[0] == 'i' && last == 'e') || t.v[0] == last) { if (t.n) prev->n = 1; continue; }
        }
        s.t[wn++] = t;
    }
    char *out = (char *)malloc((size_t)wn * 6 + 2);
    if (!out) { free(s.t); return NULL; }
    size_t o = 0;
    for (int i = 0; i < wn; i++) {
        const tok *t = &s.t[i];
        if (t->c) out[o++] = t->c;
        for (const char *v = t->v; *v; v++) out[o++] = *v;
        if (t->n) out[o++] = 'n';
        out[o++] = '.';
    }
    out[o] = 0;
    free(s.t);
    return out;
}

// ── 한국어 검색어에서 자주 쓰는 말 → 일본어 읽기 (한자어·영어 유래라 소리로는 안 맞는 것) ──
static const wchar_t *const ALIAS[][2] = {
    { L"울트라맨", L"うるとらまん" }, { L"울트라", L"うるとら" }, { L"가면", L"かめん" }, { L"전대", L"せんたい" },
    { L"슈퍼", L"すぱ" }, { L"히어로", L"ひろ" }, { L"메탈", L"めたる" }, { L"특촬", L"とくさつ" },
};
static wchar_t *alias_w(const wchar_t *w) {
    size_t cap = wcslen(w) * 4 + 1;
    wchar_t *o = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!o) return NULL;
    size_t n = 0;
    for (size_t i = 0; w[i];) {
        int hit = 0;
        for (size_t a = 0; a < sizeof ALIAS / sizeof ALIAS[0]; a++) {
            size_t al = wcslen(ALIAS[a][0]);
            if (!wcsncmp(w + i, ALIAS[a][0], al)) {
                size_t rl = wcslen(ALIAS[a][1]);
                if (n + rl + 1 > cap) break;
                wmemcpy(o + n, ALIAS[a][1], rl);
                n += rl;
                i += al;
                hit = 1;
                break;
            }
        }
        if (!hit) { if (n + 2 > cap) break; o[n++] = w[i++]; }
    }
    o[n] = 0;
    return o;
}

// ── Windows 일본어 읽기 분석기 (WinRT, combase.dll 에서 직접) ──
typedef void *RTSTR;
typedef struct { void *qi, *addref; ULONG (STDMETHODCALLTYPE *Release)(void *); void *iids, *cls, *trust;
                 HRESULT (STDMETHODCALLTYPE *GetWords)(void *, RTSTR, void **); } statics_vt;
typedef struct { void *qi, *addref; ULONG (STDMETHODCALLTYPE *Release)(void *); void *iids, *cls, *trust;
                 HRESULT (STDMETHODCALLTYPE *GetAt)(void *, UINT32, void **); HRESULT (STDMETHODCALLTYPE *get_Size)(void *, UINT32 *); } vview_vt;
typedef struct { void *qi, *addref; ULONG (STDMETHODCALLTYPE *Release)(void *); void *iids, *cls, *trust;
                 HRESULT (STDMETHODCALLTYPE *get_DisplayText)(void *, RTSTR *); HRESULT (STDMETHODCALLTYPE *get_YomiText)(void *, RTSTR *); } phon_vt;
typedef struct { void *vt; } comobj;
static HRESULT (WINAPI *pCreate)(LPCWSTR, UINT32, RTSTR *);
static HRESULT (WINAPI *pDelete)(RTSTR);
static LPCWSTR (WINAPI *pRaw)(RTSTR, UINT32 *);
static HRESULT (WINAPI *pFactory)(RTSTR, REFIID, void **);
static comobj *ANALYZER;
static int ANALYZER_TRIED;
static const GUID IID_STATICS = { 0x88ab9e90, 0x93de, 0x41b2, { 0xb4, 0xd5, 0x8e, 0xdb, 0x22, 0x7f, 0xd1, 0xc2 } };
static comobj *analyzer(void) {
    if (ANALYZER_TRIED) return ANALYZER;
    ANALYZER_TRIED = 1;
    HMODULE m = LoadLibraryW(L"combase.dll");
    if (!m) return NULL;
    pCreate = (HRESULT (WINAPI *)(LPCWSTR, UINT32, RTSTR *))(void *)GetProcAddress(m, "WindowsCreateString");
    pDelete = (HRESULT (WINAPI *)(RTSTR))(void *)GetProcAddress(m, "WindowsDeleteString");
    pRaw = (LPCWSTR (WINAPI *)(RTSTR, UINT32 *))(void *)GetProcAddress(m, "WindowsGetStringRawBuffer");
    pFactory = (HRESULT (WINAPI *)(RTSTR, REFIID, void **))(void *)GetProcAddress(m, "RoGetActivationFactory");
    if (!pCreate || !pDelete || !pRaw || !pFactory) return NULL;
    static const wchar_t CLS[] = L"Windows.Globalization.JapanesePhoneticAnalyzer";
    RTSTR h = NULL;
    if (FAILED(pCreate(CLS, (UINT32)wcslen(CLS), &h))) return NULL;
    void *f = NULL;
    HRESULT hr = pFactory(h, &IID_STATICS, &f);
    pDelete(h);
    if (FAILED(hr) || !f) return NULL;
    ANALYZER = (comobj *)f;
    return ANALYZER;
}
static char *win_reading(const char *s) {
    comobj *a = analyzer();
    if (!a) return NULL;
    wchar_t *w = utf8_to_w(s, strlen(s));
    if (!w) return NULL;
    size_t wl = wcslen(w);
    if (wl > 100) wl = 100;   //  분석기 입력 한도 100자
    RTSTR in = NULL;
    void *vec = NULL;
    char *out = NULL;
    if (SUCCEEDED(pCreate(w, (UINT32)wl, &in)) && SUCCEEDED(((statics_vt *)a->vt)->GetWords(a, in, &vec)) && vec) {
        vview_vt *vv = (vview_vt *)((comobj *)vec)->vt;
        UINT32 n = 0;
        vv->get_Size(vec, &n);
        size_t cap = wl * 4 + 16, len = 0;
        wchar_t *acc = (wchar_t *)malloc(cap * sizeof(wchar_t));
        for (UINT32 i = 0; acc && i < n; i++) {
            void *ph = NULL;
            if (FAILED(vv->GetAt(vec, i, &ph)) || !ph) continue;
            RTSTR y = NULL;
            if (SUCCEEDED(((phon_vt *)((comobj *)ph)->vt)->get_YomiText(ph, &y)) && y) {
                UINT32 yl = 0;
                LPCWSTR yr = pRaw(y, &yl);
                if (yr && len + yl + 1 <= cap) { wmemcpy(acc + len, yr, yl); len += yl; }
                pDelete(y);
            }
            ((phon_vt *)((comobj *)ph)->vt)->Release(ph);
        }
        if (acc) { acc[len] = 0; out = w_to_utf8(acc, len); free(acc); }
        vv->Release(vec);
    }
    if (in) pDelete(in);
    free(w);
    return out;
}
typedef struct { char *s, *r; } rcache;
static rcache *RC;
static int nRC;
const char *hks_reading(const char *s) {
    if (hks_read_hook) return hks_read_hook(s);
    for (int i = 0; i < nRC; i++) if (!strcmp(RC[i].s, s)) return RC[i].r;
    if (nRC >= 512) { for (int i = 0; i < nRC; i++) { free(RC[i].s); free(RC[i].r); } nRC = 0; }
    if (!RC) { RC = (rcache *)calloc(512, sizeof *RC); if (!RC) return NULL; }
    char *d = _strdup(s);
    if (!d) return NULL;
    RC[nRC].s = d;
    RC[nRC].r = win_reading(s);
    return RC[nRC++].r;
}

// ── 맞추기 ──
static wchar_t *norm_u8(const char *s) {
    wchar_t *w = utf8_to_w(s ? s : "", strlen(s ? s : ""));
    if (!w) return NULL;
    wchar_t *n = norm_w(w);
    free(w);
    return n;
}
static int has_kanji(const wchar_t *w) { for (; *w; w++) if (is_kanji(*w)) return 1; return 0; }
typedef struct { wchar_t *t, *rt; char *k, *rk; } hayx;   //  글 · 읽기 글 · 키 · 읽기 키
static int word_hit(const wchar_t *qw, const char *qk, const hayx *h, int nh) {
    char *pat = NULL;
    if (qk) { size_t l = strlen(qk); pat = (char *)malloc(l + 2); if (pat) { pat[0] = '.'; memcpy(pat + 1, qk, l + 1); } }
    int hit = 0;
    for (int i = 0; i < nh && !hit; i++) {
        if ((h[i].t && wcsstr(h[i].t, qw)) || (h[i].rt && wcsstr(h[i].rt, qw))) hit = 1;
        else if (pat) {
            const char *ks[2] = { h[i].k, h[i].rk };
            for (int j = 0; j < 2 && !hit; j++) {
                if (!ks[j]) continue;
                size_t l = strlen(ks[j]);
                char *b = (char *)malloc(l + 2);
                if (!b) continue;
                b[0] = '.';
                memcpy(b + 1, ks[j], l + 1);
                hit = strstr(b, pat) != NULL;
                free(b);
            }
        }
    }
    free(pat);
    return hit;
}
static char *word_key(const wchar_t *qw) {
    size_t l = wcslen(qw);
    wchar_t *c = _wcsdup(qw);
    if (!c) return NULL;
    while (l && is_jamo(c[l - 1])) c[--l] = 0;   //  치고 있는 낱자는 빼고 (히비ㅋ → 히비)
    char *k = NULL;
    if (l) { wchar_t *a = alias_w(c); if (a) { k = key_w(a, 1); free(a); } }
    free(c);
    return k;
}
int hks_match(const char *query, const char *const *hay, int nhay) {
    wchar_t *q = utf8_to_w(query ? query : "", strlen(query ? query : ""));
    if (!q) return 1;
    hayx *h = (hayx *)calloc((size_t)(nhay > 0 ? nhay : 1), sizeof *h);
    int ok = 1, words = 0;
    for (wchar_t *p = q; *p && ok;) {   //  띄어쓰기(반각·전각·탭)로 낱말 나누기
        while (*p == L' ' || *p == L'\t' || *p == 0x3000) p++;
        if (!*p) break;
        wchar_t *e = p;
        while (*e && *e != L' ' && *e != L'\t' && *e != 0x3000) e++;
        wchar_t sv = *e;
        *e = 0;
        wchar_t *qw = norm_w(p);
        *e = sv;
        p = e;
        if (!qw || !*qw) { free(qw); continue; }
        if (!words++ && h) {   //  첫 낱말 때 한 번만 준비
            for (int i = 0; i < nhay; i++) {
                h[i].t = norm_u8(hay[i]);
                if (!h[i].t) continue;
                h[i].k = key_w(h[i].t, 0);
                if (has_kanji(h[i].t)) {
                    const char *r = hks_reading(hay[i]);
                    if (r) { h[i].rt = norm_u8(r); if (h[i].rt) h[i].rk = key_w(h[i].rt, 0); }
                }
            }
        }
        size_t ql = wcslen(qw);
        int partial = ql && is_jamo(qw[ql - 1]);
        char *qk = word_key(qw);
        int hit = h ? word_hit(qw, qk, h, nhay) : 1;
        if (!hit && partial) {   //  낱자를 치는 중이면 낱자 빼고 글 그대로도
            qw[ql - 1] = 0;
            hit = !*qw || word_hit(qw, NULL, h, nhay);
        }
        ok = hit;
        free(qk);
        free(qw);
    }
    for (int i = 0; h && i < nhay; i++) { free(h[i].t); free(h[i].rt); free(h[i].k); free(h[i].rk); }
    free(h);
    free(q);
    return ok;
}

char *hks_debug_key(const char *s) {
    wchar_t *n = norm_u8(s);
    if (!n) return NULL;
    wchar_t *a = alias_w(n);
    char *k = a ? key_w(a, 0) : NULL;
    free(a);
    free(n);
    return k;
}
char *hks_debug_norm(const char *s) {
    wchar_t *n = norm_u8(s);
    if (!n) return NULL;
    char *u = w_to_utf8(n, wcslen(n));
    free(n);
    return u;
}
