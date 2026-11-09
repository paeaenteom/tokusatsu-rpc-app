#include "bg.h"
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "strings.h"
#include "imgutil.h"
#include "../base/sbuf.h"
#include "../base/utf.h"
#include "../core/js.h"

#define MAX_BYTES (20 * 1024 * 1024)
#define MAX_SIDE 8192
#define MAX_PIXELS 36000000.0

static const bg_set DEFAULTS = { 0, "cover", 500, 500, 100, 100, 60, 0, "", "", 0, 0 };
static const struct { const char *k; int lo, hi; } LIMITS[6] = { { "posX", 0, 1000 }, { "posY", 0, 1000 }, { "zoom", 100, 400 }, { "opacity", 0, 100 }, { "dim", 0, 90 }, { "blur", 0, 20 } };
static int *lim_field(bg_set *s, int i) { int *f[6] = { &s->posX, &s->posY, &s->zoom, &s->opacity, &s->dim, &s->blur }; return f[i]; }

static const char *tr(int id) {
    const char *l = app_lang();
    const char *const *t = !strcmp(l, "ja") ? STR_JA : !strcmp(l, "en") ? STR_EN : STR_KO;
    return t[id];
}

// ── 이름 규칙 ──
static int name_rule(const char *n, const char *prefix) {   //  ^prefix[0-9a-f]{16}\.(png|jpg|webp)$
    size_t pl = strlen(prefix);
    if (strncmp(n, prefix, pl)) return 0;
    const char *p = n + pl;
    for (int i = 0; i < 16; i++) if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) return 0;
    p += 16;
    return !strcmp(p, ".png") || !strcmp(p, ".jpg") || !strcmp(p, ".webp");
}
static int image_rule(const char *n) { return name_rule(n, "bg-"); }
static int tmp_rule(const char *n) { return name_rule(n, "tmp-"); }

