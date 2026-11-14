#include "hooks.h"
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "binge.h"
#include "settings.h"
#include "../base/sbuf.h"
#include "../base/utf.h"
#include "../core/js.h"

#define TITLE_MAX 100
#define NAME_MAX 40
#define HOOKS_MAX 100
#define CHARS_MAX 50

static char *dupn(const char *s, size_t n) { char *d = (char *)malloc(n + 1); if (d) { memcpy(d, s, n); d[n] = 0; } return d; }
static char *dups(const char *s) { return dupn(s ? s : "", strlen(s ? s : "")); }
void (*hk_random)(unsigned char *out, int n) = NULL;   //  시험용 (캐릭터 id)
static void rand_hex(char *out, int bytes) {
    unsigned char b[16];
    if (hk_random) hk_random(b, bytes); else BCryptGenRandom(NULL, b, (ULONG)bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (int i = 0; i < bytes; i++) snprintf(out + i * 2, 3, "%02x", b[i]);
}
static int64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (int64_t)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000ULL - 11644473600000ULL);
}

// ── 비밀 파일 (작품별 웹훅) ──
typedef struct { char *title, *url, *name; } raw_hook;

static int path_of(wchar_t *out, const wchar_t *sub) {
    wchar_t ad[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad) != S_OK) return 0;
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls", ad, sub);
    out[MAX_PATH - 1] = 0;
    return 1;
}
//  쓸 곳: 시험이면 시험 파일, 아니면 알파 전용 사본
static int write_path(wchar_t *out) {
    DWORD tn = GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SECRETS", out, MAX_PATH);
    if (tn && tn < MAX_PATH) return 1;
    return path_of(out, L"toku-rpc-native\\secrets.json");
}

//  titleHooksRaw — 형식이 맞는 것만 (작품명·주소 둘 다)
static int parse_hooks(const jval *sec, raw_hook **out) {
    *out = NULL;
    const jval *list = json_get(sec, "titleWebhooks");
    int n = 0;
    raw_hook *a = NULL;
    for (size_t i = 0; list && list->t == J_ARR && i < list->count; i++) {
        const jval *h = list->items[i];
        const jval *tv = json_get(h, "title"), *uv = json_get(h, "url"), *nv = json_get(h, "name");
        if (!tv || tv->t != J_STR || !uv || uv->t != J_STR) continue;
        char *t = binge_clean_ws(tv->s, tv->slen, TITLE_MAX);
        size_t s0, s1;
        js_trim(uv->s, uv->slen, &s0, &s1);
        if (!t || !*t || !binge_hook_url_ok(uv->s + s0, s1 - s0)) { free(t); continue; }
        raw_hook *na = (raw_hook *)realloc(a, sizeof *a * (size_t)(n + 1));
        if (!na) { free(t); break; }
        a = na;
        a[n].title = t;
        a[n].url = dupn(uv->s + s0, s1 - s0);
        a[n].name = (nv && nv->t == J_STR) ? binge_clean_ws(nv->s, nv->slen, NAME_MAX) : dups("");
        n++;
    }
    *out = a;
    return n;
}
//  고치는 쪽(추가·이름·지우기)은 늘 파일에서 새로
static int raw_hooks(raw_hook **out) {
    jval *sec = binge_read_secrets();
    int n = parse_hooks(sec, out);
    json_free(sec);
    return n;
}
static void raw_free(raw_hook *a, int n) { for (int i = 0; i < n; i++) { free(a[i].title); free(a[i].url); free(a[i].name); } free(a); }

// ── 읽기 캐시 (11/14 렉): 웹훅 탭을 한 번 그릴 때 secrets.json 을 폴더 수×2 번 읽고 풀던 것 → 파일이 바뀔 때만 다시 읽는다
//  바뀜 확인 = 읽은 파일 경로·수정 시각·크기. 그리는 동안(hk_cache_hold)은 처음 한 번만 확인. 이 파일에 쓰면(save_hooks) 바로 버린다
static SRWLOCK RCL = SRWLOCK_INIT;
static raw_hook *RCA;
static int RCN = -1, RC_DEF, RC_HOLD, RC_HOLD_OK;
static wchar_t RC_P[MAX_PATH];
static FILETIME RC_FT;
static ULONGLONG RC_SZ;
//  binge_read_secrets 와 같은 순서로 찾은 파일의 도장 (없으면 p = "")
static void secrets_stamp(wchar_t *p, FILETIME *ft, ULONGLONG *sz) {
    wchar_t ad[MAX_PATH];
    p[0] = 0;
    memset(ft, 0, sizeof *ft);
    *sz = 0;
    DWORD tn = GetEnvironmentVariableW(L"TOKU_NATIVE_TEST_SECRETS", p, MAX_PATH);
    int test = tn && tn < MAX_PATH;
    if (!test && SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad) != S_OK) { p[0] = 0; return; }
    const wchar_t *subs[2] = { L"toku-rpc-native\\secrets.json", L"toku-rpc\\secrets.json" };
    for (int i = 0; i < 2; i++) {
        if (test) { if (i) break; }
        else { _snwprintf(p, MAX_PATH - 1, L"%ls\\%ls", ad, subs[i]); p[MAX_PATH - 1] = 0; }
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) continue;
        *ft = a.ftLastWriteTime;
        *sz = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        return;
    }
    p[0] = 0;
}
static void rc_refresh(void) {   //  RCL 잡고 부른다
    if (RCN >= 0 && RC_HOLD && RC_HOLD_OK) return;
    wchar_t p[MAX_PATH];
    FILETIME ft;
    ULONGLONG sz;
    secrets_stamp(p, &ft, &sz);
    if (RC_HOLD) RC_HOLD_OK = 1;
    if (RCN >= 0 && !wcscmp(p, RC_P) && !memcmp(&ft, &RC_FT, sizeof ft) && sz == RC_SZ) return;
    if (RCN > 0) raw_free(RCA, RCN);
    RCA = NULL;
    jval *sec = binge_read_secrets();
    RCN = parse_hooks(sec, &RCA);
    RC_DEF = js_truthy(json_get(sec, "bingeWebhookUrl"));
    json_free(sec);
    wcscpy(RC_P, p);
    RC_FT = ft;
    RC_SZ = sz;
}
static void rc_drop(void) {
    AcquireSRWLockExclusive(&RCL);
    if (RCN > 0) raw_free(RCA, RCN);
    RCA = NULL;
    RCN = -1;
    ReleaseSRWLockExclusive(&RCL);
}
void hk_cache_hold(int on) {
    AcquireSRWLockExclusive(&RCL);
    RC_HOLD = on;
    RC_HOLD_OK = 0;
    ReleaseSRWLockExclusive(&RCL);
}

