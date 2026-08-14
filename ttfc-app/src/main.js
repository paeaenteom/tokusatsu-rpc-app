// ============================================================
//  TOKU RPC — Main Process (RPC 전용 앱 v4)
//
//  지원 사이트: 東映特撮ファンクラブ / TSUBURAYA IMAGINATION
//  사이트는 크롬 확장으로 보고, 이 앱은 백그라운드에서:
//   - 크롬 확장 WebSocket 브릿지 (port 7690)
//   - Discord RPC 송출
//   - 시청 기록 (다음 단계 리뉴얼 예정)
//   - 미니 창 (확장/Discord 상태 + RPC 설정)
//   - ttfc:// 프로토콜 (가챠 연동)
// ============================================================

const { app, BrowserWindow, ipcMain, Tray, Menu, nativeImage, shell, Notification } = require('electron');
const path = require('path');
const { execFile } = require('child_process');
const log = require('electron-log');
const Store = require('electron-store');

const DiscordRPC = require('./discord-rpc');
const ExtensionBridge = require('./extension-bridge');
const BingeLogger = require('./binge-webhook');
const WatchHistory = require('./watch-history');
const AppConsole = require('./app-console');
const i18n = require('./i18n');
const Updater = require('./updater');
const fastStart = require('./fast-start');
const { setupProtocol, handleDeepLink, findDeepLink } = require('./protocol');

// Discord 애플리케이션 ID (기본값 = TTFC 앱). 개인 설정으로 덮어쓸 수 있다.
const secrets = require('./secrets');
const DISCORD_APP_ID = secrets.appId('ttfc') || '946694629506555955';

log.transports.file.level = 'info';

// 콘솔 버퍼: electron-log 전체 + uncaughtException 후킹 (가장 먼저)
const appConsole = new AppConsole();
appConsole.hookAll();

log.info('=== TOKU RPC 시작 ===');

// 미니 창은 400×780 짜리 정적 다크 패널이다. GPU 가속으로 얻을 게 없는데
// 창을 안 열어도 gpu-process 가 70MB 를 잡고 있었다(실측). 끄면 그만큼 돌려받는다.
// ⚠ app.whenReady() 전에 불러야 효과가 있다.
app.disableHardwareAcceleration();

// Windows 는 이 값으로 "창 ↔ 앱" 을 연결한다. 없으면 작업표시줄 버튼이 우리 앱으로
// 인식되지 않아 아이콘이 Electron 기본값으로 뜬다(실제로 그랬다).
// 보통은 설치 프로그램(NSIS)이 넣어 주는데, 이 프로젝트는 win-unpacked 를 그대로
// 배포하므로 여기서 직접 지정한다. ⚠ 창을 만들기 전에 불러야 한다.
if (process.platform === 'win32') app.setAppUserModelId('com.paeaenteom.toku-rpc');

// ── Store ──
const store = new Store({
    defaults: {
        rpc: {
            enabled: true,
            autoConnect: true,
            autoStart: true,        // PC 부팅 시 자동 실행 (최소화 상태)
            showSeries: true,
            showEpisode: true,
            showThumbnail: true,
            showButtons: true,
            timeMode: 'progress',   // progress | remaining | none
        },
        lang: 'auto',               // auto | ko | en | ja
        updateNotify: true,         // 새 버전 알림 받기
        fastStart: false,           // 로그온 직후 바로 실행 (작업 스케줄러)
        booster: false,             // 반응 속도 우선 (CPU 를 더 쓴다)
    },
});

// ── 전역 ──
let mainWindow = null;
let tray = null;
let discordRPC = null;
let extensionBridge = null;
let updater = null;
// 시청기록은 ttfc:// 딥링크로만 쓰인다. 앱 시작 때 만들면 안 쓰는 사람도
// 콜드 스타트마다 동기 파일 I/O 를 치른다 — 실제로 필요할 때 만든다.
let watchHistory = null;
const historyStore = () => (watchHistory || (watchHistory = new WatchHistory()));

