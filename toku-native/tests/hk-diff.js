// 웹훅 탭 데이터 교차 점검 — 진짜 secrets.js(작품별 웹훅) + main.js 의 폴더·캐릭터 함수(그대로 옮겨 둔 것)와 C(hk_cli.exe)
//  둘 다 임시 폴더에서만 쓴다. 사용: node tests/hk-diff.js <hk_cli.exe> [수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const crypto = require('crypto');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 1500);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'hkdiff-'));
process.env.APPDATA = path.join(TMP, 'js');
const JS_SEC = path.join(TMP, 'js', 'toku-rpc', 'secrets.json');
const C_SEC = path.join(TMP, 'c-secrets.json');
fs.mkdirSync(path.dirname(JS_SEC), { recursive: true });
const BASE = { discordAppIds: { ttfc: '123456789012345678' }, bingeWebhookUrl: 'https://discord.com/api/webhooks/1/x', other: [1, { a: 2 }] };
fs.writeFileSync(JS_SEC, JSON.stringify(BASE));
fs.writeFileSync(C_SEC, JSON.stringify(BASE));
let ctr = 0;
crypto.randomBytes = (n) => { ctr++; return Buffer.from(Array.from({ length: n }, (_, i) => (ctr * 29 + i * 7) & 255)); };
const secrets = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/secrets.js');

