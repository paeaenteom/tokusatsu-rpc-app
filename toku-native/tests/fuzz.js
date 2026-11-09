// 퍼징 — 망가뜨린 입력을 수만 개 넣어 C 가 죽지 않고, JSON 판정이 JS 와 같은지
//  node tests\fuzz.js build\vec.exe build\ws_srv.exe
'use strict';
const { spawnSync, spawn } = require('child_process');
const net = require('net');
const crypto = require('crypto');
const [VEC, WSS] = process.argv.slice(2);
let fail = 0;
const ok = (l, c, e) => { console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(60) + (e !== undefined ? ' ' + JSON.stringify(e).slice(0, 300) : '')); if (!c) fail++; };
let seed = 777;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const ri = (n) => Math.floor(rnd() * n);

console.log('=== JSON 퍼징 (C vs JS) ===');
{
  const seeds = ['{"type":"VIDEO","site":"ttfc","seriesName":"仮面ライダー響鬼","currentTime":1334.5,"isPlaying":false,"tags":["a",1,null,true]}',
    '[1,-2.5e-3,"\\u00e9\\ud83d\\ude00",{"k":{"z":[]}}]', '{"keybinds":{"ttfc":[{"id":"a1","key":{"code":"KeyF","ctrl":false}}]},"on":{"ttfc":true}}'];
  const cases = [];
  for (let i = 0; i < 30000; i++) {
    let b = Buffer.from(seeds[ri(seeds.length)]);
    const k = ri(4);
    if (k === 0) b = b.slice(0, ri(b.length + 1));                                   // 자르기
    else if (k === 1) { b = Buffer.from(b); for (let j = 0; j < 1 + ri(3); j++) b[ri(b.length)] = ri(256); }   // 바이트 바꾸기
    else if (k === 2) { const p = ri(b.length); b = Buffer.concat([b.slice(0, p), Buffer.from([ri(256)]), b.slice(p)]); }   // 끼워 넣기
    else { const p = ri(b.length); b = Buffer.concat([b.slice(0, p), b.slice(p + 1 + ri(3))]); }   // 지우기
    cases.push(b);
  }
  const r = spawnSync(VEC, ['json'], { input: cases.map((b) => b.toString('base64')).join('\n') + '\n', maxBuffer: 1 << 28 });
  ok('C 가 30,000건 끝까지 처리 (죽지 않음)', r.status === 0, r.status);
  const out = r.stdout.toString().split(/\r?\n/);
  let mismatch = 0, sample = null;
  cases.forEach((b, i) => {
    const s = b.toString('utf8');
    const validUtf8 = Buffer.from(s, 'utf8').equals(b);
    let js = null;
    try { js = JSON.stringify(JSON.parse(s)); } catch (e) { js = null; }
    //  JS 는 깨진 UTF-8 을 U+FFFD 로 바꿔 받지만 C 는 거절한다 (정해 둔 차이) — 그 경우는 C 가 ERR 이어야
    //  짝 없는 서로게이트(\ud800 등)는 C 가 U+FFFD 로 바꾼다 — JS 결과에서도 같은 치환을 한 값과 비교
    let want;
    if (!validUtf8) want = 'ERR';
    else if (js === null) want = 'ERR';
    else {
      const fix = (v) => typeof v === 'string' ? v.replace(/[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g, '\ufffd') : Array.isArray(v) ? v.map(fix) : v && typeof v === 'object' ? Object.fromEntries(Object.entries(v).map(([k2, x]) => [fix(k2), fix(x)])) : v;
      want = 'OK ' + Buffer.from(JSON.stringify(fix(JSON.parse(s)))).toString('base64');
    }
    if (out[i] !== want) { mismatch++; if (!sample) sample = { in: b.toString('base64'), want: want.slice(0, 60), got: (out[i] || '').slice(0, 60) }; }
  });
  ok('판정·결과가 JS 와 같음 (깨진 UTF-8 거절 규칙 포함)', mismatch === 0, mismatch ? { mismatch, sample } : undefined);
}

console.log('=== WebSocket 퍼징 (서버가 죽지 않고 계속 받는지) ===');
(async () => {
  const p = spawn(WSS, ['17697'], { stdio: ['ignore', 'pipe', 'inherit'] });
  let outp = '';
  p.stdout.on('data', (d) => { outp += d; });
  await new Promise((r) => { const t = setInterval(() => { if (outp.includes('READY')) { clearInterval(t); r(); } }, 20); });
  const KEY = crypto.randomBytes(16).toString('base64');
  const hs = 'GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ' + KEY + '\r\nSec-WebSocket-Version: 13\r\n\r\n';
  const N = 400;
  let done = 0;
  await new Promise((res) => {
    let active = 0, started = 0;
    const next = () => {
      if (started >= N) { if (active === 0) res(); return; }
      started++; active++;
      const s = net.connect(17697, '127.0.0.1');
      s.on('error', () => {});
      s.on('close', () => { active--; done++; next(); });
      s.on('connect', () => {
        //  절반은 연결 절차부터 망가뜨리고, 절반은 정상 연결 뒤 쓰레기 프레임
        const junk = crypto.randomBytes(1 + ri(3000));
        if (ri(2)) s.write(Buffer.concat([Buffer.from(hs.slice(0, ri(hs.length))), junk]));
        else s.write(Buffer.concat([Buffer.from(hs), junk]));
        setTimeout(() => s.destroy(), 30 + ri(80));
      });
    };
    for (let i = 0; i < 8; i++) next();
  });
  ok('망가진 연결 ' + N + '개를 받고도 서버 살아 있음', p.exitCode === null, p.exitCode);
  //  그 뒤에도 정상 연결이 되는지
  const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
  const ws = new WebSocket('ws://127.0.0.1:17697', { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' });
  const echo = await new Promise((r) => { ws.on('open', () => ws.send('alive')); ws.on('message', (d) => r(d.toString())); ws.on('error', () => r('error')); setTimeout(() => r('timeout'), 3000); });
  ok('  그 뒤 정상 연결·되돌려 보내기 됨', echo === 'alive', echo);
  ws.send('!quit');
  await new Promise((r) => setTimeout(r, 300));
  ok('  정상 종료', outp.includes('BYE'));
  console.log(fail ? '실패 ' + fail : '전부 통과');
  process.exit(fail ? 1 : 0);
})();
