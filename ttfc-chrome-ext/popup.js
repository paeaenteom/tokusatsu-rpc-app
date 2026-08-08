chrome.runtime.sendMessage({ action: 'GET_STATUS' }, (res) => {
  const el = document.getElementById('status');
  if (res && res.connected) {
    el.className = 'status ok';
    el.innerHTML = '<div class="dot green"></div><span>TOKU RPC 앱 연결됨 ✓</span>';
  } else {
    el.className = 'status err';
    el.innerHTML = '<div class="dot red"></div><span>TOKU RPC 앱이 실행 중이 아닙니다</span>';
  }
});

// ── 정주행 모드 토글 ──
const bingeToggle = document.getElementById('bingeToggle');
chrome.storage.local.get('bingeMode', (r) => { bingeToggle.checked = !!r.bingeMode; });
bingeToggle.addEventListener('change', () => {
  chrome.storage.local.set({ bingeMode: bingeToggle.checked });
});

// ── 다음 화 이동 테스트 ──
//  영상이 끝날 때까지 기다리지 않고 같은 경로(다음 화 찾기 → 이동 → 재생/전체화면)를
//  즉시 실행해 동작을 눈으로 확인한다.
const testBtn = document.getElementById('testNext');
const testMsg = document.getElementById('testMsg');

testBtn.addEventListener('click', () => {
  testMsg.textContent = '확인 중…';
  chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
    const tab = tabs && tabs[0];
    if (!tab) { testMsg.textContent = '탭을 찾을 수 없습니다'; return; }
    chrome.tabs.sendMessage(tab.id, { action: 'TEST_NEXT' }, (res) => {
      if (chrome.runtime.lastError || !res) {
        testMsg.textContent = 'TTFC / IMAGINATION 영상 페이지에서 눌러주세요';
        return;
      }
      if (res.ok) {
        testMsg.textContent = '다음 화로 이동합니다 →';
        setTimeout(() => window.close(), 700);
      } else {
        testMsg.textContent = res.reason || '다음 화를 찾지 못했습니다';
      }
    });
  });
});