// ── 이미지 판별 (sniff) — 확장자가 아니라 내용으로 ──
static uint32_t CRC[256];
static uint32_t crc32_range(const unsigned char *b, size_t s, size_t e) {
    if (!CRC[1]) for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; CRC[n] = c; }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = s; i < e; i++) c = CRC[(c ^ b[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned le16(const unsigned char *p) { return (unsigned)p[1] << 8 | p[0]; }

static const char *sniff_png(const unsigned char *b, size_t n, int *w, int *h) {
    if (n < 8 + 25 + 12) return "corrupt";
    if (be32(b + 8) != 13 || memcmp(b + 12, "IHDR", 4)) return "corrupt";
    if (crc32_range(b, 12, 29) != be32(b + 29)) return "corrupt";
    uint32_t W = be32(b + 16), H = be32(b + 20);
    unsigned long long off = 8;
    int idat = 0, end = 0, anim = 0;
    while (off + 12 <= n) {
        unsigned long long len = be32(b + off);
        const unsigned char *t = b + off + 4;
        if (off + 12 + len > n) return "corrupt";   //  청크가 파일 밖으로 = 잘렸다
        if (!memcmp(t, "acTL", 4)) anim = 1;
        if (!memcmp(t, "IDAT", 4)) idat = 1;
        if (!memcmp(t, "IEND", 4)) { end = 1; break; }
        off += 12 + len;
    }
    if (!idat || !end) return "corrupt";
    if (anim) return "animated";
    *w = (int)(W > 0x7fffffff ? 0x7fffffff : W);
    *h = (int)(H > 0x7fffffff ? 0x7fffffff : H);
    return NULL;
}
//  SOF(크기)를 읽고 SOS 와 그 뒤 EOI 까지 있는지 — 잘린 파일을 거른다
static const char *sniff_jpeg(const unsigned char *b, size_t n, int *w, int *h) {
    size_t i = 2;
    int have = 0;
    while (i + 4 <= n) {
        if (b[i] != 0xff) return "corrupt";
        int m = b[i + 1];
        while (m == 0xff && i + 2 < n) { i++; m = i + 1 < n ? b[i + 1] : -1; }   //  채움 바이트
        if (i + 4 > n) return "corrupt";
        if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) { i += 2; continue; }
        if (m == 0xd9) return "corrupt";
        size_t len = be16(b + i + 2);
        if (len < 2 || i + 2 + len > n) return "corrupt";
        if (m == 0xda) {
            if (!have) return "corrupt";
            //  b.lastIndexOf(FF D9)
            long long eoi = -1;
            for (size_t k = n - 1; k >= 1; k--) if (b[k - 1] == 0xff && b[k] == 0xd9) { eoi = (long long)k - 1; break; }
            if (eoi < (long long)(i + 2 + len)) return "corrupt";
            return NULL;
        }
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
            if (len < 7) return "corrupt";
            *h = (int)be16(b + i + 5);
            *w = (int)be16(b + i + 7);
            have = 1;
        }
        i += 2 + len;
    }
    return "corrupt";
}
static const char *sniff_webp(const unsigned char *b, size_t n, int *w, int *h) {
    if (n < 30) return "corrupt";
    unsigned long long riff = le32(b + 4);
    if (riff + 8 > n || riff < 12) return "corrupt";
    unsigned long long off = 12, end = riff + 8;
    long long W = 0, H = 0;
    int still = 0, anim = 0;
    while (off + 8 <= end) {
        const unsigned char *t = b + off;
        unsigned long long len = le32(b + off + 4), d = off + 8;
        if (d + len > end) return "corrupt";
        if (!memcmp(t, "VP8X", 4)) {
            if (len < 10) return "corrupt";
            if (b[d] & 0x02) anim = 1;
            W = 1 + (long long)(b[d + 4] | b[d + 5] << 8 | b[d + 6] << 16);
            H = 1 + (long long)(b[d + 7] | b[d + 8] << 8 | b[d + 9] << 16);
        } else if (!memcmp(t, "ANIM", 4) || !memcmp(t, "ANMF", 4)) {
            anim = 1;
        } else if (!memcmp(t, "VP8 ", 4)) {
            if (len < 10 || b[d + 3] != 0x9d || b[d + 4] != 0x01 || b[d + 5] != 0x2a) return "corrupt";
            if (!W) { W = le16(b + d + 6) & 0x3fff; H = le16(b + d + 8) & 0x3fff; }
            still = 1;
        } else if (!memcmp(t, "VP8L", 4)) {
            if (len < 5 || b[d] != 0x2f) return "corrupt";
            if (!W) { uint32_t bits = le32(b + d + 1); W = (bits & 0x3fff) + 1; H = ((bits >> 14) & 0x3fff) + 1; }
            still = 1;
        }
        off = d + len + (len & 1);
    }
    if (anim) return "animated";
    if (!still) return "corrupt";
    *w = (int)W; *h = (int)H;
    return NULL;
}
const char *bg_sniff(const unsigned char *b, size_t n, char fmt[8], int *w, int *h) {
    *w = *h = 0;
    fmt[0] = 0;
    if (!b || n < 12) return "corrupt";
    const char *e;
    if (be32(b) == 0x89504e47u && be32(b + 4) == 0x0d0a1a0au) { e = sniff_png(b, n, w, h); strcpy(fmt, "png"); }
    else if (b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) { e = sniff_jpeg(b, n, w, h); strcpy(fmt, "jpg"); }
    else if (!memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) { e = sniff_webp(b, n, w, h); strcpy(fmt, "webp"); }
    else return "format";
    if (e) return e;
    if (!(*w >= 1 && *h >= 1)) return "corrupt";
    if (*w > MAX_SIDE || *h > MAX_SIDE || (double)*w * *h > MAX_PIXELS) return "tooBig";
    return NULL;
}

// ── 설정 값 ──
static int int_in(const jval *v, int lo, int hi) { return v && v->t == J_NUM && v->n == (double)(long long)v->n && v->n >= lo && v->n <= hi; }
//  화면용 이름: 경로 없이 · 제어 문자 없이 · 120 자(UTF-16) 넘으면 117 + '…'
char *bg_clean_name(const char *n) {
    const char *base = n;
    for (const char *p = n; *p; p++) if (*p == '\\' || *p == '/') base = p + 1;
    sbuf a; sb_init(&a, 0);
    for (const char *p = base; *p; p++) { unsigned char c = (unsigned char)*p; if (c < 0x20 || c == 0x7f) continue; sb_putc(&a, *p); }
    size_t s0, s1;
    js_trim(a.p ? a.p : "", a.len, &s0, &s1);
    sbuf o; sb_init(&o, 0);
    if (utf16_len(a.p ? a.p + s0 : "", s1 - s0) > 120) { js_slice16(&o, a.p + s0, s1 - s0, 117); sb_puts(&o, "\xE2\x80\xA6"); }
    else sb_putn(&o, a.p ? a.p + s0 : "", s1 - s0);
    char *r = _strdup(o.p ? o.p : "");
    sb_free(&a);
    sb_free(&o);
    return r;
}
void bg_sanitize(const jval *o, bg_set *s) {
    *s = DEFAULTS;
    if (!o || (o->t != J_OBJ && o->t != J_ARR)) return;
    const jval *im = json_get(o, "image"), *nm = json_get(o, "name");
    if (im && im->t == J_STR && image_rule(im->s) && strlen(im->s) == im->slen) snprintf(s->image, sizeof s->image, "%s", im->s);
    if (nm && nm->t == J_STR) { char *c = bg_clean_name(nm->s); snprintf(s->name, sizeof s->name, "%s", c ? c : ""); free(c); }
    if (int_in(json_get(o, "width"), 1, MAX_SIDE)) s->width = (int)json_get(o, "width")->n;
    if (int_in(json_get(o, "height"), 1, MAX_SIDE)) s->height = (int)json_get(o, "height")->n;
    const char *fit = json_get_str(o, "fit", "");
    if (!strcmp(fit, "cover") || !strcmp(fit, "contain")) snprintf(s->fit, sizeof s->fit, "%s", fit);
    //  자유 위치가 없는 예전 설정은 다섯 곳 중 하나를 같은 자리로
    const char *pos = json_get_str(o, "position", "");
    if (!strcmp(pos, "center")) { s->posX = 500; s->posY = 500; }
    else if (!strcmp(pos, "top")) { s->posX = 500; s->posY = 0; }
    else if (!strcmp(pos, "bottom")) { s->posX = 500; s->posY = 1000; }
    else if (!strcmp(pos, "left")) { s->posX = 0; s->posY = 500; }
    else if (!strcmp(pos, "right")) { s->posX = 1000; s->posY = 500; }
    for (int i = 0; i < 6; i++) { const jval *v = json_get(o, LIMITS[i].k); if (int_in(v, LIMITS[i].lo, LIMITS[i].hi)) *lim_field(s, i) = (int)v->n; }
    const jval *en = json_get(o, "enabled");
    s->enabled = en && en->t == J_BOOL && en->b && s->image[0];
    if (!s->image[0]) { s->name[0] = 0; s->width = 0; s->height = 0; }
}
int bg_validate(const jval *p, bg_set *opts) {
    if (!p || (p->t != J_OBJ && p->t != J_ARR)) return 0;
    const jval *en = json_get(p, "enabled");
    if (!en || en->t != J_BOOL) return 0;
    const char *fit = json_get_str(p, "fit", "");
    if (strcmp(fit, "cover") && strcmp(fit, "contain")) return 0;
    for (int i = 0; i < 6; i++) if (!int_in(json_get(p, LIMITS[i].k), LIMITS[i].lo, LIMITS[i].hi)) return 0;
    memset(opts, 0, sizeof *opts);
    opts->enabled = en->b;
    snprintf(opts->fit, sizeof opts->fit, "%s", fit);
    for (int i = 0; i < 6; i++) *lim_field(opts, i) = (int)json_get(p, LIMITS[i].k)->n;
    return 1;
}

// ── 폴더·파일 ──
static int dir_of(wchar_t *out, int old) {
    wchar_t ad[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad) != S_OK) return 0;
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls\\backgrounds", ad, old ? L"toku-rpc" : app_data_dir());
    out[MAX_PATH - 1] = 0;
    return 1;
}
static int file_of(const char *name, wchar_t *out, int old) {
    wchar_t d[MAX_PATH];
    if (!dir_of(d, old)) return 0;
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%hs", d, name);
    out[MAX_PATH - 1] = 0;
    return 1;
}
int bg_file(const char *name, wchar_t *out) { return (image_rule(name) || tmp_rule(name)) && file_of(name, out, 0); }
//  이 폴더 안에서 이름 규칙에 맞는 파일만 지운다
static int remove_managed(const char *name) {
    if (!name || !(image_rule(name) || tmp_rule(name))) return 0;
    wchar_t p[MAX_PATH];
    return file_of(name, p, 0) && DeleteFileW(p);
}
static void to_json(const bg_set *s, jval *o) {
    json_obj_set(o, "enabled", json_new_bool(s->enabled));
    json_obj_set(o, "fit", json_new_str(s->fit));
    json_obj_set(o, "posX", json_new_num(s->posX));
    json_obj_set(o, "posY", json_new_num(s->posY));
    json_obj_set(o, "zoom", json_new_num(s->zoom));
    json_obj_set(o, "opacity", json_new_num(s->opacity));
    json_obj_set(o, "dim", json_new_num(s->dim));
    json_obj_set(o, "blur", json_new_num(s->blur));
    json_obj_set(o, "name", json_new_str(s->name));
    json_obj_set(o, "width", json_new_num(s->width));
    json_obj_set(o, "height", json_new_num(s->height));
    json_obj_set(o, "image", json_new_str(s->image));
}
static int save(const bg_set *s) {
    jval *o = json_new(J_OBJ);
    to_json(s, o);
    settings_set("appearance", NULL, o);
    return settings_save();
}
void bg_load(bg_set *s) { bg_sanitize(settings_get("appearance", NULL), s); }

