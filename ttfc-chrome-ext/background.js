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

importScripts('locales/ko.js', 'locales/en.js', 'locales/ja.js', 'i18n.js', 'version-print.js', 'page-resolve.js');
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
// 캔버스 한 변. Discord 는 이 이미지를 카드에서 160px 안팎으로 그린다 — 512 는 필요 이상이고,
// 파일이 클수록 **Discord 가 그 URL 을 받아오는 시간**이 늘어 화면에 늦게 뜬다(실측 평균 280KB).
const SQ_SIZE = 384;
const SQ_SIZE_SMALL = 288;    // 그래도 크면 한 단계 더 줄인다
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
    // 이미 정사각이고 캔버스보다 크지 않으면 손대지 않는다 (재인코딩은 화질만 깎는다).
    //  ⚠ 크면 줄인다 — 예전엔 512x512 원본(329KB)을 그대로 올려 표시가 그만큼 늦었다.
    const square = Math.abs(w - h) / Math.max(w, h) < 0.02;
    if (square && w <= (size || SQ_SIZE)) return null;
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

  // 합성본은 배경이 정사각을 꽉 채워 투명한 곳이 없다 → PNG 를 쓸 이유가 없다.
  // JPEG 로 내보내면 용량이 1/4 로 줄어 업로드가 그만큼 빨리 끝난다(= 화면에 빨리 뜬다).
  // 레터박스 경로는 여백이 투명해야 하므로 그쪽은 계속 PNG 다.
  return await cv.convertToBlob({ type: 'image/jpeg', quality: 0.88 });
}

