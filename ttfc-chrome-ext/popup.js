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
let connected = null;
function $status() {
  if (connected === null) return;   // 아직 응답 전 — data-i18n 기본값 유지
  const el = document.getElementById('status');
  el.className = connected ? 'status ok' : 'status err';
  el.innerHTML = `<div class="dot ${connected ? 'green' : 'red'}"></div><span></span>`;
  el.querySelector('span').textContent = T(connected ? 'popup.connected' : 'popup.notRunning');
}

chrome.runtime.sendMessage({ action: 'GET_STATUS' }, (res) => {
  connected = !!(res && res.connected);
  $status();
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
