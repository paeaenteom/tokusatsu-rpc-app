// 배경 꾸미기 파일 흐름 교차 점검 — 진짜 appearance.js(createAppearance, 대화상자·저장소만 흉내)와 C(bg_cli.exe)
//  고르기 → 임시 사본 → 적용(이름 바꿔 확정·이전 파일 정리) → 버리기 → 복원 → 시작 정리 를 무작위로. 둘 다 시험 폴더에서만
//  임시 사본 이름의 무작위 부분은 고른 순서 번호로 바꿔 비교한다. 사용: node tests/bg-flow-diff.js <bg_cli.exe> [수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os'); const zlib = require('zlib');
const { createAppearance } = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/appearance.js');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 600);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'bgflow-'));
const JSDIR = path.join(TMP, 'js');
const SRC = path.join(TMP, 'src');
const CSET = path.join(TMP, 'cset');
fs.mkdirSync(JSDIR, { recursive: true }); fs.mkdirSync(SRC, { recursive: true }); fs.mkdirSync(CSET, { recursive: true });
const CDIR = path.join(process.env.APPDATA, 'toku-rpc-native-bgtest');
fs.rmSync(CDIR, { recursive: true, force: true });

// ── 고를 파일들 ──
let crcT = null;
const crc = (b) => { if (!crcT) { crcT = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crcT[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = crcT[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty, 'latin1'), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
function png(w, h, anim) { const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6; return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), anim ? chunk('acTL', Buffer.alloc(8)) : Buffer.alloc(0), chunk('IDAT', zlib.deflateSync(Buffer.alloc(64))), chunk('IEND', Buffer.alloc(0))]); }
const FILES = {
  'a.png': png(640, 480), 'b.PNG': png(1920, 1080), 'c.jpg': Buffer.concat([Buffer.from([0xff, 0xd8, 0xff, 0xc0, 0, 11, 8, 1, 0x90, 2, 0x58, 1, 1, 0x11, 0]), Buffer.from([0xff, 0xda, 0, 4, 0, 0]), Buffer.alloc(10, 7), Buffer.from([0xff, 0xd9])]),
  'd.png': png(10, 10, true), 'e.png': png(9000, 10), 'f.webp': Buffer.from('RIFF0000WEBPxxxx'), 'g.gif': png(5, 5), 'h.png': Buffer.from([1, 2, 3]),
  '이름 가.png': png(300, 200), 'trunc.png': png(100, 100).subarray(0, 50),
};
for (const [n, b] of Object.entries(FILES)) fs.writeFileSync(path.join(SRC, n), b);
const PICKS = [...Object.keys(FILES), 'missing.png'];

// ── JS 쪽 ──
let nextPath = '';
const mem = {};
const store = { get: (k) => (k in mem ? JSON.parse(mem[k]) : undefined), set: (k, v) => { mem[k] = JSON.stringify(v); } };
const ap = createAppearance({ app: { getPath: () => JSDIR }, dialog: { showOpenDialog: async () => ({ canceled: false, filePaths: [nextPath] }) }, store, log: { info() {}, warn() {} }, getWindow: () => null, T: (k) => k });
const jsTokens = [], cTokens = [];
function norm(s, tokens) { let o = s; tokens.forEach((t, i) => { if (t) o = o.split(t).join('T' + i); }); return o; }
const jsFiles = () => { try { return fs.readdirSync(path.join(JSDIR, 'backgrounds')).sort(); } catch (e) { return []; } };

const OPTS = () => (rnd() < 0.1 ? { enabled: 'yes' } : { enabled: rnd() < 0.7, fit: pick(['cover', 'contain']), posX: int(1001), posY: int(1001), zoom: 100 + 5 * int(61), opacity: int(101), dim: int(91), blur: int(21) });
function op() {
  const r = rnd();
  if (r < 0.35) return { op: 'pick', file: pick(PICKS) };
  if (r < 0.65) return { op: 'apply', v: OPTS(), tk: pick(['last', 'last', 'prev', 'none', 'none', 'bogus']) };
  if (r < 0.75) return { op: 'discard', tk: pick(['last', 'prev', 'all']) };
  if (r < 0.82) return { op: 'reset' };
  if (r < 0.92) return { op: 'get' };
  return { op: 'cleanup' };
}
const tokOf = (list, k) => (k === 'last' ? list[list.length - 1] || 'none-yet' : k === 'prev' ? list[list.length - 2] || 'none-yet' : k === 'bogus' ? '0000000000000000' : null);

