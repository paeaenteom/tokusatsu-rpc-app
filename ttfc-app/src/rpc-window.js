// ============================================================
//  TOKU RPC 미니 창 — 렌더러
// ============================================================

const $ = (id) => document.getElementById(id);

// ── 상태 갱신 ──
function applyStatus(s) {
  // 크롬 확장
  if (s.extConnected) {
    $('extDot').className = 'dot on';
    $('extStatus').textContent = `연결됨 (${s.extClients})`;
  } else {
    $('extDot').className = 'dot off';
    $('extStatus').textContent = '대기 중';
  }

  // Discord
  if (s.dcConnected) {
    $('dcDot').className = 'dot on';
    $('dcStatus').textContent = s.dcUser || '연결됨';
  } else {
    $('dcDot').className = 'dot off';
    $('dcStatus').textContent = '연결 안 됨';
  }

  // 현재 RPC 표시 내용
  const a = s.activity;
  const preview = $('preview');
  if (a && a.details) {
    preview.classList.remove('empty');
    $('prevApp').textContent = a.smallImageText || 'TOKU RPC';
    $('prevDetails').textContent = a.details || '';
    $('prevState').textContent = a.state || '';

    const thumb = $('prevThumb');
    if (a.largeImageKey && a.largeImageKey.startsWith('http')) {
      thumb.innerHTML = `<img src="${a.largeImageKey}" style="width:100%;height:100%;border-radius:8px;object-fit:cover" onerror="this.parentElement.textContent='🎬'">`;
    } else {
      thumb.textContent = '🎬';
    }

    if (a.startTimestamp && a.endTimestamp) {
      const now = Math.floor(Date.now() / 1000);
      const cur = now - a.startTimestamp;
      const total = a.endTimestamp - a.startTimestamp;
      $('prevTime').textContent = `▶ ${fmt(cur)} / ${fmt(total)}`;
    } else if (a.endTimestamp) {
      const remain = a.endTimestamp - Math.floor(Date.now() / 1000);
      $('prevTime').textContent = `⏳ ${fmt(remain)} 남음`;
    } else if (a.startTimestamp) {
      const cur = Math.floor(Date.now() / 1000) - a.startTimestamp;
      $('prevTime').textContent = `🕐 ${fmt(cur)} 경과`;
    } else {
      $('prevTime').textContent = a.smallImageText || '';
    }
  } else {
    preview.classList.add('empty');
    $('prevThumb').textContent = '?';
    $('prevApp').textContent = 'TOKU RPC';
    $('prevDetails').textContent = '표시 중인 내용 없음';
    $('prevState').textContent = '';
    $('prevTime').textContent = '';
  }

  // RPC ON/OFF 메인 토글
  const mt = $('mainToggle');
  if (s.rpcEnabled) {
    mt.className = 'main-toggle on';
    mt.textContent = 'RPC 켜짐';
  } else {
    mt.className = 'main-toggle off';
    mt.textContent = 'RPC 꺼짐';
  }
}

function fmt(sec) {
  if (!sec || sec < 0) sec = 0;
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  return `${m}:${String(s).padStart(2, '0')}`;
}

// ════════════════════════════════════════
//  콘솔 (에러 확인용)
// ════════════════════════════════════════

const MAX_CONSOLE_LINES = 600;

function consoleAppend(line) {
  const box = $('console');
  const atBottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 24;

  const el = document.createElement('div');
  el.className = `ln lv-${line.level || 'info'}`;
  const ts = new Date(line.t || Date.now());
  const hh = String(ts.getHours()).padStart(2, '0');
  const mm = String(ts.getMinutes()).padStart(2, '0');
  const ss = String(ts.getSeconds()).padStart(2, '0');
  const tsSpan = document.createElement('span');
  tsSpan.className = 'ts';
  tsSpan.textContent = `${hh}:${mm}:${ss} `;
  el.appendChild(tsSpan);
  el.appendChild(document.createTextNode(line.msg || ''));
  box.appendChild(el);

  while (box.children.length > MAX_CONSOLE_LINES) box.removeChild(box.firstChild);
  if (atBottom) box.scrollTop = box.scrollHeight;
}

async function loadConsole() {
  const lines = await window.rpcAPI.getConsole();
  const box = $('console');
  box.innerHTML = '';
  (lines || []).forEach(consoleAppend);
  box.scrollTop = box.scrollHeight;
}

$('consoleClearBtn').addEventListener('click', async () => {
  await window.rpcAPI.clearConsole();
  $('console').innerHTML = '';
});

$('consoleCopyBtn').addEventListener('click', async () => {
  const lines = await window.rpcAPI.getConsole();
  const text = (lines || [])
    .map(l => `${new Date(l.t).toLocaleTimeString()} [${l.level}] ${l.msg}`)
    .join('\n');
  try { await navigator.clipboard.writeText(text); } catch (e) {}
  $('consoleCopyBtn').textContent = '복사됨!';
  setTimeout(() => { $('consoleCopyBtn').textContent = '복사'; }, 1200);
});

window.rpcAPI.onConsoleLine((line) => consoleAppend(line));

// ════════════════════════════════════════
//  설정 / 버튼
// ════════════════════════════════════════

async function loadSettings() {
  const cfg = await window.rpcAPI.getSettings();
  $('showSeries').checked = cfg.showSeries !== false;
  $('showEpisode').checked = cfg.showEpisode !== false;
  $('showThumbnail').checked = cfg.showThumbnail !== false;
  $('showButtons').checked = cfg.showButtons !== false;
  $('timeMode').value = cfg.timeMode || 'progress';
  $('ver').textContent = 'v' + (cfg.version || '4.2.1');
}

function bindSettings() {
  ['showSeries', 'showEpisode', 'showThumbnail', 'showButtons'].forEach(id => {
    $(id).addEventListener('change', () => {
      window.rpcAPI.setSetting(id, $(id).checked);
    });
  });
  $('timeMode').addEventListener('change', () => {
    window.rpcAPI.setSetting('timeMode', $('timeMode').value);
  });
}

$('mainToggle').addEventListener('click', () => window.rpcAPI.toggleRPC());
$('reconnectBtn').addEventListener('click', () => window.rpcAPI.reconnect());

// ── 실시간 상태 수신 ──
window.rpcAPI.onStatus((s) => applyStatus(s));

// ── 초기화 ──
(async () => {
  await loadSettings();
  bindSettings();
  await loadConsole();
  const s = await window.rpcAPI.getStatus();
  applyStatus(s);
})();

// 시간 표시 1초마다 갱신
setInterval(async () => {
  const s = await window.rpcAPI.getStatus();
  applyStatus(s);
}, 1000);
