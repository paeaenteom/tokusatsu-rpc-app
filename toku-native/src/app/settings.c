#include "settings.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jval *root;
static wchar_t path[MAX_PATH];

static jval *read_file(const wchar_t *p) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    jval *v = NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart < 32 * 1024 * 1024) {
        char *buf = (char *)malloc((size_t)sz.QuadPart + 1);
        DWORD got = 0;
        if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL)) {
            size_t off = (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
            v = json_parse(buf + off, got - off, NULL);
        }
        free(buf);
    }
    CloseHandle(h);
    return v;
}

int settings_init(const wchar_t *appdata, const wchar_t *dirname, int *copied) {
    wchar_t dir[MAX_PATH], old[MAX_PATH];
    _snwprintf(dir, MAX_PATH - 1, L"%ls\\%ls", appdata, dirname);
    dir[MAX_PATH - 1] = 0;
    CreateDirectoryW(dir, NULL);
    _snwprintf(path, MAX_PATH - 1, L"%ls\\config.json", dir);
    path[MAX_PATH - 1] = 0;
    _snwprintf(old, MAX_PATH - 1, L"%ls\\toku-rpc\\config.json", appdata);
    old[MAX_PATH - 1] = 0;
    if (copied) *copied = 0;
    root = read_file(path);
    if (!root || root->t != J_OBJ) {
        json_free(root);
        //  처음: 기존 앱 설정을 복사 (기존 파일은 읽기만)
        root = read_file(old);
        if (root && root->t == J_OBJ) { if (copied) *copied = 1; settings_save(); }
        else { json_free(root); root = json_new(J_OBJ); }
    }
    return root != NULL;
}

jval *settings_root(void) { return root; }
const wchar_t *settings_path(void) { return path; }

const jval *settings_get(const char *a, const char *b) {
    const jval *x = json_get(root, a);
    return b ? json_get(x, b) : x;
}

void settings_set(const char *a, const char *b, jval *v) {
    if (!root || !v) { json_free(v); return; }
    if (!b) { json_obj_set(root, a, v); return; }
    jval *sub = (jval *)json_get(root, a);
    if (!sub || sub->t != J_OBJ) { sub = json_new(J_OBJ); if (!sub || !json_obj_set(root, a, sub)) { json_free(v); return; } }
    json_obj_set(sub, b, v);
}

int settings_save(void) {
    char *s = json_stringify(root);
    if (!s) return 0;
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH - 1, L"%ls.tmp", path);
    tmp[MAX_PATH - 1] = 0;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok = 0;
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = 0, len = (DWORD)strlen(s);
        ok = WriteFile(h, s, len, &n, NULL) && n == len && FlushFileBuffers(h);
        CloseHandle(h);
        if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) DeleteFileW(tmp);
    }
    free(s);
    return ok;
}
