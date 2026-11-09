// 지연 표시 안정 점검 — 가짜 확장(pong)·가짜 Discord(SET_ACTIVITY 답)가 세 번에 한 번 11ms 늦게 답할 때
//  트레이에 보일 값(최근 5번 중간값)은 튀지 않고, 실제 잰 값(raw)은 튀는지 본다. 시험 모드 알파 · 포트 17689
//  사용: node tests/latency-jitter-test.js <exe>
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const PORT = 17689;
const PREFIX = 'toku-jitter-' + process.pid + '-';
const LOG = path.join(process.env.APPDATA, 'toku-rpc-native-test', 'logs', 'main.log');
const SLOW = 11;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const enc = (op, o) => { const d = Buffer.from(JSON.stringify(o)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(56) + (e !== undefined && !c ? ' ' + JSON.stringify(e) : '')); if (!c) fail++; };

let acts = 0;
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
      if (op === 1 && j.cmd === 'SET_ACTIVITY') {
        const out = enc(1, { cmd: 'SET_ACTIVITY', data: j.args.activity || null, evt: null, nonce: j.nonce });
        if (++acts % 3 === 0) setTimeout(() => sock.write(out), SLOW); else sock.write(out);   //  세 번에 한 번 늦게
      }
    }
  });
  sock.on('error', () => {});
});

(async () => {
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const start = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(start).toString('utf8') : '');
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  let ws = null;
  for (let k = 0; k < 100 && !ws; k++) {
    ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm', autoPong: false }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
    if (!ws) await wait(100);
  }
  if (!ws) { console.log('X 포트에 못 붙음'); app.kill(); process.exit(1); }
  let pings = 0;
  ws.on('ping', (data) => { if (++pings % 3 === 0) setTimeout(() => ws.pong(data), SLOW); else ws.pong(data); });   //  세 번에 한 번 늦게
  ws.on('message', () => {});
  await wait(500);
  const N = 15;
  for (let i = 0; i < N; i++) {
    ws.send(JSON.stringify({ type: 'VIDEO', site: 'youtube', seriesName: 'jitter ' + i, episodeTitle: 'ch', currentTime: i, duration: 600, isPlaying: true, url: 'https://www.youtube.com/watch?v=dQw4w9WgXcQ' }));
    await wait(700);
    ws.send(JSON.stringify({ type: 'TEST_LATENCY' }));
    await wait(300);
  }
  await wait(300);
  const rows = [...logText().matchAll(/\[Test\] latency ext=(-?[\d.]+) clients=\d+ discord=(-?[\d.]+) connected=\d raw_ext=(-?[\d.]+)/g)].map((m) => ({ ext: +m[1], dc: +m[2], raw: +m[3] }));
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  pipe.close();
  try { ws.close(); } catch {}
  const after = rows.slice(4);   //  처음 몇 번은 표본이 모자람
  console.log('  실제(raw) 확장: ' + rows.map((r) => r.raw.toFixed(1)).join(' '));
  console.log('  표시 확장:      ' + rows.map((r) => r.ext.toFixed(1)).join(' '));
  console.log('  표시 Discord:   ' + rows.map((r) => r.dc.toFixed(1)).join(' '));
  ok('표본 ' + N + '번', rows.length === N, rows.length);
  ok('실제 확장 왕복은 튐 (10ms 넘는 것 있음 — 시험이 제대로 늦췄나)', rows.some((r) => r.raw >= SLOW - 1));
  ok('표시 확장은 안 튐 (5번째부터 모두 5ms 아래)', after.length && after.every((r) => r.ext >= 0 && r.ext < 5), after.map((r) => r.ext));
  ok('표시 Discord 도 안 튐 (5번째부터 모두 5ms 아래)', after.length && after.every((r) => r.dc >= 0 && r.dc < 5), after.map((r) => r.dc));
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})();