// i.ytimg.com 은 maxresdefault 가 없는 영상이 아주 많다 (실측: 살아 있는 영상 9개 중 5개가 404).
//  404 면 fetchImage 가 던지고 → 탭 재요청도 같은 404 → 썸네일이 영영 안 뜨고 로고로 떨어졌다.
//  ⚠ 대체본으로 hqdefault/sddefault 를 쓰면 안 된다. 그건 16:9 를 4:3 틀에 넣은 것이라
//    **검은 띠가 JPEG 에 구워져** 있다(픽셀 샘플링으로 확인: 위아래 행 밝기 0).
//    정사각 패딩까지 겹치면 이중 액자가 된다. mqdefault 만 정확히 16:9(320x180)이고 항상 200 이다.
function ytFallbackUrl(url) {
  if (!/^https:\/\/i\.ytimg\.com\/vi\//.test(url)) return '';
  const alt = url.replace(/\/(maxresdefault|sddefault|hq720)\.jpg$/, '/mqdefault.jpg');
  return alt === url ? '' : alt;
}

async function fetchImage(url) {
  const r = await fetch(url, { credentials: 'omit' });
  if (!r.ok) {
    const alt = ytFallbackUrl(url);
    if (alt) { LOG('썸네일 대체본으로 재시도:', alt); return fetchImage(alt); }
    throw new Error('HTTP ' + r.status);
  }
  const blob = await r.blob();
  if (!/^image\//.test(blob.type)) throw new Error('이미지 아님: ' + blob.type);
  return blob;
}

// 워커가 직접 이미지를 받아 정사각으로 만든다.
//  콘텐트 스크립트(page origin)는 CORS 에 막히지만, 워커는 host_permissions 로
//  받을 수 있다 — TTFC cloudfront · 디즈니+ bamgrid 썸네일이 이 경로를 탄다.
//  bgUrl 이 있으면 그 위에 얹어 합성한다. 배경을 못 받으면 조용히 로고만 쓴다.
//  { dataUrl: 정사각본(RPC 용), rawDataUrl: 원본(웹훅 첨부용) } 을 돌려준다.
async function fetchSquareBytes(url, bgUrl) {
  const blob = await fetchImage(url);
  const rawDataUrl = await blobToDataUrl(blob);
  if (bgUrl) {
    try {
      const out = await compositeSquare(await fetchImage(bgUrl), blob, SQ_SIZE);
      if (out) return { dataUrl: await blobToDataUrl(out), rawDataUrl };
    } catch (e) {
      LOG('배경 합성 실패 — 로고만 사용:', e.message);
    }
  }
  const out = await squarePad(blob, SQ_SIZE);
  return { dataUrl: out ? await blobToDataUrl(out) : rawDataUrl, rawDataUrl };
}

// ── 보충 기록 (2026-11-04) ──
//  모바일로 본 화는 확장이 못 본다 → 앱 '보충' 탭에 링크를 넣으면 앱이 RESOLVE_PAGE 로 묻는다.
//  워커가 페이지를 받아(크롬 VPN 경로 그대로 — 앱 쪽 Node 는 일본 사이트에 막힌다) 작품·화·썸네일을 돌려준다.
//  읽는 곳은 page-resolve.js 가 허락한 두 사이트의 에피소드 주소뿐이다.
const RESOLVE_TIMEOUT_MS = 15000;
const RESOLVE_IMG_MAX = 8 * 1024 * 1024;

async function resolveText(url) {
  const r = await fetch(url, { credentials: 'include', signal: AbortSignal.timeout(RESOLVE_TIMEOUT_MS) });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.text();
}

async function resolveThumb(url) {
  const r = await fetch(url, { credentials: 'omit', signal: AbortSignal.timeout(RESOLVE_TIMEOUT_MS) });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const buf = new Uint8Array(await r.arrayBuffer());
  if (!buf.length || buf.length > RESOLVE_IMG_MAX) throw new Error('크기 ' + buf.length);
  const type = self.TOKU_RESOLVE.sniffImage(buf);
  if (!type) throw new Error('이미지 아님');
  return blobToDataUrl(new Blob([buf], { type }));
}

async function resolvePage(msg) {
  const R = self.TOKU_RESOLVE;
  const id = String(msg.id || '').slice(0, 40);
  const c = R && R.classify(msg.url);
  if (!c) { rawSend({ type: 'RESOLVED', id, ok: false, error: 'badurl' }); return; }
  try {
    let info;
    if (c.site === 'ttfc') {
      //  작품명은 에피소드 페이지에 없다 — 같은 작품의 에피소드 목록 페이지에서 읽는다
      const [ep, list] = await Promise.all([resolveText(c.url), resolveText(c.listUrl).catch(() => '')]);
      info = R.fromTtfc(R.metaOf(ep), R.metaOf(list));
    } else {
      info = R.fromImagination(R.metaOf(await resolveText(c.url)));
    }
    if (!info.seriesName && !info.episodeNumber && !info.episodeTitle) {
      rawSend({ type: 'RESOLVED', id, ok: false, error: 'empty' });
      return;
    }
    let thumbDataUrl = '';
    if (info.thumbnail) {
      try { thumbDataUrl = await resolveThumb(info.thumbnail); } catch (e) { LOG('보충 썸네일 실패:', e.message); }
    }
    LOG('보충 페이지 읽음:', c.site, info.seriesName, info.episodeNumber);
    rawSend({ type: 'RESOLVED', id, ok: true, site: c.site, url: c.url, seriesName: info.seriesName,
      episodeNumber: info.episodeNumber, episodeTitle: info.episodeTitle, thumbnail: info.thumbnail, thumbDataUrl });
  } catch (e) {
    //  일본 밖에서 열면 TTFC 는 404, IMAGINATION 은 403 을 준다 (2026-11-04 실측) — VPN 이 꺼진 경우
    const blocked = /HTTP (403|404)/.test(e.message);
    LOG('보충 페이지 읽기 실패:', e.message);
    rawSend({ type: 'RESOLVED', id, ok: false, error: blocked ? 'blocked' : 'fetch' });
  }
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
const tabStates = new Map();      // tabId → { data, at, playing, active, win, site }
const ownerBySite = new Map();    // site → tabId
//  ⚠ 둘러보기 탭은 내용이 그대로면 10초(BROWSING_HEARTBEAT)마다만 보낸다. 예전 6초로는 그 사이
//    4초 동안 보고 있는 탭이 '죽은 탭' 으로 빠져, 뒤에 열어 둔 같은 사이트 탭이 카드를 가져갔다.
const TAB_STALE_MS = 15000;

function siteOfTab(id) {
  const s = tabStates.get(id);
  return (s && s.site) || '';
}

function dropStale() {
  const now = Date.now();
  for (const [id, s] of tabStates) if (now - s.at > TAB_STALE_MS) tabStates.delete(id);
  for (const [site, id] of ownerBySite) if (!tabStates.has(id)) ownerBySite.delete(site);
}

// 해당 사이트 안에서만 주인을 정한다.
//  순위: 재생 중 > 지금 창에서 보고 있는 탭. 순위가 같으면 지금 주인을 유지한다
//  (안 그러면 매 갱신마다 뒤바뀐다). 재생 중인 뒤쪽 에피소드 탭은 여전히 이긴다.
function chooseOwner(site) {
  dropStale();
  const cur = ownerBySite.get(site);
  const rank = (s) => (s.playing ? 2 : 0) + (s.active ? 1 : 0);
  let bestId = null, best = null;
  for (const [id, s] of tabStates) {
    if (s.site !== site) continue;
    const r = rank(s), br = best ? rank(best) : -1;
    if (r > br || (r === br && bestId !== cur && (id === cur || s.at > best.at))) { bestId = id; best = s; }
  }
  if (bestId == null) { ownerBySite.delete(site); return null; }
  if (bestId !== cur) {
    ownerBySite.set(site, bestId);
    LOG('주인 탭 변경 [' + site + '] →', bestId);
  }
  return bestId;
}

//  탭을 바꾸면 '보고 있는 탭' 표시를 옮긴다 (tabs 권한 없이 쓸 수 있다)
//  ⚠ 탭을 다른 창으로 끌어내면 저장해 둔 창 번호가 낡는다. 활성화된 탭은 창 번호도 같이 고친다
//    (안 그러면 옮긴 주인 탭이 '안 보는 탭' 이 돼 같은 사이트 다른 탭에 카드가 넘어갔다).
chrome.tabs.onActivated.addListener(({ tabId, windowId }) => {
  for (const [id, s] of tabStates) {
    if (id === tabId) { s.active = true; s.win = windowId; }
    else if (s.win === windowId) s.active = false;
  }
});
chrome.tabs.onAttached.addListener((tabId, { newWindowId }) => {
  const s = tabStates.get(tabId);
  if (s) s.win = newWindowId;
});

chrome.tabs.onRemoved.addListener((tabId) => {
  //  ⚠ 메모리 절약으로 버려졌거나 화면이 죽은 탭은 소식이 끊겨 dropStale 이 먼저 지운다.
  //    예전엔 그 뒤 탭을 닫아도 아무것도 안 해서 카드가 남았고, 앱이 다시 켜질 때 되살아났다.
  //    → 앱에 보이고 있는 상태가 이 탭 것이면 이미 잊었더라도 정리한다.
  let site = siteOfTab(tabId);
  if (!site) for (const [s, id] of lastTabBySite) if (id === tabId) { site = s; break; }
  const known = tabStates.delete(tabId);
  const shown = lastTabBySite.get(site) === tabId;
  if (!known && !shown) return;
  if (ownerBySite.get(site) === tabId || shown) {
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
      lastTabBySite.delete(site);
      if (ws && ws.readyState === WebSocket.OPEN) rawSend({ type: 'CLEAR', site });
      else ensureConnected();
    }
  }
});

let ws = null;
// 사이트마다 따로 기억한다 — 재연결하면 켜져 있던 사이트를 전부 되살려야
// 하나만 남고 나머지가 사라지지 않는다
const lastBySite = new Map();   // site → 마지막 활동 데이터
const lastTabBySite = new Map();   // site → lastBySite 에 든 상태를 보낸 탭
let pendingData = null;         // 연결 대기 중 보낼 데이터 (최신 1건)
let reconnectTimer = null;

// ── 진단 기록 (2026-10-17) ──
//  Chrome 을 다시 켠 뒤 앱에 안 붙는 원인(워커가 안 뜨나 / 떠도 연결이 막히나)을 가리려고,
//  워커 시작·연결 결과를 chrome.storage.local 'diagSw' 에 최근 20건만 남긴다 (콘솔은 디스크에 안 남는다).
//  같은 일이 되풀이되면 한 줄의 횟수만 올리고, 저장은 1분에 한 번까지 (연결 실패 5초 재시도로 쓰기가 쌓이지 않게).
const diagSw = [];
let diagSavedAt = 0;
function noteSw(ev) {
  const now = Date.now();
  const last = diagSw[diagSw.length - 1];
  if (last && last.ev === ev) {
    last.n++; last.last = new Date(now).toISOString();
    if (now - diagSavedAt < 60000) return;
  } else {
    diagSw.push({ ev, at: new Date(now).toISOString(), n: 1 });
    if (diagSw.length > 20) diagSw.shift();
  }
  diagSavedAt = now;
  try { chrome.storage.local.set({ diagSw: diagSw.slice() }).catch(() => {}); } catch (e) { /* 무시 */ }
}

function ensureConnected() {
  if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) return;

  LOG('WebSocket 연결 시도...', WS_URL);
  try {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      LOG('WebSocket 연결됨 ✓');
      noteSw('ws 연결됨');
      const pend = pendingData;
      pendingData = null;
      // 살아있는 사이트를 전부 다시 올린다 (하나만 복구되면 나머지가 사라진다)
      for (const [site, data] of lastBySite) { rawSend(data); LOG('재전송 [' + site + ']', data.type); }
      if (pend && !lastBySite.has(pend.site || '')) rawSend(pend);
    };

    ws.onclose = (e) => {
      LOG(`WebSocket 끊김 (code=${e.code}) — 재연결 대기`);
      noteSw('ws 끊김 code=' + e.code);
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
            .then((r) => rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl: r.dataUrl, rawDataUrl: r.rawDataUrl }))
            .catch(() => {
              //  ② 실패하면 예전처럼 탭에 요청 (사이트 세션이 있어야 열리는 이미지)
              chrome.tabs.query({}, (tabs) => {
                for (const t of tabs) {
                  chrome.tabs.sendMessage(t.id, { action: 'NEED_THUMB', url: msg.url }).catch(() => {});
                }
              });
            });
        }
        else if (msg.type === 'SET_BINGE') {
          //  앱 창·트레이에서 정주행 모드를 켜고 끔 — 저장값만 바꾸면 탭(content.js)·배지가 storage.onChanged 로 따라간다
          chrome.storage.local.set({ bingeMode: !!msg.on });
        }
        else if (msg.type === 'KEYBINDS') {
          //  영상 버튼 키 설정 — 앱이 원본이다. 받은 그대로 저장하면 탭(keybind.js)이 storage.onChanged 로 따라간다.
          //  처음 한 번: 예전 확장(설정 창이 페이지에 있던 판)에 만들어 둔 키가 있으면 앱에 먼저 넘긴다 (앱에 아무것도 없을 때만 앱이 가져감)
          chrome.storage.local.get(['keybinds', 'keybindsOn', 'keybindsFromApp'], (r) => {
            if (!r.keybindsFromApp && r.keybinds && typeof r.keybinds === 'object' && Object.keys(r.keybinds).length) {
              rawSend({ type: 'KEYBINDS_LOCAL', keybinds: r.keybinds, on: r.keybindsOn || {} });
            }
            chrome.storage.local.set({ keybinds: msg.keybinds || {}, keybindsOn: msg.on || {}, keybindsFromApp: true });
          });
        }
        else if (msg.type === 'KB_PICK') {
          //  앱 '키 설정' 탭의 버튼 고르기 — 그 사이트 탭을 앞으로 띄우고 고르기 모드로
          kbPick(msg);
        }
        else if (msg.type === 'KB_PICK_CANCEL') {
          if (kbPicking && kbPicking.reqId === msg.reqId) {
            chrome.tabs.sendMessage(kbPicking.tabId, { action: 'KB_PICK_CANCEL', reqId: msg.reqId }).catch(() => {});
            kbPicking = null;
          }
        }
        else if (msg.type === 'RESOLVE_PAGE') {
          //  앱 '보충' 탭 — 링크의 작품·화·썸네일을 읽어 돌려준다
          resolvePage(msg);
        }
        else if (msg.type === 'BOOSTER') {
          // 앱이 반응 속도를 바꿈 — 콘텐트 스크립트가 storage 를 보고 주기를 바꾼다
          //  level 이 없는 예전 앱이면 on 으로 단계를 짐작한다
          chrome.storage.local.set({
            booster: !!msg.on,
            boostLevel: msg.level || (msg.on ? 'high' : 'normal'),
          });
        }
        else if (msg.type === 'LANG') {
          // 앱에서 언어가 바뀜 — 확장 설정이 '시스템 언어'면 이걸 따라간다
          chrome.storage.local.set({ appLang: msg.lang || '' });
        }
        else if (msg.type === 'CONNECTED') {
          LOG('앱 핸드셰이크 수신');
          sendBingeState();   // 앱 창의 정주행 스위치가 지금 상태로 시작하게
          if (msg.lang) chrome.storage.local.set({ appLang: msg.lang });
          chrome.storage.local.set({
            booster: !!msg.booster,
            boostLevel: msg.level || (msg.booster ? 'high' : 'normal'),
          });
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
    noteSw('ws 생성 실패 ' + e.message);
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
      active: !!(sender.tab && sender.tab.active),
      win: sender.tab ? sender.tab.windowId : -1,
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
    lastTabBySite.set(site, tabId);
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
    lastTabBySite.delete(site);
    pendingData = null;
    if (connected) rawSend({ type: 'CLEAR', site });   // 그 사이트만 지운다
    sendResponse({ ok: true, connected });
  } else if (msg.action === 'GET_STATUS') {
    //  팝업을 열면 끊겨 있던 연결을 곧바로 다시 시도한다 (다음 알람 30초를 기다리지 않게)
    if (msg.reconnect && !connected) ensureConnected();
    //  불러온 순간의 파일 지문도 준다 — 팝업이 지금 디스크와 비교해 '새 버전 적용' 을 띄운다
    printJob.then(() => sendResponse({ connected, wsUrl: WS_URL, loadedPrint: loadedPrint || '' }));
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
  } else if (msg.action === 'KB_PICKED') {
    //  탭에서 버튼을 골랐다 → 앱으로 (생김새는 앱이 다시 거른다)
    if (kbPicking && kbPicking.reqId === msg.reqId) kbPicking = null;
    rawSend({ type: 'KEYBIND_PICKED', reqId: msg.reqId, site: msg.site, target: msg.target, label: msg.label || '' });
    sendResponse({ ok: true });
  } else if (msg.action === 'KB_PICK_CANCELED') {
    if (kbPicking && kbPicking.reqId === msg.reqId) kbPicking = null;
    rawSend({ type: 'KEYBIND_PICK_FAIL', reqId: msg.reqId, error: 'canceled' });
    sendResponse({ ok: true });
  } else if (msg.action === 'KB_FS') {
    //  눌렀더니 전체화면이 바뀐 키 — 앱이 기억해 두게
    rawSend({ type: 'KEYBIND_FS', site: msg.site, id: msg.id });
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
    // 보내기 전에 정사각으로 — 디스코드가 좌우를 잘라먹지 않게.
    //  ⚠ 원본도 같이 보낸다. 정주행 알림(웹훅)은 임베드에 그대로 붙는 이미지라
    //    정사각본을 쓰면 좌우에 빈 띠가 생겨 좁아 보인다. 그쪽은 원본이 맞다.
    rememberBg(msg.url, msg.bgUrl);
    toSquareDataUrl(msg.dataUrl, msg.bgUrl || bgFor.get(msg.url)).then((dataUrl) => {
      sendResponse({ ok: rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl, rawDataUrl: msg.dataUrl, site: msg.site || '' }) });
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
      .then((r) => sendResponse({ ok: rawSend({ type: 'THUMB_BYTES', url: msg.url, dataUrl: r.dataUrl, rawDataUrl: r.rawDataUrl, site: msg.site || '' }) }))
      .catch((e) => {
        LOG('워커 썸네일 페치 실패:', msg.url, e.message);
        sendResponse({ ok: false });
      });
  }
  return true;  // 비동기 sendResponse 위해 채널 유지
});

