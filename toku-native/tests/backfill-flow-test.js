// 보충 기록 실제 흐름 점검 (시험 모드 알파 · 포트 17693 · 가짜 Discord · 가짜 확장이 RESOLVED 로 답함 · 로컬 HTTP 서버를 기본 웹훅으로)
//  ⚠ 진짜 웹훅·secrets.json 은 안 쓴다 (TOKU_NATIVE_TEST_SECRETS). 폴더 웹훅 자리의 discord 주소는 가짜이고 그쪽으로는 보내지 않는다
//  사용: node tests/backfill-flow-test.js <exe> [그림 저장 폴더]
'use strict';
const net = require('net'); const http = require('http'); const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const Module = require('module');
const WebSocket = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/node_modules/ws');
const realLoad = Module._load;
Module._load = function (req) { if (req === 'electron-log') return { info() {}, warn() {} }; if (req === 'electron') return {}; return realLoad.apply(this, arguments); };
const { accentFromPixels } = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/thumb-color.js');
const EXE = path.resolve(process.argv[2]);
const SNAP = process.argv[3] ? path.resolve(process.argv[3]) : '';
const PORT = 17693;
const PREFIX = 'toku-bf-test-' + process.pid + '-';
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const CFG = path.join(DIR, 'config.json');
const LOG = path.join(DIR, 'logs', 'main.log');
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(58) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 400) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 5000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = fn(); if (v) return v; await wait(30); } return null; };

function makePng(w, h) {   //  왼쪽은 진한 파랑, 오른쪽은 회색
  const rows = [], px = [];
  for (let y = 0; y < h; y++) {
    const r = [0];
    for (let x = 0; x < w; x++) { const c = x < 28 ? [30, 60, 220, 255] : [120, 120, 120, 255]; r.push(...c); px.push(c[2], c[1], c[0], c[3]); }
    rows.push(Buffer.from(r));
  }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(type), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  const png = Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
  return { png, bgra: Buffer.from(px) };
}

