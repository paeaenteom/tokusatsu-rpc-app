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

importScripts('locales/ko.js', 'locales/en.js', 'locales/ja.js', 'i18n.js');
const i18n = self.TOKU_I18N;

const WS_URL = 'ws://127.0.0.1:7690';
const ALARM_NAME = 'toku-rpc-keepalive';
const LOG = (...a) => console.log('[TOKU RPC/bg]', ...a);

// ── 썸네일 재호스팅 ──
//  imagination CDN은 CloudFront WAF로 외부 서버 페치를 막아 디스코드 프록시가
//  썸네일을 못 불러온다("?"). content.js가 브라우저 안에서 추출한 이미지 바이트를
//  이 워커가 그대로 앱(WS)으로 전달하고, 실제 업로드(catbox)와 사용은 앱(Node)이 한다.
//  ※ catbox 등은 확장의 chrome-extension Origin을 거부(412) → 확장에서 직접 못 올림.
//    Origin 헤더가 없는 Node 요청이어야 통과하므로 업로드는 앱에서 수행.

// ── 썸네일 정사각 패딩 (2026-08-14) ──
//  디스코드는 large_image 를 "정사각 틀에 꽉 채워(cover)" 그린다. 16:9 썸네일을
//  그대로 넘기면 좌우 43%가 잘려 나가 화면 한가운데만 남는다.
//  → 원본을 정사각 캔버스 한가운데에 통째로 얹고 남는 위아래는 투명하게 둔다.
//    투명 PNG 라 디스코드 카드 배경이 그대로 비쳐 여백이 티나지 않는다.
//  (PreMiD 도 YouTube 썸네일을 같은 이유로 320x320 캔버스에 얹어 보낸다)
//  ⚠ 실패하면 반드시 원본을 그대로 돌려준다 — 예전 동작(잘린 썸네일)이 유지될 뿐
//    썸네일이 사라지지는 않아야 한다.
const SQ_SIZE = 512;          // 캔버스 한 변
const SQ_SIZE_SMALL = 384;    // PNG 가 너무 크면 한 단계 줄인다
const SQ_MAX_BYTES = 900 * 1024;

// Blob → dataURL. 서비스 워커엔 FileReader 가 없어 직접 base64 로 만든다.
async function blobToDataUrl(blob) {
  const buf = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  const CHUNK = 0x8000;   // 한 번에 넘기면 인자 수 초과로 터진다
  for (let i = 0; i < buf.length; i += CHUNK) {
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + CHUNK));
  }
  return 'data:' + (blob.type || 'image/png') + ';base64,' + btoa(bin);
}

// 정사각 캔버스 한가운데에 원본 전체를 얹는다. 이미 정사각이면 null(=손대지 않음).
async function squarePad(blob, size) {
  const bmp = await createImageBitmap(blob);
  try {
    const w = bmp.width, h = bmp.height;
    if (!w || !h) throw new Error('빈 이미지');
    // 이미 정사각이면 재인코딩할 이유가 없다 (용량만 늘고 화질만 깎인다)
    if (Math.abs(w - h) / Math.max(w, h) < 0.02) return null;
    const s = size || SQ_SIZE;
    const scale = Math.min(s / w, s / h);
    const dw = Math.round(w * scale), dh = Math.round(h * scale);
    const cv = new OffscreenCanvas(s, s);
    cv.getContext('2d').drawImage(bmp, Math.round((s - dw) / 2), Math.round((s - dh) / 2), dw, dh);
    return await cv.convertToBlob({ type: 'image/png' });
  } finally {
    bmp.close();
  }
}

// dataURL(원본) → dataURL(정사각). 어떤 이유로든 실패하면 원본을 그대로 돌려준다.
//  bgUrl 이 있으면 그 배경 위에 얹어 합성한다 (디즈니+ 작품 로고).
async function toSquareDataUrl(dataUrl, bgUrl) {
  try {
    const blob = await (await fetch(dataUrl)).blob();
    if (bgUrl) {
      try {
        const out = await compositeSquare(await fetchImage(bgUrl), blob, SQ_SIZE);
        if (out) return await blobToDataUrl(out);
      } catch (e) {
        LOG('배경 합성 실패 — 로고만 사용:', e.message);
      }
    }
    let out = await squarePad(blob, SQ_SIZE);
    if (!out) return dataUrl;                       // 이미 정사각 — 그대로
    if (out.size > SQ_MAX_BYTES) {
      const small = await squarePad(blob, SQ_SIZE_SMALL);
      if (small) out = small;
    }
    return await blobToDataUrl(out);
  } catch (e) {
    LOG('정사각 변환 실패 — 원본 사용:', e.message);
    return dataUrl;
  }
}

