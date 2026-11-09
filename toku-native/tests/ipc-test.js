// C Discord IPC (dipc_cli.exe) vs discord-rpc 라이브러리 — 가짜 Discord 파이프로 바이트 단위 비교
//  node tests\ipc-test.js build\dipc_cli.exe
//  ⚠ 진짜 Discord 파이프(discord-ipc-N)는 쓰지 않는다. 이 실행만의 이름(toku-test-ipc-<pid>-N)을 쓴다
'use strict';
const net = require('net');
const { spawn } = require('child_process');
const IPC = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/discord-rpc/src/transports/ipc.js');
const { encode } = IPC;
const EXE = process.argv[2];
const PREFIX = 'toku-test-ipc-' + process.pid + '-';
const CLIENT_ID = '123456789012345678';   // 가짜 (진짜 App ID 아님)
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(60) + (e !== undefined ? ' ' + JSON.stringify(e).slice(0, 240) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

//  가짜 Discord: 받은 바이트를 그대로 모으고 프레임으로도 나눈다
function fakeDiscord(index) {
  const st = { raw: Buffer.alloc(0), frames: [], conns: [], ended: false };
  const srv = net.createServer((sock) => {
    st.conns.push(sock);
    sock.on('data', (d) => {
      st.raw = Buffer.concat([st.raw, d]);
      let b = st.raw, i = 0;
      st.frames = [];
      while (b.length - i >= 8) { const len = b.readInt32LE(i + 4); if (b.length - i < 8 + len) break; st.frames.push({ op: b.readInt32LE(i), bytes: b.slice(i, i + 8 + len), json: b.slice(i + 8, i + 8 + len).toString() }); i += 8 + len; }
    });
    sock.on('end', () => { st.ended = true; });
    sock.on('error', () => {});
  });
  return new Promise((res) => srv.listen('\\\\?\\pipe\\' + PREFIX + index, () => res({ srv, st })));
}
function startCli() {
  const p = spawn(EXE, [PREFIX, CLIENT_ID], { stdio: ['pipe', 'pipe', 'inherit'] });
  const out = [];
  let buf = Buffer.alloc(0);
  //  바이트로 모았다가 줄 단위로 푼다 (조각 경계에 걸린 여러 바이트 글자가 깨지지 않게)
  p.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { out.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } });
  return { p, out, send: (l) => p.stdin.write(l + '\n') };
}
const until = async (fn, ms = 2000) => { const t = Date.now(); while (Date.now() - t < ms) { if (fn()) return true; await wait(10); } return false; };

