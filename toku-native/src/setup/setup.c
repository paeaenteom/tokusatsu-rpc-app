// TOKU RPC 설치 프로그램 (C · Win32) — 앱 exe 와 크롬 확장 파일을 안에 담아 두고 풀어 설치한다
//  설치: 돌고 있는 앱에 정상 종료 부탁(강제 종료 X) → %LOCALAPPDATA%\Programs\TOKU RPC Beta 에 앱·chrome-ext → 시작 메뉴 바로 가기
//        → '앱 및 기능' 제거 항목 → 앱 실행 → 확장 '압축해제 로드' 안내
//  제거: (설치 폴더의 'Uninstall TOKU RPC.exe' /uninstall) 앱 종료 → 부팅 항목·바로 가기·제거 항목·프로그램 파일 삭제
//        설정·웹훅 데이터(%APPDATA%\toku-rpc-native)는 고른 경우에만 휴지통으로
//  ⚠ 크롬 정책(ExtensionSettings) 레지스트리는 쓰지 않는다 — 예전 설치기에서 재시작마다 확장이 사라지는 원인이었다
//  명령줄: /S 조용히 · /NOLAUNCH 실행 안 함 · /D=<폴더> 설치 위치 · /uninstall 제거
//          /TEST 시험용 — 진짜 앱을 끄지 않고, 제거 항목·바로 가기 이름을 바꾸고, 부팅 항목은 안 건드린다
#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../app/channel.h"

#define ID_QUIT 104   // 앱(main.c) 의 끝내기 명령
#define BTN_OK 101
#define BTN_FOLDER 102

static int SILENT, NOLAUNCH, TESTMODE, UNINSTALL;
static wchar_t DIR[MAX_PATH];

// ── 문구 (Windows 표시 언어: 한국어 · 일본어 · 그 밖은 영어) ──
enum { T_TITLE, T_ASK, T_ASK_SUB, T_INSTALL, T_CANCEL, T_RUNNING, T_FAIL, T_DONE, T_DONE_SUB, T_EXT_FOLDER, T_CLOSE, T_SECRETS,
       T_UN_ASK, T_UN_SUB, T_UN_BTN, T_UN_DATA, T_UN_DONE, T_COUNT };
