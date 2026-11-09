// 정주행 기록기 교차 점검 — 진짜 binge-webhook.js · secrets.js · thumb-color.js 와 C(binge_cli.exe)를 같은 입력으로
//   1) 카드 색 (accentFromPixels) 2) 보낼 곳 고르기 (pickTarget: NFKC·공백·대소문자·가장 긴 이름·주소 검사)
//   3) 캐릭터 고르기 (characterFields: 켜진 것만·금지어·이름 다듬기·사진 주소·만료)
//  비밀 파일은 임시 폴더에만 쓴다 (진짜 secrets.json 은 안 건드림). 사용: node tests/binge-diff.js <binge_cli.exe> [수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const Module = require('module');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 2000);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'bingediff-'));
const SEC = path.join(TMP, 'toku-rpc', 'secrets.json');
fs.mkdirSync(path.dirname(SEC), { recursive: true });
process.env.APPDATA = TMP;   // secrets.js 가 여기를 읽는다
delete process.env.TOKU_BINGE_WEBHOOK;
//  electron·electron-log 흉내 (Node 로 진짜 모듈을 그대로 읽으려고)
const realLoad = Module._load;
Module._load = function (req, parent, isMain) {
  if (req === 'electron-log') return { info() {}, warn() {}, error() {} };
  if (req === 'electron') return {};
  return realLoad.apply(this, arguments);
};
const SRC = 'C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/';
const fresh = (m) => { delete require.cache[require.resolve(SRC + m)]; return require(SRC + m); };
const { accentFromPixels } = require(SRC + 'thumb-color.js');

// ── 무작위 값 ──
const HOOKS = ['https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz_-01',
  'https://ptb.discord.com/api/webhooks/123456789012345/AAAAAAAAAAAAAAAAAAAA', 'https://canary.discordapp.com/api/v10/webhooks/1234567890123456789012345/xxxxxxxxxxxxxxxxxxxxxxxxx',
  'https://discord.com/api/webhooks/12345678901234/aaaaaaaaaaaaaaaaaaaaaaaa', 'https://discord.com/api/webhooks/123456789012345678/short',
  'http://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz', ' https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz ',
  'https://discord.com/api/v/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz', 'https://evil.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz',
  'https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz/x', 'https://discordapp.com/api/v9/webhooks/123456789012345678/abcdefghij-klmnopqrst'];
const PARTS = ['仮面ライダー', 'フォーゼ', 'ﾌｫｰｾﾞ', 'Ｋａｍｅｎ', 'kamen', 'KAMEN', ' ', '\u3000', '\t', '\u00a0', 'Rider', 'ウルトラマン', 'ブレーザー', '①', '㍻', 'Ⅳ', 'é', 'e\u0301', 'ガ', 'ｶﾞ', '戦隊', 'ゴジュウジャー', 'x', '\ufeff', '\u2003'];
const rtitle = (k) => Array.from({ length: 1 + int(k) }, () => pick(PARTS)).join('');
const rjunk = () => pick([null, 3, true, [], {}, '', undefined]);
//  같은 작품명을 공백·대소문자·전각/반각만 다르게 (정규화 비교를 실제로 시험)
const SPACES = ['', ' ', '  ', '\t', '\u3000', '\u00a0', '\ufeff', '\u2003', '\n'];
function vary(t) {
  if (rnd() < 0.3) return t;
  let out = '';
  for (const ch of t) {
    if (/\s/.test(ch)) { out += pick(SPACES); continue; }
    if (/[a-z]/.test(ch) && rnd() < 0.4) { out += ch.toUpperCase(); continue; }
    if (/[A-Za-z]/.test(ch) && rnd() < 0.3) { out += String.fromCharCode(ch.charCodeAt(0) + 0xfee0); continue; }   // 전각
    out += ch;
    if (rnd() < 0.08) out += pick(SPACES);
  }
  return out;
}
function rsecrets() {
  const s = {};
  if (rnd() < 0.7) s.bingeWebhookUrl = pick(['https://discord.com/api/webhooks/1/default', '', 'x']);
  if (rnd() < 0.9) s.titleWebhooks = rnd() < 0.05 ? rjunk() : Array.from({ length: int(8) }, () => (rnd() < 0.08 ? rjunk() : { title: rnd() < 0.9 ? rtitle(3) : rjunk(), url: rnd() < 0.85 ? pick(HOOKS) : rjunk(), name: rnd() < 0.3 ? 'n' : undefined }));
  return s;
}
const NAMES = ['라이토', 'Discord bot', 'clyde', 'CLYDE fan', '  ', ' 하루  카  ', '\u3000ミク\u3000', 'x'.repeat(90), '😀'.repeat(45), 'ﾌｫｰｾﾞ', 'ok\ttab', 'ＤＩＳＣＯＲＤ'];
const AVATARS = ['https://a.b/c.png', 'http://a.b/c.png', 'https://a b/c', 'https://a.b/"x', 'https://', '', 'https://x.y/\u3000', 'https://ok.example/p.webp'];
function rchar(fixed) {
  const c = { id: rnd() < 0.9 ? 'c' + int(99) : rjunk(), name: rnd() < 0.9 ? pick(NAMES) : rjunk() };
  if (rnd() < 0.8) c.avatar = rnd() < 0.9 ? pick(AVATARS) : rjunk();
  if (rnd() < 0.5) c.avatarUntil = pick([0, 1, Date.now() + 864e5 * 365, '9999999999999', null]);
  if (!fixed && rnd() < 0.3) c.off = pick([true, false, 'true', 1]);
  return c;
}
function rfolders(title) {
  if (rnd() < 0.05) return rjunk();
  const f = {};
  const t = rnd() < 0.8 ? title : rtitle(2);
  f[t] = rnd() < 0.05 ? rjunk() : { characters: rnd() < 0.1 ? rjunk() : Array.from({ length: int(6) }, () => (rnd() < 0.08 ? rjunk() : rchar(false))) };
  if (rnd() < 0.3) f[rtitle(2)] = { characters: [rchar(false)] };
  return f;
}

