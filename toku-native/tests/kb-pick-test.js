// 키 설정 ↔ 확장 흐름 점검 (시험 모드, 포트 17695 · 가짜 Discord 파이프) — extension-bridge.js pickButton 과 같은 규칙인지
//  시험 폴더(toku-rpc-native-test)의 config.json 은 시작 전에 비우고 끝나면 되돌린다. 진짜 알파·지금 앱 파일은 안 건드림
//  사용: node tests/kb-pick-test.js <exe> [그림 저장 폴더]
'use strict';
const net = require('net'); const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const EXE = path.resolve(process.argv[2]);
const SNAP = process.argv[3] ? path.resolve(process.argv[3]) : '';
const PORT = 17695;
const PREFIX = 'toku-kb-test-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const LOG = path.join(DIR, 'logs', 'main.log');
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(52) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 300) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 5000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = fn(); if (v) return v; await wait(20); } return null; };
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
      if (op === 0) sock.write(enc(1, { cmd: 'DISPATCH', evt: 'READY', data: { v: 1, user: { id: '1', username: 'kb-tester' } }, nonce: null }));
      if (op === 1 && json.cmd === 'SET_ACTIVITY') sock.write(enc(1, { cmd: 'SET_ACTIVITY', data: json.args.activity || null, evt: null, nonce: json.nonce }));
    }
  });
  sock.on('error', () => {});
});

