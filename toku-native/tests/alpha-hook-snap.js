// 웹훅 탭 그림 점검 (시험 모드 · 포트 17693) — 시험용 비밀 파일(가짜 웹훅)과 시험 설정(폴더 아이콘·캐릭터)으로 띄우고
//  끝낼 때 탭 그림을 BMP 로 받는다. 시험 폴더 config.json 은 끝나면 되돌린다. 진짜 secrets.json 은 안 씀
//  사용: node tests/alpha-hook-snap.js <exe> <저장 폴더>
'use strict';
const net = require('net'); const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const EXE = path.resolve(process.argv[2]);
const OUT = path.resolve(process.argv[3]);
const PORT = 17693;
const PREFIX = 'toku-hook-snap-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

//  64×64 아이콘 (파랑·노랑 반반)
function png64() {
  const rows = [];
  for (let y = 0; y < 64; y++) { const r = [0]; for (let x = 0; x < 64; x++) r.push(...(x < 32 ? [40, 110, 230, 255] : [240, 200, 40, 255])); rows.push(Buffer.from(r)); }
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
  fs.mkdirSync(DIR, { recursive: true });
  fs.mkdirSync(OUT, { recursive: true });
  const icon = 'data:image/png;base64,' + png64().toString('base64');
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: false }, lang: 'ko', hookFolders: {
    '仮面ライダーフォーゼ': { icon, characters: [{ id: 'aa11', name: '如月弦太朗', avatar: '', avatarUntil: 0 }, { id: 'bb22', name: '歌星賢吾', avatar: 'https://example.invalid/a.png', avatarUntil: Date.now() + 36e5, off: true }] },
  } }));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooksnap-'));
  const SEC = path.join(tmp, 'secrets.json');
  fs.writeFileSync(SEC, JSON.stringify({ bingeWebhookUrl: 'https://discord.com/api/webhooks/1/default', titleWebhooks: [
    { title: '仮面ライダーフォーゼ', url: 'https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz', name: '메인' },
    { title: '仮面ライダーフォーゼ', url: 'https://discord.com/api/webhooks/223456789012345679/bbbbbbbbbbbbbbbbbbbbbbbbbbbb' },
    { title: 'ウルトラマンブレーザー', url: 'https://discord.com/api/webhooks/323456789012345670/cccccccccccccccccccccccc' },
  ] }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SECRETS: SEC, TOKU_NATIVE_TEST_SNAP: OUT });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
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
  process.exit(ok ? 0 : 1);
})();