function jsOp(m) {
  Math.random = () => m.r;
  if (m.op === 'accent') {
    const c = accentFromPixels(Buffer.from(m.px), { r: 2, g: 1, b: 0, a: 3 });
    return JSON.stringify({ c: c === null ? null : c });
  }
  if (m.op === 'pick') {
    fresh('secrets.js');
    const bw = fresh('binge-webhook.js');
    const t = bw.pickTarget(m.series);
    return JSON.stringify({ url: t.url, title: t.title });
  }
  const bw = fresh('binge-webhook.js');
  const folders = m.folders || {};
  const hookChars = (title) => { const f = folders[title]; return (f && Array.isArray(f.characters)) ? f.characters.filter((c) => c && typeof c.id === 'string' && typeof c.name === 'string') : []; };
  bw.setCharacterSource((title) => hookChars(title).filter((c) => c.off !== true));
  const r = bw.characterFields(m.title, m.fixed || undefined);
  return JSON.stringify({ f: Object.keys(r).length ? r : null });
}

function op() {
  const r = pick([0, 0.4999, 0.9999, rnd()]);
  const k = rnd();
  if (k < 0.3) {
    const n = pick([0, 1, 5, 48 * 27, 300]);
    const px = [];
    const base = [int(256), int(256), int(256)];
    for (let i = 0; i < n; i++) {
      const near = rnd() < 0.6;
      px.push(near ? Math.max(0, Math.min(255, base[0] + int(40) - 20)) : int(256), near ? Math.max(0, Math.min(255, base[1] + int(40) - 20)) : int(256),
        near ? Math.max(0, Math.min(255, base[2] + int(40) - 20)) : int(256), rnd() < 0.9 ? 255 : int(256));
    }
    return { op: 'accent', px, r };
  }
  if (k < 0.7) {
    const sec = rsecrets();
    fs.writeFileSync(SEC, JSON.stringify(sec));
    let series = rtitle(5);
    const tw = Array.isArray(sec.titleWebhooks) ? sec.titleWebhooks.filter((h) => h && typeof h.title === 'string') : [];
    if (tw.length && rnd() < 0.6) series = rtitle(1) + vary(pick(tw).title) + rtitle(1);
    return { op: 'pick', series, r };
  }
  const title = rtitle(2);
  return { op: 'chars', folders: rfolders(title), title, fixed: rnd() < 0.2 ? rchar(true) : null, r };
}

(async () => {
  const cdir = fs.mkdtempSync(path.join(os.tmpdir(), 'bingecli-'));
  const c = spawn(EXE, [cdir], { stdio: ['pipe', 'pipe', 'inherit'], env: Object.assign({}, process.env, { TOKU_NATIVE_TEST_SECRETS: SEC }) });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  let fail = 0;
  const count = {};
  for (let i = 0; i < N; i++) {
    const m0 = op();
    const m = JSON.parse(JSON.stringify(m0));   // C 가 받는 것과 같게 (undefined 빠짐)
    const want = jsOp(m);
    c.stdin.write(JSON.stringify(m) + '\n');
    const got = await next();
    count[m.op] = (count[m.op] || 0) + 1;
    if (got !== want) {
      fail++;
      if (fail <= 4) { console.log('X 다름 #' + i + ' ' + m.op); console.log('   입력: ' + JSON.stringify(m.op === 'pick' ? { series: m.series, r: m.r, secrets: JSON.parse(fs.readFileSync(SEC, 'utf8')) } : m).slice(0, 700)); console.log('   JS: ' + want.slice(0, 300)); console.log('   C : ' + got.slice(0, 300)); }
    }
  }
  c.stdin.end();
  fs.rmSync(TMP, { recursive: true, force: true });
  fs.rmSync(cdir, { recursive: true, force: true });
  console.log(fail ? '실패 ' + fail + '/' + N : '전부 같음 — ' + N + '건 (' + Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · ') + ')');
  process.exit(fail ? 1 : 0);
})();
