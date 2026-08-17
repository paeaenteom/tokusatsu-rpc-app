// ============================================================
//  TOKU RPC 미니 창 — 렌더러
// ============================================================

const $ = (id) => document.getElementById(id);

// ════════════════════════════════════════
//  다국어
//   main 프로세스가 현재 언어의 문자열 표를 통째로 넘겨준다.
//   data-i18n="키" 가 붙은 요소는 applyStrings() 가 한 번에 채운다.
// ════════════════════════════════════════

let STR = {};

function t(key, vars) {
  let s = STR[key];
  if (s === undefined) return key;
  if (vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? String(vars[k]) : m));
  return s;
}

function applyStrings() {
  // 자동 해제 선택지는 data-i18n 이 아니라 JS 로 만든 <option> 이라 따로 다시 그린다
  const idle = document.getElementById('idleTimeout');
  if (idle && idle.options.length) renderIdleOptions(Number(idle.value));
  document.querySelectorAll('[data-i18n]').forEach((el) => {
    el.textContent = t(el.getAttribute('data-i18n'));
  });
  document.documentElement.lang = LANG;
}

let LANG = 'ko';

// ── 썸네일 ──
//  URL 이 바뀔 때만 손댄다. 예전엔 매초 innerHTML 로 <img> 를 부수고 새로 만들어,
//  같은 URL 인데도 파싱·레이아웃·페인트가 매초 일어나고 이미지 재검증까지 나갔다.
let lastThumbKey = null;
function setThumb(key) {
  const k = key || '';
  if (k === lastThumbKey) return;
  lastThumbKey = k;
  const box = $('prevThumb');
  if (k.startsWith('http')) {
    let img = box.querySelector('img');
    if (!img) {
      box.textContent = '';
      img = document.createElement('img');
      img.style.cssText = 'width:100%;height:100%;border-radius:8px;object-fit:cover';
      img.onerror = () => { box.textContent = '🎬'; lastThumbKey = null; };
      box.appendChild(img);
    }
    img.src = k;   // 속성 대입 — URL 이 HTML 로 해석되지 않는다
  } else {
    box.textContent = '🎬';
  }
}