static const wchar_t *const KO[T_COUNT] = {
    L"TOKU RPC 설치",
    L"TOKU RPC 베타 %hs 를 설치할까요?",
    L"설치 위치: %ls\n\n돌고 있는 TOKU RPC 는 정상 종료한 뒤 바꿉니다. 설정·웹훅 데이터는 그대로 남습니다.",
    L"설치", L"취소",
    L"돌고 있는 TOKU RPC 가 꺼지지 않았습니다. 트레이에서 종료한 뒤 다시 설치해 주세요.",
    L"설치하지 못했습니다: %ls",
    L"설치를 마쳤습니다",
    L"크롬 확장은 직접 한 번 넣어 주세요:\n1. 크롬 주소창에 chrome://extensions\n2. 오른쪽 위 '개발자 모드' 켜기\n3. '압축해제된 확장 프로그램 로드' → 아래 폴더 선택\n%ls",
    L"확장 폴더 열기", L"닫기",
    L"\n\n⚠ Discord 애플리케이션 ID 가 든 secrets.json 이 아직 없습니다:\n%ls\n만드는 법은 README 의 'secrets.json' 을 봐 주세요.",
    L"TOKU RPC 를 제거할까요?",
    L"돌고 있으면 정상 종료한 뒤 프로그램·바로 가기·부팅 자동 시작을 지웁니다.",
    L"제거",
    L"설정·웹훅 데이터도 지우기 (휴지통으로)",
    L"제거를 마쳤습니다",
};
static const wchar_t *const EN[T_COUNT] = {
    L"TOKU RPC Setup",
    L"Install TOKU RPC beta %hs?",
    L"Install location: %ls\n\nA running TOKU RPC is closed normally first. Settings and webhook data are kept.",
    L"Install", L"Cancel",
    L"The running TOKU RPC did not close. Quit it from the tray and run setup again.",
    L"Setup failed: %ls",
    L"Setup finished",
    L"Add the Chrome extension once:\n1. Open chrome://extensions\n2. Turn on 'Developer mode' (top right)\n3. 'Load unpacked' → choose this folder\n%ls",
    L"Open extension folder", L"Close",
    L"\n\n⚠ secrets.json with your Discord application IDs is missing:\n%ls\nSee 'secrets.json' in the README.",
    L"Uninstall TOKU RPC?",
    L"If it is running it is closed normally, then the program, shortcut and start-at-logon entries are removed.",
    L"Uninstall",
    L"Also remove settings and webhook data (to the Recycle Bin)",
    L"TOKU RPC was removed",
};
static const wchar_t *const JA[T_COUNT] = {
    L"TOKU RPC セットアップ",
    L"TOKU RPC ベータ %hs をインストールしますか？",
    L"インストール先: %ls\n\n起動中の TOKU RPC は正常に終了してから入れ替えます。設定・Webhook のデータはそのまま残ります。",
    L"インストール", L"キャンセル",
    L"起動中の TOKU RPC が終了しませんでした。トレイから終了して、もう一度インストールしてください。",
    L"インストールできませんでした: %ls",
    L"インストールが完了しました",
    L"Chrome 拡張機能は一度だけ手動で追加してください:\n1. Chrome で chrome://extensions を開く\n2. 右上の「デベロッパーモード」をオン\n3. 「パッケージ化されていない拡張機能を読み込む」→ 次のフォルダを選択\n%ls",
    L"拡張機能フォルダを開く", L"閉じる",
    L"\n\n⚠ Discord アプリケーション ID の入った secrets.json がまだありません:\n%ls\n作り方は README の「secrets.json」を見てください。",
    L"TOKU RPC をアンインストールしますか？",
    L"起動中なら正常に終了してから、プログラム・ショートカット・ログオン時の自動起動を削除します。",
    L"アンインストール",
    L"設定・Webhook のデータも削除する（ごみ箱へ）",
    L"TOKU RPC を削除しました",
};
static const wchar_t *const *TX = EN;
static void pick_lang(void) {
    LANGID l = PRIMARYLANGID(GetUserDefaultUILanguage());
    TX = l == LANG_KOREAN ? KO : l == LANG_JAPANESE ? JA : EN;
}

// ── 경로 ──
static int known(REFKNOWNFOLDERID id, wchar_t *out) {
    PWSTR p = NULL;
    if (FAILED(SHGetKnownFolderPath(id, 0, NULL, &p))) return 0;
    _snwprintf(out, MAX_PATH - 1, L"%ls", p);
    out[MAX_PATH - 1] = 0;
    CoTaskMemFree(p);
    return 1;
}
static void join(wchar_t *out, const wchar_t *a, const wchar_t *b) {
    _snwprintf(out, MAX_PATH - 1, L"%ls\\%ls", a, b);
    out[MAX_PATH - 1] = 0;
}
static const wchar_t *uninst_key(void) {
    return TESTMODE ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TOKURPCBetaTest"
                    : L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TOKURPCBeta";
}
static int shortcut_path(wchar_t *out) {
    if (TESTMODE) { join(out, DIR, L"TOKU RPC Test.lnk"); return 1; }
    wchar_t pr[MAX_PATH];
    if (!known(&FOLDERID_Programs, pr)) return 0;
    join(out, pr, L"TOKU RPC.lnk");
    return 1;
}

