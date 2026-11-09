#include "keybinds.h"
#include <windows.h>
#include <bcrypt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/js.h"
#include "settings.h"

const char *const KB_SITES[6] = { "ttfc", "imagination", "youtube", "disneyplus", "chzzk", "danime" };
void (*kb_random5)(unsigned char out[5]) = NULL;

static int site_ok(const char *s) {
    if (!s) return 0;
    for (int i = 0; i < 6; i++) if (!strcmp(KB_SITES[i], s)) return 1;
    return 0;
}

// ── 거르기 (keybinds.js 와 같은 규칙) ──

//  str(v, n): 문자열이 아니면 '' · 제어 문자(U+0000~001F, 007F) → 빈칸 · 앞 n 개(UTF-16)
static jval *jstr(const jval *v, size_t n) {
    if (!v || v->t != J_STR) return json_new_str("");
    char *tmp = (char *)malloc(v->slen + 1);
    if (!tmp) return NULL;
    for (size_t i = 0; i < v->slen; i++) { unsigned char ch = (unsigned char)v->s[i]; tmp[i] = (ch < 0x20 || ch == 0x7f) ? ' ' : (char)ch; }
    sbuf b; sb_init(&b, 0);
    js_slice16(&b, tmp, v->slen, n);
    free(tmp);
    jval *r = json_new_strn(b.p ? b.p : "", b.len);
    sb_free(&b);
    return r;
}
static jval *jstr_trim(const jval *v, size_t n) {
    jval *s = jstr(v, n);
    if (!s) return NULL;
    size_t a, e;
    js_trim(s->s, s->slen, &a, &e);
    jval *r = json_new_strn(s->s + a, e - a);
    json_free(s);
    return r;
}
//  strList(v, n, each): 배열이 아니면 [] · 문자열만 · 앞 n 개 · 각각 str(x, each)
static jval *jstr_list(const jval *v, size_t n, size_t each) {
    jval *a = json_new(J_ARR);
    if (!a || !v || v->t != J_ARR) return a;
    for (size_t i = 0; i < v->count && a->count < n; i++)
        if (v->items[i] && v->items[i]->t == J_STR) json_arr_push(a, jstr(v->items[i], each));
    return a;
}
static int is_obj(const jval *v) { return v && (v->t == J_OBJ || v->t == J_ARR); }   // typeof === 'object' (null 은 빼고)
static int is_alnum_ascii(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

static jval *clean_target(const jval *t) {
    if (!is_obj(t) || t->t != J_OBJ) {
        //  배열도 typeof 'object' 지만 t.tag 가 없으니 결국 null
        return NULL;
    }
    jval *tag = jstr(json_get(t, "tag"), 30);
    if (!tag) return NULL;
    //  toLowerCase — 정규식([a-z0-9-])을 통과할 수 있는 건 ASCII 와 켈빈 기호(U+212A → k) 뿐
    sbuf low; sb_init(&low, 0);
    for (size_t i = 0; i < tag->slen; i++) {
        unsigned char c = (unsigned char)tag->s[i];
        if (c == 0xE2 && i + 2 < tag->slen && (unsigned char)tag->s[i + 1] == 0x84 && (unsigned char)tag->s[i + 2] == 0xAA) { sb_putc(&low, 'k'); i += 2; continue; }
        sb_putc(&low, (char)(c >= 'A' && c <= 'Z' ? c + 32 : c));
    }
    json_free(tag);
    int good = low.len >= 1 && low.len <= 30 && low.p[0] >= 'a' && low.p[0] <= 'z';
    for (size_t i = 1; good && i < low.len; i++) { char c = low.p[i]; good = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }
    if (!good) { sb_free(&low); return NULL; }
    jval *o = json_new(J_OBJ);
    json_obj_set(o, "tag", json_new_strn(low.p, low.len));
    sb_free(&low);
    json_obj_set(o, "cls", jstr_list(json_get(t, "cls"), 12, 60));
    json_obj_set(o, "id", jstr(json_get(t, "id"), 80));
    json_obj_set(o, "testid", jstr(json_get(t, "testid"), 80));
    json_obj_set(o, "aria", jstr(json_get(t, "aria"), 60));
    json_obj_set(o, "title", jstr(json_get(t, "title"), 60));
    json_obj_set(o, "text", jstr(json_get(t, "text"), 40));
    json_obj_set(o, "icon", jstr(json_get(t, "icon"), 80));
    json_obj_set(o, "path", jstr_list(json_get(t, "path"), 5, 40));
    json_obj_set(o, "uniq", jstr_list(json_get(t, "uniq"), 12, 60));
    const jval *ctx = json_get(t, "ctx");
    if (js_truthy(ctx) && is_obj(ctx)) {
        jval *c = json_new(J_OBJ);
        double lvl = js_to_number(ctx->t == J_OBJ ? json_get(ctx, "lvl") : NULL);
        int okl = isfinite(lvl) && floor(lvl) == lvl && lvl >= 0 && lvl < 4;
        json_obj_set(c, "lvl", json_new_num(okl ? (lvl == 0 ? 0 : lvl) : 0));
        json_obj_set(c, "prev", jstr(ctx->t == J_OBJ ? json_get(ctx, "prev") : NULL, 80));
        json_obj_set(c, "next", jstr(ctx->t == J_OBJ ? json_get(ctx, "next") : NULL, 80));
        json_obj_set(o, "ctx", c);
    } else json_obj_set(o, "ctx", json_new(J_NULL));
    return o;
}

static int code_ok(const char *s, size_t n) {
    if (!n) return 1;
    if (n > 24 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
    for (size_t i = 1; i < n; i++) if (!is_alnum_ascii(s[i])) return 0;
    return 1;
}
static int modifier_only(const char *s, size_t n) {
    static const char *B[] = { "Shift", "Control", "Alt", "Meta", "OS" };
    for (int i = 0; i < 5; i++) {
        size_t k = strlen(B[i]);
        if (n < k || strncmp(s, B[i], k)) continue;
        if (n == k) return 1;
        if ((n - k == 4 && !strncmp(s + k, "Left", 4)) || (n - k == 5 && !strncmp(s + k, "Right", 5))) return 1;
    }
    return 0;
}
//  combo: { code, ctrl, alt, shift, meta } — 실패면 0
typedef struct { char code[32]; int ctrl, alt, shift, meta; } combo_t;
static int clean_combo(const jval *c, combo_t *o) {
    if (!is_obj(c)) return 0;
    const jval *cd = c->t == J_OBJ ? json_get(c, "code") : NULL;
    const char *code = (cd && cd->t == J_STR) ? cd->s : "";
    size_t n = (cd && cd->t == J_STR) ? cd->slen : 0;
    if (n && (!code_ok(code, n) || strlen(code) != n)) return 0;
    if (modifier_only(code, n)) return 0;
    memcpy(o->code, code, n);
    o->code[n] = 0;
    o->ctrl = js_truthy(c->t == J_OBJ ? json_get(c, "ctrl") : NULL);
    o->alt = js_truthy(c->t == J_OBJ ? json_get(c, "alt") : NULL);
    //  배열이면 c.shift 는 Array.prototype.shift (함수) → 참. ctrl·alt·meta 는 배열에 없음
    o->shift = c->t == J_ARR ? 1 : js_truthy(json_get(c, "shift"));
    o->meta = js_truthy(c->t == J_OBJ ? json_get(c, "meta") : NULL);
    return 1;
}
static void put_combo(jval *o, const combo_t *c) {
    json_obj_set(o, "code", json_new_str(c->code));
    json_obj_set(o, "ctrl", json_new_bool(c->ctrl));
    json_obj_set(o, "alt", json_new_bool(c->alt));
    json_obj_set(o, "shift", json_new_bool(c->shift));
    json_obj_set(o, "meta", json_new_bool(c->meta));
}
static int same_combo(const jval *a, const combo_t *b) {
    const char *ac = json_get_str(a, "code", "");
    return *b->code && !strcmp(ac, b->code) && json_get_bool(a, "ctrl", 0) == !!b->ctrl && json_get_bool(a, "alt", 0) == !!b->alt
        && json_get_bool(a, "shift", 0) == !!b->shift && json_get_bool(a, "meta", 0) == !!b->meta;
}
static int id_ok(const jval *v) {
    if (!v || v->t != J_STR || v->slen < 1 || v->slen > 24 || strlen(v->s) != v->slen) return 0;
    for (size_t i = 0; i < v->slen; i++) { char c = v->s[i]; if (!is_alnum_ascii(c) && c != '_' && c != '-') return 0; }
    return 1;
}

static jval *clean_bind(const jval *b) {
    if (!b || b->t != J_OBJ) return NULL;   // 배열이면 target 이 없어 null
    jval *target = clean_target(json_get(b, "target"));
    combo_t c;
    if (!clean_combo(b, &c)) { memset(&c, 0, sizeof c); }
    const jval *idv = json_get(b, "id");
    if (!target || !id_ok(idv)) { json_free(target); return NULL; }
    jval *o = json_new(J_OBJ);
    json_obj_set(o, "id", json_new_strn(idv->s, idv->slen));
    json_obj_set(o, "label", jstr_trim(json_get(b, "label"), 30));
    json_obj_set(o, "target", target);
    put_combo(o, &c);
    const jval *fs = json_get(b, "fs");
    if (fs && fs->t == J_BOOL && fs->b) json_obj_set(o, "fs", json_new_bool(1));
    return o;
}

jval *kb_clean_all(const jval *raw) {
    jval *out = json_new(J_OBJ);
    if (!out || !raw || raw->t != J_OBJ) return out;
    for (int s = 0; s < 6; s++) {
        const jval *arr = json_get(raw, KB_SITES[s]);
        if (!arr || arr->t != J_ARR) continue;
        jval *list = json_new(J_ARR);
        for (size_t i = 0; i < arr->count; i++) {
            jval *c = clean_bind(arr->items[i]);
            if (!c) continue;
            const char *id = json_get_str(c, "id", "");
            int dup = 0;
            for (size_t k = 0; k < list->count && !dup; k++) dup = !strcmp(json_get_str(list->items[k], "id", ""), id);
            if (dup) { json_free(c); continue; }
            json_arr_push(list, c);
            if (list->count >= KB_MAX_PER_SITE) break;
        }
        if (list->count) json_obj_set(out, KB_SITES[s], list); else json_free(list);
    }
    return out;
}

jval *kb_clean_on(const jval *raw) {
    jval *out = json_new(J_OBJ);
    if (!out || !raw || raw->t != J_OBJ) return out;
    for (int s = 0; s < 6; s++) {
        const jval *v = json_get(raw, KB_SITES[s]);
        if (v && v->t == J_BOOL && !v->b) json_obj_set(out, KB_SITES[s], json_new_bool(0));
    }
    return out;
}

jval *kb_all(void) { return kb_clean_all(settings_get("keybinds", NULL)); }
jval *kb_on(void) { return kb_clean_on(settings_get("keybindsOn", NULL)); }

void kb_payload(sbuf *b) {
    jval *all = kb_all(), *on = kb_on();
    sb_puts(b, "{\"type\":\"KEYBINDS\",\"keybinds\":");
    if (all) json_write(b, all); else sb_puts(b, "{}");
    sb_puts(b, ",\"on\":");
    if (on) json_write(b, on); else sb_puts(b, "{}");
    sb_putc(b, '}');
    json_free(all);
    json_free(on);
}

// ── 고치기 ──
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
static void arr_del(jval *a, size_t i) {
    json_free(a->items[i]);
    for (size_t k = i + 1; k < a->count; k++) a->items[k - 1] = a->items[k];
    a->count--;
}
static jval *find_bind(jval *list, const char *id, size_t *at) {
    for (size_t i = 0; list && i < list->count; i++) if (!strcmp(json_get_str(list->items[i], "id", ""), id)) { if (at) *at = i; return list->items[i]; }
    return NULL;
}

typedef kb_res (*edit_fn)(jval *list, void *ctx);
static kb_res edit(const char *site, edit_fn fn, void *ctx) {
    kb_res r;
    memset(&r, 0, sizeof r);
    if (!site_ok(site)) { r.error = "site"; return r; }
    jval *all = kb_all();
    if (!all) { r.error = "site"; return r; }
    const jval *cur = json_get(all, site);
    jval *list = json_new(J_ARR);
    for (size_t i = 0; cur && i < cur->count; i++) {   // 줄마다 복사 (fn 이 고친다)
        char *s = json_stringify(cur->items[i]);
        jval *c = s ? json_parse(s, strlen(s), NULL) : NULL;
        free(s);
        if (c) json_arr_push(list, c);
    }
    r = fn(list, ctx);
    if (r.ok) {
        if (list->count) json_obj_set(all, site, list);
        else { obj_del(all, site); json_free(list); }
        settings_set("keybinds", NULL, all);
        settings_save();
    } else {
        json_free(list);
        json_free(all);
    }
    return r;
}

typedef struct { const char *id; jval *target; jval *label; const combo_t *combo; } edit_ctx;

static kb_res do_add(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    if (list->count >= KB_MAX_PER_SITE) { r.error = "full"; return r; }
    jval *b = json_new(J_OBJ);
    json_obj_set(b, "id", json_new_str(x->id));
    json_obj_set(b, "label", x->label); x->label = NULL;
    combo_t none; memset(&none, 0, sizeof none);
    put_combo(b, &none);
    json_obj_set(b, "target", x->target); x->target = NULL;
    json_arr_push(list, b);
    r.ok = 1;
    snprintf(r.id, sizeof r.id, "%s", x->id);
    return r;
}
kb_res kb_add(const char *site, const jval *target, const jval *label) {
    kb_res r; memset(&r, 0, sizeof r);
    jval *t = clean_target(target);
    if (!t) { r.error = "target"; return r; }
    unsigned char rb[5];
    if (kb_random5) kb_random5(rb);
    else BCryptGenRandom(NULL, rb, 5, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char id[11];
    for (int i = 0; i < 5; i++) snprintf(id + i * 2, 3, "%02x", rb[i]);
    edit_ctx x = { id, t, jstr_trim(label, 30), NULL };
    r = edit(site, do_add, &x);
    json_free(x.target);
    json_free(x.label);
    return r;
}

static kb_res do_target(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    jval *b = find_bind(list, x->id, NULL);
    if (!b) { r.error = "missing"; return r; }
    json_obj_set(b, "target", x->target); x->target = NULL;
    obj_del(b, "fs");   // 다른 버튼일 수 있다 — 전체화면 여부는 다시 배운다
    r.ok = 1;
    return r;
}
kb_res kb_set_target(const char *site, const char *id, const jval *target) {
    kb_res r; memset(&r, 0, sizeof r);
    jval *t = clean_target(target);
    if (!t) { r.error = "target"; return r; }
    edit_ctx x = { id, t, NULL, NULL };
    r = edit(site, do_target, &x);
    json_free(x.target);
    return r;
}

static kb_res do_key(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    jval *b = find_bind(list, x->id, NULL);
    if (!b) { r.error = "missing"; return r; }
    combo_t none; memset(&none, 0, sizeof none);
    for (size_t i = 0; i < list->count; i++) {
        jval *o = list->items[i];
        if (!strcmp(json_get_str(o, "id", ""), x->id) || !same_combo(o, x->combo)) continue;
        r.has_moved = 1;
        snprintf(r.moved, sizeof r.moved, "%s", json_get_str(o, "label", ""));
        put_combo(o, &none);
    }
    put_combo(b, x->combo);
    r.ok = 1;
    return r;
}
kb_res kb_set_key(const char *site, const char *id, const jval *combo) {
    kb_res r; memset(&r, 0, sizeof r);
    combo_t c;
    if (!clean_combo(combo, &c)) { r.error = "key"; return r; }
    edit_ctx x = { id, NULL, NULL, &c };
    return edit(site, do_key, &x);
}

static kb_res do_rename(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    jval *b = find_bind(list, x->id, NULL);
    if (!b) { r.error = "missing"; return r; }
    json_obj_set(b, "label", x->label); x->label = NULL;
    r.ok = 1;
    return r;
}
kb_res kb_rename(const char *site, const char *id, const jval *label) {
    edit_ctx x = { id, NULL, jstr_trim(label, 30), NULL };
    kb_res r = edit(site, do_rename, &x);
    json_free(x.label);
    return r;
}

static kb_res do_remove(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    size_t at;
    if (!find_bind(list, x->id, &at)) { r.error = "missing"; return r; }
    arr_del(list, at);
    r.ok = 1;
    return r;
}
kb_res kb_remove(const char *site, const char *id) {
    edit_ctx x = { id, NULL, NULL, NULL };
    return edit(site, do_remove, &x);
}

static kb_res do_fs(jval *list, void *p) {
    edit_ctx *x = (edit_ctx *)p;
    kb_res r; memset(&r, 0, sizeof r);
    jval *b = find_bind(list, x->id, NULL);
    if (!b) { r.error = "missing"; return r; }
    const jval *fs = json_get(b, "fs");
    if (fs && fs->t == J_BOOL && fs->b) { r.error = "same"; return r; }   // 바뀐 게 없으면 저장·전송 안 함
    json_obj_set(b, "fs", json_new_bool(1));
    r.ok = 1;
    return r;
}
kb_res kb_mark_fs(const char *site, const char *id) {
    edit_ctx x = { id, NULL, NULL, NULL };
    return edit(site, do_fs, &x);
}

kb_res kb_set_site_on(const char *site, int on) {
    kb_res r; memset(&r, 0, sizeof r);
    if (!site_ok(site)) { r.error = "site"; return r; }
    jval *o = kb_on();
    if (!o) { r.error = "site"; return r; }
    if (on) obj_del(o, site); else json_obj_set(o, site, json_new_bool(0));
    settings_set("keybindsOn", NULL, o);
    settings_save();
    r.ok = 1;
    return r;
}

int kb_adopt_local(const jval *keybinds, const jval *on) {
    if (settings_get("keybinds", NULL)) return 0;   // store.has — 앱에 이미 있으면 안 가져온다
    jval *all = kb_clean_all(keybinds);
    if (!all || !all->count) { json_free(all); return 0; }
    settings_set("keybinds", NULL, all);
    settings_set("keybindsOn", NULL, kb_clean_on(on));
    settings_save();
    return 1;
}