//  작품명+주소로 만든 id (sha256 앞 12자) — 순서가 바뀌어도 지울 대상을 정확히 가리킨다
static void hook_id(const raw_hook *h, char out[16]) {
    sbuf b; sb_init(&b, 0);
    sb_printf(&b, "%s|%s", h->title, h->url);
    unsigned char d[32];
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)b.p, (ULONG)b.len, d, 32);
    for (int i = 0; i < 6; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
    sb_free(&b);
}
//  이름표: 웹훅 번호 끝 6자리만 ('…' + 6)
static void mask(const char *url, char out[16]) {
    const char *p = strstr(url, "/webhooks/");
    strcpy(out, "\xE2\x80\xA6");
    if (!p) return;
    p += 10;
    const char *d = p;
    while (*p >= '0' && *p <= '9') p++;
    if (p == d || *p != '/') return;
    size_t n = (size_t)(p - d);
    const char *s = n > 6 ? p - 6 : d;
    snprintf(out + 3, 12, "%.*s", (int)(p - s), s);
}

//  saveTitleHooks — 다른 값(애플리케이션 ID·기본 웹훅)은 그대로 두고 titleWebhooks 만 바꿔 쓴다
static const char *save_hooks(raw_hook *a, int n) {
    wchar_t dst[MAX_PATH], src[MAX_PATH];
    if (!write_path(dst)) return "badfile";
    //  바탕: 알파 사본(또는 시험 파일)이 있으면 그것, 없으면 지금 앱 것을 복사해서
    wcscpy(src, dst);
    if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES && !path_of(src, L"toku-rpc\\secrets.json")) return "badfile";
    jval *data = NULL;
    if (GetFileAttributesW(src) != INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            if (GetFileSizeEx(h, &sz) && sz.QuadPart < 16 * 1024 * 1024) {
                char *buf = (char *)malloc((size_t)sz.QuadPart + 1);
                DWORD got = 0;
                if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL)) {
                    size_t off = (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
                    data = json_parse(buf + off, got - off, NULL);
                }
                free(buf);
            }
            CloseHandle(h);
        }
        //  읽을 수 없는(깨진) 파일은 덮어쓰지 않는다 — 안에 든 ID·웹훅을 잃는다
        if (!data || data->t != J_OBJ) { json_free(data); return "badfile"; }
    } else data = json_new(J_OBJ);
    jval *arr = json_new(J_ARR);
    for (int i = 0; i < n; i++) {
        jval *o = json_new(J_OBJ);
        json_obj_set(o, "title", json_new_str(a[i].title));
        json_obj_set(o, "url", json_new_str(a[i].url));
        if (a[i].name && *a[i].name) json_obj_set(o, "name", json_new_str(a[i].name));
        json_arr_push(arr, o);
    }
    json_obj_set(data, "titleWebhooks", arr);
    char *s = json_stringify(data);
    json_free(data);
    if (!s) return "badfile";
    //  폴더를 만들고 임시 파일에 쓴 뒤 바꿔치기
    wchar_t dir[MAX_PATH], tmp[MAX_PATH + 8];
    wcscpy(dir, dst);
    wchar_t *sl = wcsrchr(dir, L'\\');
    if (sl) { *sl = 0; CreateDirectoryW(dir, NULL); }
    _snwprintf(tmp, MAX_PATH + 7, L"%ls.tmp", dst);
    tmp[MAX_PATH + 7] = 0;
    int ok = 0;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w1 = 0, w2 = 0, len = (DWORD)strlen(s);
        ok = WriteFile(f, s, len, &w1, NULL) && w1 == len && WriteFile(f, "\n", 1, &w2, NULL) && FlushFileBuffers(f);
        CloseHandle(f);
        if (ok) ok = MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) DeleteFileW(tmp);
    }
    free(s);
    rc_drop();
    return ok ? NULL : "badfile";
}

int hk_list(hk_item **out) {
    AcquireSRWLockExclusive(&RCL);
    rc_refresh();
    const raw_hook *a = RCA;
    int n = RCN > 0 ? RCN : 0;
    hk_item *l = n ? (hk_item *)calloc((size_t)n, sizeof *l) : NULL;
    for (int i = 0; l && i < n; i++) {
        hook_id(&a[i], l[i].id);
        l[i].title = dups(a[i].title);
        l[i].name = dups(a[i].name);
        mask(a[i].url, l[i].label);
    }
    ReleaseSRWLockExclusive(&RCL);
    *out = l;
    return l ? n : 0;
}
void hk_list_free(hk_item *l, int n) { for (int i = 0; i < n; i++) { free(l[i].title); free(l[i].name); } free(l); }