// ── 대화상자 (TaskDialog) — 조용히 모드면 띄우지 않는다 ──
static int ask(const wchar_t *main, const wchar_t *sub, const wchar_t *yes, const wchar_t *check, int *checked) {
    if (SILENT) { if (checked) *checked = 0; return 1; }
    TASKDIALOG_BUTTON b[2] = { { IDOK, yes }, { IDCANCEL, TX[T_CANCEL] } };
    TASKDIALOGCONFIG c;
    ZeroMemory(&c, sizeof c);
    c.cbSize = sizeof c;
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = TX[T_TITLE];
    c.hMainIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    if (c.hMainIcon) c.dwFlags |= TDF_USE_HICON_MAIN;
    c.pszMainInstruction = main;
    c.pszContent = sub;
    c.cButtons = 2;
    c.pButtons = b;
    c.nDefaultButton = IDOK;
    c.pszVerificationText = check;
    int btn = 0;
    BOOL v = FALSE;
    if (FAILED(TaskDialogIndirect(&c, &btn, NULL, &v))) btn = MessageBoxW(NULL, sub, main, MB_OKCANCEL | MB_ICONQUESTION) == IDOK ? IDOK : IDCANCEL;
    if (checked) *checked = v ? 1 : 0;
    return btn == IDOK;
}
static void say(const wchar_t *main, const wchar_t *sub, int error) {
    if (SILENT) return;
    MessageBoxW(NULL, sub, main, MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

// ── 돌고 있는 앱에 정상 종료 부탁 (강제 종료는 안 한다 — Discord 파이프가 먹통이 될 수 있다) ──
static int quit_running(void) {
    if (TESTMODE) return 1;   //  시험: 진짜 앱은 건드리지 않는다
    HWND w = FindWindowW(CH_CLASS, NULL);
    if (!w) return 1;
    PostMessageW(w, WM_COMMAND, ID_QUIT, 0);
    for (int i = 0; i < 200 && IsWindow(w); i++) Sleep(100);   //  최대 20초 (끝내기에서 카드를 내리고 연결을 정리한다)
    if (IsWindow(w)) return 0;
    Sleep(500);   //  창이 닫힌 뒤 프로세스가 exe 를 놓을 때까지 잠깐
    return 1;
}

// ── 파일 ──
static int ensure_parent(const wchar_t *path) {
    wchar_t d[MAX_PATH];
    wcsncpy(d, path, MAX_PATH - 1);
    d[MAX_PATH - 1] = 0;
    wchar_t *s = wcsrchr(d, L'\\');
    if (!s) return 1;
    *s = 0;
    int r = SHCreateDirectoryExW(NULL, d, NULL);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}
//  임시 이름으로 쓴 뒤 바꿔치기 — 쓰는 도중 실패해도 원래 파일이 깨지지 않게
static int write_file(const wchar_t *path, const unsigned char *b, DWORD n) {
    if (!ensure_parent(path)) return 0;
    wchar_t tmp[MAX_PATH + 8];
    _snwprintf(tmp, MAX_PATH + 7, L"%ls.new", path);
    tmp[MAX_PATH + 7] = 0;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(h, b, n, &w, NULL) && w == n;
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(tmp); return 0; }
    return 1;
}
//  폴더 통째로 지우기 (영구) — 설치 폴더 안의 우리 파일에만 쓴다
static void rmtree(const wchar_t *dir) {
    wchar_t pat[MAX_PATH];
    join(pat, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t p[MAX_PATH];
            join(p, dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rmtree(p);
            else { SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL); DeleteFileW(p); }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(dir);
}
//  휴지통으로 (되살릴 수 있게) — 사용자 데이터용
static void to_recycle_bin(const wchar_t *path) {
    wchar_t from[MAX_PATH + 2];
    ZeroMemory(from, sizeof from);
    wcsncpy(from, path, MAX_PATH);   //  끝에 \0 두 개
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

// ── 담아 둔 파일 묶음 ("TOKUPAK1", tools/pack-setup.js) ──
typedef struct { wchar_t path[MAX_PATH]; const unsigned char *data; DWORD size; } entry;
static entry *ENT;
static int NENT;
static int load_payload(void) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));   //  RT_RCDATA
    if (!r) return 0;
    HGLOBAL g = LoadResource(NULL, r);
    const unsigned char *p = g ? (const unsigned char *)LockResource(g) : NULL;
    DWORD n = SizeofResource(NULL, r);
    if (!p || n < 12 || memcmp(p, "TOKUPAK1", 8)) return 0;
    uint32_t cnt;
    memcpy(&cnt, p + 8, 4);
    if (cnt == 0 || cnt > 10000) return 0;
    ENT = (entry *)calloc(cnt, sizeof *ENT);
    if (!ENT) return 0;
    size_t o = 12;
    for (uint32_t i = 0; i < cnt; i++) {
        uint16_t pl;
        uint32_t sz;
        if (o + 2 > n) return 0;
        memcpy(&pl, p + o, 2); o += 2;
        if (o + pl + 4 > n || pl == 0 || pl > 400) return 0;
        char rel[402];
        memcpy(rel, p + o, pl); rel[pl] = 0; o += pl;
        memcpy(&sz, p + o, 4); o += 4;
        if (o + sz > n) return 0;
        //  경로 검사: 위로 올라가거나(..) 절대 경로면 안 된다
        if (strstr(rel, "..") || rel[0] == '/' || strchr(rel, ':') || strchr(rel, '\\')) return 0;
        wchar_t wrel[402];
        int k = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rel, -1, wrel, 402);
        if (k <= 0) return 0;
        for (wchar_t *q = wrel; *q; q++) if (*q == L'/') *q = L'\\';
        join(ENT[i].path, DIR, wrel);
        ENT[i].data = p + o;
        ENT[i].size = sz;
        o += sz;
    }
    NENT = (int)cnt;
    return 1;
}

