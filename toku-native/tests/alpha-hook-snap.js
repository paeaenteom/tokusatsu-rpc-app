// 웹훅 탭 그림 점검 (시험 모드 · 포트 17693) — 시험용 비밀 파일(가짜 웹훅)과 시험 설정(폴더 아이콘·캐릭터)으로 띄우고
//  끝낼 때 탭 그림을 BMP 로 받는다. 시험 폴더 config.json 은 끝나면 되돌린다. 진짜 secrets.json 은 안 씀
//  사용: node tests/alpha-hook-snap.js <exe> <저장 폴더>
'use strict';
const net = require('net'); const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const EXE = path.resolve(process.argv[2]);
//  exe 가 없으면 시험 설정을 건드리기 전에 멈춘다 (11/14: 없는 exe 로 돌려 설정이 시험용으로 남은 사고)
if (!fs.existsSync(EXE)) { console.log('X   exe 없음: ' + EXE); process.exit(1); }
const OUT = path.resolve(process.argv[3]);
const PORT = 17693;
const PREFIX = 'toku-hook-snap-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const LOG = path.join(DIR, 'logs', 'main.log');
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

//  64×64 아이콘 (파랑·노랑 반반)
function png64(tint) {
  const rows = [];
  for (let y = 0; y < 64; y++) { const r = [0]; for (let x = 0; x < 64; x++) r.push(...(x < 32 ? [40, 110, 230, 255] : [240, 200, tint === undefined ? 40 : tint, 255])); rows.push(Buffer.from(r)); }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(64, 0); ih.writeUInt32BE(64, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
}

const pipe = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
  const enc = (op, o) => { const d = Buffer.from(JSON.stringify(o)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 8) {
      const len = buf.readInt32LE(4);
      if (buf.length < 8 + len) break;
      const op = buf.readInt32LE(0);
      const j = JSON.parse(buf.slice(8, 8 + len).toString());
      buf = buf.slice(8 + len);
      if (op === 0) sock.write(enc(1, { cmd: 'DISPATCH', evt: 'READY', data: { v: 1, user: { id: '1', username: 't' } }, nonce: null }));
      if (op === 1 && j.cmd === 'SET_ACTIVITY') sock.write(enc(1, { cmd: 'SET_ACTIVITY', data: j.args.activity || null, evt: null, nonce: j.nonce }));
    }
  });
  sock.on('error', () => {});
});

