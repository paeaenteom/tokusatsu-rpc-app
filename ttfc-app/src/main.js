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

const { app, BrowserWindow, ipcMain, Tray, Menu, nativeImage, shell } = require('electron');
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
    },
});

// ── 전역 ──
let mainWindow = null;
let tray = null;
let discordRPC = null;
let extensionBridge = null;
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

    // 창 닫으면 트레이로 (백그라운드 유지)
    mainWindow.on('close', (e) => {
        if (!app.isQuitting) {
            e.preventDefault();
            mainWindow.hide();
        }
    });
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
    if (!mainWindow) createWindow();
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
}));

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

        // 부팅 자동 시작 등록(설정값 반영) + 이번 실행이 자동 시작이면 최소화로
        applyAutoStart(isAutoStartEnabled());
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
