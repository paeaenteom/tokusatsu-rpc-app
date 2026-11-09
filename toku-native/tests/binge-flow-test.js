// 정주행 기록 실제 흐름 점검 (시험 모드 알파 · 포트 17694 · 가짜 Discord · 로컬 HTTP 서버를 기본 웹훅으로)
//  ⚠ 진짜 웹훅·secrets.json 은 안 쓴다 (TOKU_NATIVE_TEST_SECRETS). 썸네일이 외부로 올라가지 않게 시험 설정에서 RPC 를 끈다.
//  사용: node tests/binge-flow-test.js <exe>
'use strict';
const net = require('net'); const http = require('http'); const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const Module = require('module');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const realLoad = Module._load;
Module._load = function (req) { if (req === 'electron-log') return { info() {}, warn() {} }; if (req === 'electron') return {}; return realLoad.apply(this, arguments); };
const { accentFromPixels } = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/thumb-color.js');
const EXE = path.resolve(process.argv[2]);
const PORT = 17694;
const PREFIX = 'toku-binge-test-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const LOG = path.join(DIR, 'logs', 'main.log');
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(54) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 400) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 5000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = fn(); if (v) return v; await wait(30); } return null; };

//  40×30 PNG (줄이기 없이 그대로 색을 세게) — 왼쪽은 진한 빨강, 오른쪽 위는 회색
function makePng(w, h) {
  const rows = [], px = [];
  for (let y = 0; y < h; y++) {
    const r = [0];
    for (let x = 0; x < w; x++) {
      const c = x < 28 ? [210, 30, 40, 255] : [120, 120, 120, 255];
      r.push(...c);
      px.push(c[2], c[1], c[0], c[3]);   // BGRA 로 (accentFromPixels 비교용)
    }
    rows.push(Buffer.from(r));
  }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(type), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  const png = Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
  return { png, bgra: Buffer.from(px) };
}

//  받은 요청 (기본 웹훅 자리)
const posts = [];
const web = http.createServer((req, res) => {
  const parts = [];
  req.on('data', (d) => parts.push(d));
  req.on('end', () => { posts.push({ ct: req.headers['content-type'] || '', body: Buffer.concat(parts) }); res.writeHead(204); res.end(); });
});
function parseMultipart(p) {
  const m = /boundary=(.+)$/.exec(p.ct);
  if (!m) return null;
  const out = {};
  for (const seg of p.body.toString('latin1').split('--' + m[1])) {
    const i = seg.indexOf('\r\n\r\n');
    if (i < 0) continue;
    const head = seg.slice(0, i), name = /name="([^"]+)"/.exec(head), file = /filename="([^"]+)"/.exec(head), type = /Content-Type: ([^\r\n]+)/i.exec(head);
    if (!name) continue;
    const data = Buffer.from(seg.slice(i + 4, seg.length - 2), 'latin1');
    out[name[1]] = { data, filename: file && file[1], type: type && type[1] };
  }
  return out;
}

const pipe = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
  const enc = (op, o) => { const d = Buffer.from(JSON.stringify(o)); const h = Buffer.alloc(8); h.writeInt32LE(op, 0); h.writeInt32LE(d.length, 4); return Buffer.concat([h, d]); };
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