// ── main.js 폴더·캐릭터 함수 (1191~1243, 1340~1377 그대로) ──
const mem = {};
const store = { get: (k) => (k in mem ? JSON.parse(mem[k]) : undefined), set: (k, v) => { mem[k] = JSON.stringify(v); } };
const HOOK_CHARS_MAX = 50;
const HOOK_CHAR_BAD = /discord|clyde/i;
function hookFolders() { return store.get('hookFolders') || {}; }
function updateHookFolder(title, fn) {
  const all = Object.assign({}, hookFolders());
  const f = Object.assign({}, all[title] || {});
  fn(f);
  if (!f.icon) delete f.icon;
  if (!Array.isArray(f.characters) || !f.characters.length) delete f.characters;
  if (Object.keys(f).length) all[title] = f; else delete all[title];
  store.set('hookFolders', all);
}
function hookTitles() { return new Set(secrets.titleHookList().map((h) => h.title)); }
function hookIcons() {
  const titles = hookTitles();
  const out = {};
  for (const [title, f] of Object.entries(hookFolders())) {
    if (titles.has(title) && f && typeof f.icon === 'string' && f.icon.startsWith('data:image/png;base64,')) out[title] = f.icon;
  }
  return out;
}
function hookChars(title) {
  const f = hookFolders()[title];
  return (f && Array.isArray(f.characters)) ? f.characters.filter((c) => c && typeof c.id === 'string' && typeof c.name === 'string') : [];
}
function hookCharMap() {
  const out = {};
  for (const title of hookTitles()) {
    const list = hookChars(title).map((c) => ({ id: c.id, name: c.name, avatar: typeof c.avatar === 'string' ? c.avatar : '', avatarUntil: Number(c.avatarUntil) || 0, off: c.off === true }));
    if (list.length) out[title] = list;
  }
  return out;
}
function setHookIcon(title, icon) { updateHookFolder(title, (f) => { f.icon = icon || ''; }); }
function cleanCharName(v) { return typeof v === 'string' ? v.replace(/\s+/g, ' ').trim().slice(0, 80) : ''; }
function charNameError(name) { return !name ? 'charname' : (HOOK_CHAR_BAD.test(name) ? 'charbad' : ''); }
function updateHookChar(title, id, fn) {
  if (!hookChars(title).some((c) => c.id === id)) return { ok: false, error: 'missing' };
  let err = '';
  updateHookFolder(title, (f) => {
    f.characters = (f.characters || []).map((c) => {
      if (c.id !== id) return c;
      const n = Object.assign({}, c);
      err = fn(n) || '';
      return err ? c : n;
    });
  });
  return err ? { ok: false, error: err } : { ok: true };
}
const H = {
  add: (m) => secrets.addTitleHook(m.t, m.url, m.name),
  rename: (m) => secrets.renameTitleHook(String(m.id || ''), m.name),
  remove: (m) => secrets.removeTitleHook(String(m.id || '')),
  icon: (m) => { if (!hookTitles().has(m.title)) return { ok: false, error: 'missing' }; setHookIcon(m.title, m.png); return { ok: true }; },
  iconClear: (m) => { setHookIcon(String(m.title || ''), ''); return { ok: true }; },
  charAdd: (m) => {
    const title = m.title;
    if (!hookTitles().has(title)) return { ok: false, error: 'missing' };
    const n = cleanCharName(m.name);
    const bad = charNameError(n);
    if (bad) return { ok: false, error: bad };
    if (hookChars(title).length >= HOOK_CHARS_MAX) return { ok: false, error: 'charfull' };
    const id = crypto.randomBytes(6).toString('hex');
    updateHookFolder(title, (f) => { f.characters = (f.characters || []).concat([{ id, name: n, avatar: '', avatarUntil: 0 }]); });
    return { ok: true, id };
  },
  charRename: (m) => { const n = cleanCharName(m.name); return updateHookChar(m.title, m.id, (c) => { const bad = charNameError(n); if (bad) return bad; c.name = n; return ''; }); },
  charRemove: (m) => {
    if (!hookChars(m.title).some((c) => c.id === m.id)) return { ok: false, error: 'missing' };
    updateHookFolder(m.title, (f) => { f.characters = (f.characters || []).filter((c) => c.id !== m.id); });
    return { ok: true };
  },
  charToggle: (m) => { const want = !!m.on; return updateHookChar(m.title, m.id, (c) => { if (want) delete c.off; else c.off = true; return ''; }); },
  charUrl: (m) => {
    const u = typeof m.url === 'string' ? m.url.trim() : '';
    if (u && !(/^https:\/\/[^\s"'<>]+$/.test(u) && u.length <= 2000)) return { ok: false, error: 'avatarurl' };
    return updateHookChar(m.title, m.id, (c) => { if (c.avatar === u) return ''; c.avatar = u; c.avatarUntil = 0; return ''; });
  },
  folders: (m) => { store.set('hookFolders', m.v === undefined ? null : m.v); return { ok: true }; },
};

// ── 무작위 명령 ──
const URLS = ['https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz_-01', 'https://discord.com/api/webhooks/223456789012345678/bbbbbbbbbbbbbbbbbbbbbbbbb',
  ' https://ptb.discord.com/api/webhooks/323456789012345/CCCCCCCCCCCCCCCCCCCCCC ', 'https://discord.com/api/webhooks/1/x', 'not a url', 'https://canary.discordapp.com/api/v9/webhooks/423456789012345678/ddddddddddddddddddddd'];
const TITLES = ['仮面ライダーフォーゼ', '仮面ライダー  フォーゼ', ' 戦隊 ', 'ウルトラマン', '', 'x'.repeat(120), '\u3000Ｘ\u3000', 'Rider\tKick'];
const NAMES = ['', '메인', '  보조 채널 ', 'x'.repeat(50), 'Discord bot', 'clyde', '하루', 'ＤＩＳＣＯＲＤ', '\t'];
const AV = ['', 'https://a.b/c.png', ' https://a.b/c.png ', 'http://x', 'https://a b', 'https://' + 'y'.repeat(2000), 'https://x.y/\u3000z'];
const junk = () => pick([null, 3, true, 'str', [], {}]);
function curTitles() { return [...new Set(secrets.titleHookList().map((h) => h.title))]; }
function curIds() { return secrets.titleHookList().map((h) => h.id); }
function op() {
  const r = rnd();
  const ts = curTitles();
  const title = ts.length && rnd() < 0.85 ? pick(ts) : pick(TITLES);
  const chars = hookChars(title);
  const cid = chars.length && rnd() < 0.85 ? pick(chars).id : pick(['nope', '', 'abc']);
  if (r < 0.2) return { op: 'add', t: rnd() < 0.9 ? pick(TITLES) : junk(), url: rnd() < 0.9 ? pick(URLS) : junk(), name: rnd() < 0.8 ? pick(NAMES) : junk() };
  if (r < 0.27) { const ids = curIds(); return { op: 'rename', id: ids.length && rnd() < 0.85 ? pick(ids) : 'x', name: rnd() < 0.9 ? pick(NAMES) : junk() }; }
  if (r < 0.32) { const ids = curIds(); return { op: 'remove', id: ids.length && rnd() < 0.8 ? pick(ids) : 'x' }; }
  if (r < 0.38) return { op: 'icon', title, png: pick(['data:image/png;base64,iVBORw0KGgo=', 'data:image/jpeg;base64,xx', '']) };
  if (r < 0.41) return { op: 'iconClear', title };
  if (r < 0.58) return { op: 'charAdd', title, name: rnd() < 0.9 ? pick(NAMES) : junk() };
  if (r < 0.66) return { op: 'charRename', title, id: cid, name: rnd() < 0.9 ? pick(NAMES) : junk() };
  if (r < 0.72) return { op: 'charRemove', title, id: cid };
  if (r < 0.8) return { op: 'charToggle', title, id: cid, on: rnd() < 0.5 };
  if (r < 0.9) return { op: 'charUrl', title, id: cid, url: rnd() < 0.9 ? pick(AV) : junk() };
  //  설정에 다른 모양 직접 넣기 (null 항목·배열 아닌 목록 등)
  const v = {};
  for (const t of ts.slice(0, 2)) v[t] = pick([{ characters: [{ id: 'k1', name: '하나' }, null, { id: 'k2', name: 'clyde' }] }, { characters: 3 }, { icon: 'data:image/png;base64,QQ==', characters: [{ id: 'k3', name: 'x', off: true, avatarUntil: '12' }] }, null, 5]);
  return { op: 'folders', v: rnd() < 0.1 ? junk() : v };
}
function jsOp(m) {
  let r;
  try { r = H[m.op](m); } catch (e) { r = { ok: false, error: 'badfile' }; }   //  지금 앱은 예외 → 창에서 badfile
  const fold = store.get('hookFolders');
  return JSON.stringify({ r: r.ok ? null : r.error, id: r.id || null, list: secrets.titleHookList(), icons: hookIcons(), chars: hookCharMap(), folders: fold === undefined ? null : fold });
}

(async () => {
  const cdir = fs.mkdtempSync(path.join(os.tmpdir(), 'hkcli-'));
  const c = spawn(EXE, [cdir], { stdio: ['pipe', 'pipe', 'inherit'], env: Object.assign({}, process.env, { TOKU_NATIVE_TEST_SECRETS: C_SEC }) });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  let fail = 0;
  const count = {};
  for (let i = 0; i < N; i++) {
    const m = JSON.parse(JSON.stringify(op()));
    const want = jsOp(m);
    c.stdin.write(JSON.stringify(m) + '\n');
    const got = await next();
    count[m.op] = (count[m.op] || 0) + 1;
    //  비밀 파일: titleWebhooks 와 나머지 값이 같아야 (JS 는 들여쓰기, C 는 한 줄 — 내용으로 비교)
    const a = JSON.parse(fs.readFileSync(JS_SEC, 'utf8')), b = JSON.parse(fs.readFileSync(C_SEC, 'utf8'));
    const same = got === want && JSON.stringify(a) === JSON.stringify(b);
    if (!same) {
      fail++;
      if (fail <= 3) {
        console.log('X 다름 #' + i + ' ' + m.op + '  ' + JSON.stringify(m).slice(0, 300));
        let k = 0; while (k < want.length && want[k] === got[k]) k++;
        console.log('   JS: …' + want.slice(Math.max(0, k - 120), k + 200));
        console.log('   C : …' + got.slice(Math.max(0, k - 120), k + 200));
        if (JSON.stringify(a) !== JSON.stringify(b)) console.log('   파일 다름');
      }
    }
  }
  c.stdin.end();
  fs.rmSync(TMP, { recursive: true, force: true });
  fs.rmSync(cdir, { recursive: true, force: true });
  console.log(fail ? '실패 ' + fail + '/' + N : '전부 같음 — 명령 ' + N + '개 (' + Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · ') + ')');
  process.exit(fail ? 1 : 0);
})();