(async () => {
  console.log('=== 1. 연결·HANDSHAKE ===');
  const D0 = await fakeDiscord(0);
  let cli = startCli();
  ok('파이프 0 에 붙음', await until(() => cli.out.includes('OPEN 0')), cli.out);
  await until(() => D0.st.frames.length >= 1);
  const hs = encode(0, { v: 1, client_id: CLIENT_ID });
  ok('HANDSHAKE 바이트가 discord-rpc 와 똑같음', D0.st.frames[0] && D0.st.frames[0].bytes.equals(hs), D0.st.frames[0] && D0.st.frames[0].json);
  const sock = D0.st.conns[0];
  const ready = { cmd: 'DISPATCH', data: { v: 1, config: { cdn_host: 'cdn.discordapp.com' }, user: { id: '1', username: 'paeaenteom', discriminator: '0' } }, evt: 'READY', nonce: null };
  sock.write(encode(1, ready));
  ok('READY 받음 (JSON 그대로)', await until(() => cli.out.includes('RECV 1 ' + JSON.stringify(ready))), cli.out.slice(-1));

  console.log('=== 2. 명령 (SET_ACTIVITY) ===');
  const args = { pid: 4321, activity: { details: '仮面ライダー響鬼', state: '七之巻 息吹く鬼 😀', timestamps: { start: 1762500000, end: 1762501422 }, assets: { large_image: 'https://x/y.png', large_text: '響鬼' }, buttons: [{ label: '사이트에서 보기', url: 'https://pc.tokusatsu-fc.jp/' }], instance: false, type: 3, status_display_type: 2 } };
  cli.send('REQ SET_ACTIVITY ' + JSON.stringify(args));
  await until(() => cli.out.some((l) => l.startsWith('SENT ')) && D0.st.frames.length >= 2);
  const nonce = (cli.out.find((l) => l.startsWith('SENT ')) || '').slice(5);
  ok('nonce 는 uuid 모양', /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(nonce), nonce);
  const want = encode(1, { cmd: 'SET_ACTIVITY', args, evt: undefined, nonce });
  ok('명령 바이트가 discord-rpc request() 와 똑같음 (evt 없음)', D0.st.frames[1] && D0.st.frames[1].bytes.equals(want), D0.st.frames[1] && D0.st.frames[1].json.slice(0, 120));
  const reply = { cmd: 'SET_ACTIVITY', data: { details: '仮面ライダー響鬼', type: 3 }, evt: null, nonce };
  sock.write(encode(1, reply));
  ok('응답 받음', await until(() => cli.out.includes('RECV 1 ' + JSON.stringify(reply))));
  cli.send('REQ GET_SOMETHING -');
  await until(() => D0.st.frames.length >= 3);
  const n2 = JSON.parse(D0.st.frames[2].json).nonce;
  ok('args 없는 명령 = discord-rpc 와 같음', D0.st.frames[2].bytes.equals(encode(1, { cmd: 'GET_SOMETHING', args: undefined, evt: undefined, nonce: n2 })), D0.st.frames[2].json);
  ok('명령마다 nonce 다름', n2 !== nonce);

  console.log('=== 3. 받는 쪽 (조각·묶음·크기·PING) ===');
  {
    const f = encode(1, { cmd: 'DISPATCH', evt: 'ACTIVITY_JOIN', data: { secret: '한글😀' } });
    for (const b of f) { sock.write(Buffer.from([b])); await wait(2); }
    ok('1바이트씩 나눠 와도 하나로', await until(() => cli.out.some((l) => l.includes('ACTIVITY_JOIN'))));
    const a = encode(1, { evt: 'A', n: 1 }), b = encode(1, { evt: 'B', n: 2 });
    sock.write(Buffer.concat([a, b]));
    ok('두 프레임이 한 번에 와도 둘 다', await until(() => cli.out.includes('RECV 1 {"evt":"A","n":1}') && cli.out.includes('RECV 1 {"evt":"B","n":2}')));
    const big = { evt: 'BIG', s: 'x'.repeat(2 * 1024 * 1024) };
    sock.write(encode(1, big));
    ok('2MB 프레임', await until(() => cli.out.some((l) => l.startsWith('RECV 1 {"evt":"BIG"') && l.length > 2 * 1024 * 1024), 5000));
    const before = D0.st.frames.length;
    sock.write(encode(3, 'ping-uuid-1234'));
    await until(() => D0.st.frames.length > before);
    const pong = D0.st.frames[before];
    ok('PING → 같은 내용 PONG (discord-rpc 와 같은 바이트)', pong && pong.bytes.equals(encode(4, 'ping-uuid-1234')), pong && pong.json);
    const b2 = D0.st.frames.length;
    sock.write(encode(3, { nonce: 'x', v: [1, 2] }));
    await until(() => D0.st.frames.length > b2);
    ok('  객체 PING 도', D0.st.frames[b2] && D0.st.frames[b2].bytes.equals(encode(4, { nonce: 'x', v: [1, 2] })));
  }

  console.log('=== 4. 끝내기 ===');
  cli.send('QUIT');
  await until(() => cli.out.includes('BYE') && D0.st.ended);
  const last = D0.st.frames[D0.st.frames.length - 1];
  ok('QUIT → CLOSE {} (discord-rpc close 와 같은 바이트)', last && last.bytes.equals(encode(2, {})), last && last.json);
  {
    cli = startCli();
    await until(() => D0.st.conns.length >= 2);
    const s2 = D0.st.conns[1];
    s2.write(encode(2, { code: 4000, message: 'Invalid Client ID' }));
    ok('Discord 가 CLOSE 보내면 내용 전달 후 끊김', await until(() => cli.out.includes('RECV 2 {"code":4000,"message":"Invalid Client ID"}') && cli.out.includes('DEAD')), cli.out);
    cli.send('QUIT');
    await until(() => cli.out.includes('BYE'));
  }
  {
    cli = startCli();
    await until(() => D0.st.conns.length >= 3);
    D0.st.conns[2].end();
    ok('Discord 가 파이프를 닫으면 DEAD', await until(() => cli.out.includes('DEAD')));
    cli.send('QUIT');
  }
  for (const [label, bytes] of [['음수 길이', Buffer.from([1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff])], ['모르는 op 9', encode(9, {})], ['17MB 길이 선언', (() => { const b = Buffer.alloc(8); b.writeInt32LE(1, 0); b.writeInt32LE(17 * 1024 * 1024, 4); return b; })()], ['깨진 JSON', Buffer.concat([Buffer.from([1, 0, 0, 0, 3, 0, 0, 0]), Buffer.from('{x}')])]]) {
    cli = startCli();
    const k = D0.st.conns.length;
    await until(() => D0.st.conns.length > k);
    D0.st.conns[k].write(bytes);
    ok('이상한 프레임 (' + label + ') → 끊음, 죽지 않음', await until(() => cli.out.includes('DEAD')), cli.out);
    cli.send('QUIT');
    ok('  그 뒤 정상 종료', await until(() => cli.out.includes('BYE')));
  }

  console.log('=== 5. 파이프 번호 찾기 ===');
  D0.srv.close();
  await wait(100);
  const D3 = await fakeDiscord(3);
  cli = startCli();
  ok('0~2 가 없으면 3 에 붙음 (discord-rpc 처럼 차례로)', await until(() => cli.out.includes('OPEN 3')), cli.out);
  cli.send('QUIT');
  await until(() => cli.out.includes('BYE'));
  D3.srv.close();
  await wait(100);
  cli = startCli();
  ok('아무 파이프도 없으면 FAIL 2 (파일 없음)', await until(() => cli.out.includes('FAIL 2')), cli.out);

  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log('테스트 오류', e && e.stack); process.exit(2); });