// 배경 아트워크를 정사각에 꽉 채우고(cover) 그 위에 작품 로고를 얹는다.
//  디즈니+ 작품 페이지용. 로고만 넣으면 4.5:1 이라 카드에서 아주 작은 띠가 되는데,
//  배경을 깔면 정사각을 다 쓰면서 로고도 읽힌다 (디즈니+ 페이지 히어로와 같은 모양).
//  배경은 장식이라 잘려도 상관없다 — 잘리면 안 되는 건 로고 쪽이다.
async function compositeSquare(bgBlob, logoBlob, size) {
  const s = size || SQ_SIZE;
  const cv = new OffscreenCanvas(s, s);
  const ctx = cv.getContext('2d');

  const bg = await createImageBitmap(bgBlob);   // 실패하면 호출부가 로고만 쓰도록 던진다
  try {
    const k = Math.max(s / bg.width, s / bg.height);       // cover
    const w = bg.width * k, h = bg.height * k;
    ctx.drawImage(bg, (s - w) / 2, (s - h) / 2, w, h);
  } finally { bg.close(); }
  ctx.fillStyle = 'rgba(0,0,0,0.45)';                      // 로고가 묻히지 않게
  ctx.fillRect(0, 0, s, s);

  const fg = await createImageBitmap(logoBlob);
  try {
    const k = Math.min((s * 0.84) / fg.width, (s * 0.44) / fg.height);   // contain — 로고는 절대 안 자른다
    const w = fg.width * k, h = fg.height * k;
    ctx.drawImage(fg, (s - w) / 2, (s - h) / 2, w, h);
  } finally { fg.close(); }

  return await cv.convertToBlob({ type: 'image/png' });
}

