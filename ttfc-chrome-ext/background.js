// ============================================================
//  TOKU RPC — Background Service Worker
//  content.js → (이 워커) → WebSocket(127.0.0.1:7690) → TOKU RPC 앱 → Discord
//
//  ⚠ MV3 서비스 워커는 ~30초 비활성 시 종료되어 ws 변수가 사라진다.
//  대책:
//   1. chrome.alarms (30초) — 워커를 주기적으로 깨워 연결 점검 + PING
//   2. 메시지 전송 시마다 연결 확인 → 끊겨 있으면 재연결 + 큐 보관 후 flush
//   3. content.js 의 주기 전송이 워커를 계속 깨움
// ============================================================

const WS_URL = 'ws://127.0.0.1:7690';
const ALARM_NAME = 'toku-rpc-keepalive';
const LOG = (...a) => console.log('[TOKU RPC/bg]', ...a);

// ── 썸네일 재호스팅 ──
//  imagination CDN은 CloudFront WAF로 외부 서버 페치를 막아 디스코드 프록시가
//  썸네일을 못 불러온다("?"). content.js가 브라우저 안에서 추출한 이미지 바이트를
//  이 워커가 그대로 앱(WS)으로 전달하고, 실제 업로드(catbox)와 사용은 앱(Node)이 한다.
//  ※ catbox 등은 확장의 chrome-extension Origin을 거부(412) → 확장에서 직접 못 올림.
//    Origin 헤더가 없는 Node 요청이어야 통과하므로 업로드는 앱에서 수행.

// ── 탭 중재 (2026-07-30 버그 수정) ──
//  TTFC와 IMAGINATION 탭을 동시에 열면 두 탭이 각자 상태를 보내,
//  Discord 프레즌스가 두 작품 사이에서 깜빡이고 정주행 웹훅이 폭주했다.
//  → 한 번에 한 탭만 "주인"으로 삼아 그 탭의 상태만 앱으로 보낸다.
//    재생 중인 탭이 우선, 주인이 계속 재생 중이면 그대로 유지(깜빡임 방지).
const tabStates = new Map();   // tabId → { data, at, playing }
let ownerTab = null;
const TAB_STALE_MS = 6000;

function chooseOwner() {
  const now = Date.now();
  for (const [id, s] of tabStates) if (now - s.at > TAB_STALE_MS) tabStates.delete(id);
  // 주인 자리는 최대한 유지한다(깜빡임 방지).
  //  넘기는 경우는 둘뿐: ① 주인이 응답 없음(위에서 정리됨)
  //                     ② 주인은 재생 중이 아니고, 다른 탭이 재생 중일 때
  //  ⚠ "재생 중일 때만 유지"로 두면 두 탭이 모두 일시정지일 때 주인이
  //    매 갱신마다 뒤바뀌어 상태가 계속 깜빡인다(실측 설계 결함).
  if (ownerTab != null && tabStates.has(ownerTab)) {
    const cur = tabStates.get(ownerTab);
    if (cur.playing) return ownerTab;
    let othersPlaying = false;
    for (const [id, s] of tabStates) if (id !== ownerTab && s.playing) { othersPlaying = true; break; }
    if (!othersPlaying) return ownerTab;   // 아무도 재생 안 함 → 주인 유지
  }
  let bestId = null, best = null;
  for (const [id, s] of tabStates) {
    if (!best || (s.playing && !best.playing) ||
        (s.playing === best.playing && s.at > best.at)) { bestId = id; best = s; }
  }
  if (bestId !== ownerTab) {
    ownerTab = bestId;
    if (bestId != null) LOG('상태 주인 탭 변경 →', bestId, tabStates.get(bestId).data.site || '');
  }
  return ownerTab;
}

chrome.tabs.onRemoved.addListener((tabId) => {
  if (tabStates.delete(tabId) && ownerTab === tabId) { ownerTab = null; chooseOwner(); }
});

let ws = null;
let lastData = null;      // 마지막 활동 데이터 — 재연결 직후 재전송
let pendingData = null;   // 연결 대기 중 보낼 데이터 (최신 1건)
let reconnectTimer = null;

function ensureConnected() {
  if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) return;

  LOG('WebSocket 연결 시도...', WS_URL);
  try {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      LOG('WebSocket 연결됨 ✓');
      const data = pendingData || lastData;
      pendingData = null;
      if (data) { rawSend(data); LOG('연결 직후 상태 재전송:', data.type); }
    };

    ws.onclose = (e) => {
      LOG(`WebSocket 끊김 (code=${e.code}) — 재연결 대기`);
      ws = null;
      scheduleReconnect();
    };

    ws.onerror = () => { /* onclose에서 처리 */ };

    ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.type === 'PONG') LOG('PONG 수신 (연결 정상)');
        else if (msg.type === 'NEED_THUMB') {
          // 앱이 특정 이미지 바이트를 요청 (캐시 없음/만료/업로드 실패 복구)
          chrome.tabs.query({}, (tabs) => {
            for (const t of tabs) {
              chrome.tabs.sendMessage(t.id, { action: 'NEED_THUMB', url: msg.url }).catch(() => {});
            }
          });
        }
        else if (msg.type === 'CONNECTED') {
          LOG('앱 핸드셰이크 수신');
          // 앱 (재)시작 = 앱의 재호스팅 캐시 소실 → content의 "이미 보냄" 기록을
          // 리셋해 썸네일 바이트 재전송 유도 (안 하면 재시작 후 로고만 계속 뜸)
          chrome.tabs.query({}, (tabs) => {
            for (const t of tabs) {
              chrome.tabs.sendMessage(t.id, { action: 'RESET_THUMBS' }).catch(() => {});
            }
          });
        }
      } catch (err) { /* 무시 */ }
    };
  } catch (e) {
    LOG('WebSocket 생성 실패:', e.message);
    ws = null;
    scheduleReconnect();
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => { reconnectTimer = null; ensureConnected(); }, 5000);
}

