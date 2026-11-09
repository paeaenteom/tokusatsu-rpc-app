// 로그온 시 자동 시작 — 지금 앱(main.js applyAutoStart / fast-start.js)과 같은 방식, 이름만 알파용
//  · Run 키: HKCU\...\Run 의 "TOKU RPC Alpha" = "<exe>" --hidden
//  · 빠른 시작: 작업 스케줄러 "TOKU RPC Alpha Fast Start" (로그온 트리거, 지연 없음, 우선순위 4 — 지금 앱 작업과 같은 설정)
//  ⚠ 지금 앱의 Run 값("TOKU RPC")·작업("TOKU RPC Fast Start")은 여기서 건드리지 않는다
#define COBJMACROS
#include "autostart.h"
#include "channel.h"
#include <windows.h>
#include <taskschd.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#ifndef RUN_NAME
#define RUN_NAME CH_RUN_NAME
#endif
#ifndef TASK_NAME
#define TASK_NAME CH_TASK_NAME
#endif

static void exe_path(wchar_t *out, DWORD n) {
    DWORD k = GetModuleFileNameW(NULL, out, n);
    if (!k || k >= n) out[0] = 0;
}

int as_installed(void) {
    wchar_t exe[MAX_PATH], want[MAX_PATH], la[MAX_PATH];
    exe_path(exe, MAX_PATH);
    if (!exe[0] || SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, la) != S_OK) return 0;
    _snwprintf(want, MAX_PATH - 1, L"%ls\\Programs\\%ls\\%ls", la, CH_PROG_DIR, CH_EXE);
    want[MAX_PATH - 1] = 0;
    return !_wcsicmp(exe, want);
}

int as_run_set(int on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return 0;
    LONG r;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
        exe_path(exe, MAX_PATH);
        if (!exe[0]) { RegCloseKey(k); return 0; }
        _snwprintf(cmd, MAX_PATH + 31, L"\"%ls\" --hidden", exe);   // 경로에 빈칸이 있어 꼭 따옴표
        cmd[MAX_PATH + 31] = 0;
        r = RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, RUN_NAME);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;   // 원래 없으면 목표 달성
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

int as_run_exists(void) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return 0;
    LONG r = RegQueryValueExW(k, RUN_NAME, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

//  XML 글자 이스케이프 (경로·사용자 이름)
static void xml_put(wchar_t *out, size_t n, size_t *len, const wchar_t *s) {
    for (; *s && *len + 8 < n; s++) {
        const wchar_t *rep = *s == L'&' ? L"&amp;" : *s == L'<' ? L"&lt;" : *s == L'>' ? L"&gt;" : NULL;
        if (rep) { while (*rep) out[(*len)++] = *rep++; }
        else out[(*len)++] = *s;
    }
    out[*len] = 0;
}

static ITaskFolder *open_root(ITaskService **svc_out) {
    ITaskService *svc = NULL;
    ITaskFolder *root = NULL;
    if (FAILED(CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void **)&svc))) return NULL;
    VARIANT e;
    VariantInit(&e);
    BSTR slash = SysAllocString(L"\\");
    if (FAILED(ITaskService_Connect(svc, e, e, e, e)) || FAILED(ITaskService_GetFolder(svc, slash, &root))) root = NULL;
    SysFreeString(slash);
    if (!root) { ITaskService_Release(svc); return NULL; }
    *svc_out = svc;
    return root;
}