async function fetchImage(url) {
  const r = await fetch(url, { credentials: 'omit' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const blob = await r.blob();
  if (!/^image\//.test(blob.type)) throw new Error('이미지 아님: ' + blob.type);
  return blob;
}

// 워커가 직접 이미지를 받아 정사각으로 만든다.
//  콘텐트 스크립트(page origin)는 CORS 에 막히지만, 워커는 host_permissions 로
//  받을 수 있다 — TTFC cloudfront · 디즈니+ bamgrid 썸네일이 이 경로를 탄다.
//  bgUrl 이 있으면 그 위에 얹어 합성한다. 배경을 못 받으면 조용히 로고만 쓴다.
async function fetchSquareBytes(url, bgUrl) {
  const blob = await fetchImage(url);
  if (bgUrl) {
    try {
      const out = await compositeSquare(await fetchImage(bgUrl), blob, SQ_SIZE);
      if (out) return await blobToDataUrl(out);
    } catch (e) {
      LOG('배경 합성 실패 — 로고만 사용:', e.message);
    }
  }
  const out = await squarePad(blob, SQ_SIZE);
  return await blobToDataUrl(out || blob);
}

// url → 배경 url. 앱이 NEED_THUMB 로 다시 요청할 때도 같은 합성본을 만들려면
// 배경이 뭐였는지 기억하고 있어야 한다 (그 요청엔 배경 정보가 안 실린다).
const bgFor = new Map();
function rememberBg(url, bgUrl) {
  if (!url || !bgUrl) return;
  if (bgFor.size > 60) bgFor.clear();
  bgFor.set(url, bgUrl);
}

// ── 탭 중재 (2026-07-30 버그 수정) ──
//  TTFC와 IMAGINATION 탭을 동시에 열면 두 탭이 각자 상태를 보내,
//  Discord 프레즌스가 두 작품 사이에서 깜빡이고 정주행 웹훅이 폭주했다.
//  → 한 번에 한 탭만 "주인"으로 삼아 그 탭의 상태만 앱으로 보낸다.
//    재생 중인 탭이 우선, 주인이 계속 재생 중이면 그대로 유지(깜빡임 방지).
// ── 탭 중재 (사이트별) ──
//  예전엔 전체에서 탭 하나만 주인으로 뽑아 나머지를 버렸다. 사이트가 둘 이상
//  열려 있으면 어느 쪽을 보여줄지 정할 방법이 마땅치 않아 계속 깜빡였다.
//  지금은 사이트마다 Discord 애플리케이션이 따로 있으므로, 사이트별로 하나씩
//  주인을 뽑아 각자 올린다 — 켜 둔 사이트가 전부 프로필에 뜬다(유빈 요청).
//  같은 사이트를 여러 탭에서 열었을 때의 깜빡임은 그대로 막는다.
const tabStates = new Map();      // tabId → { data, at, playing, site }
const ownerBySite = new Map();    // site → tabId
const TAB_STALE_MS = 6000;

function siteOfTab(id) {
  const s = tabStates.get(id);
  return (s && s.site) || '';
}

function dropStale() {
  const now = Date.now();
  for (const [id, s] of tabStates) if (now - s.at > TAB_STALE_MS) tabStates.delete(id);
  for (const [site, id] of ownerBySite) if (!tabStates.has(id)) ownerBySite.delete(site);
}

// 해당 사이트 안에서만 주인을 정한다. 규칙은 예전과 같다:
//  주인 자리는 최대한 유지하고, 주인이 재생 중이 아닌데 같은 사이트의 다른 탭이
//  재생 중일 때만 넘긴다. (둘 다 일시정지면 유지 — 안 그러면 매 갱신마다 뒤바뀐다)
function chooseOwner(site) {
  dropStale();
  const cur = ownerBySite.get(site);
  if (cur != null && tabStates.has(cur) && siteOfTab(cur) === site) {
    if (tabStates.get(cur).playing) return cur;
    let othersPlaying = false;
    for (const [id, s] of tabStates)
      if (id !== cur && s.site === site && s.playing) { othersPlaying = true; break; }
    if (!othersPlaying) return cur;
  }
  let bestId = null, best = null;
  for (const [id, s] of tabStates) {
    if (s.site !== site) continue;
    if (!best || (s.playing && !best.playing) ||
        (s.playing === best.playing && s.at > best.at)) { bestId = id; best = s; }
  }
  if (bestId == null) { ownerBySite.delete(site); return null; }
  if (bestId !== cur) {
    ownerBySite.set(site, bestId);
    LOG('주인 탭 변경 [' + site + '] →', bestId);
  }
  return bestId;
}

chrome.tabs.onRemoved.addListener((tabId) => {
  const site = siteOfTab(tabId);
  if (!tabStates.delete(tabId)) return;
  if (ownerBySite.get(site) === tabId) {
    ownerBySite.delete(site);
    // 같은 사이트의 다른 탭이 남아 있으면 그쪽이 이어받고, 없으면 그 사이트만 지운다
    if (chooseOwner(site) == null) {
      // ⚠ 2026-08-14 유령 프레즌스 수정.
      //   예전 코드: `if (chooseOwner(site) == null && connected)`
      //   그런데 `connected` 는 이 스코프에 없다 — onMessage 콜백 안의 지역 const 뿐이다.
      //   → ReferenceError 로 리스너가 그 자리에서 죽어 CLEAR 가 영영 안 나갔다.
      //   하필 단축평가 때문에 왼쪽이 true 인 경우, 즉 "CLEAR 를 보내야 하는 바로 그때"만
      //   터져서 아무도 못 알아챘다. (탭이 남아 있으면 오른쪽을 읽지도 않아 조용히 지나감)
      lastBySite.delete(site);   // 안 지우면 재연결 때 onopen 이 닫힌 탭 상태를 되올린다
      if (ws && ws.readyState === WebSocket.OPEN) rawSend({ type: 'CLEAR', site });
      else ensureConnected();
    }
  }
});

let ws = null;
// 사이트마다 따로 기억한다 — 재연결하면 켜져 있던 사이트를 전부 되살려야
// 하나만 남고 나머지가 사라지지 않는다
const lastBySite = new Map();   // site → 마지막 활동 데이터
let pendingData = null;         // 연결 대기 중 보낼 데이터 (최신 1건)
let reconnectTimer = null;

function ensureConnected() {
  if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) return;

  LOG('WebSocket 연결 시도...', WS_URL);
  try {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      LOG('WebSocket 연결됨 ✓');
      const pend = pendingData;
      pendingData = null;
      // 살아있는 사이트를 전부 다시 올린다 (하나만 복구되면 나머지가 사라진다)
      for (const [site, data] of lastBySite) { rawSend(data); LOG('재전송 [' + site + ']', data.type); }
      if (pend && !lastBySite.has(pend.site || '')) rawSend(pend);
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
          //  ① 워커가 직접 받아 본다 — CORS 를 안 타서 가장 확실하다
          fetchSquareBytes(msg.url, bgFor.get(msg.url))
            .then((dataUrl) => rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl }))
            .catch(() => {
              //  ② 실패하면 예전처럼 탭에 요청 (사이트 세션이 있어야 열리는 이미지)
              chrome.tabs.query({}, (tabs) => {
                for (const t of tabs) {
                  chrome.tabs.sendMessage(t.id, { action: 'NEED_THUMB', url: msg.url }).catch(() => {});
                }
              });
            });
        }
        else if (msg.type === 'BOOSTER') {
          // 앱이 부스터를 켜고 끔 — 콘텐트 스크립트가 storage 를 보고 주기를 바꾼다
          chrome.storage.local.set({ booster: !!msg.on });
        }
        else if (msg.type === 'LANG') {
          // 앱에서 언어가 바뀜 — 확장 설정이 '시스템 언어'면 이걸 따라간다
          chrome.storage.local.set({ appLang: msg.lang || '' });
        }
        else if (msg.type === 'CONNECTED') {
          LOG('앱 핸드셰이크 수신');
          if (msg.lang) chrome.storage.local.set({ appLang: msg.lang });
          chrome.storage.local.set({ booster: !!msg.booster });
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
    const site = msg.data.site || '';
    tabStates.set(tabId, {
      data: { ...msg.data },
      at: Date.now(),
      playing: !!msg.data.isPlaying,
      site,
    });
    const owner = chooseOwner(site);
    if (owner !== tabId) {
      // 같은 사이트의 다른 탭이 주인 → 이 탭 상태는 보내지 않음 (깜빡임·웹훅 폭주 방지)
      // 다른 사이트는 서로 막지 않는다 — 각자 자기 Discord 앱에 올라간다
      sendResponse({ ok: true, connected, muted: true });
      return true;
    }
    lastBySite.set(site, { ...msg.data });
    const sent = sendOrQueue({ ...msg.data });
    sendResponse({ ok: sent, connected });
  } else if (msg.action === 'CLEAR') {
    // 숨겨진 탭이 보내는 CLEAR가 "재생 중인 다른 탭"의 프레즌스를 지우면 안 된다
    const tabId = sender.tab ? sender.tab.id : -1;
    const site = siteOfTab(tabId) || (msg.site || '');
    tabStates.delete(tabId);
    if (ownerBySite.get(site) === tabId) ownerBySite.delete(site);
    const nextOwner = chooseOwner(site);
    if (nextOwner != null && nextOwner !== tabId) {
      LOG('CLEAR 무시 — 같은 사이트의 다른 탭이 살아있음', nextOwner);
      sendResponse({ ok: true, connected, muted: true });
      return true;
    }
    LOG('CLEAR 요청 [' + site + ']');
    lastBySite.delete(site);
    pendingData = null;
    if (connected) rawSend({ type: 'CLEAR', site });   // 그 사이트만 지운다
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
    if (!connected) {
      ensureConnected();
      sendResponse({ ok: false });
      return true;
    }
    // 보내기 전에 정사각으로 — 디스코드가 좌우를 잘라먹지 않게
    rememberBg(msg.url, msg.bgUrl);
    toSquareDataUrl(msg.dataUrl, msg.bgUrl || bgFor.get(msg.url)).then((dataUrl) => {
      sendResponse({ ok: rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl }) });
    });
  } else if (msg.action === 'THUMB_FETCH') {
    // 콘텐트 스크립트가 CORS 로 못 가져온 이미지 — 워커가 직접 받는다
    if (!connected) {
      ensureConnected();
      sendResponse({ ok: false });
      return true;
    }
    rememberBg(msg.url, msg.bgUrl);
    fetchSquareBytes(msg.url, msg.bgUrl || bgFor.get(msg.url))
      .then((dataUrl) => sendResponse({ ok: rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl }) }))
      .catch((e) => {
        LOG('워커 썸네일 페치 실패:', msg.url, e.message);
        sendResponse({ ok: false });
      });
  }
  return true;  // 비동기 sendResponse 위해 채널 유지
});

// ── 정주행 모드: 확장 아이콘 배지로 ON 표시 ──
function syncBingeBadge() {
  chrome.storage.local.get('bingeMode', (r) => {
    const on = !!r.bingeMode;
    chrome.action.setBadgeText({ text: on ? 'ON' : '' });
    chrome.action.setBadgeBackgroundColor({ color: '#FFC233' });
    chrome.action.setTitle({ title: on ? i18n.t('bg.titleOn') : 'TOKU RPC' });
  });
}
chrome.storage.onChanged.addListener((ch, area) => {
  if (area !== 'local') return;
  // 언어가 바뀌면 툴팁도 다시 쓴다 (i18n 이 먼저 갱신되도록 한 틱 뒤에)
  if (ch.bingeMode || ch.lang || ch.appLang) setTimeout(syncBingeBadge, 0);
});
i18n.refresh(() => syncBingeBadge());

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