// ── 바로 가기 ──
static int make_shortcut(const wchar_t *lnk, const wchar_t *target) {
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return 0;
    IShellLinkW_SetPath(sl, target);
    IShellLinkW_SetWorkingDirectory(sl, DIR);
    IShellLinkW_SetDescription(sl, L"TOKU RPC");
    IShellLinkW_SetIconLocation(sl, target, 0);
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        ensure_parent(lnk);
        ok = SUCCEEDED(IPersistFile_Save(pf, lnk, TRUE));
        IPersistFile_Release(pf);
    }
    IShellLinkW_Release(sl);
    return ok;
}

// ── '앱 및 기능' 제거 항목 ──
static void reg_str(HKEY k, const wchar_t *name, const wchar_t *v) { RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t))); }
static void reg_dw(HKEY k, const wchar_t *name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v); }
static int write_uninstall_entry(const wchar_t *uninst, const wchar_t *app, DWORD kb) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, uninst_key(), 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return 0;
    wchar_t v[MAX_PATH * 2], ver[32];
    _snwprintf(ver, 31, L"%hs", CH_VERSION_BETA);
    ver[31] = 0;
    reg_str(k, L"DisplayName", L"TOKU RPC");
    reg_str(k, L"DisplayVersion", ver);
    reg_str(k, L"Publisher", L"paeaenteom");
    reg_str(k, L"URLInfoAbout", L"https://github.com/paeaenteom/tokusatsu-rpc-app");
    reg_str(k, L"InstallLocation", DIR);
    reg_str(k, L"DisplayIcon", app);
    _snwprintf(v, MAX_PATH * 2 - 1, L"\"%ls\" /uninstall%ls", uninst, TESTMODE ? L" /TEST" : L"");
    v[MAX_PATH * 2 - 1] = 0;
    reg_str(k, L"UninstallString", v);
    _snwprintf(v, MAX_PATH * 2 - 1, L"\"%ls\" /uninstall /S%ls", uninst, TESTMODE ? L" /TEST" : L"");
    v[MAX_PATH * 2 - 1] = 0;
    reg_str(k, L"QuietUninstallString", v);
    reg_dw(k, L"NoModify", 1);
    reg_dw(k, L"NoRepair", 1);
    reg_dw(k, L"EstimatedSize", kb);
    RegCloseKey(k);
    return 1;
}