// ── 상태 갱신 ──
function applyStatus(s) {
  // 크롬 확장
  if (s.extConnected) {
    $('extDot').className = 'dot on';
    $('extStatus').textContent = t('win.extConnected', { n: s.extClients });
  } else {
    $('extDot').className = 'dot off';
    $('extStatus').textContent = t('win.extWaiting');
  }

  // Discord
  if (s.dcConnected) {
    $('dcDot').className = 'dot on';
    $('dcStatus').textContent = s.dcUser || t('win.dcConnected');
  } else {
    $('dcDot').className = 'dot off';
    $('dcStatus').textContent = t('win.dcDisconnected');
  }

  // 현재 RPC 표시 내용
  const a = s.activity;
  const preview = $('preview');
  if (a && a.details) {
    preview.classList.remove('empty');
    $('prevApp').textContent = a.smallImageText || 'TOKU RPC';
    $('prevDetails').textContent = a.details || '';
    $('prevState').textContent = a.state || '';

    setThumb(a.largeImageKey);

    if (a.startTimestamp && a.endTimestamp) {
      const now = Math.floor(Date.now() / 1000);
      const cur = now - a.startTimestamp;
      const total = a.endTimestamp - a.startTimestamp;
      $('prevTime').textContent = `▶ ${fmt(cur)} / ${fmt(total)}`;
    } else if (a.endTimestamp) {
      const remain = a.endTimestamp - Math.floor(Date.now() / 1000);
      $('prevTime').textContent = '⏳ ' + t('win.timeLeft', { t: fmt(remain) });
    } else if (a.startTimestamp) {
      const cur = Math.floor(Date.now() / 1000) - a.startTimestamp;
      $('prevTime').textContent = '🕐 ' + t('win.timeElapsed', { t: fmt(cur) });
    } else {
      $('prevTime').textContent = a.smallImageText || '';
    }
  } else {
    preview.classList.add('empty');
    $('prevThumb').textContent = '?';
    $('prevApp').textContent = 'TOKU RPC';
    $('prevDetails').textContent = t('win.nothing');
    $('prevState').textContent = '';
    $('prevTime').textContent = '';
  }

  // RPC ON/OFF 메인 토글
  const mt = $('mainToggle');
  if (s.rpcEnabled) {
    mt.className = 'main-toggle on';
    mt.textContent = t('win.rpcOn');
  } else {
    mt.className = 'main-toggle off';
    mt.textContent = t('win.rpcOff');
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
  $('consoleCopyBtn').textContent = t('win.copied');
  setTimeout(() => { $('consoleCopyBtn').textContent = t('win.copy'); }, 1200);
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
  renderIdleOptions(cfg.idleTimeout);
  $('ver').textContent = 'v' + (cfg.version || '4.2.1');
  return cfg;
}

// 자동 해제 시간 선택지 — 0 은 '안 사라짐'
const IDLE_CHOICES = [0, 5, 30, 60, 180, 360, 720, 1440];
function idleLabel(min) {
  if (!min) return t('win.idleNever');
  if (min < 60) return t('win.idleMin', { n: min });
  return t('win.idleHour', { n: min / 60 });
}
function renderIdleOptions(current) {
  const sel = $('idleTimeout');
  const cur = current == null ? 5 : Number(current);
  sel.innerHTML = '';
  for (const m of IDLE_CHOICES) {
    const o = document.createElement('option');
    o.value = String(m);
    o.textContent = idleLabel(m);
    sel.appendChild(o);
  }
  sel.value = String(IDLE_CHOICES.includes(cur) ? cur : 5);
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
  $('idleTimeout').addEventListener('change', () => {
    window.rpcAPI.setSetting('idleTimeout', Number($('idleTimeout').value));
  });
}

// ── 앱 설정 토글 (업데이트 알림 / 빠른 시작 / 부스터) ──
function renderUpdate(u) {
  const el = $('updateState'), btn = $('updateBtn');
  if (u && u.latest) {
    el.textContent = t('win.updateFound', { version: u.latest });
    el.classList.add('found');
    btn.textContent = t('win.updateGet');
    btn.onclick = () => window.rpcAPI.openUpdate();
  } else {
    el.textContent = u && u.checkedAt ? t('win.updateLatest') : '';
    el.classList.remove('found');
    btn.textContent = t('win.updateCheck');
    btn.onclick = async () => { btn.disabled = true; renderUpdate(await window.rpcAPI.checkUpdate()); btn.disabled = false; };
  }
}

async function loadFeatures(cfg) {
  $('updateNotify').checked = cfg.updateNotify !== false;
  $('fastStart').checked = !!cfg.fastStart;
  $('booster').checked = !!cfg.booster;
  renderUpdate(cfg.update);
}

function bindFeatures() {
  ['updateNotify', 'fastStart', 'booster'].forEach((id) => {
    $(id).addEventListener('change', async () => {
      const box = $(id);
      box.disabled = true;
      const r = await window.rpcAPI.setFeature(id, box.checked);
      // 등록에 실패하면(빠른 시작 등) 화면을 실제 상태로 되돌린다 — 거짓말하지 않는다
      if (r && typeof r.value === 'boolean') box.checked = r.value;
      box.disabled = false;
    });
  });
}

// ── 언어 ──
async function loadLanguage() {
  const i = await window.rpcAPI.getI18n();
  STR = i.strings;
  LANG = i.lang;
  $('langSelect').value = i.setting || 'auto';
  applyStrings();
}

function bindLanguage() {
  $('langSelect').addEventListener('change', async () => {
    const r = await window.rpcAPI.setLang($('langSelect').value);
    STR = r.strings;
    LANG = r.lang;
    applyStrings();
    // 문자열이 코드로 들어가는 자리(상태·미리보기)도 즉시 다시 그린다
    applyStatus(await window.rpcAPI.getStatus());
  });

  // 트레이 등 다른 경로로 언어가 바뀐 경우
  window.rpcAPI.onLangChange(async (p) => {
    STR = p.strings;
    LANG = p.lang;
    applyStrings();
    applyStatus(await window.rpcAPI.getStatus());
  });
}

$('mainToggle').addEventListener('click', () => window.rpcAPI.toggleRPC());
$('reconnectBtn').addEventListener('click', () => window.rpcAPI.reconnect());

// ── 실시간 상태 수신 ──
window.rpcAPI.onStatus((s) => applyStatus(s));

// ── 초기화 ──
(async () => {
  await loadLanguage();     // 문자열부터 채우고 나머지를 그린다
  bindLanguage();
  const cfg = await loadSettings();
  bindSettings();
  await loadFeatures(cfg);
  bindFeatures();
  await loadConsole();
  const s = await window.rpcAPI.getStatus();
  applyStatus(s);
})();

// 창이 다시 보이면 그 순간 최신으로 맞춘다 (숨김 중 건너뛴 것을 메운다).
//  숨김 동안에는 console-line 도 안 오므로 콘솔은 통째로 다시 받는다 — 유실 없음.
document.addEventListener('visibilitychange', async () => {
  if (document.visibilityState !== 'visible') return;
  applyStatus(await window.rpcAPI.getStatus());
  await loadConsole();
});

// 시간 표시 1초마다 갱신.
//  창이 안 보이면 건너뛴다 — 아무도 안 보는 화면을 위해 매초 IPC 왕복과 DOM 갱신을
//  하고 있었다. 상태 변화 자체는 main 이 push 해 주므로(onStatus) 이 주기는
//  시계 숫자를 굴리는 용도다.
setInterval(async () => {
  if (document.visibilityState === 'hidden') return;
  const s = await window.rpcAPI.getStatus();
  applyStatus(s);
}, 1000);