(async () => {
  const saved = fs.existsSync(CFG) ? fs.readFileSync(CFG) : null;
  const logStart = fs.existsSync(LOG) ? fs.readFileSync(LOG, 'utf8').length : 0;
  fs.mkdirSync(DIR, { recursive: true });
  fs.mkdirSync(OUT, { recursive: true });
  const icon = 'data:image/png;base64,' + png64().toString('base64');
  //  폴더 120개 더 (아이콘마다 다른 그림) — 그림 칸(96)보다 많아도 보이는 아이콘은 나와야 한다 (11/14 버그)
  const EXTRA = Array.from({ length: 120 }, (_, i) => 'テスト作品' + String(i + 1).padStart(3, '0'));
  const EXTRA_FOLDERS = Object.fromEntries(EXTRA.map((t, i) => [t, { icon: 'data:image/png;base64,' + png64(i).toString('base64') }]));
  const EXTRA_HOOKS = EXTRA.map((t, i) => ({ title: t, url: 'https://discord.com/api/webhooks/9' + String(i).padStart(17, '0') + '/' + 'x'.repeat(40) }));
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: false }, lang: 'ko', hookFolders: {
    '仮面ライダーフォーゼ': { icon, characters: [{ id: 'aa11', name: '如月弦太朗', avatar: '', avatarUntil: 0 }, { id: 'bb22', name: '歌星賢吾', avatar: 'https://example.invalid/a.png', avatarUntil: Date.now() + 36e5, off: true }] },
    ...EXTRA_FOLDERS,
  } }));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooksnap-'));
  const SEC = path.join(tmp, 'secrets.json');
  fs.writeFileSync(SEC, JSON.stringify({ bingeWebhookUrl: 'https://discord.com/api/webhooks/1/default', titleWebhooks: [
    { title: '仮面ライダーフォーゼ', url: 'https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz', name: '메인' },
    { title: '仮面ライダーフォーゼ', url: 'https://discord.com/api/webhooks/223456789012345679/bbbbbbbbbbbbbbbbbbbbbbbbbbbb' },
    { title: 'ウルトラマンブレーザー', url: 'https://discord.com/api/webhooks/323456789012345670/cccccccccccccccccccccccc' },
    ...EXTRA_HOOKS,
  ] }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SECRETS: SEC, TOKU_NATIVE_TEST_SNAP: OUT });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  app.on('error', (e) => { console.log('X   실행 실패 ' + e.message); });   //  죽지 말고 아래에서 설정을 되돌린다
  await wait(2500);
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  pipe.close();
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);
  fs.rmSync(tmp, { recursive: true, force: true });
  const ok = fs.existsSync(path.join(OUT, 'tab5@100.bmp')) && fs.existsSync(path.join(OUT, 'tab5@150.bmp'));
  console.log((ok ? 'OK  ' : 'X   ') + '웹훅 탭 그림 (' + OUT + ')');
  //  검색 (2026-11-13): '포제' → 仮面ライダーフォーゼ 하나 · 'zzqq' → 없음 — 배율 100·150 둘 다
  const log = fs.existsSync(LOG) ? fs.readFileSync(LOG, 'utf8').slice(logStart) : '';
  const s0 = (log.match(/\[Test\] hook search 0: (\d+)\/(\d+)/g) || []);
  const s1 = (log.match(/\[Test\] hook search 1: (\d+)\/(\d+)/g) || []);
  const okS = s0.length >= 2 && s0.every((l) => / 1\/122$/.test(l)) && s1.length >= 2 && s1.every((l) => / 0\/122$/.test(l))
    && fs.existsSync(path.join(OUT, 'tab5q0@100.bmp')) && fs.existsSync(path.join(OUT, 'tab5q1@150.bmp'));
  console.log((okS ? 'OK  ' : 'X   ') + '웹훅 검색 거르기 ' + JSON.stringify(s0.concat(s1)));
  //  입력 창 자리 (11/13 버그): 글이 생기면 칸이 '지우기' 만큼 좁아지고 입력 창도 그 안으로 따라 들어와야 한다
  const ed = [...log.matchAll(/\[Test\] hook search edit: field (\d+)-(\d+) edit (\d+)-(\d+) -> field (\d+)-(\d+) edit (\d+)-(\d+) q=(.*)/g)];
  const okE = ed.length >= 2 && ed.every((m) => {
    const [f0l, f0r, e0l, e0r, f1l, f1r, e1l, e1r] = m.slice(1, 9).map(Number);
    return f1r < f0r && e0r > f1r && e1l >= f1l && e1r <= f1r && e1r < e0r && m[9].trim() === 'dasdasdD';
  });
  console.log((okE ? 'OK  ' : 'X   ') + '검색 입력 창이 좁아진 칸에 맞춰 옮겨짐 ' + JSON.stringify(ed.map((m) => m[0].replace('[Test] hook search edit: ', ''))));
  const ic = [...log.matchAll(/\[Test\] hook icon req=(\d+) drawn=(\d+)/g)].map((m) => [Number(m[1]), Number(m[2])]);
  const okI = ic.length >= 2 && ic.every(([r, d]) => r >= 1 && r <= 20 && d === r);
  console.log((okI ? 'OK  ' : 'X   ') + '폴더 122개: 보이는 아이콘만 요청하고 다 그려짐 (요청·그림) ' + JSON.stringify(ic));
  //  그리기 시간 (11/14 렉): 폴더 122개 — 다 펼침·다 접힘 한 번 그리기 100% 20ms · 150% 35ms 아래
  const pt = [...log.matchAll(/\[Test\] hook paint open=([\d.]+)ms closed=([\d.]+)ms/g)].map((m) => [Number(m[1]), Number(m[2])]);
  const LIM = [20, 35];   //  100% · 150% (고치기 전 40~80ms)
  const okP = pt.length >= 2 && pt.slice(-2).every(([o, c], i) => o < LIM[i] && c < LIM[i]);
  console.log((okP ? 'OK  ' : 'X   ') + '웹훅 탭 그리기 시간 ms [펼침, 접힘] ' + JSON.stringify(pt));
  process.exit(ok && okS && okE && okI && okP ? 0 : 1);
})();