(async () => {
  const saved = fs.existsSync(CFG) ? fs.readFileSync(CFG) : null;
  fs.mkdirSync(DIR, { recursive: true });
  fs.writeFileSync(CFG, '{}');   // 키 설정이 없는 상태에서 시작
  const logStart = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(logStart).toString('utf8') : '');
  await new Promise((r) => srv.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX });
  if (SNAP) { fs.mkdirSync(SNAP, { recursive: true }); env.TOKU_NATIVE_TEST_SNAP = SNAP; }
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  try {
    let ws = null;
    for (let k = 0; k < 100 && !ws; k++) {
      ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
      if (!ws) await wait(100);
    }
    if (!ws) { ok('포트에 붙음', false); throw new Error('stop'); }
    const got = [];
    ws.on('message', (d) => got.push(JSON.parse(d.toString())));
    const send = (o) => ws.send(JSON.stringify(o));
    const take = (type, from) => until(() => got.slice(from).find((m) => m.type === type));
    const kbAfter = (from) => until(() => [...got.slice(from)].reverse().find((m) => m.type === 'KEYBINDS'));

    console.log('=== 처음 · 예전 확장 키 가져오기 ===');
    const k0 = await take('KEYBINDS', 0);
    ok('붙자마자 KEYBINDS (비어 있음)', k0 && JSON.stringify(k0.keybinds) === '{}' && JSON.stringify(k0.on) === '{}', k0);
    let mark = got.length;
    send({ type: 'KEYBINDS_LOCAL', keybinds: { youtube: [{ id: 'ab', label: ' 다음 ', target: { tag: 'BUTTON', cls: ['ytp-next-button'] }, code: 'KeyN', shift: 1 }, { id: 'bad id', target: { tag: 'b' } }] }, on: { youtube: false, ttfc: true } });
    const k1 = await kbAfter(mark);
    const y1 = k1 && k1.keybinds.youtube;
    ok('KEYBINDS_LOCAL → 걸러서 가져옴 (잘못된 줄 빠짐)', y1 && y1.length === 1 && y1[0].id === 'ab' && y1[0].label === '다음' && y1[0].target.tag === 'button' && y1[0].shift === true, k1);
    ok('  켜고 끄기도 가져옴 (false 인 사이트만)', k1 && JSON.stringify(k1.on) === '{"youtube":false}', k1 && k1.on);
    ok('  로그: 확장에 있던 키 설정을 앱으로 가져옴', await until(() => logText().includes('확장에 있던 키 설정을 앱으로 가져옴')));
    mark = got.length;
    send({ type: 'KEYBINDS_LOCAL', keybinds: { ttfc: [{ id: 'zz', target: { tag: 'button' } }] } });
    await wait(400);
    ok('두 번째 KEYBINDS_LOCAL → 무시 (이미 있음)', !got.slice(mark).some((m) => m.type === 'KEYBINDS'));

    console.log('=== 버튼 고르기 (새 줄) ===');
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'youtube', id: '' });
    const p1 = await take('KB_PICK', mark);
    ok('KB_PICK {reqId, site} 를 보냄', p1 && /^[0-9a-f]{12}$/.test(p1.reqId) && p1.site === 'youtube', p1);
    send({ type: 'KEYBIND_PICK_START', reqId: p1.reqId });
    send({ type: 'KEYBIND_PICKED', reqId: 'nope', site: 'youtube', target: { tag: 'button' } });   // 다른 요청 → 무시
    await wait(200);
    mark = got.length;
    send({ type: 'KEYBIND_PICKED', reqId: p1.reqId, site: 'youtube', target: { tag: 'BUTTON', aria: 'Full screen (f)', cls: ['ytp-fullscreen-button'] }, label: '전체화면' });
    const k2 = await kbAfter(mark);
    const y2 = k2 && k2.keybinds.youtube;
    const nr = y2 && y2[1];
    ok('새 줄 추가 (id 10자리 · 이름 · 키 없음)', y2 && y2.length === 2 && /^[0-9a-f]{10}$/.test(nr.id) && nr.label === '전체화면' && nr.code === '' && nr.target.aria === 'Full screen (f)', k2);
    ok('  다른 reqId 의 PICKED 는 무시됨', y2 && y2.length === 2);
    ok('  로그: 버튼 고름 (새 줄)', await until(() => logText().includes('[Keys] 버튼 고름: youtube (새 줄)')));

    console.log('=== 전체화면 키 배움 ===');
    mark = got.length;
    send({ type: 'KEYBIND_FS', site: 'youtube', id: nr.id });
    const k3 = await kbAfter(mark);
    ok('KEYBIND_FS → fs:true', k3 && k3.keybinds.youtube[1].fs === true, k3 && k3.keybinds.youtube[1]);
    mark = got.length;
    send({ type: 'KEYBIND_FS', site: 'youtube', id: nr.id });
    await wait(400);
    ok('  같은 걸 또 → 저장·전송 안 함', !got.slice(mark).some((m) => m.type === 'KEYBINDS'));

    console.log('=== 다시 고르기 ===');
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'youtube', id: nr.id });
    const p2 = await take('KB_PICK', mark);
    send({ type: 'KEYBIND_PICK_START', reqId: p2.reqId });
    mark = got.length;
    send({ type: 'KEYBIND_PICKED', reqId: p2.reqId, site: 'youtube', target: { tag: 'button', aria: 'Next' } });
    const k4 = await kbAfter(mark);
    const r4 = k4 && k4.keybinds.youtube[1];
    ok('같은 줄 버튼만 바뀜 · fs 지움 · 이름 그대로', r4 && r4.id === nr.id && r4.target.aria === 'Next' && r4.fs === undefined && r4.label === '전체화면', r4);

    console.log('=== 실패 ===');
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'ttfc', id: '' });
    const p3 = await take('KB_PICK', mark);
    send({ type: 'KEYBIND_PICK_START', reqId: p3.reqId });
    send({ type: 'KEYBIND_PICK_FAIL', reqId: p3.reqId, error: 'weird' });
    ok('PICK_FAIL (모르는 이유) → notab', await until(() => logText().includes('[Test] 고르기 끝: notab')));
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'ttfc', id: '' });
    const p4 = await take('KB_PICK', mark);
    send({ type: 'KEYBIND_PICK_START', reqId: p4.reqId });
    send({ type: 'KEYBIND_PICKED', reqId: p4.reqId, site: 'youtube', target: { tag: 'button' } });
    ok('다른 사이트에서 고름 → notab', await until(() => (logText().match(/고르기 끝: notab/g) || []).length >= 2));
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'ttfc', id: '' });
    const p5 = await take('KB_PICK', mark);
    send({ type: 'KEYBIND_PICK_START', reqId: p5.reqId });
    mark = got.length;
    send({ type: 'TEST_KB_CANCEL' });
    const c5 = await take('KB_PICK_CANCEL', mark);
    ok('취소 → KB_PICK_CANCEL {reqId}', c5 && c5.reqId === p5.reqId, c5);
    ok('  결과 canceled', await until(() => logText().includes('[Test] 고르기 끝: canceled')));
    mark = got.length;
    send({ type: 'TEST_KB_PICK', site: 'ttfc', id: '' });
    const p6 = await take('KB_PICK', mark);
    const t0 = Date.now();
    ok('4초 안에 START 가 없으면 oldext', await until(() => logText().includes('[Test] 고르기 끝: oldext'), 7000) && Date.now() - t0 >= 3500, Date.now() - t0);
    ok('  그 뒤 늦게 온 PICKED 는 무시', (() => { send({ type: 'KEYBIND_PICKED', reqId: p6.reqId, site: 'ttfc', target: { tag: 'button' } }); return true; })());
    await wait(400);
    const last = [...got].reverse().find((m) => m.type === 'KEYBINDS');
    ok('  ttfc 에는 아무것도 안 생김', last && !last.keybinds.ttfc, last && last.keybinds.ttfc);
    ok('설정 파일에 저장됨 (keybinds·keybindsOn)', (() => { const c = JSON.parse(fs.readFileSync(CFG, 'utf8')); return c.keybinds && c.keybinds.youtube.length === 2 && c.keybindsOn && c.keybindsOn.youtube === false; })());
    ws.close();
  } catch (e) { if (e.message !== 'stop') { ok('예외 없음', false, e.message); } }
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  srv.close();
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);   // 시험 폴더 설정 되돌리기
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})();
