// 배경 꾸미기 그림 점검 (시험 모드 · 포트 17692) — 시험 폴더에 배경 사진·설정을 넣고 띄워 탭 그림을 받는다
//  시험 폴더(toku-rpc-native-test)만 쓴다. config.json 은 되돌리고 넣은 사진은 지운다
//  사용: node tests/alpha-bg-snap.js <exe> <저장 폴더>
'use strict';
const net = require('net'); const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const EXE = path.resolve(process.argv[2]);
const OUT = path.resolve(process.argv[3]);
const PORT = 17692;
const PREFIX = 'toku-bg-snap-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const BGDIR = path.join(DIR, 'backgrounds');
const IMG = 'bg-0123456789abcdef.png';
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

//  640×400 그림: 하늘(파랑→보라) 위에 노란 해, 아래 초록 언덕
function scene(w, h) {
  const rows = [];
  for (let y = 0; y < h; y++) {
    const r = [0];
    for (let x = 0; x < w; x++) {
      let c;
      const dx = x - w * 0.7, dy = y - h * 0.3;
      if (dx * dx + dy * dy < (h * 0.12) ** 2) c = [250, 210, 60];
      else if (y > h * 0.65 + Math.sin(x / 40) * 18) c = [40, 150, 80];
      else c = [Math.round(40 + 80 * y / h), Math.round(80 + 40 * y / h), 220];
      r.push(...c, 255);
    }
    rows.push(Buffer.from(r));
  }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
}
const pipe = net.createServer((sock) => { sock.on('error', () => {}); });

(async () => {
  const saved = fs.existsSync(CFG) ? fs.readFileSync(CFG) : null;
  fs.mkdirSync(BGDIR, { recursive: true });
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(BGDIR, IMG), scene(640, 400));
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: false }, lang: 'ko', appearance: { enabled: true, image: IMG, name: 'scene.png', width: 640, height: 400, fit: 'cover', posX: 500, posY: 500, zoom: 100, opacity: 100, dim: 40, blur: 4 } }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SNAP: OUT });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  await wait(2000);
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  pipe.close();
  const after = JSON.parse(fs.readFileSync(CFG, 'utf8'));
  const kept = fs.existsSync(path.join(BGDIR, IMG));
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);
  try { fs.unlinkSync(path.join(BGDIR, IMG)); } catch (e) { /* 이미 없음 */ }
  const ok = fs.existsSync(path.join(OUT, 'tab4@100.bmp')) && kept && after.appearance && after.appearance.image === IMG;
  console.log((ok ? 'OK  ' : 'X   ') + '배경 그림 (' + OUT + ') · 설정이 가리키는 사진은 정리에서 남김: ' + kept);
  process.exit(ok ? 0 : 1);
})();
