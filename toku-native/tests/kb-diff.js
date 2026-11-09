// 키 설정 저장소 교차 점검 — 진짜 keybinds.js (KeybindStore) 와 C(kb_cli.exe) 에 같은 명령을 넣고 결과·KEYBINDS 를 줄마다 비교
//  사용: node tests/kb-diff.js <kb_cli.exe> [명령 수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const crypto = require('crypto');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 3000);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

//  JS 쪽: 같은 무작위 id (C 의 rnd5 와 같은 수열)
let ctr = 0;
crypto.randomBytes = (n) => { ctr++; return Buffer.from([0, 1, 2, 3, 4].slice(0, n).map((i) => (ctr * 37 + i * 11) & 255)); };
const { KeybindStore } = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/keybinds.js');
//  electron-store 흉내 — 쓰고 읽을 때 JSON 왕복 (파일에 썼다 다시 읽는 것과 같게)
const mem = {};
const store = {
  get: (k) => (k in mem ? JSON.parse(mem[k]) : undefined),
  set: (k, v) => { mem[k] = JSON.stringify(v); },
  has: (k) => k in mem,
};
const kbs = new KeybindStore(store);

// ── 무작위 값 ──
const SITES = ['ttfc', 'imagination', 'youtube', 'disneyplus', 'chzzk', 'danime'];
const CHUNKS = ['a', 'B', ' ', '\t', '\n', '\u0000', '\u001f', '\u007f', '\u0085', '가', '仮面', '😀', 'é', 'K', 'İ', '-', '_', '0', '9', 'x'.repeat(50), '  ', '\u00a0', '\u3000', '\ufeff', 'Btn', 'ytp-play-button'];
const rstr = (max) => { let s = ''; const k = int(max); for (let i = 0; i < k; i++) s += pick(CHUNKS); return s; };
const rjunk = (d) => {
  const r = int(10);
  if (r === 0) return null; if (r === 1) return int(5) - 1; if (r === 2) return rnd() < 0.5; if (r === 3) return rstr(4);
  if (r === 4 && d < 2) return [rjunk(d + 1), rjunk(d + 1)];
  if (r === 5 && d < 2) return { a: rjunk(d + 1) };
  if (r === 6) return 2.5; if (r === 7) return '3'; if (r === 8) return -0; return undefined;
};
const TAGS = ['button', 'BUTTON', 'div', 'ytp-button', 'a1', '1a', '', 'x'.repeat(31), 'x'.repeat(30), 'butt on', 'KBD', 'Kbd', 'b\u0000', 'svg-icon'];
const rlist = () => (rnd() < 0.2 ? rjunk(1) : Array.from({ length: int(16) }, () => (rnd() < 0.85 ? rstr(5) : rjunk(1))));
function rtarget() {
  if (rnd() < 0.06) return rjunk(0);
  const t = { tag: rnd() < 0.85 ? pick(TAGS) : rjunk(1) };
  for (const k of ['id', 'testid', 'aria', 'title', 'text', 'icon']) if (rnd() < 0.7) t[k] = rnd() < 0.85 ? rstr(8) : rjunk(1);
  for (const k of ['cls', 'path', 'uniq']) if (rnd() < 0.7) t[k] = rlist();
  if (rnd() < 0.6) t.ctx = rnd() < 0.2 ? rjunk(1) : { lvl: pick([0, 1, 2, 3, 4, -1, 1.5, '2', true, null, -0, undefined, [2]]), prev: rnd() < 0.8 ? rstr(6) : rjunk(1), next: rstr(6) };
  if (rnd() < 0.2) t.extra = rstr(3);
  return t;
}
const CODES = ['KeyA', 'KeyK', 'Digit1', 'Space', 'ArrowLeft', 'F12', 'Numpad5', 'ShiftLeft', 'ControlRight', 'Alt', 'OS', 'MetaLeft', '', '1Key', 'Key A', 'x'.repeat(24), 'x'.repeat(25), 'Ke\u0000y', 'Shiftx', 'AltRight', 'Lang1'];
const rcombo = () => (rnd() < 0.05 ? rjunk(0) : { code: rnd() < 0.9 ? pick(CODES) : rjunk(1), ctrl: rnd() < 0.3 ? rjunk(1) : rnd() < 0.3, alt: rnd() < 0.2, shift: rnd() < 0.2, meta: rnd() < 0.1 });
const rsite = () => (rnd() < 0.93 ? pick(SITES) : pick(['', 'TTFC', 'netflix']));
function existingId(site) {
  const all = kbs.all();
  const list = all[site] || [];
  if (list.length && rnd() < 0.85) return pick(list).id;
  return pick(['', 'nope', '0000000000']);
}
function rbindRaw() {
  const b = { id: rnd() < 0.8 ? pick(['a1', 'b2', 'c3', 'x-y', 'bad id', 'z'.repeat(25), 'q_1', '']) : rjunk(1), label: rnd() < 0.8 ? rstr(6) : rjunk(1), target: rtarget() };
  Object.assign(b, rnd() < 0.8 ? rcombo() : {});
  if (rnd() < 0.3) b.fs = pick([true, false, 'true', 1]);
  return rnd() < 0.05 ? rjunk(0) : b;
}
function rraw() {
  if (rnd() < 0.1) return rjunk(0);
  const o = {};
  for (const s of SITES) if (rnd() < 0.5) o[s] = rnd() < 0.85 ? Array.from({ length: int(35) }, rbindRaw) : rjunk(1);
  if (rnd() < 0.2) o.other = [rbindRaw()];
  return o;
}
const ron = () => (rnd() < 0.2 ? rjunk(0) : Object.fromEntries(SITES.filter(() => rnd() < 0.4).map((s) => [s, pick([false, true, 0, 'false', null])])));

