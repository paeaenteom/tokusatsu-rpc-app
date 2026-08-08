// ============================================================
//  TOKU RPC - Deep Link Protocol Handler (SPA 대응)
//  ttfc:// URL → TTFC 콘텐츠로 이동 (스킴은 가챠 연동 호환을 위해 유지)
// ============================================================
//
//  지원 URL:
//    ttfc://show/12345    → 콘텐츠 페이지
//    ttfc://search/키워드  → 검색 페이지
//    ttfc://               → 홈
//
//  SPA 문제 해결:
//    TTFC는 SPA라서 loadURL()로 직접 콘텐츠 URL을 열면
//    앱이 초기화되지 않아 빈 페이지가 뜸.
//    → 이미 로딩된 상태면 executeJavaScript로 SPA 내 이동
//    → 콜드 스타트면 홈 로딩 후 SPA 내 이동
//
// ============================================================

const { app, shell } = require('electron');
const path = require('path');
const log = require('electron-log');

const TTFC_BASE = 'https://pc.tokusatsu-fc.jp';

// ── URL 안전 검증 ──
function isSafeTTFCUrl(url) {
    try {
        const parsed = new URL(url);
        return parsed.origin === TTFC_BASE;
    } catch { return false; }
}

function escapeJsString(str) {
    return str.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\r/g, '\\r');
}

// ── 프로토콜 등록 ──
function setupProtocol() {
    if (process.defaultApp) {
        if (process.argv.length >= 2) {
            app.setAsDefaultProtocolClient('ttfc', process.execPath, [path.resolve(process.argv[1])]);
        }
    } else {
        app.setAsDefaultProtocolClient('ttfc');
    }
    log.info('[Protocol] ttfc:// 프로토콜 등록 완료');
}

// ── URL 파싱 ──
function parseDeepLink(url) {
    if (!url) return null;

    // ttfc://watch/12345 (가챠에서 정주행 시작)
    const watchMatch = url.match(/ttfc:\/\/watch\/(\d+)/);
    if (watchMatch) {
        return {
            type: 'watch',
            showId: watchMatch[1],
            url: `${TTFC_BASE}/contents?contentId=${watchMatch[1]}&contentType=0`
        };
    }

    // ttfc://show/12345
    const showMatch = url.match(/ttfc:\/\/show\/(\d+)/);
    if (showMatch) {
        return {
            type: 'show',
            url: `${TTFC_BASE}/contents?contentId=${showMatch[1]}&contentType=0`
        };
    }

    // ttfc://search/키워드
    const searchMatch = url.match(/ttfc:\/\/search\/(.+)/);
    if (searchMatch) {
        return {
            type: 'search',
            url: `${TTFC_BASE}/search?keyword=${encodeURIComponent(searchMatch[1])}`
        };
    }

    // ttfc:// (기본)
    return { type: 'home', url: TTFC_BASE };
}

// ── 딥링크 처리 (SPA 대응) ──
function handleDeepLink(url, mainWindow) {
    if (!url) return null;

    const parsed = parseDeepLink(url);
    if (!parsed) return null;

    const targetUrl = parsed.url;

    // URL 안전 검증
    if (!isSafeTTFCUrl(targetUrl)) {
        log.warn(`[Protocol] 허용되지 않은 URL 차단: ${targetUrl}`);
        return null;
    }

    log.info(`[Protocol] 딥링크: ${url} → ${targetUrl} (type: ${parsed.type})`);

    // RPC 전용 앱: 사이트는 크롬에서 보므로 앱은 미니 창만 표시.
    // watch 타입은 main.js에서 showId로 시청기록 시작.
    // show/search/home 타입은 기본 브라우저(크롬)로 열어줌.
    if (parsed.type !== 'watch') {
        try { shell.openExternal(targetUrl); } catch (e) {}
    }

    // 미니 창 표시
    if (mainWindow && !mainWindow.isDestroyed()) {
        if (mainWindow.isMinimized()) mainWindow.restore();
        mainWindow.show();
        mainWindow.focus();
    }

    return parsed;
}

// ── argv에서 ttfc:// 찾기 ──
function findDeepLink(argv) {
    return argv.find(a => a.startsWith('ttfc://')) || null;
}

module.exports = { setupProtocol, handleDeepLink, findDeepLink, escapeJsString };