int as_task_set(int on) {
    HRESULT ci = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int ok = 0;
    ITaskService *svc = NULL;
    ITaskFolder *root = open_root(&svc);
    if (root) {
        BSTR name = SysAllocString(TASK_NAME);
        if (!on) {
            HRESULT hr = ITaskFolder_DeleteTask(root, name, 0);
            ok = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        } else {
            wchar_t exe[MAX_PATH], dom[256] = L"", usr[256] = L"", who[520];
            exe_path(exe, MAX_PATH);
            GetEnvironmentVariableW(L"USERDOMAIN", dom, 256);
            GetEnvironmentVariableW(L"USERNAME", usr, 256);
            _snwprintf(who, 519, L"%ls\\%ls", dom, usr);
            who[519] = 0;
            static wchar_t x[8192];
            //  고정 부분(태그)은 그대로 붙이고, 사용자 이름·경로만 이스케이프한다
            size_t len = 0;
            x[0] = 0;
            #define RAW(s) do { const wchar_t *q_ = (s); while (*q_ && len + 1 < 8192) x[len++] = *q_++; x[len] = 0; } while (0)
            RAW(L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
                L"<Task version=\"1.3\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                L"<RegistrationInfo><Description>TOKU RPC C \xBCA0\xD0C0\xB97C \xB85C\xADF8\xC628 \xC9C1\xD6C4 \xBC14\xB85C \xB744\xC6B4\xB2E4</Description></RegistrationInfo>"
                L"<Principals><Principal id=\"Author\"><UserId>");
            xml_put(x, 8192, &len, who);
            RAW(L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
                L"<Settings><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
                L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
                L"<Priority>4</Priority><StartWhenAvailable>true</StartWhenAvailable></Settings>"
                L"<Triggers><LogonTrigger><UserId>");
            xml_put(x, 8192, &len, who);
            RAW(L"</UserId></LogonTrigger></Triggers><Actions Context=\"Author\"><Exec><Command>");
            xml_put(x, 8192, &len, exe);
            RAW(L"</Command><Arguments>--hidden</Arguments></Exec></Actions></Task>");
            #undef RAW
            if (exe[0] && dom[0] && usr[0] && len + 2 < 8192) {
                BSTR xml = SysAllocString(x);
                VARIANT e;
                VariantInit(&e);
                IRegisteredTask *t = NULL;
                HRESULT hr = ITaskFolder_RegisterTask(root, name, xml, TASK_CREATE_OR_UPDATE, e, e, TASK_LOGON_INTERACTIVE_TOKEN, e, &t);
                ok = SUCCEEDED(hr);
                if (t) IRegisteredTask_Release(t);
                SysFreeString(xml);
            }
        }
        SysFreeString(name);
        ITaskFolder_Release(root);
        ITaskService_Release(svc);
    }
    if (SUCCEEDED(ci)) CoUninitialize();
    return ok;
}

int as_task_exists(void) {
    HRESULT ci = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int ok = 0;
    ITaskService *svc = NULL;
    ITaskFolder *root = open_root(&svc);
    if (root) {
        BSTR name = SysAllocString(TASK_NAME);
        IRegisteredTask *t = NULL;
        if (SUCCEEDED(ITaskFolder_GetTask(root, name, &t)) && t) { ok = 1; IRegisteredTask_Release(t); }
        SysFreeString(name);
        ITaskFolder_Release(root);
        ITaskService_Release(svc);
    }
    if (SUCCEEDED(ci)) CoUninitialize();
    return ok;
}

//  다른 이름으로 등록된 부팅 항목 지우기 (예전 알파 → 베타 옮길 때). 없으면 성공
int as_remove_named(const wchar_t *run_name, const wchar_t *task_name) {
    int ok = 1;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        LONG r = RegDeleteValueW(k, run_name);
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) ok = 0;
        RegCloseKey(k);
    } else ok = 0;
    HRESULT ci = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ITaskService *svc = NULL;
    ITaskFolder *root = open_root(&svc);
    if (root) {
        BSTR name = SysAllocString(task_name);
        HRESULT hr = ITaskFolder_DeleteTask(root, name, 0);
        if (!(SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) ok = 0;
        SysFreeString(name);
        ITaskFolder_Release(root);
        ITaskService_Release(svc);
    } else ok = 0;
    if (SUCCEEDED(ci)) CoUninitialize();
    return ok;
}
