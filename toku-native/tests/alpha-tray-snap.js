// 알파 트레이 메뉴 그림 점검 — 가짜 Discord 파이프 + 가짜 확장(포트 17698) 을 붙이고 TEST_TRAY_SNAP 으로 메뉴 그림(tray@100·tray@150.bmp)을 받는다
//  ⚠ 진짜 7690·Discord 에는 닿지 않는다. 화면에도 띄우지 않는다 (메모리 DC). App ID 값은 출력하지 않는다
//  사용: node tests/alpha-tray-snap.js <exe> <저장 폴더>
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const OUT = path.resolve(process.argv[3]);
const PORT = 17698;
const PREFIX = 'toku-alpha-tray-' + process.pid + '-';
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const enc = (op, obj) => { const d = Buffer.from(JSON.stringify(obj)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };

const srv = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 8) {
      const len = buf.readInt32LE(4);
      if (buf.length < 8 + len) break;
      const op = buf.readInt32LE(0);
      const json = JSON.parse(buf.slice(8, 8 + len).toString());
      buf = buf.slice(8 + len);
      if (op === 0) sock.write(enc(1, { cmd: 'DISPATCH', evt: 'READY', data: { v: 1, user: { id: '1', username: 'snap-tester' } }, nonce: null }));
      if (op === 1 && json.cmd === 'SET_ACTIVITY') sock.write(enc(1, { cmd: 'SET_ACTIVITY', data: json.args.activity || null, evt: null, nonce: json.nonce }));
    }
  });
  sock.on('error', () => {});
});

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  await new Promise((r) => srv.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SNAP: OUT });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  let sock = null;
  for (let k = 0; k < 100 && !sock; k++) {
    sock = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
    if (!sock) await wait(100);
  }
  if (!sock) { console.log('X 포트에 못 붙음'); app.kill(); process.exit(1); }
  sock.on('message', () => {});
  await wait(800);
  //  재생 중 YouTube (재호스팅 전이라 앱 로고 에셋) + 일시정지 TTFC (https 썸네일 직접 받기)
  sock.send(JSON.stringify({ type: 'VIDEO', site: 'youtube', seriesName: 'Rick Astley - Never Gonna Give You Up (Official Video)', episodeTitle: 'Rick Astley', currentTime: 61, duration: 213, isPlaying: true, thumbnail: 'https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg', url: 'https://www.youtube.com/watch?v=dQw4w9WgXcQ' }));
  sock.send(JSON.stringify({ type: 'VIDEO', site: 'ttfc', seriesName: '仮面ライダー響鬼', episodeNumber: '七之巻', episodeTitle: '息吹く鬼', currentTime: 1337, duration: 1422, isPlaying: false, thumbnail: 'https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg', url: 'https://pc.tokusatsu-fc.jp/watch/1' }));
  await wait(1500);
  for (const f of ['tray@100.bmp', 'tray@150.bmp']) fs.rmSync(path.join(OUT, f), { force: true });
  sock.send(JSON.stringify({ type: 'TEST_TRAY_SNAP' }));
  const t0 = Date.now();
  while (Date.now() - t0 < 5000 && !fs.existsSync(path.join(OUT, 'tray@150.bmp'))) await wait(100);
  await wait(300);
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  const ok = ['tray@100.bmp', 'tray@150.bmp'].filter((f) => fs.existsSync(path.join(OUT, f)) && fs.statSync(path.join(OUT, f)).size > 1000);
  console.log((ok.length === 2 ? 'OK  ' : 'X   ') + '트레이 메뉴 그림 ' + ok.length + '장 (' + OUT + ')');
  const pass = ok.length === 2;
  srv.close();
  try { sock.close(); } catch {}
  process.exit(pass ? 0 : 1);
})();
