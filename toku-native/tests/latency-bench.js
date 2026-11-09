// 지연 측정 — 시험 모드 알파(포트 17692) 에 가짜 확장·가짜 Discord(바로 답함)를 붙이고
//  트레이 메뉴와 같은 방법(확장 ping 왕복 · Discord SET_ACTIVITY 왕복)으로 여러 번 재서 최소·중간·최대를 낸다
//  ⚠ 진짜 7690·Discord 는 안 건드린다. 화면에도 안 띄운다
//  사용: node tests/latency-bench.js <exe> [횟수] [--max-ext 기준ms] [--max-discord 기준ms]
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 20);
const arg = (k) => { const i = process.argv.indexOf(k); return i > 0 ? Number(process.argv[i + 1]) : null; };
const MAX_EXT = arg('--max-ext'), MAX_DC = arg('--max-discord');
const PORT = 17692;
const PREFIX = 'toku-lat-bench-' + process.pid + '-';
const LOG = path.join(process.env.APPDATA, 'toku-rpc-native-test', 'logs', 'main.log');
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const enc = (op, o) => { const d = Buffer.from(JSON.stringify(o)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };

const pipe = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
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
const stats = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? { min: s[0], med: s[Math.floor(s.length / 2)], max: s[s.length - 1], n: s.length } : null; };
const f = (v) => v.toFixed(3) + 'ms';

(async () => {
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const start = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(start).toString('utf8') : '');
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  let ws = null;
  for (let k = 0; k < 100 && !ws; k++) {
    ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
    if (!ws) await wait(100);
  }
  if (!ws) { console.log('X 포트에 못 붙음'); app.kill(); process.exit(1); }
  ws.on('message', () => {});
  await wait(500);
  const ext = [], dc = [];
  for (let i = 0; i < N; i++) {
    //  카드 내용을 바꿔 SET_ACTIVITY 가 나가게 (Discord 왕복 표본)
    ws.send(JSON.stringify({ type: 'VIDEO', site: 'youtube', seriesName: 'bench ' + i, episodeTitle: 'ch', currentTime: i, duration: 600, isPlaying: true, url: 'https://www.youtube.com/watch?v=dQw4w9WgXcQ' }));
    await wait(700);
    ws.send(JSON.stringify({ type: 'TEST_LATENCY' }));
    await wait(300);
  }
  await wait(500);
  for (const m of logText().matchAll(/\[Test\] latency ext=(-?[\d.]+) clients=(\d+) discord=(-?[\d.]+) connected=(\d)/g)) {
    if (Number(m[2]) > 0 && Number(m[1]) >= 0) ext.push(Number(m[1]));
    if (m[4] === '1' && Number(m[3]) >= 0) dc.push(Number(m[3]));
  }
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  pipe.close();
  try { ws.close(); } catch {}
  const se = stats(ext), sd = stats(dc);
  console.log('확장 왕복   ' + (se ? `최소 ${f(se.min)} · 중간 ${f(se.med)} · 최대 ${f(se.max)} (${se.n}번)` : '표본 없음'));
  console.log('Discord 왕복 ' + (sd ? `최소 ${f(sd.min)} · 중간 ${f(sd.med)} · 최대 ${f(sd.max)} (${sd.n}번)` : '표본 없음'));
  let bad = !se || !sd;
  if (MAX_EXT !== null && se && se.med > MAX_EXT) { console.log('X 확장 중간값이 기준 ' + MAX_EXT + 'ms 보다 큼'); bad = true; }
  if (MAX_DC !== null && sd && sd.med > MAX_DC) { console.log('X Discord 중간값이 기준 ' + MAX_DC + 'ms 보다 큼'); bad = true; }
  process.exit(bad ? 1 : 0);
})();