int hk_has_default(void) {
    char env[64];
    DWORD e = GetEnvironmentVariableA("TOKU_BINGE_WEBHOOK", env, sizeof env);
    if (e) return 1;
    AcquireSRWLockExclusive(&RCL);
    rc_refresh();
    int r = RC_DEF;
    ReleaseSRWLockExclusive(&RCL);
    return r;
}

const char *hk_add(const jval *title, const jval *url, const jval *name) {
    char *t = (title && title->t == J_STR) ? binge_clean_ws(title->s, title->slen, TITLE_MAX) : dups("");
    char *u = NULL;
    if (url && url->t == J_STR) { size_t a, b; js_trim(url->s, url->slen, &a, &b); if (binge_hook_url_ok(url->s + a, b - a)) u = dupn(url->s + a, b - a); }
    const char *err = NULL;
    if (!t || !*t) err = "title";
    else if (!u) err = "url";
    raw_hook *a = NULL;
    int n = 0;
    if (!err) {
        n = raw_hooks(&a);
        for (int i = 0; i < n && !err; i++) if (!strcmp(a[i].url, u) && !strcmp(a[i].title, t)) err = "dup";
        if (!err && n >= HOOKS_MAX) err = "full";
    }
    if (!err) {
        raw_hook *na = (raw_hook *)realloc(a, sizeof *a * (size_t)(n + 1));
        if (!na) err = "badfile";
        else {
            a = na;
            a[n].title = t; a[n].url = u; t = NULL; u = NULL;
            a[n].name = (name && name->t == J_STR) ? binge_clean_ws(name->s, name->slen, NAME_MAX) : dups("");
            n++;
            err = save_hooks(a, n);
            if (!err) {
                char *lt = (title && title->t == J_STR) ? NULL : NULL;
                size_t s0, s1;
                js_trim(title->s, title->slen, &s0, &s1);
                sbuf sb; sb_init(&sb, 0);
                js_slice16(&sb, title->s + s0, s1 - s0, 40);
                char m[300];
                snprintf(m, sizeof m, "[Binge] 작품 웹훅 추가: %s", sb.p ? sb.p : "");
                app_log(0, m);
                sb_free(&sb);
                (void)lt;
            }
        }
    }
    raw_free(a, n);
    free(t);
    free(u);
    return err;
}