function rawSend(data) {
  try { ws.send(JSON.stringify(data)); return true; }
  catch (e) { LOG('전송 실패:', e.message); return false; }
}

function sendOrQueue(data) {
  if (ws && ws.readyState === WebSocket.OPEN) return rawSend(data);
  LOG(`연결 안 됨 (ws=${ws ? ws.readyState : '없음'}) → 큐에 보관 + 재연결`);
  pendingData = data;
  ensureConnected();
  return false;
}

// ── content.js 메시지 수신 ──
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const connected = !!(ws && ws.readyState === WebSocket.OPEN);

  if (msg.action === 'UPDATE_STATE') {
    const tabId = sender.tab ? sender.tab.id : -1;
    tabStates.set(tabId, {
      data: { ...msg.data },
      at: Date.now(),
      playing: !!msg.data.isPlaying,
    });
    const owner = chooseOwner();
    if (owner !== tabId) {
      // 다른 탭이 주인 → 이 탭 상태는 보내지 않음 (깜빡임·웹훅 폭주 방지)
      sendResponse({ ok: true, connected, muted: true });
      return true;
    }
    lastData = { ...msg.data };
    const sent = sendOrQueue(lastData);
    sendResponse({ ok: sent, connected });
  } else if (msg.action === 'CLEAR') {
    // 숨겨진 탭이 보내는 CLEAR가 "재생 중인 다른 탭"의 프레즌스를 지우면 안 된다
    const tabId = sender.tab ? sender.tab.id : -1;
    tabStates.delete(tabId);
    if (ownerTab === tabId) ownerTab = null;
    const nextOwner = chooseOwner();
    if (nextOwner != null && nextOwner !== tabId) {
      LOG('CLEAR 무시 — 다른 탭이 재생 중', nextOwner);
      sendResponse({ ok: true, connected, muted: true });
      return true;
    }
    LOG('CLEAR 요청');
    lastData = null;
    pendingData = null;
    if (connected) rawSend({ type: 'CLEAR' });
    sendResponse({ ok: true, connected });
  } else if (msg.action === 'GET_STATUS') {
    sendResponse({ connected, wsUrl: WS_URL });
  } else if (msg.action === 'BINGE_FULLSCREEN') {
    // 정주행: 다음 화 이동 후 창을 전체화면으로 (유저 제스처 불필요한 windows API)
    if (sender.tab && sender.tab.windowId != null) {
      chrome.windows.update(sender.tab.windowId, { state: 'fullscreen' });
    }
    sendResponse({ ok: true });
  } else if (msg.action === 'BINGE_RESTORE') {
    if (sender.tab && sender.tab.windowId != null) {
      chrome.windows.update(sender.tab.windowId, { state: 'normal' });
    }
    sendResponse({ ok: true });
  } else if (msg.action === 'BINGE_END') {
    // 마지막 화 완주 → 앱이 "정주행 끝" 웹훅 전송
    sendOrQueue({ type: 'BINGE_END', seriesName: msg.seriesName || '', site: msg.site || '' });
    sendResponse({ ok: true });
  } else if (msg.action === 'THUMB_BYTES') {
    // 썸네일 바이트를 앱으로 전달 (앱이 catbox 업로드 + 사용)
    // 연결 안 됐으면 ok:false → content가 다음 틱에 재시도 (상태 큐를 안 뺏음)
    if (connected) {
      sendResponse({ ok: rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl: msg.dataUrl }) });
    } else {
      ensureConnected();
      sendResponse({ ok: false });
    }
  }
  return true;  // 비동기 sendResponse 위해 채널 유지
});

// ── 정주행 모드: 확장 아이콘 배지로 ON 표시 ──
function syncBingeBadge() {
  chrome.storage.local.get('bingeMode', (r) => {
    const on = !!r.bingeMode;
    chrome.action.setBadgeText({ text: on ? 'ON' : '' });
    chrome.action.setBadgeBackgroundColor({ color: '#FFC233' });
    chrome.action.setTitle({ title: on ? 'TOKU RPC — 정주행 모드 ON' : 'TOKU RPC' });
  });
}
chrome.storage.onChanged.addListener((ch, area) => {
  if (area === 'local' && ch.bingeMode) syncBingeBadge();
});
syncBingeBadge();

// ── keepalive 알람 ──
chrome.alarms.create(ALARM_NAME, { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name !== ALARM_NAME) return;
  if (ws && ws.readyState === WebSocket.OPEN) rawSend({ type: 'PING' });
  else { LOG('알람: 연결 끊김 상태 → 재연결'); ensureConnected(); }
});

chrome.runtime.onStartup.addListener(() => { LOG('onStartup'); ensureConnected(); });
chrome.runtime.onInstalled.addListener(() => { LOG('onInstalled'); ensureConnected(); });

ensureConnected();
LOG('서비스 워커 시작됨');