const posts = [];
let IMG = null;   //  직접 입력의 이미지 링크 자리 (GET /img.png)
const web = http.createServer((req, res) => {
  if (req.method === 'GET') {
    if (req.url === '/img.png' && IMG) { res.writeHead(200, { 'content-type': 'image/png' }); res.end(IMG); return; }
    res.writeHead(404); res.end('nope'); return;
  }
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
    out[name[1]] = { data: Buffer.from(seg.slice(i + 4, seg.length - 2), 'latin1'), filename: file && file[1], type: type && type[1] };
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
  //  폴더 '仮面ライダー響鬼' 캐릭터: 켜진 것 2 (discord 이름도 activeHookChars 에는 센다) · 꺼진 것 1
  fs.writeFileSync(CFG, JSON.stringify({ rpc: { enabled: false }, lang: 'ko', hookFolders: { '仮面ライダー響鬼': { characters: [{ id: 'a1', name: 'ヒビキ' }, { id: 'a2', name: '明日夢', off: true }, { id: 'a3', name: 'discord' }] } } }));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bfflow-'));
  const SEC = path.join(tmp, 'secrets.json');
  await new Promise((r) => web.listen(0, '127.0.0.1', r));
  //  폴더 웹훅은 가짜 discord 주소 (모양 검사만 통과, 이 작품으로는 보내지 않는다)
  fs.writeFileSync(SEC, JSON.stringify({ bingeWebhookUrl: 'http://127.0.0.1:' + web.address().port + '/hook', titleWebhooks: [{ title: '仮面ライダー響鬼', url: 'https://discord.com/api/webhooks/123456789012345678/' + 'x'.repeat(40) }] }));
  await new Promise((r) => pipe.listen('\\\\?\\pipe\\' + PREFIX + '0', r));
  const logStart = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const logText = () => (fs.existsSync(LOG) ? fs.readFileSync(LOG).slice(logStart).toString('utf8') : '');
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: PREFIX, TOKU_NATIVE_TEST_SECRETS: SEC });
  if (SNAP) { fs.mkdirSync(SNAP, { recursive: true }); env.TOKU_NATIVE_TEST_SNAP = SNAP; }
  const app = spawn(EXE, [], { env, stdio: 'ignore' });
  const { png, bgra } = makePng(40, 30);
  const durl = 'data:image/png;base64,' + png.toString('base64');
  const PAGES = {
    'https://pc.tokusatsu-fc.jp/movies/1/movie-stories/2': { ok: true, site: 'ttfc', url: 'https://pc.tokusatsu-fc.jp/movies/1/movie-stories/2', seriesName: ' 仮面ライダー響鬼\n', episodeNumber: '七之巻', episodeTitle: '息吹く鬼', thumbDataUrl: durl },
    'https://pc.tokusatsu-fc.jp/movies/1/movie-stories/9': { ok: false, error: 'blocked' },
    'https://imagination.m-78.jp/video/abc': { ok: true, site: 'imagination', url: 'https://imagination.m-78.jp/video/abc', seriesName: 'ウルトラマンアーク', episodeNumber: '第1話', episodeTitle: '', thumbDataUrl: durl },
    'https://pc.tokusatsu-fc.jp/movies/5/movie-stories/6': { ok: true, site: 'ttfc', url: 'https://pc.tokusatsu-fc.jp/movies/5/movie-stories/6', seriesName: '電子戦隊デンジマン', episodeNumber: '第2話', episodeTitle: 'タイトル' },
  };
  const asked = [];
  const resolvedLog = () => (logText().match(/\[Test\] bf resolved (.+)/g) || []).map((l) => JSON.parse(l.replace(/^\[Test\] bf resolved /, '')));
  const sendLog = () => (logText().match(/\[Test\] bf send: (\S+)/g) || []).map((l) => l.replace(/^\[Test\] bf send: /, ''));
  try {
    let ws = null;
    for (let k = 0; k < 100 && !ws; k++) {
      ws = await new Promise((r) => { const w = new WebSocket('ws://127.0.0.1:' + PORT, { origin: 'chrome-extension://dciaobllfdcegjcdmimclgglapnhggjm' }); w.on('open', () => r(w)); w.on('error', () => r(null)); });
      if (!ws) await wait(100);
    }
    if (!ws) throw new Error('포트에 못 붙음');
    ws.on('message', (d) => {
      const m = JSON.parse(d.toString());
      if (m.type !== 'RESOLVE_PAGE') return;
      asked.push(m);
      //  먼저 다른 id 로 거짓 답 (무시돼야 함) → 진짜 답
      ws.send(JSON.stringify({ type: 'RESOLVED', id: 'nope', ok: true, site: 'ttfc', seriesName: 'WRONG' }));
      const page = PAGES[m.url.trim()];   //  창은 앞뒤 빈칸을 떼고 보내지만 시험 훅은 그대로 보낸다 — 가짜 확장 쪽에서 뗀다
      if (page) setTimeout(() => ws.send(JSON.stringify(Object.assign({ type: 'RESOLVED', id: m.id }, page))), 50);
    });
    const send = (o) => ws.send(JSON.stringify(o));
    await wait(300);

    console.log('=== 링크 읽기 (폴더 있는 작품) ===');
    send({ type: 'TEST_BF_RESOLVE', url: '  https://pc.tokusatsu-fc.jp/movies/1/movie-stories/2' });
    const r1 = (await until(() => resolvedLog()[0], 5000)) || {};
    ok('RESOLVE_PAGE { id 16자 hex, url }', asked[0] && /^[0-9a-f]{16}$/.test(asked[0].id) && asked[0].url === '  https://pc.tokusatsu-fc.jp/movies/1/movie-stories/2', asked[0]);
    ok('다른 id 답은 무시 · 작품명 거름 (앞뒤 빈칸·줄바꿈)', r1.ok === true && r1.seriesName === '仮面ライダー響鬼' && r1.episodeNumber === '七之巻' && r1.site === 'ttfc', r1);
    ok('보낼 곳: 폴더 · 웹훅 있음 · 켜진 캐릭터 2', r1.folder === '仮面ライダー響鬼' && r1.hasHook === true && r1.chars === 2, r1);
    const want = accentFromPixels(bgra, { r: 2, g: 1, b: 0, a: 3 });
    ok('썸네일 받음 · 미리보기 색 = thumb-color.js', r1.thumb === true && want !== null && r1.accent === want, [r1.accent, want]);
    ok('토큰 16자 · 썸네일 바이트는 창에 안 감', /^[0-9a-f]{16}$/.test(r1.token || '') && !JSON.stringify(r1).includes('base64'));
    ok('로그: 보충 링크 읽음', logText().includes('[Binge] 보충 링크 읽음: ttfc 仮面ライダー響鬼 七之巻'));

    console.log('=== 못 읽은 링크 ===');
    send({ type: 'TEST_BF_RESOLVE', url: 'https://pc.tokusatsu-fc.jp/movies/1/movie-stories/9' });
    const r2 = await until(() => resolvedLog()[1], 5000);
    ok('error blocked 그대로', r2 && r2.ok === false && r2.error === 'blocked', r2);

    console.log('=== 보내기 검사 (폴더 웹훅 쪽으로는 안 보냄) ===');
    const pad = (x) => String(x).padStart(2, '0');
    const fmt = (d) => d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
    send({ type: 'TEST_BF_SEND', token: r1.token, seriesName: ' \u3000 ', episodeNumber: '', episodeTitle: '', when: '2026-11-01 21:05' });
    send({ type: 'TEST_BF_SEND', token: r1.token, seriesName: 'x', when: fmt(new Date(Date.now() + 20 * 60000)) });
    send({ type: 'TEST_BF_SEND', token: r1.token, seriesName: 'x', when: '1999-12-31 23:59' });
    send({ type: 'TEST_BF_SEND', token: 'ffffffffffffffff', seriesName: 'x', when: '2026-11-01 21:05' });
    const s1 = await until(() => (sendLog().length >= 4 ? sendLog() : null), 5000);
    ok('빈 작품명 → series · 미래 20분 → when · 1999년 → when · 다른 토큰 → stale', s1 && s1.join(',') === 'series,when,when,stale', s1);

    console.log('=== 다른 링크 → 앞 토큰은 끝 ===');
    send({ type: 'TEST_BF_RESOLVE', url: 'https://imagination.m-78.jp/video/abc' });
    const r3 = (await until(() => resolvedLog()[2], 5000)) || {};
    ok('맞는 폴더 없음 → 기본 웹훅 · 캐릭터 0', r3.ok && r3.folder === '' && r3.hasHook === true && r3.chars === 0, r3);
    send({ type: 'TEST_BF_SEND', token: r1.token, seriesName: '仮面ライダー響鬼', when: '2026-11-01 21:05' });
    await until(() => sendLog().length >= 5, 3000);
    ok('앞 링크 토큰 → stale', sendLog()[4] === 'stale', sendLog());

    console.log('=== 보내기 (썸네일 첨부) ===');
    send({ type: 'TEST_BF_SEND', token: r3.token, seriesName: '  ウルトラマン\u3000\u3000アーク ', episodeNumber: ' 第1話 ', episodeTitle: '', when: '2026-11-01 21:05' });
    const p1 = await until(() => posts[0], 8000);
    const mp = p1 && parseMultipart(p1);
    const pj = mp && mp.payload_json ? JSON.parse(mp.payload_json.data.toString('utf8')) : null;
    const e = pj && pj.embeds && pj.embeds[0];
    ok('multipart: files[0] thumb.png image/png = 받은 PNG', mp && mp['files[0]'] && mp['files[0]'].filename === 'thumb.png' && mp['files[0]'].type === 'image/png' && mp['files[0]'].data.equals(png), mp && Object.keys(mp));
    ok('  embed: 빈칸 정리된 작품명 · 굵은 화 · 정한 시각 줄', e && e.title === 'ウルトラマン アーク' && e.description === '**第1話**\n2026\\. 11. 01. 21:05', e);
    ok('  embed: 사이트 · 링크 · 썸네일 · 색 · 키 순서', e && e.author.name === 'TSUBURAYA IMAGINATION' && e.url === 'https://imagination.m-78.jp/video/abc' && e.thumbnail.url === 'attachment://thumb.png' && e.color === want && Object.keys(e).join(',') === 'title,description,color,author,url,thumbnail', e);
    ok('  캐릭터 없음 → embeds 만', pj && Object.keys(pj).join(',') === 'embeds', pj && Object.keys(pj));
    ok('  결과 ok 204 · 로그 전송 OK · 카드 색', await until(() => logText().includes('[Test] bf sent 1 204')) && logText().includes('[Binge] 보충 기록 전송 OK · ウルトラマン アーク 第1話') && logText().includes('[Binge] 보충 카드 색: #'));
    send({ type: 'TEST_BF_SEND', token: r3.token, seriesName: 'x', when: '2026-11-01 21:05' });
    await until(() => sendLog().length >= 7, 3000);
    ok('보낸 링크는 한 번만 (다시 → stale)', sendLog()[6] === 'stale', sendLog());

    console.log('=== 보내기 (썸네일 없음) ===');
    send({ type: 'TEST_BF_RESOLVE', url: 'https://pc.tokusatsu-fc.jp/movies/5/movie-stories/6' });
    const r4 = (await until(() => resolvedLog()[3], 5000)) || {};
    ok('썸네일 없음 → thumb false · 색 없음', r4.ok && r4.thumb === false && r4.accent === null, r4);
    send({ type: 'TEST_BF_SEND', token: r4.token, seriesName: '電子戦隊デンジマン', episodeNumber: '第2話', episodeTitle: 'タイトル', when: '2025-01-02 03:04' });
    const p2 = await until(() => posts[1], 8000);
    const j2 = p2 ? JSON.parse(p2.body.toString('utf8')) : null;
    const e2 = j2 && j2.embeds[0];
    ok('JSON · 사이트 색 #2ea6ff · 화 "번호 / 제목" · 썸네일 없음', p2 && /json/.test(p2.ct) && e2 && e2.color === 0x2ea6ff && e2.description === '**第2話 / タイトル**\n2025\\. 01. 02. 03:04' && !e2.thumbnail && e2.author.name === '東映特撮ファンクラブ', e2);
    ok('로그에 웹훅 주소 없음', !logText().includes('127.0.0.1:' + web.address().port) && !logText().includes('discord.com/api/webhooks'));

    console.log('=== 직접 입력 (TTFC 에 없는 특전 영상) ===');
    IMG = png;
    const base = 'http://127.0.0.1:' + web.address().port;
    const manualLog = () => (logText().match(/\[Test\] bf manual: (\S+)/g) || []).map((l) => l.replace(/^\[Test\] bf manual: /, ''));
    const postN = posts.length;
    send({ type: 'TEST_BF_MANUAL', seriesName: '響鬼 劇場版 特別編', episodeNumber: '特別編', episodeTitle: '超・特写', source: 'DVD 特典映像', imageUrl: base + '/img.png', when: '2026-11-02 20:00' });
    const m1 = await until(() => posts[postN], 8000);
    const mp1 = m1 && parseMultipart(m1);
    const pj1 = mp1 && mp1.payload_json ? JSON.parse(mp1.payload_json.data.toString('utf8')) : null;
    const e1 = pj1 && pj1.embeds[0];
    ok('이미지 링크 → 앱이 받아서 첨부 (thumb.png = 서버 PNG)', mp1 && mp1['files[0]'] && mp1['files[0]'].filename === 'thumb.png' && mp1['files[0]'].data.equals(png), mp1 && Object.keys(mp1));
    ok('  embed: 출처 줄 · 작품명 · "특별편 / 제목" · 정한 시각 · 그림 색', e1 && e1.author && e1.author.name === 'DVD 特典映像' && e1.title === '響鬼 劇場版 特別編' && e1.description === '**特別編 / 超・特写**\n2026\\. 11. 02. 20:00' && e1.color === want && !e1.url, e1);
    ok('  결과 ok 204 (기본 웹훅 = 로컬 서버)', await until(() => (logText().match(/\[Test\] bf sent 1 204/g) || []).length >= 3, 3000) !== null);
    const n2 = posts.length;
    send({ type: 'TEST_BF_MANUAL', seriesName: '特別編 X', episodeNumber: 'SP', source: '', imageUrl: base + '/nope.png', when: '2026-11-02 21:00' });
    const r2m = await until(() => posts[n2], 8000);
    const q2 = r2m && /json/.test(r2m.ct) ? JSON.parse(r2m.body.toString('utf8')) : null;
    ok('링크가 404 → 링크 그대로 thumbnail.url · 출처 없으면 author 없음', q2 && q2.embeds[0].thumbnail && q2.embeds[0].thumbnail.url === base + '/nope.png' && !q2.embeds[0].author && logText().includes('[Binge] 보충 이미지 링크를 받지 못함'), q2 && q2.embeds[0]);
    const n3 = posts.length;
    send({ type: 'TEST_BF_MANUAL', seriesName: '特別編 Y', episodeNumber: '', episodeTitle: 'ナレーション', source: 'ttfc', imageDataUrl: 'data:image/png;base64,' + png.toString('base64'), imageUrl: base + '/ignored.png', when: '2026-11-02 22:00' });
    const r3m = await until(() => posts[n3], 8000);
    const mp3 = r3m && parseMultipart(r3m);
    const f3 = mp3 && mp3.payload_json ? JSON.parse(mp3.payload_json.data.toString('utf8')).embeds[0] : null;
    ok('파일 그림 → 첨부 (링크는 무시) · 출처 "ttfc" 는 사이트 이름', mp3 && mp3['files[0]'] && mp3['files[0]'].data.equals(png) && f3 && f3.author.name === '東映特撮ファンクラブ' && /^\*\*ナレーション\*\*/.test(f3.description), f3);
    const n4 = posts.length;
    send({ type: 'TEST_BF_MANUAL', seriesName: '特別編 Z', episodeNumber: '1', source: '', imageUrl: '', when: '2026-11-02 23:00' });
    const r4m = await until(() => posts[n4], 8000);
    const q4 = r4m && /json/.test(r4m.ct) ? JSON.parse(r4m.body.toString('utf8')) : null;
    ok('그림·출처 없음 → JSON · thumbnail·author 없음 · 기본 파랑', q4 && !q4.embeds[0].thumbnail && !q4.embeds[0].author && q4.embeds[0].color === 0x2ea6ff, q4 && q4.embeds[0]);
    const before = manualLog().length;
    send({ type: 'TEST_BF_MANUAL', seriesName: 'A', imageUrl: 'http://example.com/x.png', when: '2026-11-02 23:00' });
    send({ type: 'TEST_BF_MANUAL', seriesName: 'A', imageUrl: 'https://ok.example/a b.png', when: '2026-11-02 23:00' });
    send({ type: 'TEST_BF_MANUAL', seriesName: '  ', imageUrl: '', when: '2026-11-02 23:00' });
    send({ type: 'TEST_BF_MANUAL', seriesName: 'A', imageUrl: '', when: '2099-01-01 00:00' });
    const ml = await until(() => (manualLog().length >= before + 4 ? manualLog().slice(before) : null), 4000);
    ok('거부: http 링크 · 빈칸 든 링크 → imgurl · 빈 작품명 → series · 미래 → when', ml && ml.join(',') === 'imgurl,imgurl,series,when', ml);
    await wait(500);
    ok('직접 입력으로 받은 것 4건 (거부된 건 안 감)', posts.length - postN === 4, posts.length - postN);
    ok('로그에 이미지 링크·웹훅 주소 없음', !logText().includes('/img.png') && !logText().includes('/nope.png') && !logText().includes('127.0.0.1:' + web.address().port));

    if (SNAP) {   //  끝낼 때 탭 그림 — 보충 탭 직접 입력 화면 (작품·화·출처·이미지 링크 채움)
      send({ type: 'TEST_BF_FILL', seriesName: '響鬼 劇場版 特別編', episodeNumber: '特別編', episodeTitle: '超・特写', source: 'DVD 特典映像', imageUrl: 'https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg', when: '2026-11-02 20:00' });
      await wait(3000);
    }
  } catch (err) { ok('예외 없음', false, err.message); }
  spawnSync(EXE, ['--quit'], { env });
  const t = Date.now();
  while (app.exitCode === null && Date.now() - t < 30000) await wait(100);
  if (app.exitCode === null) app.kill();
  if (SNAP) ok('보충 탭 그림 (tab6@100 · tab6@150)', fs.existsSync(path.join(SNAP, 'tab6@100.bmp')) && fs.existsSync(path.join(SNAP, 'tab6@150.bmp')));
  ok('폴더 웹훅(가짜 discord 주소) 쪽으로 보낸 것 없음 · 받은 것 6건 (링크 2 + 직접 입력 4)', posts.length === 6, posts.length);
  web.close(); pipe.close();
  if (saved) fs.writeFileSync(CFG, saved); else fs.unlinkSync(CFG);
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
  process.exit(fail ? 1 : 0);
})();
