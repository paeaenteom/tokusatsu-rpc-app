// 알파 창 그림 점검 — 가짜 Discord 파이프 + 가짜 확장(포트 17697) 으로 카드를 띄우고, 끝낼 때 탭 그림을 BMP 로 받는다
//  ⚠ 진짜 7690·Discord 에는 닿지 않는다. 화면에도 띄우지 않는다 (메모리 DC). App ID 값은 출력하지 않는다
//  사용: node tests/alpha-snap.js <exe> <저장 폴더>
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const OUT = path.resolve(process.argv[3]);
const PORT = 17697;
const PREFIX = 'toku-alpha-snap-' + process.pid + '-';
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
  await wait(3000);
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  const files = fs.readdirSync(OUT).filter((f) => /^tab\d@\d+\.bmp$/.test(f));   //  탭 7개 × 배율 2 (웹훅 탭 아래 그림 tab5b 는 빼고)
  console.log((files.length === 14 ? 'OK  ' : 'X   ') + '탭 그림 ' + files.length + '장 (' + OUT + ')');
  srv.close();
  try { sock.close(); } catch {}
  process.exit(files.length === 14 ? 0 : 1);
})();