// ── 영상 버튼 키: 앱의 '버튼 고르기' 를 그 사이트 탭에 전한다 ──
//  고를 탭: 그 사이트 카드를 띄우고 있는 탭 → 앞에 있는 탭 → 최근에 본 탭 순. 탭에 키 스크립트가 없으면(새로고침 전) 다음 탭.
const KB_HOSTS = {
  ttfc: ['pc.tokusatsu-fc.jp'], imagination: ['imagination.m-78.jp'], youtube: ['www.youtube.com'],
  disneyplus: ['www.disneyplus.com'], chzzk: ['chzzk.naver.com'], danime: ['animestore.docomo.ne.jp'],
};
let kbPicking = null;   // { reqId, tabId }
async function kbPick(msg) {
  const reqId = String(msg.reqId || '');
  const hosts = KB_HOSTS[msg.site];
  let tabs = [];
  if (hosts) { try { tabs = await chrome.tabs.query({ url: hosts.map((h) => 'https://' + h + '/*') }); } catch (e) { tabs = []; } }
  const owner = lastTabBySite.get(msg.site);
  tabs.sort((a, b) => ((b.id === owner) - (a.id === owner)) || ((b.active ? 1 : 0) - (a.active ? 1 : 0)) || ((b.lastAccessed || 0) - (a.lastAccessed || 0)));
  for (const t of tabs) {
    let r = null;
    try { r = await chrome.tabs.sendMessage(t.id, { action: 'KB_PICK', reqId }); } catch (e) { r = null; }
    if (!r || !r.ok) continue;
    kbPicking = { reqId, tabId: t.id };
    rawSend({ type: 'KEYBIND_PICK_START', reqId });
    //  사용자가 바로 클릭할 수 있게 그 탭·창을 앞으로
    chrome.tabs.update(t.id, { active: true }).catch(() => {});
    chrome.windows.update(t.windowId, { focused: true }).catch(() => {});
    return;
  }
  rawSend({ type: 'KEYBIND_PICK_FAIL', reqId, error: 'notab' });
}
//  고르는 중에 그 탭을 닫으면 앱이 3분을 기다리지 않게
chrome.tabs.onRemoved.addListener((tabId) => {
  if (kbPicking && kbPicking.tabId === tabId) {
    rawSend({ type: 'KEYBIND_PICK_FAIL', reqId: kbPicking.reqId, error: 'canceled' });
    kbPicking = null;
  }
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
//  앱에 정주행 모드 상태 알리기 (팝업·앱·마지막 화 자동 꺼짐 어느 쪽으로 바뀌어도)
function sendBingeState() {
  chrome.storage.local.get('bingeMode', (r) => {
    if (ws && ws.readyState === WebSocket.OPEN) rawSend({ type: 'BINGE_STATE', on: !!r.bingeMode });
  });
}
chrome.storage.onChanged.addListener((ch, area) => {
  if (area !== 'local') return;
  // 언어가 바뀌면 툴팁도 다시 쓴다 (i18n 이 먼저 갱신되도록 한 틱 뒤에)
  if (ch.bingeMode || ch.lang || ch.appLang) setTimeout(syncBingeBadge, 0);
  if (ch.bingeMode) sendBingeState();
});
i18n.refresh(() => syncBingeBadge());

// ── keepalive 알람 ──
chrome.alarms.create(ALARM_NAME, { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name !== ALARM_NAME) return;
  if (ws && ws.readyState === WebSocket.OPEN) rawSend({ type: 'PING' });
  else { LOG('알람: 연결 끊김 상태 → 재연결'); ensureConnected(); }
});

// ── 새 버전 적용 (2026-10-15) ──
//  압축 해제로 로드한 확장은 파일을 바꿔도 다시 불러오기 전까지 옛 코드로 돈다.
//  불러온 순간의 파일 지문(version-print.js)을 저장해 두고, 디스크 지문과 달라지면
//   · 지원 사이트 탭이 하나도 없으면 → 스스로 다시 불러온다 (끊길 탭이 없어 보이는 변화가 없다)
//   · 열려 있으면 → 팝업이 '새 버전 적용' 을 띄운다 (누르면 다시 불러오고 그 탭들을 새로고침)
//  ⚠ 'chrome_update' 등 다른 이유의 onInstalled 에서는 지문을 새로 저장하지 않는다 —
//    그때 도는 워커는 디스크 것이 아니라 마지막으로 불러온 코드라서, 저장하면 새 버전을 놓친다.
const UPDATE_ALARM = 'toku-rpc-update-check';
const AUTO_RELOAD_GAP_MS = 10 * 60 * 1000;   // 자동 적용이 되풀이되지 않게 (10분에 한 번까지)
let loadedPrint = '';
let printFresh = false;            // 이번에 불러오며 새로 잰 지문이면 저장소의 옛 값으로 덮지 않는다
let printJob = chrome.storage.local.get('loadedPrint')
  .then((r) => { if (!printFresh) loadedPrint = (r && r.loadedPrint) || ''; })
  .catch(() => {});

function siteUrlPatterns() {
  const set = new Set();
  for (const cs of chrome.runtime.getManifest().content_scripts || []) for (const m of cs.matches || []) set.add(m);
  return [...set];
}

async function rememberLoadedPrint() {
  const p = await self.TOKU_PRINT();
  printFresh = true;
  loadedPrint = p;
  await chrome.storage.local.set({ loadedPrint: p });
  LOG('불러온 코드 지문 저장', p ? p.slice(0, 8) : '(못 읽음)');
  refreshTabsIfAsked().catch((e) => LOG('사이트 탭 새로고침 실패', e && e.message));
}

//  팝업의 '새 버전 적용' 으로 다시 불러왔다면 열린 사이트 탭을 새로고침해 새 content script 를 붙인다.
//  (다시 불러오면 그 탭들의 옛 스크립트는 끊겨, 새로고침 전까지 카드가 안 뜬다)
async function refreshTabsIfAsked() {
  const r = await chrome.storage.local.get('refreshTabsAfterUpdate');
  if (!r.refreshTabsAfterUpdate) return;
  await chrome.storage.local.remove('refreshTabsAfterUpdate');
  if (Date.now() - r.refreshTabsAfterUpdate > 60000) return;   // 오래된 표시는 무시
  const tabs = await chrome.tabs.query({ url: siteUrlPatterns() });
  for (const t of tabs) {
    if (t.discarded) continue;   // 버려진 탭은 다시 열 때 어차피 새로 불러온다
    chrome.tabs.reload(t.id).catch(() => {});
  }
  LOG('새 버전 적용 — 사이트 탭 새로고침', tabs.filter((t) => !t.discarded).length + '개');
}

async function checkUpdate() {
  await printJob;
  if (!loadedPrint) return;                    // 불러온 지문을 모르면 판단하지 않는다
  const now = await self.TOKU_PRINT();
  if (!now || now === loadedPrint) return;
  //  파일을 고치는 도중(여러 파일 중 일부만 바뀜)에 불러오지 않게, 같은 새 지문을 두 번 연속(5분 간격) 볼 때만 적용한다
  const r = await chrome.storage.local.get(['autoReloadAt', 'seenPrint']);
  const seen = r.seenPrint || {};
  if (seen.p !== now) { await chrome.storage.local.set({ seenPrint: { p: now, at: Date.now() } }); return; }
  if (Date.now() - (seen.at || 0) < 4 * 60 * 1000) return;
  const tabs = await chrome.tabs.query({ url: siteUrlPatterns() });
  if (tabs.length) return;                     // 보고 있는 탭을 끊지 않는다 — 팝업에서 적용
  if (Date.now() - (r.autoReloadAt || 0) < AUTO_RELOAD_GAP_MS) return;
  await chrome.storage.local.set({ autoReloadAt: Date.now() });
  LOG('새 버전 자동 적용 — 열린 사이트 탭이 없음');
  chrome.runtime.reload();
}

//  워커가 다시 깨어날 때마다 알람을 새로 만들면 주기가 처음부터 다시 세어져 영영 안 울릴 수 있다 → 없을 때만
chrome.alarms.get(UPDATE_ALARM, (a) => { if (!a) chrome.alarms.create(UPDATE_ALARM, { periodInMinutes: 5 }); });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === UPDATE_ALARM) checkUpdate().catch((e) => LOG('새 버전 확인 실패', e && e.message));
});

chrome.runtime.onStartup.addListener(() => { LOG('onStartup'); noteSw('onStartup'); ensureConnected(); });
chrome.runtime.onInstalled.addListener((d) => {
  LOG('onInstalled', d && d.reason);
  noteSw('onInstalled ' + (d && d.reason));
  ensureConnected();
  if (d && (d.reason === 'install' || d.reason === 'update')) {
    printJob = rememberLoadedPrint().catch((e) => LOG('지문 저장 실패', e && e.message));
  }
});

//  진단 한 줄 때문에 워커 맨 위에서 예외가 나면 아래 연결까지 다 멈춘다 → 어떤 경우에도 던지지 않게
try { noteSw('워커 시작 v' + chrome.runtime.getManifest().version); } catch (e) { /* 무시 */ }
ensureConnected();
LOG('서비스 워커 시작됨');