// ── 설치 ──
static int install(void) {
    wchar_t sub[1200], head[200];
    _snwprintf(head, 199, TX[T_ASK], CH_VERSION_BETA);
    head[199] = 0;
    _snwprintf(sub, 1199, TX[T_ASK_SUB], DIR);
    sub[1199] = 0;
    if (!ask(head, sub, TX[T_INSTALL], NULL, NULL)) return 2;
    if (!quit_running()) { say(TX[T_TITLE], TX[T_RUNNING], 1); return 1; }
    //  확장 폴더는 비우고 다시 — 지난 버전에만 있던 파일이 남지 않게
    wchar_t ext[MAX_PATH], app[MAX_PATH], uninst[MAX_PATH], self[MAX_PATH];
    join(ext, DIR, L"chrome-ext");
    join(app, DIR, CH_EXE);
    join(uninst, DIR, L"Uninstall TOKU RPC.exe");
    rmtree(ext);
    DWORD total = 0;
    for (int i = 0; i < NENT; i++) {
        if (!write_file(ENT[i].path, ENT[i].data, ENT[i].size)) {
            wchar_t m[MAX_PATH + 200];
            _snwprintf(m, MAX_PATH + 199, TX[T_FAIL], ENT[i].path);
            m[MAX_PATH + 199] = 0;
            say(TX[T_TITLE], m, 1);
            return 1;
        }
        total += ENT[i].size;
    }
    //  제거 프로그램 = 이 설치 프로그램의 사본
    DWORD k = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (k && k < MAX_PATH && _wcsicmp(self, uninst)) CopyFileW(self, uninst, FALSE);
    write_uninstall_entry(uninst, app, total / 1024 + 1);
    wchar_t lnk[MAX_PATH];
    if (shortcut_path(lnk)) make_shortcut(lnk, app);
    //  앱 실행 (설치한 사람 권한 그대로)
    if (!NOLAUNCH) ShellExecuteW(NULL, L"open", app, NULL, DIR, SW_SHOWNORMAL);
    if (SILENT) return 0;
    //  끝: 확장 넣는 법 (+ secrets.json 이 없으면 알림)
    wchar_t ad[MAX_PATH], sec[MAX_PATH], note[600] = L"";
    if (known(&FOLDERID_RoamingAppData, ad)) {
        join(sec, ad, L"toku-rpc-native\\secrets.json");
        if (GetFileAttributesW(sec) == INVALID_FILE_ATTRIBUTES) { _snwprintf(note, 599, TX[T_SECRETS], sec); note[599] = 0; }
    }
    wchar_t body[2400];
    _snwprintf(body, 2399, TX[T_DONE_SUB], ext);
    body[2399] = 0;
    wcsncat(body, note, 2399 - wcslen(body));
    TASKDIALOG_BUTTON b[2] = { { BTN_FOLDER, TX[T_EXT_FOLDER] }, { BTN_OK, TX[T_CLOSE] } };
    TASKDIALOGCONFIG c;
    ZeroMemory(&c, sizeof c);
    c.cbSize = sizeof c;
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    c.pszWindowTitle = TX[T_TITLE];
    c.pszMainIcon = TD_INFORMATION_ICON;
    c.pszMainInstruction = TX[T_DONE];
    c.pszContent = body;
    c.cButtons = 2;
    c.pButtons = b;
    c.nDefaultButton = BTN_FOLDER;
    int btn = 0;
    if (FAILED(TaskDialogIndirect(&c, &btn, NULL, NULL))) MessageBoxW(NULL, body, TX[T_DONE], MB_OK | MB_ICONINFORMATION);
    else if (btn == BTN_FOLDER) ShellExecuteW(NULL, L"open", ext, NULL, NULL, SW_SHOWNORMAL);
    return 0;
}

