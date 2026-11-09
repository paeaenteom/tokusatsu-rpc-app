// ============================================================
//  영상 버튼 키 (지원 사이트 전부, 사이트마다 따로) — 2026-11-07 유빈 요청
//
//  플레이어의 버튼(이전·10초·재생·다음·음량·설정·전체화면·스킵 …)마다 키를 하나씩 붙인다.
//  설정(이름·키·삭제·켜고 끄기)은 TOKU RPC 앱 창의 '키 설정' 탭에서 한다 — 앱이 원본을 갖고 KEYBINDS 로 내려보내면
//  background.js 가 chrome.storage 에 두고, 이 스크립트는 그걸 읽어 키를 누를 때 그 버튼을 찾아 누른다.
//  이 페이지에서 하는 일은 '버튼 고르기' 하나: 앱이 부르면(KB_PICK) 안내 띠를 띄우고, 사용자가 클릭한 버튼의
//  생김새(태그·클래스·이름표·글자·아이콘·위치)를 앱으로 돌려보낸다. 사이트 구조를 미리 알 수 없어 정해 둔 선택자를 쓰지 않는다.
//
//  저장(앱이 내려준 그대로): chrome.storage.local
//    keybinds   = { [사이트 id]: [{ id, label, code, ctrl, alt, shift, meta, fs?, target }] }
//    keybindsOn = { [사이트 id]: false }   (없으면 켜짐)
//  키는 e.code(자판 위치)로 기억한다 — 한글 입력 상태여도 같은 키로 동작한다.
//  글자 입력 칸에 있을 때·영상 페이지가 아닐 때는 키를 가로채지 않는다.
// ============================================================
(function () {
  'use strict';
  const SITE = window.TOKU_SITE;
  if (!SITE || window.__tokuKeybind) return;
  window.__tokuKeybind = true;
  const I18N = self.TOKU_I18N;
  const T = (k, v) => (I18N ? I18N.t(k, v) : k);
  const STORE = 'keybinds';
  const ON_KEY = 'keybindsOn';

  let binds = [];
  let enabled = true;

  // ── 앱이 내려준 설정 읽기 ──
  function readAll(cb) {
    try { chrome.storage.local.get([STORE, ON_KEY], (r) => cb(r || {})); } catch (e) { cb({}); }
  }
  function applyStored(r) {
    const all = r[STORE] && typeof r[STORE] === 'object' ? r[STORE] : {};
    binds = Array.isArray(all[SITE.id]) ? all[SITE.id].filter((b) => b && b.id && b.target && b.target.tag) : [];
    const on = r[ON_KEY] && typeof r[ON_KEY] === 'object' ? r[ON_KEY] : {};
    enabled = on[SITE.id] !== false;
  }
  readAll(applyStored);
  try {
    chrome.storage.onChanged.addListener((ch, area) => {
      if (area !== 'local' || (!ch[STORE] && !ch[ON_KEY])) return;
      readAll(applyStored);
    });
  } catch (e) { /* 확장이 다시 불러와져 끊긴 옛 스크립트 */ }
  function send(m) {
    try { const p = chrome.runtime.sendMessage(m); if (p && p.catch) p.catch(() => {}); } catch (e) { /* 끊긴 옛 스크립트 */ }
  }
  //  눌렀더니 전체화면이 바뀐 키 — 바로 이 탭에서 쓰고, 앱에도 알려 저장하게 한다
  function markFs(b) {
    b.fs = true;
    send({ action: 'KB_FS', site: SITE.id, id: b.id });
  }

  // ── 버튼 기억하기 ──
  //  상태에 따라 붙었다 떨어지는 클래스(재생 중·마우스 올림 등)는 버튼을 알아보는 데 쓰지 않는다
  const STATE_CLS = /^(?:vjs-(?:playing|paused|ended|user-(?:in)?active|hover|has-started|waiting|seeking|scrubbing|fullscreen|lock-showing|hidden|disabled)|is-|has-)|(?:^|[-_])(?:active|hover|hovered|focus|focused|selected|disabled|show|shown|hide|hidden|visible|open|opened|on|off|current)$/i;
  const INTERACTIVE = 'button, [role="button"], a[href], input[type="button"], input[type="submit"], summary';
  const INLINE = /^(?:SPAN|I|B|EM|STRONG|IMG|PICTURE|SMALL|LABEL)$/;

  function stableClasses(el) {
    return [...(el.classList || [])].filter((c) => c.length <= 60 && !STATE_CLS.test(c)).slice(0, 12);
  }
  function textOf(el) { return (el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 40); }
  //  아이콘만 있는 버튼(이전·다음·10초)을 서로 구별하는 지문
  function iconSig(el) {
    const img = el.querySelector('img');
    if (img) return 'img:' + (img.getAttribute('src') || '').split('?')[0].split('/').pop().slice(0, 60);
    const use = el.querySelector('use');
    if (use) return 'use:' + (use.getAttribute('href') || use.getAttribute('xlink:href') || '').slice(0, 60);
    const p = el.querySelector('path');
    if (p) return 'path:' + (p.getAttribute('d') || '').replace(/\s+/g, ' ').slice(0, 48);
    return '';
  }
  //  위치 — 자기와 위로 4단계 부모가 형제 중 몇 번째인지
  function pathOf(el) {
    const out = [];
    for (let n = el, i = 0; n && n.parentElement && i < 5; n = n.parentElement, i++) {
      out.push(n.tagName.toLowerCase() + ':' + Array.prototype.indexOf.call(n.parentElement.children, n));
    }
    return out;
  }
  function describe(el) {
    return {
      tag: el.tagName.toLowerCase(),
      cls: stableClasses(el),
      id: el.id && !/\d{3,}/.test(el.id) ? el.id : '',
      testid: el.getAttribute('data-testid') || '',
      aria: (el.getAttribute('aria-label') || '').slice(0, 60),
      title: (el.getAttribute('title') || '').slice(0, 60),
      text: textOf(el),
      icon: iconSig(el),
      path: pathOf(el),
      uniq: uniqClasses(el),
      ctx: ctxOf(el),
    };
  }
  //  같은 줄의 형제 버튼에는 없는 클래스 (예: play, vjs-mute-control) — 있으면 이게 그 버튼의 이름표다
  function uniqClasses(el) {
    const p = el.parentElement;
    const sib = p ? [...p.children].filter((x) => x !== el && x.tagName === el.tagName) : [];
    if (!sib.length) return [];
    return stableClasses(el).filter((c) => !sib.some((x) => x.classList.contains(c)));
  }
  //  짧은 지문 (이웃 비교용)
  function sig(el) { return iconSig(el) || textOf(el).slice(0, 20) || stableClasses(el).join('.') || el.tagName; }
  //  이웃 — 자기(없으면 위로 3단계까지 올라가며) 앞뒤 형제의 지문.
  //  재생 버튼처럼 눌릴 때마다 아이콘이 바뀌는 버튼도 이웃(10초 버튼들)은 그대로라 알아볼 수 있고,
  //  1화라 '이전' 버튼이 아예 없을 때 그 자리에 온 '10초 뒤로' 를 '이전' 으로 착각하지 않는다 (이웃이 다르다).
  function ctxOf(el) {
    for (let n = el, lvl = 0; n && lvl < 4; n = n.parentElement, lvl++) {
      const a = n.previousElementSibling;
      const b = n.nextElementSibling;
      if (a || b) return { lvl, prev: a ? sig(a) : '', next: b ? sig(b) : '' };
    }
    return null;
  }
  function ctxMatch(el, c) {
    let n = el;
    for (let i = 0; i < c.lvl && n; i++) n = n.parentElement;
    if (!n) return false;
    const a = n.previousElementSibling ? sig(n.previousElementSibling) : '';
    const b = n.nextElementSibling ? sig(n.nextElementSibling) : '';
    if (!c.prev && !c.next) return false;
    return (!c.prev || c.prev === a) && (!c.next || c.next === b);
  }
  //  클릭한 곳(아이콘·글자일 수 있다) → 누를 버튼
  function pickTarget(node) {
    let el = node && node.nodeType === 1 ? node : node && node.parentElement;
    if (!el) return null;
    const inter = el.closest(INTERACTIVE);
    if (inter && !tooBig(inter)) return inter;
    while (el && (el instanceof SVGElement || INLINE.test(el.tagName))) el = el.parentElement;
    return el && el !== document.body && el !== document.documentElement && !tooBig(el) ? el : null;
  }
  function tooBig(el) {
    const r = el.getBoundingClientRect();
    return r.width * r.height > innerWidth * innerHeight * 0.3;   // 플레이어 전체 같은 큰 판은 버튼이 아니다
  }
  function commonRun(a, b) {
    //  가장 긴 공통 부분 글자 수 (エンディングスキップ ↔ オープニングスキップ = 6)
    let best = 0;
    for (let i = 0; i < a.length; i++) for (let j = 0; j < b.length; j++) {
      let k = 0;
      while (a[i + k] && a[i + k] === b[j + k]) k++;
      if (k > best) best = k;
    }
    return best;
  }
  //  글자가 같은 버튼인가 — 한 글자 아이콘(⟲ ▶)은 같아야, 긴 글자는 2자 이상 겹치면 (エンディングスキップ ↔ オープニングスキップ)
  const textLike = (a, b) => a === b || commonRun(a, b) >= Math.min(2, a.length, b.length);
  function score(el, d) {
    //  ① 형제와 구별되는 클래스를 기억했으면 그게 있어야 같은 버튼
    if (d.uniq && d.uniq.length && !d.uniq.some((c) => el.classList.contains(c))) return -1;
    //  ② 아니면(모두 같은 클래스인 아이콘 버튼 줄) 이름표·아이콘·이웃 중 하나는 맞아야 한다 — 엉뚱한 버튼을 누르지 않게
    if (!(d.uniq && d.uniq.length) && !(d.id && el.id === d.id) && !(d.testid && el.getAttribute('data-testid') === d.testid)) {
      const hasId = d.icon || d.aria || d.title || d.text || (d.ctx && (d.ctx.prev || d.ctx.next));
      const same = (d.icon && iconSig(el) === d.icon) || (d.aria && el.getAttribute('aria-label') === d.aria) ||
        (d.title && el.getAttribute('title') === d.title) || (d.text && textOf(el) && textLike(d.text, textOf(el))) ||
        (d.ctx && ctxMatch(el, d.ctx));
      if (hasId && !same) return -1;
    }
    let s = 0;
    if (d.id && el.id === d.id) s += 10;
    if (d.testid && el.getAttribute('data-testid') === d.testid) s += 8;
    if (d.cls.length) {
      const hit = d.cls.filter((c) => el.classList.contains(c)).length;
      s += 6 * hit / Math.max(d.cls.length, stableClasses(el).length || 1);
    }
    if (d.aria && el.getAttribute('aria-label') === d.aria) s += 4;
    if (d.title && el.getAttribute('title') === d.title) s += 3;
    const tx = textOf(el);
    if (d.text) {
      if (tx && textLike(d.text, tx)) s += tx === d.text ? 4 : 2;
      //  글자 버튼인데 글자가 전혀 다르면 다른 버튼 — 단, 형제와 구별되는 클래스가 있거나 양옆 버튼이 그대로면
      //  같은 버튼의 다른 상태다 (再生 ↔ 一時停止 처럼 글자가 바뀌는 버튼)
      else if (!((d.uniq && d.uniq.length) || (d.ctx && ctxMatch(el, d.ctx)))) return -1;
    } else if (tx && tx.length > 2) s -= 2;
    if (d.icon && iconSig(el) === d.icon) s += 3;
    const p = pathOf(el);
    let same = 0;
    while (same < d.path.length && p[same] === d.path[same]) same++;
    s += 3 * same / Math.max(1, d.path.length);
    if (el.getClientRects().length) s += 1;   // 화면에 그려진 것 우선
    return s;
  }
  //  열린 그림자 DOM 안까지 찾는다 (디즈니+ 처럼 플레이어를 웹 컴포넌트로 만드는 사이트).
  //  그림자 루트 목록은 2초 기억한다 — 키를 누를 때마다 페이지 전체를 훑지 않게
  let rootsCache = { at: 0, list: [] };
  function shadowRoots() {
    if (Date.now() - rootsCache.at < 2000) return rootsCache.list;
    const list = [];
    const walk = (root, depth) => {
      if (depth > 6) return;
      for (const e of root.querySelectorAll('*')) {
        if (e.shadowRoot && e !== host) { list.push(e.shadowRoot); walk(e.shadowRoot, depth + 1); }
      }
    };
    walk(document, 0);
    rootsCache = { at: Date.now(), list };
    return list;
  }
  function qsaDeep(sel) {
    const out = [...document.querySelectorAll(sel)];
    for (const r of shadowRoots()) for (const e of r.querySelectorAll(sel)) out.push(e);
    return out;
  }
  function candidates(d) {
    if (d.id) { const e = qsaDeep('#' + CSS.escape(d.id))[0]; if (e) return [e]; }
    if (d.cls.length) {
      const set = new Set();
      for (const c of d.cls) for (const e of qsaDeep(d.tag + '.' + CSS.escape(c))) set.add(e);
      if (set.size) return [...set];
    }
    return qsaDeep(d.tag).slice(0, 3000);
  }
  function resolve(d) {
    let best = null;
    let bestS = 4.5;   // 이보다 덜 닮았으면 '없음' — 엉뚱한 버튼을 누르지 않는다
    for (const el of candidates(d)) {
      if (el === host || el === overlay || (host && host.contains(el))) continue;
      const s = score(el, d);
      if (s > bestS) { best = el; bestS = s; }
    }
    return best;
  }

  // ── 키 ──
  const MOD_CODES = /^(?:Shift|Control|Alt|Meta|OS)(?:Left|Right)?$/;
  function comboOf(e) { return { code: e.code, ctrl: e.ctrlKey, alt: e.altKey, shift: e.shiftKey, meta: e.metaKey }; }
  const sameCombo = (a, b) => !!a.code && a.code === b.code && !!a.ctrl === !!b.ctrl && !!a.alt === !!b.alt && !!a.shift === !!b.shift && !!a.meta === !!b.meta;
  const KEY_NAMES = { Space: 'Space', ArrowLeft: '←', ArrowRight: '→', ArrowUp: '↑', ArrowDown: '↓', Enter: 'Enter', Backspace: 'Backspace',
    Tab: 'Tab', Delete: 'Del', Insert: 'Ins', Home: 'Home', End: 'End', PageUp: 'PgUp', PageDown: 'PgDn', Minus: '-', Equal: '=',
    BracketLeft: '[', BracketRight: ']', Backslash: '\\', Semicolon: ';', Quote: "'", Comma: ',', Period: '.', Slash: '/', Backquote: '`' };
  function keyName(b) {
    if (!b || !b.code) return T('kb.noKey');
    let k = KEY_NAMES[b.code] || b.code;
    if (/^Key[A-Z]$/.test(b.code)) k = b.code.slice(3);
    else if (/^Digit\d$/.test(b.code)) k = b.code.slice(5);
    else if (/^Numpad/.test(b.code)) k = 'Num ' + b.code.slice(6);
    return (b.ctrl ? 'Ctrl+' : '') + (b.alt ? 'Alt+' : '') + (b.shift ? 'Shift+' : '') + (b.meta ? 'Win+' : '') + k;
  }
  function isEditable(e) {
    const t = (e.composedPath && e.composedPath()[0]) || e.target;
    if (!t || t.nodeType !== 1) return false;
    if (t.isContentEditable) return true;
    if (t.tagName === 'TEXTAREA' || t.tagName === 'SELECT') return true;
    return t.tagName === 'INPUT' && !/^(?:button|submit|checkbox|radio|range|reset|image|color)$/i.test(t.type || '');
  }

  //  가로챈 키 — keyup 도 막아야 포커스된 버튼이 Space 로 한 번 더 눌리지 않는다
  const swallowed = new Set();
  window.addEventListener('keydown', (e) => {
    if (picking) return;                  // 앱의 '버튼 고르기' 중
    if (!enabled || !binds.length || e.isComposing || isEditable(e)) return;
    if (!SITE.isPlaybackUrl()) return;
    const c = comboOf(e);
    const b = binds.find((x) => sameCombo(x, c));
    if (!b) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    swallowed.add(e.code);
    if (!e.repeat) press(b);
  }, true);
  window.addEventListener('keyup', (e) => {
    if (!swallowed.has(e.code)) return;
    swallowed.delete(e.code);
    e.preventDefault();
    e.stopImmediatePropagation();
  }, true);

  function fire(el) {
    if (typeof el.click === 'function') el.click();
    else el.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, view: window }));
  }
  //  조작 칸이 숨어 있으면(마우스를 안 움직이면 사라지는 플레이어) 마우스를 움직인 것처럼 깨운 뒤 한 번 더 찾는다
  function wakeControls() {
    const v = SITE.getVideoElement && SITE.getVideoElement();
    const at = v || document.body;
    const r = at.getBoundingClientRect();
    const ev = { bubbles: true, cancelable: true, view: window, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 };
    for (let n = at, i = 0; n && i < 4; n = n.parentElement, i++) {
      n.dispatchEvent(new MouseEvent('mousemove', ev));
      n.dispatchEvent(new PointerEvent('pointermove', ev));
    }
  }
  // ── 전체화면 ↔ 원래 크기 (2026-11-07 유빈 요청: 같은 키로) ──
  //  플레이어가 전체화면이 되면 그 버튼을 '나가기' 버튼으로 바꿔 끼우는 경우가 있어, 처음 고른 버튼을 못 찾는다.
  //  그래서 전체화면 키는 따로 알아 두고(이름표에 전체화면이 들어 있거나, 눌렀더니 전체화면이 바뀐 키 → fs:true),
  //  전체화면 중에 누르면 버튼을 찾지 않고 바로 원래 크기로 돌린다 (플레이어는 fullscreenchange 로 따라온다).
  const FS_RE = /full\s*-?\s*screen|maximi[sz]e|全画面|フルスクリーン|전체\s*화면/i;
  const labelsOf = (el) => [el.getAttribute('aria-label') || '', el.getAttribute('title') || '', textOf(el), el.getAttribute('class') || ''].join(' ');
  const looksFs = (d) => FS_RE.test([d.aria, d.title, d.text, (d.cls || []).join(' ')].join(' '));
  const isFsBind = (b) => b.fs === true || looksFs(b.target);
  //  IMAGINATION 정주행의 유사 전체화면(확장이 창을 키우고 플레이어를 화면 가득) — content.js 가 열어 둔 것
  const pseudoFs = () => { const p = self.__tokuPseudoFs; return !!(p && typeof p.active === 'function' && p.active()); };
  const fsNow = () => !!(document.fullscreenElement || document.webkitFullscreenElement);
  //  처음 고른 버튼이 없을 때 — 이름표에 전체화면이 들어간 버튼 아무거나 (나가기 버튼 포함)
  function findFsButton() {
    for (const el of qsaDeep(INTERACTIVE + ', [class*="fullscreen" i], [class*="full-screen" i]')) {
      if (host && host.contains(el)) continue;
      if (el.getClientRects().length && FS_RE.test(labelsOf(el))) return el;
    }
    return null;
  }
  //  눌러서 전체화면이 바뀌면 그 키를 전체화면 키로 기억한다 (아이콘만 있는 버튼도 알아보게)
  function learnFs(b) {
    if (b.fs === true) return;
    const before = fsNow();
    const done = () => { document.removeEventListener('fullscreenchange', done); clearTimeout(timer); if (fsNow() !== before) markFs(b); };
    const timer = setTimeout(() => document.removeEventListener('fullscreenchange', done), 1500);
    document.addEventListener('fullscreenchange', done);
  }
  function press(b) {
    if (isFsBind(b) && pseudoFs()) { self.__tokuPseudoFs.exit(); return; }
    if (isFsBind(b) && fsNow() && document.exitFullscreen) {
      document.exitFullscreen().catch(() => { /* 이미 풀림 */ });
      return;
    }
    const el = resolve(b.target);
    if (el) { learnFs(b); fire(el); return; }
    wakeControls();
    setTimeout(() => {
      const again = resolve(b.target) || (isFsBind(b) ? findFsButton() : null);
      if (again) { learnFs(b); fire(again); } else toast(T('kb.notFound', { name: b.label || keyName(b) }));
    }, 150);
  }

  // ── 앱의 '버튼 고르기' ──
  //  안내 띠(그림자 DOM — 사이트 CSS 와 섞이지 않게) + 마우스 아래 버튼 테두리.
  //  고르는 동안 페이지에는 클릭을 보내지 않는다 (고르려다 그 버튼이 눌리지 않게).
  let host = null;
  let bannerText = null;
  let overlay = null;
  let picking = null;      // { reqId }

  //  전체화면이면 그 요소 안에 붙여야 보인다 (영상 요소 자체에는 못 붙인다)
  function mountPoint() {
    const fs = document.fullscreenElement;
    return fs && !/^(?:VIDEO|IFRAME|IMG|CANVAS)$/.test(fs.tagName) ? fs : document.body;
  }
  document.addEventListener('fullscreenchange', () => {
    for (const n of [host, overlay, toastEl]) if (n && n.isConnected) mountPoint().appendChild(n);
  });

  function showBanner(text, bad) {
    if (!host) {
      host = document.createElement('div');
      host.setAttribute('data-toku-keybind', '');
      const root = host.attachShadow({ mode: 'open' });
      const style = document.createElement('style');
      style.textContent = `
        :host { all: initial; }
        .bar { position: fixed; top: 14px; left: 50%; transform: translateX(-50%); z-index: 2147483647; max-width: calc(100vw - 32px);
          display: flex; align-items: center; gap: 12px; padding: 10px 12px 10px 16px; box-sizing: border-box;
          background: #12121f; color: #ffd76a; border: 1px solid #4a4020; border-radius: 10px; box-shadow: 0 10px 32px rgba(0,0,0,.5);
          font: 600 13.5px/1.4 'Segoe UI', 'Malgun Gothic', 'Yu Gothic UI', 'Meiryo', sans-serif; }
        .bar.bad { color: #ff8a80; border-color: #6a2a2a; }
        button { font: inherit; font-weight: 600; font-size: 12.5px; color: #e8eaf0; background: #22223a; border: 1px solid #3a3a55;
          border-radius: 7px; padding: 5px 12px; cursor: pointer; }
        button:hover { background: #2e2e4a; }
        button:focus-visible { outline: 2px solid #ffc233; outline-offset: 2px; }
      `;
      const bar = document.createElement('div');
      bar.className = 'bar';
      bar.setAttribute('role', 'status');
      bannerText = document.createElement('span');
      const cancel = document.createElement('button');
      cancel.type = 'button';
      cancel.textContent = T('kb.pickCancel');
      cancel.addEventListener('click', () => cancelPick());
      bar.append(bannerText, cancel);
      root.append(style, bar);
    }
    bannerText.textContent = text;
    bannerText.parentElement.classList.toggle('bad', !!bad);
    mountPoint().appendChild(host);
  }
  function hideBanner() { if (host) host.remove(); }

  function autoLabel(d) { return (d.aria || d.title || d.text || '').slice(0, 30); }
  const BLOCK = ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click', 'dblclick'];
  function fromHost(e) { return !!host && e.composedPath().includes(host); }
  function onPickMove(e) {
    if (fromHost(e)) { showOverlay(null); return; }
    showOverlay(pickTarget(e.composedPath()[0] || e.target));
  }
  function onPickBlock(e) {
    if (fromHost(e)) return;   // 안내 띠의 취소 버튼
    e.preventDefault();
    e.stopImmediatePropagation();
    if (e.type !== 'click' || !picking) return;
    const t = pickTarget(e.composedPath()[0] || e.target);
    if (!t) { showBanner(T('kb.pickBad'), true); return; }
    const d = describe(t);
    const reqId = picking.reqId;
    stopPick();
    send({ action: 'KB_PICKED', reqId, site: SITE.id, target: d, label: autoLabel(d) });
    toast(T('kb.picked'));
  }
  function onPickKey(e) {
    if (e.key !== 'Escape') return;
    e.preventDefault();
    e.stopImmediatePropagation();
    cancelPick();
  }
  function cancelPick() {
    if (!picking) return;
    const reqId = picking.reqId;
    stopPick();
    send({ action: 'KB_PICK_CANCELED', reqId });
  }
  function startPick(reqId) {
    stopPick();
    picking = { reqId };
    for (const t of BLOCK) window.addEventListener(t, onPickBlock, true);
    window.addEventListener('mousemove', onPickMove, true);
    window.addEventListener('keydown', onPickKey, true);
    showBanner(T('kb.pickNow'));
  }
  function stopPick() {
    for (const t of BLOCK) window.removeEventListener(t, onPickBlock, true);
    window.removeEventListener('mousemove', onPickMove, true);
    window.removeEventListener('keydown', onPickKey, true);
    picking = null;
    showOverlay(null);
    hideBanner();
  }
  function showOverlay(t) {
    if (!t) { if (overlay) overlay.remove(); return; }
    if (!overlay) {
      overlay = document.createElement('div');
      overlay.setAttribute('data-toku-keybind', '');
      overlay.style.cssText = 'position:fixed;z-index:2147483646;pointer-events:none;border:2px solid #ffc233;border-radius:6px;box-shadow:0 0 0 4000px rgba(0,0,0,.18);transition:all .06s';
    }
    const r = t.getBoundingClientRect();
    Object.assign(overlay.style, { left: (r.left - 3) + 'px', top: (r.top - 3) + 'px', width: (r.width + 6) + 'px', height: (r.height + 6) + 'px' });
    if (!overlay.isConnected) mountPoint().appendChild(overlay);
  }

  // ── 알림 (버튼을 못 찾았을 때·골랐을 때) ──
  let toastEl = null;
  let toastTimer = 0;
  function toast(text) {
    if (!toastEl) {
      toastEl = document.createElement('div');
      toastEl.setAttribute('data-toku-keybind', '');
      toastEl.setAttribute('role', 'status');
      toastEl.style.cssText = 'position:fixed;left:50%;bottom:72px;transform:translateX(-50%);z-index:2147483647;pointer-events:none;' +
        'background:rgba(18,18,31,.92);color:#ffd76a;border:1px solid #4a4020;border-radius:8px;padding:8px 14px;' +
        "font:600 13px/1.4 'Segoe UI','Malgun Gothic','Yu Gothic UI',sans-serif;max-width:80vw";
    }
    toastEl.textContent = '⌨ ' + text;
    mountPoint().appendChild(toastEl);
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.remove(), 2200);
  }

  // ── background.js 가 앱의 요청을 전해 준다 ──
  try {
    chrome.runtime.onMessage.addListener((m, sender, reply) => {
      if (!m) return;
      if (m.action === 'KB_PICK') { startPick(String(m.reqId || '')); reply({ ok: true, playback: SITE.isPlaybackUrl() }); }
      else if (m.action === 'KB_PICK_CANCEL') { if (picking && picking.reqId === m.reqId) stopPick(); }
    });
  } catch (e) { /* 무시 */ }

  //  검증용 (페이지 MAIN 월드에선 안 보인다 — 콘텐트 스크립트 세계에만)
  self.__tokuKeybindTest = { describe, resolve, pickTarget, keyName, startPick, cancelPick, get binds() { return binds; }, get picking() { return picking; } };
})();