async function jsRun(m) {
  let res = { e: null, name: '', w: 0, h: 0, fmt: '', d: -1 };
  if (m.op === 'pick') {
    nextPath = path.join(SRC, m.file);
    const r = await ap.pick();
    if (r.ok) { jsTokens.push(r.token); Object.assign(res, { name: r.name, w: r.width, h: r.height, fmt: r.format }); } else Object.assign(res, { e: r.reason, name: r.name || '' });
    if (!r.ok) jsTokens.push(null);
  } else if (m.op === 'apply') {
    const r = await ap.apply(Object.assign({}, m.v, { token: tokOf(jsTokens.filter(Boolean), m.tk) }));
    if (!r.ok) res.e = r.reason;
  } else if (m.op === 'discard') {
    res.d = m.tk === 'all' ? (ap.discardPending() ? 1 : 0) : (ap.discardPending(tokOf(jsTokens.filter(Boolean), m.tk)) ? 1 : 0);
  } else if (m.op === 'reset') { const r = await ap.reset(); if (!r.ok) res.e = r.reason; }
  else if (m.op === 'get') { const r = await ap.get(); res.e = r.notice || null; }
  else ap.cleanup();
  const s = ap.get ? require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/appearance.js').sanitizeStored(store.get('appearance')) : {};
  const saved = { enabled: s.enabled, image: s.image, name: s.name, width: s.width, height: s.height, fit: s.fit, posX: s.posX, posY: s.posY, zoom: s.zoom, opacity: s.opacity, dim: s.dim, blur: s.blur };
  return norm(JSON.stringify(Object.assign(res, { saved, files: jsFiles().map((n) => norm(n, jsTokens)).sort() })), jsTokens);
}

(async () => {
  const c = spawn(EXE, [CSET], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  let fail = 0;
  const count = {};
  for (let i = 0; i < N; i++) {
    const m = op();
    const want = await jsRun(m);
    const wire = { op: m.op };
    if (m.op === 'pick') wire.path = path.join(SRC, m.file);
    if (m.op === 'apply') { wire.v = m.v; const t = tokOf(cTokens.filter(Boolean), m.tk); if (t) wire.token = t; }
    if (m.op === 'discard' && m.tk !== 'all') wire.token = tokOf(cTokens.filter(Boolean), m.tk);
    c.stdin.write(JSON.stringify(wire) + '\n');
    const raw = JSON.parse(await next());
    if (m.op === 'pick') cTokens.push(raw.e ? null : raw.token);
    if (raw.e) { raw.w = 0; raw.h = 0; raw.fmt = ''; }   //  실패면 지금 앱은 이름만 돌려준다
    const got = norm(JSON.stringify({ e: raw.e, name: raw.name, w: raw.w, h: raw.h, fmt: raw.fmt, d: raw.d, saved: raw.saved, files: raw.files.map((n) => norm(n, cTokens)).sort() }), cTokens);
    count[m.op] = (count[m.op] || 0) + 1;
    if (got !== want) { fail++; if (fail <= 4) { console.log('X 다름 #' + i + ' ' + JSON.stringify(m)); console.log('   JS: ' + want.slice(0, 500)); console.log('   C : ' + got.slice(0, 500)); } }
  }
  c.stdin.end();
  await new Promise((r) => c.on('exit', r));
  fs.rmSync(TMP, { recursive: true, force: true });
  fs.rmSync(CDIR, { recursive: true, force: true });
  console.log(fail ? '실패 ' + fail + '/' + N : '전부 같음 — ' + N + '건 (' + Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · ') + ')');
  process.exit(fail ? 1 : 0);
})();