//  지금 앱이 쓰던 사진 — 알파 폴더에 없으면 읽어서 복사 (지금 앱 폴더는 읽기만)
static void adopt_old(const char *name) {
    wchar_t mine[MAX_PATH], old[MAX_PATH], d[MAX_PATH];
    if (!image_rule(name) || !file_of(name, mine, 0) || GetFileAttributesW(mine) != INVALID_FILE_ATTRIBUTES) return;
    if (!file_of(name, old, 1) || GetFileAttributesW(old) == INVALID_FILE_ATTRIBUTES) return;
    if (!dir_of(d, 0)) return;
    CreateDirectoryW(d, NULL);
    if (CopyFileW(old, mine, TRUE)) app_log(0, "[배경] 지금 앱의 배경 사진을 알파 폴더로 복사했습니다");
}
//  'ok' · 'gone'(없음·손상) · 'busy'(잠깐 못 읽음)
static const char *check_image(const char *name) {
    wchar_t p[MAX_PATH];
    if (!file_of(name, p, 0)) return "gone";
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? "gone" : "busy"; }
    LARGE_INTEGER sz;
    const char *r = "gone";
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < 64 * 1024 * 1024) {
        unsigned char *b = (unsigned char *)malloc((size_t)sz.QuadPart);
        DWORD got = 0;
        if (b && ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL) && got == sz.QuadPart) { char f[8]; int w, hh; r = bg_sniff(b, got, f, &w, &hh) ? "gone" : "ok"; }
        else r = "busy";
        free(b);
    }
    CloseHandle(h);
    return r;
}
//  저장된 사진을 못 쓰면 기본 바탕으로 (옵션은 남기고 이미지만 뗀다) — 검사한 그 이름이 아직 저장돼 있을 때만
static void drop_image(const char *reason, const char *checked, bg_set *out) {
    bg_load(out);
    if (!out->image[0] || strcmp(out->image, checked)) return;
    bg_set next = *out;
    next.enabled = 0; next.image[0] = 0; next.name[0] = 0; next.width = next.height = 0;
    if (!save(&next)) { app_log(1, "[배경] 기본 배경으로 되돌리는 저장 실패"); return; }
    remove_managed(checked);
    char m[200];
    snprintf(m, sizeof m, "[배경] 저장된 배경 이미지를 쓸 수 없어 기본 배경으로 돌아갑니다 (%s)", reason);
    app_log(1, m);
    *out = next;
}
const char *bg_get(bg_set *s) {
    bg_load(s);
    if (!s->image[0]) return NULL;
    adopt_old(s->image);
    const char *c = check_image(s->image);
    if (!strcmp(c, "gone")) { char im[40]; snprintf(im, sizeof im, "%s", s->image); drop_image("missing", im, s); return "missing"; }
    if (!strcmp(c, "busy")) { app_log(1, "[배경] 배경 이미지를 지금 읽을 수 없어 이번에는 기본 배경으로 보입니다"); s->enabled = 0; return "unavailable"; }
    return NULL;
}

