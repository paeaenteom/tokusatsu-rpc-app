// 모드 전환 점검 — 시험 모드 앱(포트 17687)에 TEST_SET_CHANNEL 을 보내면 설정을 저장하고 스스로 다시 시작해 그 모드로 뜨는지
//  베타 → 테스트 버전(알파) → 베타. 시험 폴더(toku-rpc-native-test) 설정만 쓰고 끝나면 원래대로 돌린다. 진짜 7690·Discord 안 건드림
//  사용: node tests/channel-switch-test.js <exe>
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const PORT = 17687;
const PREFIX = 'toku-chswitch-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const LOG = path.join(DIR, 'logs', 'main.log');
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(58) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 300) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 15000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = fn(); if (v) return v; await wait(100); } return null; };
const enc = (op, o) => { const d = Buffer.from(JSON.stringify(o)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };
const pipe = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 8) {
      const len = buf.readInt32LE(4); if (buf.length < 8 + len) break;
      const op = buf.readInt32LE(0); const j = JSON.parse(buf.slice(8, 8 + len).toString()); buf = buf.slice(8 + len);
      if (op === 0) sock.write(enc(1, { cmd: 'DISPATCH', evt: 'READY', data: { v: 1, user: { id: '1', username: 't' } }, nonce: null }));
      if (op === 1 && j.cmd === 'SET_ACTIVITY') sock.write(enc(1, { cmd: 'SET_ACTIVITY', data: j.args.activity || null, evt: null, nonce: j.nonce }));
    }
  });
  sock.on('error', () => {});
});
const connect = async () => {
  for (let k = 0; k < 150; k++) {
    const ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
    if (ws) { ws.on('message', () => {}); ws.on('error', () => {}); return ws; }
    await wait(100);
  }
  return null;
};
const readCfg = () => { try { return JSON.parse(fs.readFileSync(CFG, 'utf8')); } catch (e) { return {}; } };

(async () => {
  const saved = fs.existsSync(CFG) ? fs.readFileSync(CFG) : null;
  fs.mkdirSync(DIR, { recursive: true });
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: true }, lang: 'ko' }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const start = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(start).toString('utf8') : '');
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX });
  const first = spawn(EXE, [], { env, stdio: 'ignore' });
  try {
    let ws = await connect();
    ok('처음엔 베타 (설정 없음)', ws && await until(() => /=== TOKU RPC 시작 \(C 베타 0\.1\.0\) ===/.test(logText())));
    ok('  베타는 V2 줄이 없음', !/전송 V2/.test(logText()));

    ws.send(JSON.stringify({ type: 'TEST_SET_CHANNEL', on: true }));
    const exited = await until(() => first.exitCode !== null, 20000);
    ok('스위치 켬 → 설정 저장 · 앞 프로세스 정상 종료', exited && readCfg().testChannel === true && /\[Channel\] 테스트 버전\(알파\) 모드로 바꾸려고 다시 시작합니다/.test(logText()), { exit: first.exitCode, cfg: readCfg().testChannel });
    const alphaUp = await until(() => /=== TOKU RPC 시작 \(C 알파 0\.2\.0\) ===/.test(logText()), 20000);
    ok('  스스로 다시 떠서 알파 0.2.0 · "모드를 바꿔 다시 시작"', alphaUp && /\[Channel\] 모드를 바꿔 다시 시작했습니다/.test(logText()));
    ok('  알파는 전송 V2 를 켬', /\[Channel\] 테스트 버전\(알파\) — 전송 V2/.test(logText()));
    try { ws.close(); } catch {}
    ws = await connect();
    ok('  새 프로세스가 포트를 다시 잡고 확장을 받음', !!ws);

    const before = logText().length;
    ws.send(JSON.stringify({ type: 'TEST_SET_CHANNEL', on: false }));
    const betaUp = await until(() => /=== TOKU RPC 시작 \(C 베타 0\.1\.0\) ===/.test(logText().slice(before)), 20000);
    ok('스위치 끔 → 다시 시작해서 베타로 돌아옴', betaUp && readCfg().testChannel === false);
    try { ws.close(); } catch {}
    ws = await connect();
    ok('  다시 포트를 잡음', !!ws);
    try { ws.close(); } catch {}
  } catch (err) { ok('예외 없음', false, err.message); }
  spawnSync(EXE, ['--quit'], { env });
  await wait(1500);
  pipe.close();
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})();