static int find_hook(raw_hook *a, int n, const char *id) {
    for (int i = 0; i < n; i++) { char h[16]; hook_id(&a[i], h); if (!strcmp(h, id)) return i; }
    return -1;
}
const char *hk_rename(const char *id, const jval *name) {
    raw_hook *a;
    int n = raw_hooks(&a);
    int i = find_hook(a, n, id);
    const char *err = "missing";
    if (i >= 0) {
        free(a[i].name);
        a[i].name = (name && name->t == J_STR) ? binge_clean_ws(name->s, name->slen, NAME_MAX) : dups("");
        err = save_hooks(a, n);
    }
    raw_free(a, n);
    return err;
}
const char *hk_remove(const char *id) {
    raw_hook *a;
    int n = raw_hooks(&a);
    int keep = 0;
    const char *err = NULL;
    for (int i = 0; i < n; i++) {
        char h[16];
        hook_id(&a[i], h);
        if (!strcmp(h, id)) { free(a[i].title); free(a[i].url); free(a[i].name); continue; }
        a[keep++] = a[i];
    }
    if (keep == n) err = "missing";
    else { err = save_hooks(a, keep); if (!err) app_log(0, "[Binge] 작품 웹훅 삭제"); }
    raw_free(a, keep);
    return err;
}
char *hk_url(const char *id) {
    raw_hook *a;
    int n = raw_hooks(&a);
    int i = find_hook(a, n, id);
    char *u = i >= 0 ? dups(a[i].url) : NULL;
    raw_free(a, n);
    return u;
}
int hk_has_title(const char *title) {
    AcquireSRWLockExclusive(&RCL);
    rc_refresh();
    int r = 0;
    for (int i = 0; i < RCN && !r; i++) r = !strcmp(RCA[i].title, title);
    ReleaseSRWLockExclusive(&RCL);
    return r;
}
char *hk_test_url_for_char(const char *title) {
    raw_hook *a;
    int n = raw_hooks(&a), m = 0, idx[HOOKS_MAX + 1];
    for (int i = 0; i < n && m <= HOOKS_MAX; i++) if (!strcmp(a[i].title, title)) idx[m++] = i;
    char *u = NULL;
    if (m) {
        unsigned int r = 0;
        BCryptGenRandom(NULL, (PUCHAR)&r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        u = dups(a[idx[(int)((r / 4294967296.0) * m)]].url);
    } else {
        char env[2048];
        DWORD e = GetEnvironmentVariableA("TOKU_BINGE_WEBHOOK", env, sizeof env);
        if (e && e < sizeof env) u = dups(env);
        else { jval *sec = binge_read_secrets(); u = dups(json_get_str(sec, "bingeWebhookUrl", "")); json_free(sec); }
    }
    raw_free(a, n);
    return u;
}

// ── 폴더 (hookFolders: 아이콘·캐릭터) ──
//  updateHookFolder: 폴더 하나를 고치고, 빈 아이콘·빈 캐릭터 목록은 빼고, 다 비면 폴더도 뺀다
static void obj_del(jval *o, const char *key) {
    if (!o || o->t != J_OBJ) return;
    size_t kl = strlen(key);
    for (size_t i = 0; i < o->count; i++) {
        if (o->klens[i] != kl || memcmp(o->keys[i], key, kl)) continue;
        json_free(o->items[i]);
        free(o->keys[i]);
        for (size_t k = i + 1; k < o->count; k++) { o->items[k - 1] = o->items[k]; o->keys[k - 1] = o->keys[k]; o->klens[k - 1] = o->klens[k]; }
        o->count--;
        return;
    }
}
static jval *copy(const jval *v) {
    char *s = v ? json_stringify(v) : NULL;
    jval *r = s ? json_parse(s, strlen(s), NULL) : NULL;
    free(s);
    return r;
}
//  Object.assign({}, v): 객체는 복사, 문자열·배열은 "0","1",… 키, 그 밖(숫자·참거짓·null)은 빈 객체
//  (문자열은 UTF-16 한 칸씩 — 이모지 반쪽은 U+FFFD 로. 손상된 설정에서만 생기는 경우)
static jval *assign_obj(const jval *v) {
    if (v && v->t == J_OBJ) return copy(v);
    jval *o = json_new(J_OBJ);
    char k[24];
    if (v && v->t == J_ARR) for (size_t i = 0; i < v->count; i++) { snprintf(k, sizeof k, "%u", (unsigned)i); json_obj_set(o, k, copy(v->items[i])); }
    if (v && v->t == J_STR) {
        wchar_t *w = utf8_to_w(v->s, v->slen);
        for (size_t i = 0; w && w[i]; i++) {
            wchar_t one[2] = { (w[i] >= 0xD800 && w[i] <= 0xDFFF) ? 0xFFFD : w[i], 0 };
            char *u = w_to_utf8(one, 1);
            snprintf(k, sizeof k, "%u", (unsigned)i);
            json_obj_set(o, k, json_new_str(u ? u : ""));
            free(u);
        }
        free(w);
    }
    return o;
}
typedef void (*folder_fn)(jval *f, void *ctx);
static void update_folder(const char *title, folder_fn fn, void *ctx) {
    const jval *cur = settings_get("hookFolders", NULL);
    jval *all = assign_obj(js_truthy(cur) ? cur : NULL);   //  Object.assign({}, store.get('hookFolders') || {})
    const jval *f0 = json_get(all, title);
    jval *f = assign_obj(js_truthy(f0) ? f0 : NULL);       //  Object.assign({}, all[title] || {})
    fn(f, ctx);
    if (!js_truthy(json_get(f, "icon"))) obj_del(f, "icon");
    const jval *ch = json_get(f, "characters");
    if (!ch || ch->t != J_ARR || !ch->count) obj_del(f, "characters");
    if (f->count) json_obj_set(all, title, f); else { json_free(f); obj_del(all, title); }
    settings_set("hookFolders", NULL, all);
    settings_save();
}

const char *hk_icon(const char *title) {
    if (!hk_has_title(title)) return NULL;
    const char *ic = json_get_str(settings_get("hookFolders", title), "icon", NULL);
    return (ic && !strncmp(ic, "data:image/png;base64,", 22)) ? ic : NULL;
}
static void set_icon_fn(jval *f, void *ctx) { json_obj_set(f, "icon", json_new_str((const char *)ctx)); }
const char *hk_set_icon(const char *title, const char *png) {
    if (!hk_has_title(title)) return "missing";
    update_folder(title, set_icon_fn, (void *)png);
    return NULL;
}
void hk_clear_icon(const char *title) { update_folder(title, set_icon_fn, (void *)""); }

//  hookChars: id·name 이 문자열인 것만
static const jval *chars_raw(const char *title) {
    const jval *c = json_get(settings_get("hookFolders", title), "characters");
    return (c && c->t == J_ARR) ? c : NULL;
}
static int char_ok(const jval *c) {
    const jval *i = json_get(c, "id"), *n = json_get(c, "name");
    return c && c->t == J_OBJ && i && i->t == J_STR && n && n->t == J_STR;
}
int hk_chars(const char *title, hk_char **out) {
    *out = NULL;
    if (!hk_has_title(title)) return 0;
    const jval *a = chars_raw(title);
    int n = 0;
    hk_char *l = a ? (hk_char *)calloc(a->count + 1, sizeof *l) : NULL;
    for (size_t i = 0; l && i < a->count; i++) {
        const jval *c = a->items[i];
        if (!char_ok(c)) continue;
        l[n].id = dups(json_get_str(c, "id", ""));
        l[n].name = dups(json_get_str(c, "name", ""));
        l[n].avatar = dups(json_get_str(c, "avatar", ""));
        double u = js_to_number(json_get(c, "avatarUntil"));
        l[n].until = (u == u && u) ? u : 0;   //  Number(x) || 0
        const jval *off = json_get(c, "off");
        l[n].off = off && off->t == J_BOOL && off->b;
        n++;
    }
    *out = l;
    return n;
}
void hk_chars_free(hk_char *l, int n) { for (int i = 0; i < n; i++) { free(l[i].id); free(l[i].name); free(l[i].avatar); } free(l); }
static int has_char(const char *title, const char *id) {
    const jval *a = chars_raw(title);
    for (size_t i = 0; a && i < a->count; i++) if (char_ok(a->items[i]) && !strcmp(json_get_str(a->items[i], "id", ""), id)) return 1;
    return 0;
}
static int bad_name(const char *s) { for (const char *p = s; *p; p++) if (!_strnicmp(p, "discord", 7) || !_strnicmp(p, "clyde", 5)) return 1; return 0; }
static const char *name_err(const char *n) { return !*n ? "charname" : bad_name(n) ? "charbad" : NULL; }

typedef struct { const char *id; char *name; jval *add; int on; const char *url; double until; int changed; const char *err; } cctx;
static void add_fn(jval *f, void *p) {
    cctx *x = (cctx *)p;
    const jval *cur = json_get(f, "characters");
    jval *a = (cur && cur->t == J_ARR) ? copy(cur) : json_new(J_ARR);
    json_arr_push(a, x->add);
    x->add = NULL;
    json_obj_set(f, "characters", a);
}
const char *hk_char_add(const char *title, const jval *name, char id_out[16]) {
    id_out[0] = 0;
    if (!hk_has_title(title)) return "missing";
    char *n = (name && name->t == J_STR) ? binge_clean_ws(name->s, name->slen, 80) : dups("");
    const char *e = name_err(n);
    if (e) { free(n); return e; }
    hk_char *l;
    int cnt = hk_chars(title, &l);
    hk_chars_free(l, cnt);
    if (cnt >= CHARS_MAX) { free(n); return "charfull"; }
    char id[16];
    rand_hex(id, 6);   //  지금 앱처럼 id 를 먼저 뽑는다 (아래 예외 경우에도 무작위 순번이 같게)
    {   //  (f.characters || []).concat — 배열이 아닌 값이 있으면 지금 앱은 예외 (아무것도 안 바뀜)
        const jval *raw = json_get(settings_get("hookFolders", title), "characters");
        if (js_truthy(raw) && raw->t != J_ARR) { free(n); return "badfile"; }
    }
    jval *c = json_new(J_OBJ);
    json_obj_set(c, "id", json_new_str(id));
    json_obj_set(c, "name", json_new_str(n));
    json_obj_set(c, "avatar", json_new_str(""));
    json_obj_set(c, "avatarUntil", json_new_num(0));
    cctx x = { id, NULL, c, 0, NULL, 0, 0, NULL };
    update_folder(title, add_fn, &x);
    json_free(x.add);
    strcpy(id_out, id);
    sbuf t1, t2; sb_init(&t1, 0); sb_init(&t2, 0);
    js_slice16(&t1, title, strlen(title), 30);
    js_slice16(&t2, n, strlen(n), 30);
    char m[400];
    snprintf(m, sizeof m, "[Binge] 캐릭터 추가: %s · %s", t1.p ? t1.p : "", t2.p ? t2.p : "");
    app_log(0, m);
    sb_free(&t1); sb_free(&t2);
    free(n);
    return NULL;
}

//  캐릭터 하나 고치기 (updateHookChar) — fn 이 오류를 내면 그 캐릭터는 그대로
typedef const char *(*char_fn)(jval *c, cctx *x);
static char_fn CUR_FN;
static void each_fn(jval *f, void *p) {
    cctx *x = (cctx *)p;
    jval *a = (jval *)json_get(f, "characters");
    for (size_t i = 0; a && a->t == J_ARR && i < a->count; i++) {
        jval *c = a->items[i];
        if (!c || c->t != J_OBJ || strcmp(json_get_str(c, "id", ""), x->id)) continue;
        jval *n = copy(c);
        const char *e = CUR_FN(n, x);
        if (e) { x->err = e; json_free(n); }
        else { json_free(a->items[i]); a->items[i] = n; }
    }
}
//  지금 앱은 목록에 null 이 끼어 있으면 c.id 에서 예외가 나 아무것도 안 바뀐다 (창엔 badfile)
static int has_null(const char *title) {
    const jval *a = chars_raw(title);
    for (size_t i = 0; a && i < a->count; i++) if (!a->items[i] || a->items[i]->t == J_NULL) return 1;
    return 0;
}
static const char *update_char(const char *title, const char *id, char_fn fn, cctx *x) {
    if (!has_char(title, id)) return "missing";
    if (has_null(title)) return "badfile";
    x->id = id;
    CUR_FN = fn;
    update_folder(title, each_fn, x);
    return x->err;
}
static const char *rename_fn(jval *c, cctx *x) { const char *e = name_err(x->name); if (e) return e; json_obj_set(c, "name", json_new_str(x->name)); return NULL; }
const char *hk_char_rename(const char *title, const char *id, const jval *name) {
    cctx x;
    memset(&x, 0, sizeof x);
    x.name = (name && name->t == J_STR) ? binge_clean_ws(name->s, name->slen, 80) : dups("");
    const char *e = update_char(title, id, rename_fn, &x);
    free(x.name);
    return e;
}
static void remove_fn(jval *f, void *p) {
    cctx *x = (cctx *)p;
    jval *a = (jval *)json_get(f, "characters");
    if (!a || a->t != J_ARR) return;
    size_t k = 0;
    for (size_t i = 0; i < a->count; i++) {
        jval *c = a->items[i];
        if (c && c->t == J_OBJ && !strcmp(json_get_str(c, "id", ""), x->id)) { json_free(c); continue; }
        a->items[k++] = c;
    }
    a->count = k;
}
const char *hk_char_remove(const char *title, const char *id) {
    if (!has_char(title, id)) return "missing";
    if (has_null(title)) return "badfile";
    cctx x;
    memset(&x, 0, sizeof x);
    x.id = id;
    update_folder(title, remove_fn, &x);
    hk_avatar_orig_drop(id);
    return NULL;
}
static const char *toggle_fn(jval *c, cctx *x) { if (x->on) obj_del(c, "off"); else json_obj_set(c, "off", json_new_bool(1)); return NULL; }
const char *hk_char_toggle(const char *title, const char *id, int on) {
    cctx x;
    memset(&x, 0, sizeof x);
    x.on = on;
    const char *e = update_char(title, id, toggle_fn, &x);
    if (!e) {
        hk_char *l;
        int n = hk_chars(title, &l), act = 0;
        for (int i = 0; i < n; i++) act += !l[i].off;
        hk_chars_free(l, n);
        sbuf t; sb_init(&t, 0);
        js_slice16(&t, title, strlen(title), 30);
        char m[300];
        snprintf(m, sizeof m, "[Binge] 캐릭터 %s: %s · 켜진 캐릭터 %d/%d", on ? "켬" : "끔", t.p ? t.p : "", act, n);
        app_log(0, m);
        sb_free(&t);
    }
    return e;
}
//  캐릭터 한꺼번에 (2026-11-10 유빈 요청) — mode 0 모두 끄기 · 1 모두 켜기 · 2 무작위
//  무작위는 캐릭터마다 따로 동전 던지기(시스템 난수) — 몇 명이 켜질지 정해져 있지 않고, 다 켜지거나 다 꺼질 수도 있다
typedef struct { int mode, on, n; } allctx;
static void all_fn(jval *f, void *p) {
    allctx *x = (allctx *)p;
    jval *a = (jval *)json_get(f, "characters");
    if (!a || a->t != J_ARR) return;
    for (size_t i = 0; i < a->count; i++) {
        jval *c = a->items[i];
        if (!c || c->t != J_OBJ) continue;
        const jval *id = json_get(c, "id"), *nm = json_get(c, "name");
        if (!id || id->t != J_STR || !nm || nm->t != J_STR) continue;   //  hookChars 가 보여 주는 캐릭터만
        int want = x->mode;
        if (x->mode == 2) {
            unsigned char b = 0;
            if (hk_random) hk_random(&b, 1); else BCryptGenRandom(NULL, &b, 1, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            want = b & 1;
        }
        if (want) obj_del(c, "off"); else json_obj_set(c, "off", json_new_bool(1));
        x->n++;
        x->on += want;
    }
}
const char *hk_char_set_all(const char *title, int mode, int *on, int *n) {
    if (on) *on = 0;
    if (n) *n = 0;
    if (mode < 0 || mode > 2) return "missing";
    hk_char *l;
    int cnt = hk_chars(title, &l);
    hk_chars_free(l, cnt);
    if (!cnt) return "missing";
    if (has_null(title)) return "badfile";
    allctx x = { mode, 0, 0 };
    update_folder(title, all_fn, &x);
    sbuf t; sb_init(&t, 0);
    js_slice16(&t, title, strlen(title), 30);
    char m[300];
    snprintf(m, sizeof m, "[Binge] 캐릭터 %s: %s · 켜진 캐릭터 %d/%d", mode == 2 ? "무작위" : mode ? "모두 켬" : "모두 끔", t.p ? t.p : "", x.on, x.n);
    app_log(0, m);
    sb_free(&t);
    if (on) *on = x.on;
    if (n) *n = x.n;
    return NULL;
}
static int avatar_url_ok(const char *s, size_t n) {
    if (n < 9 || n > 2000 || strncmp(s, "https://", 8)) return 0;
    for (size_t i = 8; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0) return 0;
        if (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) return 0;
        if ((c == 0xE2 || c == 0xE3 || c == 0xE1 || c == 0xEF) && i + 2 < n) {   //  유니코드 공백 (\s)
            unsigned char b = (unsigned char)s[i + 1], d = (unsigned char)s[i + 2];
            if ((c == 0xE2 && b == 0x80 && (d <= 0x8A || d == 0xA8 || d == 0xA9 || d == 0xAF)) || (c == 0xE2 && b == 0x81 && d == 0x9F) ||
                (c == 0xE3 && b == 0x80 && d == 0x80) || (c == 0xE1 && b == 0x9A && d == 0x80) || (c == 0xEF && b == 0xBB && d == 0xBF)) return 0;
        }
    }
    //  ⚠ JS 는 UTF-16 길이 ≤ 2000 — 위의 n 은 바이트라 한 번 더 본다
    return utf16_len(s, n) <= 2000;
}
static const char *avurl_fn(jval *c, cctx *x) {
    if (!strcmp(json_get_str(c, "avatar", "\x01"), x->url)) return NULL;   //  같으면 그대로
    x->changed = 1;
    json_obj_set(c, "avatar", json_new_str(x->url));
    json_obj_set(c, "avatarUntil", json_new_num(0));
    return NULL;
}
const char *hk_char_avatar_url(const char *title, const char *id, const jval *url) {
    char *u = dups("");
    if (url && url->t == J_STR) { size_t a, b; js_trim(url->s, url->slen, &a, &b); free(u); u = dupn(url->s + a, b - a); }
    if (*u && !avatar_url_ok(u, strlen(u))) { free(u); return "avatarurl"; }
    cctx x;
    memset(&x, 0, sizeof x);
    x.url = u;
    const char *e = update_char(title, id, avurl_fn, &x);
    if (!e && x.changed) hk_avatar_orig_drop(id);   //  주소를 직접 바꾸면 보관한 원본은 더 이상 그 사진이 아니다
    free(u);
    return e;
}
static const char *setav_fn(jval *c, cctx *x) { json_obj_set(c, "avatar", json_new_str(x->url)); json_obj_set(c, "avatarUntil", json_new_num(x->until)); return NULL; }
const char *hk_char_set_avatar(const char *title, const char *id, const char *url, double until) {
    cctx x;
    memset(&x, 0, sizeof x);
    x.url = url;
    x.until = until;
    return update_char(title, id, setav_fn, &x);
}
jval *hk_char_get(const char *title, const char *id) {
    const jval *a = chars_raw(title);
    for (size_t i = 0; a && i < a->count; i++) if (char_ok(a->items[i]) && !strcmp(json_get_str(a->items[i], "id", ""), id)) return copy(a->items[i]);
    return NULL;
}

// ── 캐릭터 원본 사진 ──
static void avatar_key(const char *id, char *out, size_t n) {
    size_t k = 0;
    for (const char *p = id; *p && k + 1 < n; p++) if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) out[k++] = *p;
    out[k] = 0;
}
static int find_orig(const wchar_t *dirsub, const char *key, wchar_t *out) {
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    if (!path_of(dir, dirsub)) return 0;
    _snwprintf(pat, MAX_PATH - 1, L"%ls\\%hs.*", dir, key);
    pat[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls", dir, fd.cFileName);
    out[MAX_PATH - 1] = 0;
    return 1;
}
int hk_avatar_orig_read(const char *id, unsigned char **buf, size_t *n) {
    char key[32];
    avatar_key(id, key, sizeof key);
    *buf = NULL; *n = 0;
    if (!*key) return 0;
    wchar_t p[MAX_PATH];
    if (!find_orig(L"toku-rpc-native\\hook-avatars", key, p) && !find_orig(L"toku-rpc\\hook-avatars", key, p)) return 0;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    int ok = 0;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart <= 20 * 1024 * 1024) {
        *buf = (unsigned char *)malloc((size_t)sz.QuadPart);
        DWORD got = 0;
        if (*buf && ReadFile(h, *buf, (DWORD)sz.QuadPart, &got, NULL) && got == sz.QuadPart) { *n = got; ok = 1; }
        else { free(*buf); *buf = NULL; }
    }
    CloseHandle(h);
    return ok;
}
void hk_avatar_orig_drop(const char *id) {
    char key[32];
    avatar_key(id, key, sizeof key);
    if (!*key) return;
    wchar_t p[MAX_PATH];
    for (int i = 0; i < 8 && find_orig(L"toku-rpc-native\\hook-avatars", key, p); i++) DeleteFileW(p);   //  알파 폴더만 (지금 앱 폴더는 안 건드림)
}
void hk_avatar_orig_write(const char *id, const unsigned char *buf, size_t n, const char *mime) {
    hk_avatar_orig_drop(id);
    char key[32];
    avatar_key(id, key, sizeof key);
    if (!*key) return;
    static const char *const M[][2] = { { "image/png", "png" }, { "image/jpeg", "jpg" }, { "image/gif", "gif" }, { "image/webp", "webp" }, { "image/bmp", "bmp" }, { "image/x-icon", "ico" }, { "image/avif", "avif" } };
    const char *ext = "img";
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (mime && !strcmp(mime, M[i][0])) ext = M[i][1];
    wchar_t dir[MAX_PATH], p[MAX_PATH];
    if (!path_of(dir, L"toku-rpc-native\\hook-avatars")) return;
    CreateDirectoryW(dir, NULL);
    _snwprintf(p, MAX_PATH - 1, L"%ls\\%hs.%hs", dir, key, ext);
    p[MAX_PATH - 1] = 0;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { app_log(1, "[Binge] 캐릭터 원본 사진 보관 실패"); return; }
    DWORD w = 0;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    CloseHandle(h);
}