// ── 언어 ──
//  'auto' 면 OS 언어를 따른다. app.getLocale() 은 whenReady 이후에만 정확하므로
//  실제 값은 currentLang() 이 불릴 때마다 계산한다.
function currentLang() {
    let osLocale = '';
    try { osLocale = app.getLocale(); } catch (e) { /* ready 전 */ }
    return i18n.resolve(store.get('lang'), osLocale);
}

function T(key, vars) {
    return i18n.t(currentLang(), key, vars);
}

// 언어가 바뀌면 이미 그려진 것들을 다시 그린다
function applyLanguage() {
    const lang = currentLang();
    if (discordRPC) discordRPC.setLang(lang);
    if (extensionBridge) extensionBridge.setLang(lang);   // 확장에도 알려준다
    rebuildTrayMenu();
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('lang-changed', { lang, strings: i18n.table(lang) });
    }
    log.info('[i18n] 언어 적용:', lang, `(설정: ${store.get('lang')})`);
}

// ── RPC 설정 헬퍼 ──
function rpcSettings() {
    return {
        showTime: store.get('rpc.timeMode') !== 'none',
        timeMode: store.get('rpc.timeMode'),
        showSeries: store.get('rpc.showSeries'),
        showEpisode: store.get('rpc.showEpisode'),
        showThumbnail: store.get('rpc.showThumbnail'),
        showButtons: store.get('rpc.showButtons'),
    };
}

// ── 부스터 ──
//  켜면 확장이 상태를 더 자주 살펴 반응이 빨라진다. 대신 CPU 를 더 쓴다.
//  실제 주기 값은 확장이 정하고, 앱은 켜짐/꺼짐만 알려준다.
function applyBooster() {
    const on = !!store.get('booster');
    if (extensionBridge) extensionBridge.setBooster(on);
    log.info('[Booster]', on ? '켜짐 (반응 우선)' : '꺼짐 (기본)');
}

// ── 빠른 시작 ──
//  Run 키로 등록하면 Windows 가 일부러 늦게 띄운다. 작업 스케줄러의 로그온
//  트리거는 그 대기열을 타지 않아 로그온과 거의 동시에 뜬다.
//  둘을 같이 켜면 두 번 실행되므로, 빠른 시작을 켜면 Run 키는 지운다.
async function applyFastStart(enabled) {
    if (process.platform !== 'win32') return false;
    if (enabled) {
        const ok = await fastStart.enable(process.execPath);
        if (ok) applyAutoStart(false);      // Run 키 제거 — 중복 실행 방지
        else applyAutoStart(isAutoStartEnabled());  // 실패하면 원래 방식으로 되돌린다
        return ok;
    }
    await fastStart.disable();
    applyAutoStart(isAutoStartEnabled());   // Run 키 복구
    return true;
}

// ── 업데이트 알림 ──
function notifyUpdate(info) {
    log.info('[Update] 알림 표시:', info.tag);
    try {
        if (!Notification.isSupported()) return;
        const n = new Notification({
            title: T('update.title'),
            body: T('update.body', { version: info.tag }),
            icon: loadIcon(['icon.png', 'icon.ico']),
            silent: false,
        });
        n.on('click', () => { try { shell.openExternal(info.url); } catch (e) {} });
        n.show();
    } catch (e) {
        log.warn('[Update] 알림 실패:', e.message);
    }
    rebuildTrayMenu();   // 트레이에 "새 버전" 항목이 뜨게
    pushStatus();
}

// ── 아이콘 ──
//  후보를 앞에서부터 시도해 비어 있지 않은 첫 이미지를 쓴다.
//  아이콘이 통째로 비면 Windows 가 기본 아이콘을 그려서 "아이콘이 없다"로 보인다.
//  어느 파일도 못 읽으면 로그에 남긴다 — 조용히 넘어가면 원인을 못 찾는다.
function loadIcon(names) {
    for (const name of names) {
        const p = path.join(__dirname, '..', 'assets', name);
        try {
            const img = nativeImage.createFromPath(p);
            if (img && !img.isEmpty()) return img;
            log.warn('[Icon] 비어 있음:', name);
        } catch (e) {
            log.warn('[Icon] 읽기 실패:', name, e.message);
        }
    }
    log.error('[Icon] 아이콘을 하나도 못 읽었다 — 기본 아이콘으로 표시된다:', names.join(', '));
    return nativeImage.createEmpty();
}