function op() {
  const site = rsite();
  const r = rnd();
  if (r < 0.3) return { op: 'add', site, target: rtarget(), label: rnd() < 0.85 ? rstr(10) : rjunk(1) };
  if (r < 0.38) return { op: 'setTarget', site, id: existingId(site), target: rtarget() };
  if (r < 0.58) return { op: 'setKey', site, id: existingId(site), combo: rcombo() };
  if (r < 0.68) return { op: 'rename', site, id: existingId(site), label: rnd() < 0.9 ? rstr(14) : rjunk(1) };
  if (r < 0.75) return { op: 'remove', site, id: existingId(site) };
  if (r < 0.82) return { op: 'siteOn', site, on: rnd() < 0.5 };
  if (r < 0.88) return { op: 'markFs', site, id: existingId(site) };
  if (r < 0.93) return { op: 'adopt', kb: rraw(), on: ron() };
  return { op: 'raw', v: rraw(), on: ron() };
}
function runJs(m0) {
  const m = JSON.parse(JSON.stringify(m0));   // 확장 메시지처럼 JSON 을 거친 값 (C 가 받는 것과 같게)
  let r;
  const s = m.site, id = m.id;
  switch (m.op) {
    case 'add': r = kbs.add(s, m.target, typeof m.label === 'string' ? m.label : ''); break;
    case 'setTarget': r = kbs.setTarget(s, id, m.target); break;
    case 'setKey': r = kbs.setKey(s, id, m.combo); break;
    case 'rename': r = kbs.rename(s, id, m.label); break;
    case 'remove': r = kbs.remove(s, id); break;
    case 'siteOn': r = kbs.setSiteOn(s, !!m.on); break;
    case 'markFs': r = kbs.markFs(s, id); break;
    case 'adopt': r = { ok: kbs.adoptLocal(m.kb, m.on) }; break;
    case 'raw': store.set('keybinds', m.v === undefined ? null : m.v); if (m.on !== undefined) store.set('keybindsOn', m.on); r = { ok: true }; break;
  }
  const p = kbs.payload();
  //  ⚠ 일부러 다른 점: 자르기가 이모지 가운데면 JS 는 반쪽(�)을 두고 C 는 U+FFFD — 그 경우만 같은 규칙으로 맞춰 비교 (PLAN.md)
  const wf = (v) => (typeof v === 'string' ? v.toWellFormed() : Array.isArray(v) ? v.map(wf) : v && typeof v === 'object' ? Object.fromEntries(Object.entries(v).map(([k, x]) => [k, wf(x)])) : v);
  return JSON.stringify(wf({ r: { ok: !!r.ok, error: r.ok ? null : (r.error || null), id: r.id || null, moved: r.moved === undefined || r.moved === null ? null : r.moved },
    p: { type: 'KEYBINDS', keybinds: p.keybinds || {}, on: p.on || {} } }));
}

(async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'kbdiff-'));
  const c = spawn(EXE, [dir], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  //  ⚠ 바이트로 모았다가 줄 단위로 푼다 — 조각마다 풀면 경계에 걸린 여러 바이트 글자가 깨진다
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  let fail = 0, ops = 0;
  const count = {};
  for (let i = 0; i < N; i++) {
    const m = op();
    //  JS 는 add 할 때 다른 결과에서도 id 를 하나 쓴다 → 순서를 맞추려고 같은 순서로 부른다
    const want = runJs(m);
    c.stdin.write(JSON.stringify(m) + '\n');
    const got = await next();
    ops++;
    count[m.op] = (count[m.op] || 0) + 1;
    if (got !== want) {
      fail++;
      if (fail === 1 && process.env.KB_DUMP) fs.writeFileSync(process.env.KB_DUMP, JSON.stringify({ m, want, got }));
      if (fail <= 3) { console.log('X 다름 #' + i + ' ' + m.op); console.log('   명령: ' + JSON.stringify(m).slice(0, 400)); let k = 0; while (k < want.length && want[k] === got[k]) k++; const at = Math.max(0, k - 200); console.log('   (' + k + '번째 글자부터 다름)'); console.log('   JS: …' + want.slice(at, k + 250)); console.log('   C : …' + got.slice(at, k + 250)); }
    }
  }
  c.stdin.end();
  fs.rmSync(dir, { recursive: true, force: true });
  const sum = Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · ');
  console.log(fail ? '실패 ' + fail + '/' + ops : '전부 같음 — 명령 ' + ops + '개 (' + sum + ')');
  process.exit(fail ? 1 : 0);
})();