const char *hk_sniff_mime(const unsigned char *b, size_t n) {
    if (!b || n < 12) return NULL;
    if (b[0] == 0x89 && !memcmp(b + 1, "PNG", 3)) return "image/png";
    if (b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return "image/jpeg";
    if (!memcmp(b, "GIF8", 4)) return "image/gif";
    if (!memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return "image/webp";
    if (b[0] == 'B' && b[1] == 'M') return "image/bmp";
    if (b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0) return "image/x-icon";
    if (!memcmp(b + 4, "ftyp", 4) && (!memcmp(b + 8, "avif", 4) || !memcmp(b + 8, "avis", 4))) return "image/avif";
    return NULL;
}

// ── 캐릭터 사진 올리기 (hook-avatar.js uploadAvatar) ──
#include "../net/http.h"
#define LITTER_KEEP_MS (70.0 * 60 * 60 * 1000)   // 72시간 중 70시간만 믿는다

typedef struct { HWND notify; int tag; unsigned char *png; size_t n; char *wurl, *wtoken; } up_job;

static int url_plain_ok(const char *s) {   //  /^https:\/\/[^\s"'<>]+$/ (ASCII 공백·따옴표·꺾쇠 · 유니코드 공백은 아래 avatar_url_ok 와 같게)
    return avatar_url_ok(s, strlen(s));
}
//  내 Worker → 주소 (/a/<id>.png 여야 한다). 토큰은 Authorization 헤더로만
static int post_worker(up_job *j, char **url, char *err, size_t en) {
    char hdr[400];
    snprintf(hdr, sizeof hdr, "Content-Type: image/png\r\nAuthorization: Bearer %s\r\n", j->wtoken);
    int st = 0;
    sbuf r; sb_init(&r, 0);
    char e[96];
    int ok = http_post(j->wurl, hdr, j->png, j->n, 20000, &st, &r, e, sizeof e);
    SecureZeroMemory(hdr, sizeof hdr);
    if (!ok) { snprintf(err, en, "%s", e); sb_free(&r); return 0; }
    size_t a, b;
    js_trim(r.p ? r.p : "", r.len, &a, &b);
    if (st < 200 || st > 299) { snprintf(err, en, "HTTP %d %.*s", st, (int)((b - a) > 40 ? 40 : (b - a)), r.p ? r.p + a : ""); sb_free(&r); return 0; }
    jval *jv = json_parse(r.p ? r.p + a : "", b - a, NULL);
    const char *u = json_get_str(jv, "url", NULL);
    //  origin = 업로드 주소의 scheme://host[:port]
    const char *sl = strstr(j->wurl, "://");
    size_t olen = 0;
    if (sl) { const char *p = strchr(sl + 3, '/'); olen = p ? (size_t)(p - j->wurl) : strlen(j->wurl); }
    int good = u && olen && !strncmp(u, j->wurl, olen) && !strncmp(u + olen, "/a/", 3) && url_plain_ok(u) && strlen(u) > 4 && !strcmp(u + strlen(u) - 4, ".png");
    if (good) *url = dups(u); else snprintf(err, en, "bad response");
    json_free(jv);
    sb_free(&r);
    return good;
}
static int post_form(const char *endpoint, int litter, up_job *j, char **url, char *err, size_t en) {
    form_t f;
    form_init(&f);
    form_field(&f, "reqtype", "fileupload");
    if (litter) form_field(&f, "time", "72h");
    form_file(&f, "fileToUpload", "avatar.png", "image/png", j->png, j->n);
    form_end(&f);
    char hdr[160];
    snprintf(hdr, sizeof hdr, "Content-Type: multipart/form-data; boundary=%s\r\nUser-Agent: curl/8.4.0\r\n", f.boundary);
    int st = 0;
    sbuf r; sb_init(&r, 0);
    char e[96];
    int ok = http_post(endpoint, hdr, f.body.p, f.body.len, 60000, &st, &r, e, sizeof e);
    form_free(&f);
    if (!ok) { snprintf(err, en, "%s", e); sb_free(&r); return 0; }
    size_t a, b;
    js_trim(r.p ? r.p : "", r.len, &a, &b);
    char *t = dupn(r.p ? r.p + a : "", b - a);
    if (st < 200 || st > 299 || !url_plain_ok(t)) { snprintf(err, en, "HTTP %d %.*s", st, (int)(strlen(t) > 50 ? 50 : strlen(t)), t); free(t); sb_free(&r); return 0; }
    *url = t;
    sb_free(&r);
    return 1;
}
static DWORD WINAPI up_thread(LPVOID p) {
    up_job *j = (up_job *)p;
    hk_upload_res *res = (hk_upload_res *)calloc(1, sizeof *res);
    if (res) {
        res->tag = j->tag;
        char e1[100] = "", e2[100] = "", e3[100] = "";
        char *u = NULL;
        if (j->wurl && post_worker(j, &u, e1, sizeof e1)) { res->ok = 1; res->url = u; strcpy(res->host, "cloudflare"); }
        else if (post_form("https://catbox.moe/user/api.php", 0, j, &u, e2, sizeof e2)) { res->ok = 1; res->url = u; strcpy(res->host, "catbox"); }
        else if (post_form("https://litterbox.catbox.moe/resources/internals/api.php", 1, j, &u, e3, sizeof e3)) { res->ok = 1; res->url = u; res->until = (double)now_ms() + LITTER_KEEP_MS; strcpy(res->host, "litterbox"); }
        else snprintf(res->err, sizeof res->err, "%s%s%scatbox: %s / litterbox: %s", j->wurl ? "cloudflare: " : "", j->wurl ? e1 : "", j->wurl ? " / " : "", e2, e3);
        if (!PostMessageW(j->notify, WM_HK_UPLOADED, 0, (LPARAM)res)) hk_upload_res_free(res);
    }
    if (j->wtoken) SecureZeroMemory(j->wtoken, strlen(j->wtoken));
    free(j->png); free(j->wurl); free(j->wtoken); free(j);
    return 0;
}
void hk_upload_res_free(hk_upload_res *r) { if (r) { free(r->url); free(r); } }
void hk_upload_avatar(HWND notify, int tag, unsigned char *png, size_t n) {
    up_job *j = (up_job *)calloc(1, sizeof *j);
    if (!j) { free(png); return; }
    j->notify = notify; j->tag = tag; j->png = png; j->n = n;
    //  secrets "avatarUpload": { url: https://…/upload, token: [\w-]{32,200} } 형식이 맞을 때만
    jval *sec = binge_read_secrets();
    const jval *a = json_get(sec, "avatarUpload");
    const jval *uv = json_get(a, "url"), *tv = json_get(a, "token");
    if (uv && uv->t == J_STR && tv && tv->t == J_STR) {
        size_t u0, u1, t0, t1;
        js_trim(uv->s, uv->slen, &u0, &u1);
        js_trim(tv->s, tv->slen, &t0, &t1);
        const char *u = uv->s + u0, *t = tv->s + t0;
        size_t ul = u1 - u0, tl = t1 - t0;
        int ok = ul > 15 && !_strnicmp(u, "https://", 8) && ul >= 7 && !strncmp(u + ul - 7, "/upload", 7);
        for (size_t i = 8; ok && i < ul - 7; i++) { char c = u[i]; ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; }
        int tok = tl >= 32 && tl <= 200;
        for (size_t i = 0; tok && i < tl; i++) { char c = t[i]; tok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
        if (ok && tok) { j->wurl = dupn(u, ul); j->wtoken = dupn(t, tl); }
    }
    json_free(sec);
    HANDLE h = CreateThread(NULL, 0, up_thread, j, 0, NULL);
    if (h) CloseHandle(h); else { free(j->png); free(j->wurl); free(j->wtoken); free(j); }
}