// ── 미니 창 ──
//  startHidden=true 면 창을 만들되 표시하지 않음 (부팅 자동 시작 시 트레이만)
function createWindow(startHidden = false) {
    const icon = loadIcon(['icon.ico', 'icon.png']);

    mainWindow = new BrowserWindow({
        width: 400,
        height: 780,
        resizable: true,
        minWidth: 380,
        minHeight: 640,
        maximizable: false,
        fullscreenable: false,
        show: !startHidden,     // 자동 시작 시 숨긴 채로 (트레이 상주)
        title: 'TOKU RPC',
        icon,
        autoHideMenuBar: true,
        backgroundColor: '#0d1117',
        webPreferences: {
            preload: path.join(__dirname, 'rpc-preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
    });

    mainWindow.loadFile(path.join(__dirname, 'rpc-window.html'));

    // 창을 닫으면 트레이로 (앱은 계속 돈다).
    //  ⚠ 예전엔 hide() 로 숨기기만 했다. 그러면 렌더러 프로세스가 그대로 살아
    //    트레이에만 있는 동안에도 계속 메모리를 물고 있었다 (실측 렌더러 95MB).
    //    시작할 때 창을 아예 안 만드는 최적화(아래 launchedHidden 분기)를 해 놓고도,
    //    한 번 열었다 닫으면 그 이득이 통째로 사라졌다.
    //    → 실제로 파괴하고, 다시 열 때 showWindow() 가 새로 만든다.
    //      (첫 실행과 똑같은 경로라 동작은 그대로다. window-all-closed 는 no-op 이라
    //       창이 없어져도 앱은 종료되지 않는다)
    mainWindow.on('closed', () => { mainWindow = null; });
}

// ── 트레이 ──
function createTray() {
    // 트레이는 16~32px로 그려진다. 256px짜리 icon.ico 를 주면 Windows 가 줄이면서
    // 오르카·사슬 디테일이 뭉개져 알아볼 수 없게 된다.
    // 그래서 그 크기에 맞춰 만들어 둔 tray-icon.png 를 먼저 쓴다.
    const icon = loadIcon(['tray-icon.png', 'icon.ico', 'icon.png']);
    tray = new Tray(icon);
    tray.setToolTip('TOKU RPC');
    rebuildTrayMenu();

    tray.on('double-click', () => showWindow());
}

function rebuildTrayMenu() {
    if (!tray) return;
    const rpcOn = store.get('rpc.enabled');
    const menu = Menu.buildFromTemplate([
        { label: '🎬 TOKU RPC', enabled: false },
        { type: 'separator' },
        { label: T('tray.open'), click: () => showWindow() },
        {
            label: rpcOn ? T('tray.rpcOn') : T('tray.rpcOff'),
            click: () => toggleRPC(),
        },
        { label: T('tray.reconnect'), click: () => reconnectRPC() },
        // 새 버전이 있을 때만 보인다
        ...(updater && updater.latest ? [
            { type: 'separator' },
            {
                label: T('tray.update', { version: updater.latest.tag }),
                click: () => { try { shell.openExternal(updater.latest.url); } catch (e) {} },
            },
        ] : []),
        { type: 'separator' },
        {
            label: isAutoStartEnabled() ? T('tray.autoStartOn') : T('tray.autoStartOff'),
            click: () => toggleAutoStart(),
        },
        { type: 'separator' },
        { label: T('tray.quit'), click: () => { app.isQuitting = true; app.quit(); } },
    ]);
    tray.setContextMenu(menu);
}

function showWindow() {
    // 닫으면 파괴되므로 여기서 다시 만든다. isDestroyed 는 'closed' 가 아직 안 온
    // 찰나를 대비한 방어 — 그 상태의 창에 show() 를 부르면 예외가 난다.
    if (!mainWindow || mainWindow.isDestroyed()) createWindow();
    else { mainWindow.show(); mainWindow.focus(); }
}

// ── RPC 제어 ──
function toggleRPC() {
    const newState = !store.get('rpc.enabled');
    store.set('rpc.enabled', newState);
    if (newState) {
        discordRPC.connect();
    } else {
        discordRPC.clearActivity();
        discordRPC.disconnect();
    }
    rebuildTrayMenu();
    pushStatus();
}

function reconnectRPC() {
    if (!discordRPC) return;
    discordRPC.disconnect();
    setTimeout(() => discordRPC.connect(), 1000);
}

// ── 부팅 시 자동 시작 (최소화) ──
function isAutoStartEnabled() {
    return store.get('rpc.autoStart', true);
}

// OS 로그인 항목에 등록/해제. '--hidden' 인자로 최소화(트레이) 상태로 부팅한다.
//  ⚠ Windows: Electron의 setLoginItemSettings는 인자를 붙일 때 exe 경로를
//    따옴표로 감싸지 않는다. 설치 경로에 공백이 있어("TOKU RPC") 부팅 시
//    실행이 실패하므로, Run 키를 직접 기록해 경로를 반드시 따옴표로 감싼다.
const WIN_RUN_KEY = 'HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run';
const WIN_RUN_NAME = 'TOKU RPC';
const WIN_RUN_LEGACY = 'electron.app.TOKU RPC';  // 구버전(따옴표 없음) 항목

function applyAutoStart(enabled) {
    if (process.platform === 'darwin') {
        try {
            app.setLoginItemSettings({ openAtLogin: enabled, openAsHidden: true, args: ['--hidden'] });
        } catch (e) {
            log.warn('[AutoStart] 설정 실패:', e.message);
        }
        return;
    }
    if (process.platform !== 'win32') return;

    // 구버전이 남긴 깨진 항목은 항상 제거 (없으면 조용히 실패)
    execFile('reg', ['delete', WIN_RUN_KEY, '/v', WIN_RUN_LEGACY, '/f'], () => {});

    if (enabled) {
        const cmd = `"${process.execPath}" --hidden`;
        execFile('reg', ['add', WIN_RUN_KEY, '/v', WIN_RUN_NAME, '/t', 'REG_SZ', '/d', cmd, '/f'],
            (err) => {
                if (err) log.warn('[AutoStart] 등록 실패:', err.message);
                else log.info('[AutoStart] 부팅 자동 시작 등록:', cmd);
            });
    } else {
        execFile('reg', ['delete', WIN_RUN_KEY, '/v', WIN_RUN_NAME, '/f'],
            () => log.info('[AutoStart] 부팅 자동 시작 해제'));
    }
}

function toggleAutoStart() {
    const next = !isAutoStartEnabled();
    store.set('rpc.autoStart', next);
    applyAutoStart(next);
    rebuildTrayMenu();
}

// 이번 실행이 "부팅 자동 시작"인지 판별 (숨김 시작 여부)
function launchedHidden() {
    if (process.argv.includes('--hidden')) return true;
    try { return app.getLoginItemSettings().wasOpenedAtLogin === true; }
    catch (e) { return false; }
}

// ── 미니 창에 상태 전송 ──
function buildStatus() {
    const dc = discordRPC ? discordRPC.getStatus() : { connected: false, user: '', activity: null };
    return {
        extConnected: extensionBridge ? extensionBridge.clientCount > 0 : false,
        extClients: extensionBridge ? extensionBridge.clientCount : 0,
        dcConnected: dc.connected,
        dcUser: dc.user,
        activity: dc.activity,
        rpcEnabled: store.get('rpc.enabled'),
    };
}

function pushStatus() {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('rpc-status-update', buildStatus());
    }
}

// ── ttfc:// 프로토콜 (가챠 연동 → 시청기록 시작) ──
function handleWatchStart(showId) {
    // 시청기록은 다음 단계 리뉴얼 — 지금은 조용히 기록만
    historyStore().startRun(showId, '', '');
    log.info('[Protocol] watch start:', showId);
}

// ══════════════════════════════════
//  IPC (미니 창 ↔ main)
// ══════════════════════════════════

ipcMain.handle('rpc-get-status', () => buildStatus());

ipcMain.handle('rpc-get-settings', () => ({
    showSeries: store.get('rpc.showSeries'),
    showEpisode: store.get('rpc.showEpisode'),
    showThumbnail: store.get('rpc.showThumbnail'),
    showButtons: store.get('rpc.showButtons'),
    timeMode: store.get('rpc.timeMode'),
    version: app.getVersion(),
    lang: store.get('lang'),          // 설정값 그대로 ('auto' 포함)
    updateNotify: store.get('updateNotify'),
    fastStart: store.get('fastStart'),
    booster: store.get('booster'),
    update: updater ? updater.status() : null,
}));

// ── 새 기능 토글 ──
ipcMain.handle('feature-set', async (e, key, value) => {
    const v = !!value;
    if (key === 'updateNotify') {
        store.set('updateNotify', v);
        if (v) { updater.start(); updater.check(); } else { updater.stop(); }
        log.info('[Update] 알림', v ? '켜짐' : '꺼짐');
        return { ok: true, value: v };
    }
    if (key === 'booster') {
        store.set('booster', v);
        applyBooster();
        return { ok: true, value: v };
    }
    if (key === 'fastStart') {
        const ok = await applyFastStart(v);
        // 등록에 실패하면 설정을 켜진 것으로 남기지 않는다 — 화면과 실제가 어긋난다
        store.set('fastStart', ok && v);
        rebuildTrayMenu();
        return { ok, value: ok && v };
    }
    return { ok: false };
});

// 사용자가 직접 누른 확인은 알림이 꺼져 있어도 한다
ipcMain.handle('update-check', async () => {
    if (!updater) return null;
    await updater.check(true);
    rebuildTrayMenu();
    return updater.status();
});

ipcMain.handle('update-open', () => {
    const url = updater ? updater.status().url : '';
    if (url) { try { shell.openExternal(url); } catch (e) {} }
    return true;
});

// ── 언어 ──
ipcMain.handle('i18n-get', () => ({
    setting: store.get('lang'),
    lang: currentLang(),
    langs: i18n.LANGS,
    strings: i18n.table(currentLang()),
}));

ipcMain.handle('i18n-set', (e, setting) => {
    const next = (setting === 'auto' || i18n.LANGS.includes(setting)) ? setting : 'auto';
    store.set('lang', next);
    applyLanguage();
    return { setting: next, lang: currentLang(), strings: i18n.table(currentLang()) };
});

ipcMain.handle('rpc-set-setting', (e, key, value) => {
    const allowed = ['showSeries', 'showEpisode', 'showThumbnail', 'showButtons', 'timeMode'];
    if (allowed.includes(key)) {
        store.set('rpc.' + key, value);
    }
    return true;
});

ipcMain.handle('rpc-toggle', () => { toggleRPC(); return store.get('rpc.enabled'); });
ipcMain.handle('rpc-reconnect', () => { reconnectRPC(); return true; });

// ── 콘솔 (에러 확인용) ──
ipcMain.handle('console-get', () => appConsole.getAll());
ipcMain.handle('console-clear', () => { appConsole.clear(); return true; });

// ══════════════════════════════════
//  단일 인스턴스 + 프로토콜
// ══════════════════════════════════

const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
    app.quit();
} else {
    app.on('second-instance', (event, argv) => {
        const deepLink = findDeepLink(argv);
        if (deepLink) {
            showWindow();   // 창을 아직 안 만들었을 수 있다 (트레이로 시작한 경우)
            const parsed = handleDeepLink(deepLink, mainWindow);
            if (parsed && parsed.type === 'watch' && parsed.showId) {
                handleWatchStart(parsed.showId);
            }
        } else {
            showWindow();
        }
    });

    app.whenReady().then(() => {
        setupProtocol();

        // 콘솔 → 미니 창 실시간 push (에러 확인용)
        appConsole.onLine = (line) => {
            // 창이 보일 때만 보낸다. 숨겨져 있으면 아무도 안 보는 렌더러로
            // 줄마다 IPC + DOM 노드 생성 + 강제 레이아웃이 일어난다.
            // 유실은 없다 — main 의 버퍼는 계속 쌓이고, 창을 열면
            // loadConsole() 이 600줄을 통째로 다시 받아간다.
            if (mainWindow && !mainWindow.isDestroyed() && mainWindow.isVisible()) {
                mainWindow.webContents.send('console-line', line);
            }
        };

        log.info('[i18n] OS 언어:', app.getLocale(), '→', currentLang());

        // Discord RPC
        discordRPC = new DiscordRPC(DISCORD_APP_ID);
        discordRPC.setLang(currentLang());
        discordRPC.onStatusChange = () => pushStatus();
        if (store.get('rpc.enabled') && store.get('rpc.autoConnect')) {
            discordRPC.connect();
        }

        // 크롬 확장 브릿지 (+정주행 웹훅 로거)
        extensionBridge = new ExtensionBridge(discordRPC, rpcSettings, new BingeLogger(discordRPC));
        extensionBridge.setLang(currentLang());   // 확장이 붙으면 이 언어를 알려준다
        extensionBridge.onConnectionChange = () => pushStatus();
        extensionBridge.start();

        // 업데이트 확인기 (알림을 꺼놨으면 시작하지 않는다 — 요청도 안 나간다)
        updater = new Updater({
            currentVersion: app.getVersion(),
            isEnabled: () => !!store.get('updateNotify'),
            onFound: (info) => notifyUpdate(info),
        });
        if (store.get('updateNotify')) updater.start();

        applyBooster();

        // 부팅 자동 시작. 빠른 시작(작업 스케줄러)을 켰으면 Run 키는 쓰지 않는다.
        if (store.get('fastStart') && process.platform === 'win32') {
            // 설치 경로가 바뀌었을 수 있으니 매번 현재 경로로 다시 등록한다
            applyFastStart(true).then((ok) => { if (!ok) store.set('fastStart', false); });
        } else {
            applyAutoStart(isAutoStartEnabled());
        }
        // 트레이로 시작할 땐 창을 아예 만들지 않는다.
        //  창을 숨겨만 두면 렌더러와 GPU 프로세스가 계속 떠서 170MB 를 잡고 있는다
        //  (실측: 숨김 상태 309MB → 창 없이 139MB). 트레이나 딥링크로 열면
        //  showWindow() 가 그때 만들고, 그 뒤로는 예전과 완전히 같다.
        if (!launchedHidden()) createWindow(false);
        createTray();

        // 시작 시 ttfc:// 링크
        const startLink = findDeepLink(process.argv);
        if (startLink) {
            showWindow();   // 창이 아직 없을 수 있다 — 딥링크는 창을 띄우는 동작이다
            const parsed = handleDeepLink(startLink, mainWindow);
            if (parsed && parsed.type === 'watch' && parsed.showId) {
                handleWatchStart(parsed.showId);
            }
        }

        app.on('activate', () => {
            if (BrowserWindow.getAllWindows().length === 0) createWindow();
            else showWindow();
        });
    });
}

app.on('before-quit', () => {
    app.isQuitting = true;
    if (discordRPC) discordRPC.disconnect();
    if (extensionBridge) extensionBridge.stop();
});

// 트레이 앱이므로 창 다 닫혀도 종료 안 함
app.on('window-all-closed', () => {
    // 아무것도 안 함 (트레이 유지)
});