(async () => {
  const saved = fs.existsSync(CFG) ? fs.readFileSync(CFG) : null;
  fs.mkdirSync(DIR, { recursive: true });
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: false }, lang: 'ko' }));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bingeflow-'));
  const SEC = path.join(tmp, 'secrets.json');
  await new Promise((r) => web.listen(0, '127.0.0.1', r));
  fs.writeFileSync(SEC, JSON.stringify({ bingeWebhookUrl: 'http://127.0.0.1:' + web.address().port + '/hook' }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const logStart = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(logStart).toString('utf8') : '');
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SECRETS: SEC });
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  try {
    let ws = null;
    for (let k = 0; k < 100 && !ws; k++) {
      ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
      if (!ws) await wait(100);
    }
    if (!ws) throw new Error('포트에 못 붙음');
    const got = [];
    ws.on('message', (d) => got.push(JSON.parse(d.toString())));
    const send = (o) => ws.send(JSON.stringify(o));
    await wait(300);

    console.log('=== 정주행 상태 ===');
    send({ type: 'BINGE_STATE', on: true });
    ok('BINGE_STATE → 로그 \'정주행 모드: ON\'', await until(() => logText().includes('[Binge] 정주행 모드: ON')));

    console.log('=== 첨부 기록 (썸네일 바이트) ===');
    const { png, bgra } = makePng(40, 30);
    const T = 'https://example.invalid/thumb/1.png';
    send({ type: 'THUMB_BYTES', site: 'ttfc', url: T, dataUrl: 'data:image/png;base64,' + png.toString('base64'), rawDataUrl: 'data:image/png;base64,' + png.toString('base64') });
    const v1 = { type: 'VIDEO', site: 'ttfc', binge: true, seriesName: '仮面ライダー響鬼', episodeNumber: '七之巻', episodeTitle: '息吹く鬼', currentTime: 5, duration: 1422, isPlaying: true, thumbnail: T, url: 'https://pc.tokusatsu-fc.jp/watch/1' };
    const t0 = Date.now();
    for (let i = 0; i < 30; i++) { send(v1); await wait(250); if (posts.length) break; }
    const p1 = await until(() => posts[0], 12000);
    ok('6초 안정화 뒤 한 번 보냄', p1 && Date.now() - t0 >= 5500, Date.now() - t0);
    const mp = p1 && parseMultipart(p1);
    const pj = mp && mp.payload_json ? JSON.parse(mp.payload_json.data.toString('utf8')) : null;
    const e = pj && pj.embeds && pj.embeds[0];
    ok('multipart: payload_json + files[0] (thumb.png, image/png)', mp && mp['files[0]'] && mp['files[0]'].filename === 'thumb.png' && mp['files[0]'].type === 'image/png', mp && Object.keys(mp));
    ok('  첨부 바이트 = 보낸 PNG 그대로', mp && mp['files[0]'] && mp['files[0]'].data.equals(png));
    ok('  embed: 작품명 · 굵은 화 줄 · 날짜 줄 ("2026\\. 11. 08. HH:MM")', e && e.title === '仮面ライダー響鬼' && /^\*\*七之巻 \/ 息吹く鬼\*\*\n\d{4}\\\. \d\d\. \d\d\. \d\d:\d\d$/.test(e.description), e);
    ok('  embed: 사이트 이름 · 링크 · 썸네일 attachment://thumb.png', e && e.author && e.author.name === '東映特撮ファンクラブ' && e.url === v1.url && e.thumbnail && e.thumbnail.url === 'attachment://thumb.png');
    const want = accentFromPixels(bgra, { r: 2, g: 1, b: 0, a: 3 });
    ok('  카드 색 = thumb-color.js accentFromPixels 와 같음', e && want !== null && e.color === want, [e && e.color, want]);
    ok('  키 순서 (title·description·color·author·url·thumbnail)', e && Object.keys(e).join(',') === 'title,description,color,author,url,thumbnail', e && Object.keys(e));
    ok('  로그: 확인 (최대 대기) · 카드 색 · 전송 OK (이미지 첨부)', /\[Binge\] 확인 \(\d+ms, 최대 대기\)/.test(logText()) && logText().includes('[Binge] 카드 색: #') && logText().includes('[Binge] 웹훅 전송 OK (이미지 첨부)'));
    ok('  로그에 웹훅 주소 없음', !logText().includes('127.0.0.1:' + web.address().port));

    console.log('=== 같은 화 · 정주행 아님 ===');
    for (let i = 0; i < 4; i++) { send(v1); await wait(200); }
    send(Object.assign({}, v1, { binge: false, episodeNumber: '八之巻' }));
    await wait(7000);
    ok('같은 화를 또 받아도 다시 안 보냄 · binge:false 는 무시', posts.length === 1, posts.length);

    console.log('=== 주소 썸네일 (바이트 없음) ===');
    const v2 = Object.assign({}, v1, { site: 'imagination', seriesName: 'ウルトラマンブレーザー', episodeNumber: '第1話', episodeTitle: '', thumbnail: 'https://d1.cloudfront.net/none/x.jpg', url: 'https://imagination.m-78.jp/x' });
    for (let i = 0; i < 30; i++) { send(v2); await wait(250); if (posts.length > 1) break; }
    const p2 = await until(() => posts[1], 15000);
    const j2 = p2 && /json/.test(p2.ct) ? JSON.parse(p2.body.toString('utf8')) : null;
    const e2 = j2 && j2.embeds[0];
    ok('JSON 전송 · 썸네일 주소 · 사이트 색(받기 실패) #d32f5a', e2 && e2.thumbnail && e2.thumbnail.url === v2.thumbnail && e2.color === 0xd32f5a && e2.author.name === 'TSUBURAYA IMAGINATION', e2);
    ok('  화 제목이 없으면 화 번호만 굵게', e2 && /^\*\*第1話\*\*\n/.test(e2.description), e2 && e2.description);

    console.log('=== 완주 ===');
    send({ type: 'BINGE_END', site: 'youtube', seriesName: 'yt' });
    await wait(800);
    ok('유튜브 BINGE_END → 안 보냄', posts.length === 2, posts.length);
    send({ type: 'BINGE_END', site: 'ttfc', seriesName: '仮面ライダー響鬼' });
    const p3 = await until(() => posts[2], 8000);
    const e3 = p3 ? JSON.parse(p3.body.toString('utf8')).embeds[0] : null;
    ok('완주 카드: "{name} 완주" · 굵은 줄 · 노랑 #ffc233 · 사이트', e3 && e3.title === '仮面ライダー響鬼 완주' && /^\*\*마지막 화까지 시청 완료\*\*\n/.test(e3.description) && e3.color === 0xffc233 && e3.author.name === '東映特撮ファンクラブ', e3);
    ok('  로그: BINGE_END 수신', logText().includes('[Bridge] BINGE_END 수신: 仮面ライダー響鬼'));

    console.log('=== 웹훅 탭 시험 보내기 ===');
    const hookUrl = 'http://127.0.0.1:' + web.address().port + '/hook';
    send({ type: 'TEST_HOOK_SEND', url: hookUrl, title: '仮面ライダーフォーゼ', character: { name: '  如月  弦太朗 ', avatar: 'https://ok.example/a.png', avatarUntil: 0 } });
    const p4 = await until(() => posts[3], 8000);
    const j4 = p4 ? JSON.parse(p4.body.toString('utf8')) : null;
    ok('캐릭터가 먼저 (username · avatar_url) 그다음 embeds', j4 && Object.keys(j4).join(',') === 'username,avatar_url,embeds' && j4.username === '如月 弦太朗' && j4.avatar_url === 'https://ok.example/a.png', j4);
    const e4 = j4 && j4.embeds[0];
    ok('  embed: author TOKU RPC · 작품명 · "웹훅 연결 확인" · #7775fc', e4 && Object.keys(e4).join(',') === 'author,title,description,color' && e4.author.name === 'TOKU RPC' && e4.title === '仮面ライダーフォーゼ' && /^\*\*웹훅 연결 확인\*\*\n/.test(e4.description) && e4.color === 0x7775fc, e4);
    send({ type: 'TEST_HOOK_SEND', url: hookUrl, title: 'x', character: { name: 'discord bot' } });
    const p5 = await until(() => posts[4], 8000);
    const j5 = p5 ? JSON.parse(p5.body.toString('utf8')) : null;
    ok('  금지 이름 캐릭터는 빼고 embeds 만', j5 && Object.keys(j5).join(',') === 'embeds', j5);
    ok('  로그: 웹훅 시험 전송 OK', await until(() => (logText().match(/\[Binge\] 웹훅 시험 전송 OK/g) || []).length >= 2));

    console.log('=== 확장이 다 끊김 ===');
    ws.close();
    ok('정주행 상태 → 모름', await until(() => logText().includes('[Binge] 정주행 모드: 모름')));
  } catch (err) { ok('예외 없음', false, err.message); }
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  web.close(); pipe.close();
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})();
