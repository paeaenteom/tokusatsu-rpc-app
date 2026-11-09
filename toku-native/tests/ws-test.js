// C WebSocket 서버(ws_srv.exe) vs 지금 앱과 같은 설정의 ws 서버 — 같은 입력에 같은 답을 하는지
//  node tests\ws-test.js build\ws_srv.exe        (포트 17695 = C, 17696 = 기준 ws. 7690 은 건드리지 않는다)
'use strict';
const { spawn } = require('child_process');
const net = require('net');
const crypto = require('crypto');
const WS_DIR = 'C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws';
const WebSocket = require(WS_DIR);
const { WebSocketServer } = require(WS_DIR);
const EXE = process.argv[2];
const CP = 17695, RP = 17696;
const ORIGIN = 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm';
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(58) + (e !== undefined ? ' ' + JSON.stringify(e).slice(0, 260) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

//  ── 기준 서버: extension-bridge.js 와 같은 옵션, ws_srv 와 같은 되돌려 보내기 ──
function startRef() {
  return new Promise((res) => {
    const wss = new WebSocketServer({
      port: RP, host: '127.0.0.1', maxPayload: 16 * 1024 * 1024,
      verifyClient: ({ origin }) => !origin || /^chrome-extension:\/\/[a-p]{32}$/.test(origin),
    });
    wss.on('connection', (ws) => {
      ws.on('message', (d, isBin) => {
        const s = d.toString();
        if (s === '!ping') return ws.ping();
        if (s.startsWith('!close ')) return ws.close(Number(s.slice(7)));
        if (s.startsWith('!big ')) return ws.send('a'.repeat(Number(s.slice(5))));
        ws.send(isBin ? d : s);
      });
      ws.on('error', () => {});
    });
    wss.on('listening', () => res(wss));
  });
}

//  ── C 서버 ──
const events = [];
function startC() {
  return new Promise((res, rej) => {
    const p = spawn(EXE, [String(CP)], { stdio: ['ignore', 'pipe', 'inherit'] });
    let buf = Buffer.alloc(0);
    //  바이트로 모았다가 줄 단위로 푼다 (조각 경계에 걸린 여러 바이트 글자가 깨지지 않게)
    p.stdout.on('data', (d) => {
      buf = Buffer.concat([buf, d]);
      let i;
      while ((i = buf.indexOf(10)) >= 0) {
        const line = buf.subarray(0, i).toString('utf8').replace(/\r$/, '');
        buf = buf.subarray(i + 1);
        events.push(line);
        if (line.startsWith('READY')) res(p);
        if (line.startsWith('LISTEN_FAIL')) rej(new Error(line));
      }
    });
    p.on('exit', (c) => events.push('EXIT ' + c));
  });
}

//  ── 날것 TCP: 보낸 바이트에 대한 응답 상태 줄 ──
function rawStatus(port, bytes, waitMs = 600) {
  return new Promise((res) => {
    const s = net.connect(port, '127.0.0.1');
    let got = Buffer.alloc(0);
    s.on('data', (d) => { got = Buffer.concat([got, d]); });
    s.on('error', () => {});
    s.on('connect', () => s.write(bytes));
    setTimeout(() => { s.destroy(); const line = got.toString('latin1').split('\r\n')[0]; res({ status: Number((/^HTTP\/1\.1 (\d{3})/.exec(line) || [])[1]) || 0, raw: got }); }, waitMs);
  });
}
const key = () => crypto.randomBytes(16).toString('base64');
const req = (o = {}) => {
  const h = Object.assign({ Host: '127.0.0.1', Upgrade: 'websocket', Connection: 'Upgrade', 'Sec-WebSocket-Key': key(), 'Sec-WebSocket-Version': '13', Origin: ORIGIN }, o.headers);
  for (const k of Object.keys(h)) if (h[k] === null) delete h[k];
  const lines = [(o.line || 'GET / HTTP/1.1')].concat(Object.entries(h).flatMap(([k, v]) => Array.isArray(v) ? v.map((x) => k + ': ' + x) : [k + ': ' + v]));
  return Buffer.from(lines.join('\r\n') + '\r\n\r\n' + (o.extra || ''), 'latin1');
};

//  ── 날것 프레임: 연결 절차 뒤 프레임을 보내고, 서버가 보낸 닫기 코드(또는 받은 것)를 본다 ──
function frame(op, payload, { fin = true, mask = true, rsv = 0, lenOverride } = {}) {
  const p = Buffer.from(payload);
  const len = lenOverride !== undefined ? lenOverride : p.length;
  let h;
  if (len < 126) h = Buffer.from([(fin ? 0x80 : 0) | rsv | op, (mask ? 0x80 : 0) | len]);
  else if (len < 65536) { h = Buffer.alloc(4); h[0] = (fin ? 0x80 : 0) | rsv | op; h[1] = (mask ? 0x80 : 0) | 126; h.writeUInt16BE(len, 2); }
  else { h = Buffer.alloc(10); h[0] = (fin ? 0x80 : 0) | rsv | op; h[1] = (mask ? 0x80 : 0) | 127; h.writeBigUInt64BE(BigInt(len), 2); }
  if (!mask) return Buffer.concat([h, p]);
  const m = crypto.randomBytes(4);
  const q = Buffer.from(p.map((b, i) => b ^ m[i & 3]));
  return Buffer.concat([h, m, q]);
}
function rawSession(port, chunks, waitMs = 700) {
  return new Promise((res) => {
    const s = net.connect(port, '127.0.0.1');
    let got = Buffer.alloc(0), upgraded = false;
    s.on('data', (d) => {
      got = Buffer.concat([got, d]);
      if (!upgraded) {
        const i = got.indexOf('\r\n\r\n');
        if (i >= 0) {
          upgraded = got.toString('latin1').startsWith('HTTP/1.1 101');
          got = got.slice(i + 4);
          (async () => { for (const c of chunks) { if (typeof c === 'number') await wait(c); else s.write(c); } })();
        }
      }
    });
    s.on('error', () => {});
    s.on('connect', () => s.write(req()));
    setTimeout(() => { s.destroy(); res(parseFrames(got)); }, waitMs);
  });
}
function parseFrames(b) {
  const out = [];
  let i = 0;
  while (i + 2 <= b.length) {
    const op = b[i] & 15; let len = b[i + 1] & 127; let h = 2;
    if (len === 126) { len = b.readUInt16BE(i + 2); h = 4; } else if (len === 127) { len = Number(b.readBigUInt64BE(i + 2)); h = 10; }
    if (i + h + len > b.length) break;
    const p = b.slice(i + h, i + h + len);
    out.push(op === 8 ? 'close:' + (p.length >= 2 ? p.readUInt16BE(0) : 'none') : op === 1 ? 'text:' + p.toString() : op === 10 ? 'pong:' + p.toString() : 'op' + op);
    i += h + len;
  }
  return out;
}
function client(port, origin = ORIGIN) {
  return new Promise((res, rej) => {
    const ws = new WebSocket('ws://127.0.0.1:' + port, { origin, perMessageDeflate: false });
    ws.on('open', () => res(ws));
    ws.on('error', rej);
  });
}
const nextMsg = (ws) => new Promise((r) => ws.once('message', (d, bin) => r({ d, bin })));
const closeCode = (ws) => new Promise((r) => ws.once('close', (code) => r(code)));

(async () => {
  const ref = await startRef();
  const cp = await startC();

  console.log('=== 1. 연결 절차 (상태 코드가 기준 ws 와 같은지) ===');
  const cases = [
    ['정상 (확장 출처)', {}],
    ['출처 없음', { headers: { Origin: null } }],
    ['다른 출처 http://evil', { headers: { Origin: 'http://evil.example' } }],
    ['확장 ID 대문자', { headers: { Origin: 'chrome-extension://DCIAOBLLFDCEGJCDMIMCLGGLAPNHGGJM' } }],
    ['확장 ID 31자', { headers: { Origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggj' } }],
    ['확장 ID 에 q (a~p 밖)', { headers: { Origin: 'chrome-extension://qciaobllfdcegjcdmimclgglapnhggjm' } }],
    ['출처 두 번', { headers: { Origin: [ORIGIN, ORIGIN] } }],
    ['POST', { line: 'POST / HTTP/1.1' }],
    ['Connection 없음 (업그레이드 아님)', { headers: { Connection: null } }],
    ['Connection: keep-alive, Upgrade', { headers: { Connection: 'keep-alive, Upgrade' } }],
    ['Upgrade: h2c', { headers: { Upgrade: 'h2c' } }],
    ['Upgrade 대소문자 WebSocket', { headers: { Upgrade: 'WebSocket' } }],
    ['키 없음', { headers: { 'Sec-WebSocket-Key': null } }],
    ['키 모양 틀림', { headers: { 'Sec-WebSocket-Key': 'abc' } }],
    ['키 23자+=', { headers: { 'Sec-WebSocket-Key': 'AAAAAAAAAAAAAAAAAAAAAAA=' } }],
    ['버전 12', { headers: { 'Sec-WebSocket-Version': '12' } }],
    ['버전 013', { headers: { 'Sec-WebSocket-Version': '013' } }],
    ['버전 8 + 옛 출처 머리 정상', { headers: { 'Sec-WebSocket-Version': '8', Origin: null, 'Sec-WebSocket-Origin': ORIGIN } }],
    ['버전 8 + 옛 출처 머리 나쁨', { headers: { 'Sec-WebSocket-Version': '8', Origin: null, 'Sec-WebSocket-Origin': 'http://x' } }],
    ['버전 없음', { headers: { 'Sec-WebSocket-Version': null } }],
    ['압축 요청 (무시하고 연결)', { headers: { 'Sec-WebSocket-Extensions': 'permessage-deflate; client_max_window_bits' } }],
    ['경로 /abc?x=1', { line: 'GET /abc?x=1 HTTP/1.1' }],
    ['평범한 GET', { headers: { Upgrade: null, Connection: null } }],
  ];
  for (const [label, o] of cases) {
    const bytes = req(o);
    const [a, b] = await Promise.all([rawStatus(CP, bytes), rawStatus(RP, bytes)]);
    ok(label + ' → ' + b.status, a.status === b.status && a.status !== 0, { c: a.status, ref: b.status });
  }
  {
    const big = req({ headers: { 'X-Pad': 'x'.repeat(20000) } });
    const [a, b] = await Promise.all([rawStatus(CP, big), rawStatus(RP, big)]);
    ok('머리 20KB → 431', a.status === 431 && b.status === 431, { c: a.status, ref: b.status });
    const nul = Buffer.concat([Buffer.from('GET / HTTP/1.1\r\nHost: x\r\nX-A: a'), Buffer.from([0]), Buffer.from('b\r\n\r\n')]);
    const c1 = await rawStatus(CP, nul);
    ok('머리에 0 바이트 → 죽지 않고 답함', c1.status > 0, c1.status);
    const garbage = Buffer.from('\x16\x03\x01\x00\xa5garbage\r\n\r\n', 'latin1');
    const c2 = await rawStatus(CP, garbage);
    ok('엉뚱한 데이터 (TLS 시작 등) → 400', c2.status === 400, c2.status);
  }
  {
    //  Sec-WebSocket-Accept 계산 (RFC 6455 예시 키)
    const r = await rawStatus(CP, req({ headers: { 'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ==' } }));
    ok('RFC 예시 키 → s3pPLMBiTxaQ9kYGzzhZRbK+xOo=', r.raw.toString().includes('Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n'));
  }

  console.log('=== 2. 진짜 ws 클라이언트로 주고받기 ===');
  {
    const ws = await client(CP);
    const msg = JSON.stringify({ type: 'VIDEO', site: 'ttfc', seriesName: '仮面ライダー響鬼', episodeTitle: '😀 七之巻', currentTime: 1334.5 });
    ws.send(msg);
    const r = await nextMsg(ws);
    ok('한글·일본어·이모지 JSON 그대로 되돌아옴', r.d.toString() === msg && !r.bin);
    const N = 1000; const got = [];
    const all = new Promise((res) => { ws.on('message', (d) => { got.push(d.toString()); if (got.length === N) res(); }); });
    for (let i = 0; i < N; i++) ws.send('m' + i);
    await Promise.race([all, wait(5000)]);
    ws.removeAllListeners('message');
    ok('1000개 연속 → 순서 그대로', got.length === N && got.every((g, i) => g === 'm' + i), got.length);
    for (const sz of [125, 126, 65535, 65536, 1 << 20]) {
      const s = 'x'.repeat(sz);
      ws.send(s);
      const e = await nextMsg(ws);
      ok('  ' + sz + ' 바이트 경계 왕복', e.d.length === sz);
    }
    const big = crypto.randomBytes(15 * 1024 * 1024).toString('base64').slice(0, 15 * 1024 * 1024);
    ws.send(big);
    const e = await nextMsg(ws);
    ok('15MB 왕복 (썸네일 바이트 메시지 크기)', e.d.length === big.length && e.d.toString() === big);
    ws.send('!big 3000000');
    const e2 = await nextMsg(ws);
    ok('서버 → 3MB 보내기', e2.d.length === 3000000);
    const pong = new Promise((r) => ws.once('pong', () => r(true)));
    ws.ping('hi');
    ok('클라이언트 ping → pong', await Promise.race([pong, wait(1000).then(() => false)]));
    ws.send('!ping');
    await wait(300);
    ok('서버 ping → 클라이언트 자동 pong 을 서버가 받음', events.some((x) => /^PONG \d+/.test(x)));
    const cc = closeCode(ws);
    ws.send('!close 4001');
    ok('서버가 닫기 4001', (await cc) === 4001);
  }
  {
    const ws = await client(CP);
    const cc = closeCode(ws);
    ws.close(4000, 'bye');
    ok('클라이언트가 닫기 4000 → 같은 코드로 답함', (await cc) === 4000);
    await wait(200);
    ok('  서버 쪽 사건 CLOSE … 4000', events.some((x) => / 4000$/.test(x) && x.startsWith('CLOSE')));
  }
  {
    const many = await Promise.all([0, 1, 2, 3, 4].map(() => client(CP)));
    const rs = await Promise.all(many.map((w, i) => { const p = nextMsg(w); w.send('c' + i); return p; }));
    ok('동시 연결 5개 → 각자 자기 답', rs.every((r, i) => r.d.toString() === 'c' + i));
    many[0].terminate();
    await wait(300);
    ok('갑자기 끊김 → 서버 CLOSE 1006', events.some((x) => /^CLOSE \d+ 1006$/.test(x)));
    for (const w of many.slice(1)) w.close();
  }

  console.log('=== 3. 날것 프레임 (닫기 코드가 기준 ws 와 같은지) ===');
  const fcases = [
    ['조각 3개 이어 붙이기', [frame(1, 'ab', { fin: false }), frame(0, 'cd', { fin: false }), frame(0, 'ef')]],
    ['조각 사이 ping 끼워 넣기', [frame(1, 'ab', { fin: false }), frame(9, 'pp'), frame(0, 'cd')]],
    ['한 바이트씩 천천히', [...frame(1, 'slow')].map((b) => Buffer.from([b])).flatMap((b) => [b, 5])],
    ['마스크 없음 → 1002', [frame(1, 'x', { mask: false })]],
    ['RSV1 켜짐 (압축 안 했는데) → 1002', [frame(1, 'x', { rsv: 0x40 })]],
    ['모르는 종류 3 → 1002', [frame(3, 'x')]],
    ['조각난 ping → 1002', [frame(9, 'x', { fin: false })]],
    ['126 바이트 ping → 1002', [frame(9, 'x'.repeat(126))]],
    ['시작 없이 이어지는 조각 → 1002', [frame(0, 'x')]],
    ['조각 도중 새 메시지 → 1002', [frame(1, 'a', { fin: false }), frame(1, 'b')]],
    ['깨진 UTF-8 텍스트 → 1007', [frame(1, Buffer.from([0xff, 0xfe]))]],
    ['조각 경계에서 나뉜 한글 (정상)', [frame(1, Buffer.from('가').slice(0, 2), { fin: false }), frame(0, Buffer.from('가').slice(2))]],
    ['바이너리 프레임', [frame(2, Buffer.from([1, 2, 3]))]],
    ['16MB 초과 길이 선언 → 1009', [frame(1, '', { lenOverride: 16 * 1024 * 1024 + 1 })]],
    ['닫기 1바이트 → 1002', [frame(8, Buffer.from([3]))]],
    ['닫기 코드 1005 (금지) → 1002', [frame(8, Buffer.from([0x03, 0xed]))]],
    ['닫기 코드 999 → 1002', [frame(8, Buffer.from([0x03, 0xe7]))]],
    ['닫기 코드 1000 + 이유', [frame(8, Buffer.concat([Buffer.from([0x03, 0xe8]), Buffer.from('bye')]))]],
    ['닫기 내용 없음', [frame(8, '')]],
  ];
  for (const [label, chunks] of fcases) {
    const [a, b] = await Promise.all([rawSession(CP, chunks), rawSession(RP, chunks)]);
    const norm = (x) => x.map((f) => f.startsWith('op2') ? 'bin' : f);
    ok(label, JSON.stringify(norm(a)) === JSON.stringify(norm(b)), { c: a, ref: b });
  }
  {
    const t0 = Date.now();
    const r = await new Promise((res) => {
      const s = net.connect(CP, '127.0.0.1');
      s.on('close', () => res(Date.now() - t0));
      s.on('error', () => {});
      s.write('GET / HTTP/1.1\r\n');   // 끝내지 않고 붙들기
    });
    ok('연결 절차를 끝내지 않으면 10초 뒤 끊음', r >= 9500 && r < 12000, r + 'ms');
  }

  //  끝
  const ws = await client(CP);
  ws.send('!quit');
  await wait(400);
  ok('정상 종료', events.includes('BYE'));
  ref.close();
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log('테스트 오류', e && e.stack); process.exit(2); });