// ── 고르기 · 적용 · 복원 ──
typedef struct { char token[24], file[48], name[512], fmt[8]; int w, h, used; } pend_t;
static pend_t PEND, PREV;
static int PICKING, APPLYING;

int bg_discard(const char *token) {
    if (!token) {
        int had = PEND.used || PREV.used;
        if (PEND.used) remove_managed(PEND.file);
        if (PREV.used) remove_managed(PREV.file);
        PEND.used = PREV.used = 0;
        return had;
    }
    if (PEND.used && !strcmp(token, PEND.token)) { remove_managed(PEND.file); PEND = PREV; PREV.used = 0; return 1; }
    if (PREV.used && !strcmp(token, PREV.token)) { remove_managed(PREV.file); PREV.used = 0; return 1; }
    return 0;
}
void bg_cleanup(void) {
    wchar_t d[MAX_PATH], pat[MAX_PATH];
    if (!dir_of(d, 0)) return;
    _snwprintf(pat, MAX_PATH - 1, L"%ls\\*", d);
    pat[MAX_PATH - 1] = 0;
    bg_set s;
    bg_load(&s);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char *n = w_to_utf8(fd.cFileName, wcslen(fd.cFileName));
        if (n && (tmp_rule(n) || (image_rule(n) && strcmp(n, s.image)))) remove_managed(n);
        free(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

const char *bg_pick(const wchar_t *test_path, char token[24], char name[512], int *w, int *h, char fmt[8]) {
    if (PICKING || APPLYING) return "busy";
    PICKING = 1;
    const char *err = NULL;
    wchar_t file[MAX_PATH] = L"";
    if (test_path) wcsncpy(file, test_path, MAX_PATH - 1);
    else {
        wchar_t *title = utf8_to_w(tr(S_bgDialogTitle), strlen(tr(S_bgDialogTitle)));
        wchar_t *fname = utf8_to_w(tr(S_bgDialogFilter), strlen(tr(S_bgDialogFilter)));
        wchar_t filter[200];
        _snwprintf(filter, 198, L"%ls%lc*.png;*.jpg;*.jpeg;*.webp%lc", fname ? fname : L"Images", 0, 0);
        filter[198] = filter[199] = 0;
        OPENFILENAMEW of;
        ZeroMemory(&of, sizeof of);
        of.lStructSize = sizeof of;
        of.hwndOwner = GetActiveWindow();
        of.lpstrFilter = filter;
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.lpstrTitle = title;
        of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        int r = GetOpenFileNameW(&of);
        free(title);
        free(fname);
        if (!r) { PICKING = 0; return "canceled"; }
    }
    char *u = w_to_utf8(file, wcslen(file));
    char *cn = bg_clean_name(u ? u : "");
    snprintf(name, 512, "%s", cn ? cn : "");
    free(cn);
    //  확장자 (png·jpg·jpeg·webp)
    const char *dot = u ? strrchr(u, '.') : NULL;
    const char *sl = u ? strrchr(u, '\\') : NULL;
    char ext[8] = "";
    if (dot && (!sl || dot > sl) && strlen(dot + 1) < sizeof ext) { snprintf(ext, sizeof ext, "%s", dot + 1); for (char *p = ext; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; }
    free(u);
    if (strcmp(ext, "png") && strcmp(ext, "jpg") && strcmp(ext, "jpeg") && strcmp(ext, "webp")) { PICKING = 0; return "format"; }
    unsigned char *buf = NULL;
    size_t n = 0;
    int rr = iu_read_file(file, MAX_BYTES, &buf, &n);
    if (rr < 0) { PICKING = 0; return "tooLarge"; }
    if (rr == 0) { PICKING = 0; return "read"; }
    err = bg_sniff(buf, n, fmt, w, h);
    if (err) {
        char m[700];
        snprintf(m, sizeof m, "[배경] 쓸 수 없는 이미지: %s %s", name, err);
        app_log(0, m);
        free(buf);
        PICKING = 0;
        return err;
    }
    //  원본은 읽기만 했다. 검사한 바로 그 내용을 앱 폴더에 쓴다
    unsigned char rb[8];
    BCryptGenRandom(NULL, rb, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (int i = 0; i < 8; i++) snprintf(token + i * 2, 3, "%02x", rb[i]);
    char fname[48];
    snprintf(fname, sizeof fname, "tmp-%s.%s", token, fmt);
    wchar_t d[MAX_PATH], p[MAX_PATH];
    int ok = 0;
    if (dir_of(d, 0) && file_of(fname, p, 0)) {
        SHCreateDirectoryExW(NULL, d, NULL);
        HANDLE fh = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);   //  flag 'wx'
        if (fh != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            ok = WriteFile(fh, buf, (DWORD)n, &wr, NULL) && wr == n;
            CloseHandle(fh);
            if (!ok) DeleteFileW(p);
        }
    }
    free(buf);
    if (!ok) { app_log(1, "[배경] 사본을 만들지 못함"); remove_managed(fname); PICKING = 0; return "copy"; }
    //  최신 선택은 한 칸 뒤로 물리고, 그보다 앞의 것은 지금 버린다
    if (PREV.used) remove_managed(PREV.file);
    PREV = PEND;
    memset(&PEND, 0, sizeof PEND);
    PEND.used = 1;
    snprintf(PEND.token, sizeof PEND.token, "%s", token);
    snprintf(PEND.file, sizeof PEND.file, "%s", fname);
    snprintf(PEND.name, sizeof PEND.name, "%s", name);
    snprintf(PEND.fmt, sizeof PEND.fmt, "%s", fmt);
    PEND.w = *w; PEND.h = *h;
    char m[700];
    snprintf(m, sizeof m, "[배경] 이미지 고름: %s %dx%d %s", name, *w, *h, fmt);
    app_log(0, m);
    PICKING = 0;
    return NULL;
}

const char *bg_apply(const bg_set *opts, const char *token, bg_set *saved) {
    if (PICKING || APPLYING) return "busy";
    if (token && (!PEND.used || strcmp(token, PEND.token))) return "stale";
    APPLYING = 1;
    bg_set cur;
    bg_load(&cur);
    int use = token != NULL;
    char image[40];
    if (use) snprintf(image, sizeof image, "bg-%s.%s", PEND.token, PEND.fmt); else snprintf(image, sizeof image, "%s", cur.image);
    if (opts->enabled && !image[0]) { APPLYING = 0; return "noImage"; }
    if (use) {
        wchar_t a[MAX_PATH], b[MAX_PATH];
        if (!file_of(PEND.file, a, 0) || !file_of(image, b, 0) || !MoveFileExW(a, b, 0)) { app_log(1, "[배경] 적용 실패 (파일)"); APPLYING = 0; return "copy"; }
    }
    bg_set next = *opts;
    if (use) { snprintf(next.name, sizeof next.name, "%s", PEND.name); next.width = PEND.w; next.height = PEND.h; }
    else { snprintf(next.name, sizeof next.name, "%s", cur.name); next.width = cur.width; next.height = cur.height; }
    snprintf(next.image, sizeof next.image, "%s", image);
    if (!save(&next)) {
        //  저장 실패 — 파일도 되돌려 다시 적용할 수 있게
        if (use) { wchar_t a[MAX_PATH], b[MAX_PATH]; if (file_of(image, b, 0) && file_of(PEND.file, a, 0)) MoveFileExW(b, a, 0); }
        app_log(1, "[배경] 적용 실패 (저장)");
        APPLYING = 0;
        return "save";
    }
    if (use) PEND.used = 0;
    if (PREV.used) { remove_managed(PREV.file); PREV.used = 0; }
    if (cur.image[0] && strcmp(cur.image, image)) remove_managed(cur.image);   //  복사·저장이 다 끝난 뒤에만 이전 파일을 지운다
    bg_load(saved);
    char m[300];
    snprintf(m, sizeof m, "[배경] 적용: %s %s pos %g%%,%g%% zoom %d%% opacity %d dim %d blur %d", saved->enabled ? "사용" : "사용 안 함", saved->fit,
             saved->posX / 10.0, saved->posY / 10.0, saved->zoom, saved->opacity, saved->dim, saved->blur);
    app_log(0, m);
    APPLYING = 0;
    return NULL;
}

const char *bg_reset(bg_set *saved) {
    if (PICKING || APPLYING) return "busy";
    bg_set cur;
    bg_load(&cur);
    if (!save(&DEFAULTS)) { app_log(1, "[배경] 복원 실패 (저장)"); return "save"; }
    bg_discard(NULL);
    if (cur.image[0]) remove_managed(cur.image);
    bg_cleanup();
    app_log(0, "[배경] 기본 배경으로 복원");
    bg_load(saved);
    return NULL;
}

const char *bg_report_broken(const char *image, bg_set *state) {
    bg_load(state);
    if (!image || !state->image[0] || strcmp(image, state->image)) return "stale";
    wchar_t p[MAX_PATH];
    int busy = 0;
    if (file_of(image, p, 0)) {
        HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); busy = !(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND); }
        else CloseHandle(h);
    }
    if (busy) { state->enabled = 0; return "unavailable"; }
    char im[40];
    snprintf(im, sizeof im, "%s", image);
    drop_image("decode", im, state);
    return "missing";
}
