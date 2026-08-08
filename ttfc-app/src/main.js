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
const { setupProtocol, handleDeepLink, findDeepLink } = require('./protocol');

// Discord 애플리케이션 ID (기본값 = TTFC 앱). 개인 설정으로 덮어쓸 수 있다.
const secrets = require('./secrets');
const DISCORD_APP_ID = secrets.appId('ttfc') || '946694629506555955';

log.transports.file.level = 'info';

// 콘솔 버퍼: electron-log 전체 + uncaughtException 후킹 (가장 먼저)
const appConsole = new AppConsole();
appConsole.hookAll();

log.info('=== TOKU RPC 시작 ===');

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
    },
});

// ── 전역 ──
let mainWindow = null;
let tray = null;
let discordRPC = null;
let extensionBridge = null;
const watchHistory = new WatchHistory();

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

// ── 미니 창 ──
//  startHidden=true 면 창을 만들되 표시하지 않음 (부팅 자동 시작 시 트레이만)
function createWindow(startHidden = false) {
    let icon;
    try {
        icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'icon.ico'));
        if (icon.isEmpty()) throw new Error('empty');
    } catch (e) {
        icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'icon.png'));
    }

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
    let icon;
    try {
        icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'icon.ico'));
        if (icon.isEmpty()) throw new Error('empty');
    } catch (e) {
        icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'icon.png'));
    }
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
        { label: '창 열기', click: () => showWindow() },
        {
            label: rpcOn ? '✓ RPC 켜짐' : 'RPC 꺼짐',
            click: () => toggleRPC(),
        },
        { label: '🔁 Discord 재연결', click: () => reconnectRPC() },
        { type: 'separator' },
        {
            label: isAutoStartEnabled() ? '✓ 부팅 시 자동 시작' : '부팅 시 자동 시작',
            click: () => toggleAutoStart(),
        },
        { type: 'separator' },
        { label: '종료', click: () => { app.isQuitting = true; app.quit(); } },
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
    watchHistory.startRun(showId, '', '');
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
}));

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
            if (mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('console-line', line);
            }
        };

        // Discord RPC
        discordRPC = new DiscordRPC(DISCORD_APP_ID);
        discordRPC.onStatusChange = () => pushStatus();
        if (store.get('rpc.enabled') && store.get('rpc.autoConnect')) {
            discordRPC.connect();
        }

        // 크롬 확장 브릿지 (+정주행 웹훅 로거)
        extensionBridge = new ExtensionBridge(discordRPC, rpcSettings, new BingeLogger(discordRPC));
        extensionBridge.onConnectionChange = () => pushStatus();
        extensionBridge.start();

        // 부팅 자동 시작 등록(설정값 반영) + 이번 실행이 자동 시작이면 최소화로
        applyAutoStart(isAutoStartEnabled());
        createWindow(launchedHidden());
        createTray();

        // 시작 시 ttfc:// 링크
        const startLink = findDeepLink(process.argv);
        if (startLink) {
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