// ── 제거 ──
static void run_hidden(const wchar_t *cmdline) {
    wchar_t c[1200];
    wcsncpy(c, cmdline, 1199);
    c[1199] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
static int uninstall(void) {
    int data = 0;
    if (!ask(TX[T_UN_ASK], TX[T_UN_SUB], TX[T_UN_BTN], TX[T_UN_DATA], &data)) return 2;
    if (!quit_running()) { say(TX[T_TITLE], TX[T_RUNNING], 1); return 1; }
    if (!TESTMODE) {
        //  부팅 자동 시작 (앱이 등록한 이름 — channel.h)
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, CH_RUN_NAME);
            RegCloseKey(k);
        }
        wchar_t sys[MAX_PATH], cmd[MAX_PATH + 120];
        GetSystemDirectoryW(sys, MAX_PATH);
        _snwprintf(cmd, MAX_PATH + 119, L"\"%ls\\schtasks.exe\" /Delete /TN \"%ls\" /F", sys, CH_TASK_NAME);
        cmd[MAX_PATH + 119] = 0;
        run_hidden(cmd);
    }
    wchar_t lnk[MAX_PATH];
    if (shortcut_path(lnk)) DeleteFileW(lnk);
    RegDeleteKeyW(HKEY_CURRENT_USER, uninst_key());
    wchar_t p[MAX_PATH];
    join(p, DIR, CH_EXE);
    DeleteFileW(p);
    join(p, DIR, L"chrome-ext");
    rmtree(p);
    if (data) {
        wchar_t ad[MAX_PATH];
        if (known(&FOLDERID_RoamingAppData, ad)) { join(p, ad, L"toku-rpc-native"); to_recycle_bin(p); }
    }
    //  나(제거 프로그램)는 돌고 있어 바로 못 지운다 → 잠시 뒤 지우고 빈 폴더도 정리
    wchar_t self[MAX_PATH];
    DWORD k2 = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (k2 && k2 < MAX_PATH) {
        wchar_t sys[MAX_PATH], cmd[MAX_PATH * 3 + 120];
        GetSystemDirectoryW(sys, MAX_PATH);
        _snwprintf(cmd, MAX_PATH * 3 + 119, L"\"%ls\\cmd.exe\" /d /c ping 127.0.0.1 -n 3 >nul & del /f /q \"%ls\" & rmdir \"%ls\"", sys, self, DIR);
        cmd[MAX_PATH * 3 + 119] = 0;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    say(TX[T_TITLE], TX[T_UN_DONE], 0);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pick_lang();
    int n = 0;
    wchar_t **av = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; av && i < n; i++) {
        if (!_wcsicmp(av[i], L"/S")) SILENT = 1;
        else if (!_wcsicmp(av[i], L"/NOLAUNCH")) NOLAUNCH = 1;
        else if (!_wcsicmp(av[i], L"/TEST")) TESTMODE = 1;
        else if (!_wcsicmp(av[i], L"/uninstall")) UNINSTALL = 1;
        else if (!_wcsnicmp(av[i], L"/D=", 3)) { wcsncpy(DIR, av[i] + 3, MAX_PATH - 1); DIR[MAX_PATH - 1] = 0; }
    }
    if (av) LocalFree(av);
    if (!DIR[0] && UNINSTALL) {   //  제거: 이 제거 프로그램이 있는 폴더가 설치 폴더
        DWORD k = GetModuleFileNameW(NULL, DIR, MAX_PATH);
        wchar_t *sl = (k && k < MAX_PATH) ? wcsrchr(DIR, L'\\') : NULL;
        if (sl) *sl = 0; else DIR[0] = 0;
    }
    if (!DIR[0]) {
        wchar_t la[MAX_PATH];
        if (!known(&FOLDERID_LocalAppData, la)) return 1;
        _snwprintf(DIR, MAX_PATH - 1, L"%ls\\Programs\\%ls", la, CH_PROG_DIR);
        DIR[MAX_PATH - 1] = 0;
    }
    size_t dl = wcslen(DIR);
    while (dl > 3 && (DIR[dl - 1] == L'\\' || DIR[dl - 1] == L'/')) DIR[--dl] = 0;
    if (UNINSTALL) return uninstall();
    if (!load_payload()) { wchar_t m[200]; _snwprintf(m, 199, TX[T_FAIL], L"payload"); m[199] = 0; say(TX[T_TITLE], m, 1); return 1; }
    return install();
}
