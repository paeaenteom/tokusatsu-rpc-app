const i18n = self.TOKU_I18N;
const T = (k, v) => i18n.t(k, v);

// ── 문자열 채우기 ──
//  i18n 은 chrome.storage 를 비동기로 읽으므로, 준비되면 다시 한 번 칠한다.
function paint() {
  i18n.apply(document);
  document.documentElement.lang = i18n.lang;
  $status();
  document.getElementById('footer').textContent =
    `TOKU RPC v${chrome.runtime.getManifest().version} ${T('popup.bridge')}`;
}

// ── 앱 연결 상태 ──
//  connected: null(확인 중) · true · false.  stuck: 확장 안쪽(서비스 워커)이 아예 답을 안 한다.
//  ⚠ Chrome 이 업데이트된 뒤 확장 서비스 워커가 멈춰, 앱이 켜져 있어도 다시 불러오기 전까지
//    연결이 안 된 적이 있다 (2026-10-14 실측: Chrome 155 로 바뀐 뒤 3시간 넘게 연결 0회, 다시 불러오자 즉시 연결).
//    팝업은 서비스 워커와 따로 돌아서 그 상태에서도 열린다 → 여기서 고칠 수 있게 '다시 불러오기' 를 둔다.
let connected = null;
let stuck = false;
function $status() {
  const fix = document.getElementById('fixBox');
  const el = document.getElementById('status');
  if (stuck) {
    el.className = 'status err';
    el.innerHTML = '<div class="dot red"></div><span></span>';
    el.querySelector('span').textContent = T('popup.swStuck');
    fix.hidden = false;
    return;
  }
  if (connected === null) return;   // 아직 응답 전 — data-i18n 기본값 유지
  el.className = connected ? 'status ok' : 'status err';
  el.innerHTML = `<div class="dot ${connected ? 'green' : 'red'}"></div><span></span>`;
  el.querySelector('span').textContent = T(connected ? 'popup.connected' : 'popup.notRunning');
  fix.hidden = connected;            // 앱이 켜져 있는데도 안 붙으면 다시 불러오기로 고칠 수 있다
}

//  서비스 워커에 상태를 묻는다. 2초 안에 답이 없으면 멈춘 것으로 본다 (cb(null)).
function askStatus(reconnect, cb) {
  let done = false;
  const finish = (r) => { if (done) return; done = true; clearTimeout(timer); cb(r); };
  const timer = setTimeout(() => finish(null), 2000);
  try {
    chrome.runtime.sendMessage({ action: 'GET_STATUS', reconnect }, (res) => {
      finish(chrome.runtime.lastError ? null : (res || null));
    });
  } catch (e) { finish(null); }
}

// ── 새 버전 ──
//  팝업은 열 때마다 디스크에서 새로 읽힌다(다시 불러오기 불필요 — Chrome 문서). 그래서 확장 파일을 바꾼 뒤
//  아무도 다시 불러오지 않았어도 이 코드는 늘 최신이다. 서비스 워커가 '불러온 순간의 지문' 을 주면
//  지금 디스크 지문(version-print.js)과 비교하고, 그 값을 아예 안 주면(지문을 모르는 예전 워커) 새 버전으로 본다.
function checkUpdate(res) {
  if (!res) return;                 // 워커가 멈춤 — 다시 불러오기 칸이 따로 뜬다
  const show = () => { document.getElementById('updateBox').hidden = false; };
  if (!('loadedPrint' in res)) { show(); return; }
  if (!res.loadedPrint || typeof self.TOKU_PRINT !== 'function') return;   // 모르면 띄우지 않는다
  self.TOKU_PRINT().then((p) => { if (p && p !== res.loadedPrint) show(); });
}

askStatus(true, (res) => {
  checkUpdate(res);
  if (!res) { stuck = true; $status(); return; }
  connected = !!res.connected;
  $status();
  //  방금 다시 붙여 봤으니 잠시 뒤 한 번 더 확인한다
  if (!connected) setTimeout(() => askStatus(false, (r) => { if (r) { connected = !!r.connected; $status(); } }), 1500);
});

//  확장 다시 불러오기 — chrome://extensions 의 새로고침과 같다. 팝업은 닫힌다.
document.getElementById('reloadExt').addEventListener('click', () => {
  try { chrome.runtime.reload(); } catch (e) { /* 무시 */ }
});

//  새 버전 적용 — 다시 불러오면 이 팝업은 닫힌다. 새 워커가 이 표시를 보고 열린 사이트 탭을 새로고침한다
//  (안 하면 그 탭들의 옛 스크립트가 끊긴 채 남아 새로고침 전까지 카드가 안 뜬다).
document.getElementById('applyUpdate').addEventListener('click', () => {
  chrome.storage.local.set({ refreshTabsAfterUpdate: Date.now() }, () => {
    try { chrome.runtime.reload(); } catch (e) { /* 무시 */ }
  });
});

// ── 정주행 모드 토글 ──
const bingeToggle = document.getElementById('bingeToggle');
chrome.storage.local.get('bingeMode', (r) => { bingeToggle.checked = !!r.bingeMode; });
bingeToggle.addEventListener('change', () => {
  chrome.storage.local.set({ bingeMode: bingeToggle.checked });
});

// ── 언어 ──
const langSelect = document.getElementById('langSelect');
chrome.storage.local.get('lang', (r) => { langSelect.value = r.lang || 'auto'; });
langSelect.addEventListener('change', () => {
  chrome.storage.local.set({ lang: langSelect.value });
  // storage 반영 → i18n 이 갱신 → onChange 로 다시 칠해진다
});
i18n.onChange(() => paint());

// ── 다음 화 이동 테스트 ──
//  영상이 끝날 때까지 기다리지 않고 같은 경로(다음 화 찾기 → 이동 → 재생/전체화면)를
//  즉시 실행해 동작을 눈으로 확인한다.
const testBtn = document.getElementById('testNext');
const testMsg = document.getElementById('testMsg');

testBtn.addEventListener('click', () => {
  testMsg.textContent = T('popup.testChecking');
  chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
    const tab = tabs && tabs[0];
    if (!tab) { testMsg.textContent = T('popup.testNoTab'); return; }
    chrome.tabs.sendMessage(tab.id, { action: 'TEST_NEXT' }, (res) => {
      if (chrome.runtime.lastError || !res) {
        testMsg.textContent = T('popup.testWrongPage');
        return;
      }
      if (res.ok) {
        testMsg.textContent = T('popup.testMoving');
        setTimeout(() => window.close(), 700);
      } else {
        // content 가 보내는 reason 은 이미 그 탭의 언어로 번역돼 있다
        testMsg.textContent = res.reason || T('popup.testNotFound');
      }
    });
  });
});

// i18n 이 저장소를 읽고 나서 실제 언어로 다시 칠한다
i18n.refresh(() => paint());
paint();
