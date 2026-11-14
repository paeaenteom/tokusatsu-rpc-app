// 알파 exe 실전 점검 — 가짜 Discord 파이프 + 가짜 확장(포트 17698) 로 실제 exe 를 끝까지 돌린다
//  ⚠ 진짜 7690·Discord 파이프에는 닿지 않는다 (시험용 환경 변수). App ID 값은 출력하지 않는다
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const PORT = 17698;
const PREFIX = 'toku-alpha-smoke-' + process.pid + '-';
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(56) + (e !== undefined ? ' ' + JSON.stringify(e).slice(0, 200) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 5000) => { const t = Date.now(); while (Date.now() - t < ms) { if (fn()) return true; await wait(20); } return false; };
const enc = (op, obj) => { const d = Buffer.from(JSON.stringify(obj)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };

//  가짜 Discord — 연결마다 프레임 기록, HANDSHAKE 에 READY, SET_ACTIVITY 에 답
const conns = [];
const srv = net.createServer((sock) => {
  const c = { sock, frames: [], buf: Buffer.alloc(0), closed: false };
  conns.push(c);
  sock.on('data', (d) => {
    c.buf = Buffer.concat([c.buf, d]);
    while (c.buf.length >= 8) {
      const len = c.buf.readInt32LE(4);
      if (c.buf.length < 8 + len) break;
      const op = c.buf.readInt32LE(0);
      const json = JSON.parse(c.buf.slice(8, 8 + len).toString());
      c.buf = c.buf.slice(8 + len);
      c.frames.push({ op, json });
      if (op === 0) sock.write(enc(1, { cmd: 'DISPATCH', evt: 'READY', data: { v: 1, user: { id: '1', username: 'smoke-tester' } }, nonce: null }));
      if (op === 1 && json.cmd === 'SET_ACTIVITY') sock.write(enc(1, { cmd: 'SET_ACTIVITY', data: json.args.activity || null, evt: null, nonce: json.nonce }));
    }
  });
  sock.on('close', () => { c.closed = true; });
  sock.on('error', () => {});
});

(async () => {
  for (let i = 0; i < 1; i++) await new Promise((r) => srv.listen('\\\\?\\pipe\\' + PREFIX + i, r));
  const logFile = path.join(process.env.APPDATA, 'toku-rpc-native-test', 'logs', 'main.log');
  const logStart = fs.existsSync(logFile) ? fs.statSync(logFile).size : 0;
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  let exited = null;
  app.on('exit', (code) => { exited = code; });

  console.log('=== 시작 ===');
  const sock = await (async () => { for (let k = 0; k < 100; k++) { const s = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.got = []; w.on('message', (d) => w.got.push(JSON.parse(d.toString()))); w.on('open', () => r(w)); w.on('error', () => r(null)); }); if (s) return s; await wait(100); } return null; })();
  ok('포트 17698 에 확장이 붙음', !!sock);
  if (!sock) { console.log('중단'); process.exit(1); }
  const got = sock.got;   //  연결과 동시에 듣는다 — 앱이 바로 보내는 CONNECTED 를 놓치지 않게 (11/14)
  ok('CONNECTED (lang·booster·level) 받음', await until(() => got.some((m) => m.type === 'CONNECTED' && typeof m.lang === 'string' && typeof m.level === 'string')), got[0]);
  ok('KEYBINDS 받음 (기존 앱 설정에서)', await until(() => got.some((m) => m.type === 'KEYBINDS' && typeof m.keybinds === 'object')));
  ok('포트를 잡은 뒤 가짜 Discord 에 붙음 (TTFC 클라이언트)', await until(() => conns.length >= 1 && conns[0].frames.length >= 1));
  const hs = conns[0] && conns[0].frames[0];
  ok('HANDSHAKE v:1 + client_id 문자열 (값은 안 봄)', hs && hs.op === 0 && hs.json.v === 1 && typeof hs.json.client_id === 'string' && /^\d{17,20}$/.test(hs.json.client_id));

  console.log('=== 카드 ===');
  sock.send(JSON.stringify({ type: 'VIDEO', site: 'ttfc', seriesName: '仮面ライダー響鬼', episodeNumber: '七之巻', episodeTitle: '息吹く鬼', currentTime: 10, duration: 1422, isPlaying: true, thumbnail: 'https://d1.cloudfront.net/x.jpg', url: 'https://pc.tokusatsu-fc.jp/watch/1' }));
  ok('Discord 에 SET_ACTIVITY 나감', await until(() => conns[0].frames.some((f) => f.op === 1 && f.json.cmd === 'SET_ACTIVITY' && f.json.args.activity)));
  const act = (conns[0].frames.find((f) => f.op === 1 && f.json.args && f.json.args.activity) || { json: { args: {} } }).json.args;
  ok('  카드 내용 (작품·화·썸네일·시간·버튼)', act.activity && act.activity.details === '仮面ライダー響鬼' && act.activity.state === '七之巻 / 息吹く鬼'
    && act.activity.assets.large_image === 'https://d1.cloudfront.net/x.jpg' && act.activity.timestamps && act.activity.timestamps.end - act.activity.timestamps.start === 1422
    && act.activity.buttons && act.activity.buttons[0].url === 'https://pc.tokusatsu-fc.jp/watch/1', act.activity);
  ok('  pid = 알파 프로세스', act.pid === app.pid, [act.pid, app.pid]);
  ok('썸네일 요청(NEED_THUMB) 받음', await until(() => got.some((m) => m.type === 'NEED_THUMB' && m.url === 'https://d1.cloudfront.net/x.jpg')));
  sock.send(JSON.stringify({ type: 'PING' }));
  ok('PING → PONG', await until(() => got.some((m) => m.type === 'PONG')));
  sock.send(JSON.stringify({ type: 'BROWSING', site: 'youtube', page: '홈', detail: '', banner: '', url: 'https://www.youtube.com/results?search_query=비밀' }));
  ok('다른 사이트(YouTube) → 새 연결 + 카드', await until(() => conns.length >= 2 && conns[1].frames.some((f) => f.op === 1 && f.json.args && f.json.args.activity)));
  const yt = (conns[1] && conns[1].frames.find((f) => f.op === 1 && f.json.args && f.json.args.activity) || { json: { args: { activity: {} } } }).json.args.activity;
  ok('  검색어는 버튼 주소에서 뗌', yt.buttons && yt.buttons[0].url === 'https://www.youtube.com/results', yt.buttons);
  sock.send(JSON.stringify({ type: 'CLEAR', site: 'youtube' }));
  ok('CLEAR → YouTube 카드 지움 ({pid} 만)', await until(() => conns[1].frames.some((f) => f.op === 1 && f.json.args && !f.json.args.activity && f.json.args.pid === app.pid)));

  console.log('=== Discord 가 꺼졌다 켜짐 ===');
  conns[0].sock.destroy();
  ok('끊기면 다시 붙음 (10초 간격)', await until(() => conns.length >= 3, 13000), conns.length);

  console.log('=== 끝내기 (--quit) ===');
  spawnSync(EXE, ['--quit'], { env });
  ok('--quit 으로 정상 종료', await until(() => exited !== null, 5000), exited);
  ok('  남은 카드 지우고 Discord 연결 닫음', conns.slice(2).every((c) => c.closed) && conns[2].frames.some((f) => f.op === 2));
  const log = fs.readFileSync(logFile).slice(logStart).toString('utf8');   // 바이트 위치로 자른다 (한글)
  ok('로그: 시작·연결·카드·종료', /=== TOKU RPC 시작 \(C (알파|베타) \d+\.\d+\.\d+\) ===/.test(log) && /Connected: smoke-tester/.test(log) && /setActivity largeImageKey/.test(log) && /\[Shutdown\]/.test(log));
  ok('로그에 App ID 값 없음', !/\d{17,20}/.test(log.replace(/\[\d{4}-\d\d-\d\d [\d:.]+\]/g, '')));
  srv.close();
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log('테스트 오류', e && e.stack); process.exit(2); });
