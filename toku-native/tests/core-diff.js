// 카드 엔진 차이 검사 — 같은 시나리오를 ① 지금 앱의 진짜 discord-rpc.js·extension-bridge.js (가짜 시계·가짜 Discord)
//  ② C 엔진(core_cli.exe) 에 넣고, Discord 로 나간 요청·로그·확장에 보낸 메시지를 한 줄씩 비교한다
//  node tests\core-diff.js build\core_cli.exe [시나리오 수] [시드]
'use strict';
const fs = require('fs'); const vm = require('vm'); const path = require('path'); const EventEmitter = require('events');
const { spawnSync } = require('child_process');
const APP = 'C:/Users/paeae/Downloads/toku rpc/ttfc-app/';
const SRC = APP + 'src/';
const EXE = process.argv[2];
const NSCEN = Number(process.argv[3] || 200);
const SEED0 = Number(process.argv[4] || 1);
const RealClient = require(APP + 'node_modules/discord-rpc').Client;
const READY_DELAY = 5;
const TEST_IDS = { ttfc: '100000000000000001', imagination: '100000000000000002', youtube: '100000000000000003', disneyplus: '100000000000000004', chzzk: '100000000000000005', danime: '100000000000000006' };
const SITE_OF_ID = Object.fromEntries(Object.entries(TEST_IDS).map(([k, v]) => [v, k]));

// ════════ JS 쪽 실행기 ════════
async function runJs(events) {
  const rec = [];
  let clock = 0, seq = 0, tid = 0;
  const timers = [];   // { id, at, seq, every, fn, args, obj }
  const norm = (d) => { d = Number(d); return (d >= 1 && d <= 2147483647) ? d : 1; };
  function mk(fn, delay, args, repeat) {
    const d = norm(delay);
    const t = { id: ++tid, at: clock + d, seq: ++seq, every: repeat ? d : 0, fn, args };
    const obj = { _t: t, unref() { return this; }, ref() { return this; }, hasRef() { return false; } };
    t.obj = obj;
    timers.push(t);
    return obj;
  }
  function clear(o) { if (!o || !o._t) return; const i = timers.indexOf(o._t); if (i >= 0) timers.splice(i, 1); }
  function next() { let b = null; for (const t of timers) if (!b || t.at < b.at || (t.at === b.at && t.seq < b.seq)) b = t; return b; }
  class FakeDate extends Date { constructor(...a) { if (a.length) super(...a); else super(clock); } static now() { return clock; } }
  const ctx = {
    setTimeout: (fn, d, ...a) => mk(fn, d, a, false), setInterval: (fn, d, ...a) => mk(fn, d, a, true),
    clearTimeout: clear, clearInterval: clear, Date: FakeDate, performance: { now: () => clock },
    console, Buffer, URL, Promise, Math, JSON, Map, Set, WeakMap, Number, String, Object, Array, Error, TypeError, RangeError, RegExp, Symbol, isFinite, isNaN, encodeURIComponent,
    FormData, Blob, fetch: () => Promise.reject(new Error('no network in test')), AbortSignal, process,
  };
  vm.createContext(ctx);
  const out = (k, o) => rec.push(Object.assign({ t: clock, k }, o));
  const fix = (s) => Buffer.from(String(s), 'utf8').toString('utf8');   // 짝 없는 서로게이트 → U+FFFD (C 와 같은 바이트)
  const logger = { info: (...a) => out('log', { lvl: 0, text: fix(a.map(String).join(' ')) }), warn: (...a) => out('log', { lvl: 1, text: fix(a.map(String).join(' ')) }), error: (...a) => out('log', { lvl: 2, text: fix(a.map(String).join(' ')) }) };
  let discordUp = true, replyStall = false, noApp = [], connSeq = 0;
  const clients = [];
  class FakeClient extends EventEmitter {
    constructor() { super(); this.conn = ++connSeq; this.open = false; this.destroyed = false; this._expecting = new Map(); this.user = undefined; clients.push(this); }
    login({ clientId }) {
      out('connect', { conn: this.conn, site: SITE_OF_ID[clientId] });
      return new Promise((res, rej) => {
        ctx.setTimeout(() => {
          if (this.destroyed) { rej(new Error('connection closed')); return; }
          if (discordUp) { this.user = { username: 'tester' }; this.open = true; this.emit('ready'); res(this); }
          else rej(new Error('Could not connect'));
        }, READY_DELAY);
      });
    }
    request(cmd, args, evt) {
      if (!this.open) return Promise.reject(new TypeError("Cannot read properties of null (reading 'write')"));
      out('req', { conn: this.conn, args: JSON.parse(JSON.stringify(Object.assign({}, args, { pid: 0 }))) });
      const data = args.activity ? { type: args.activity.type, status_display_type: args.activity.status_display_type } : null;
      if (!replyStall) return Promise.resolve(data);
      return new Promise((res, rej) => { this._expecting.set(Symbol(), { resolve: res, reject: rej }); });
    }
    _rejectAll() { for (const e of this._expecting.values()) e.reject(new Error('connection closed')); this._expecting.clear(); }
    destroy() {
      out('destroy', { conn: this.conn });
      this.open = false; this.destroyed = true;
      this._rejectAll();
      Promise.resolve().then(() => this.emit('disconnected'));
      return Promise.resolve();
    }
  }
  FakeClient.prototype.setActivity = RealClient.prototype.setActivity;
  FakeClient.prototype.clearActivity = RealClient.prototype.clearActivity;
  function load(file, mocks) {
    const src = fs.readFileSync(SRC + file, 'utf8');
    const fn = vm.compileFunction(src, ['require', 'module', 'exports', '__filename', '__dirname'], { parsingContext: ctx });
    const module = { exports: {} };
    const req = (m) => {
      if (m in mocks) return mocks[m];
      if (m === './i18n') return require(SRC + 'i18n.js');
      if (m === 'electron') return {};
      return require(m);
    };
    fn(req, module, module.exports, SRC + file, SRC);
    return module.exports;
  }
  const secrets = { appId: (s) => (noApp.includes(s) ? '' : (TEST_IDS[s] || '')), avatarUpload: () => null };
  const Hub = load('discord-rpc.js', { 'discord-rpc': { Client: FakeClient }, 'electron-log': logger, './secrets': secrets });
  const Bridge = load('extension-bridge.js', { 'electron-log': logger, ws: { WebSocketServer: class {} } });
  let hub = null, bridge = null, settings = {}, rpcOn = true;
  const wsOf = (client) => ({ send: (d) => out('ws', { client, json: d }), readyState: 1 });
  const drain = () => new Promise((r) => setImmediate(r));
  for (const e of events) {
    if (e.ev === 'init') { clock = e.t; noApp = e.noApp || []; hub = new Hub(TEST_IDS.ttfc); bridge = new Bridge(hub, () => settings, null); continue; }
    for (;;) {
      const t = next();
      if (!t || t.at > e.t) break;
      clock = t.at;
      if (!t.every) timers.splice(timers.indexOf(t), 1);
      t.fn(...t.args);
      if (t.every && timers.includes(t)) { t.at = clock + t.every; t.seq = ++seq; }
      await drain(); await drain();
    }
    clock = e.t;
    switch (e.ev) {
      case 'msg': bridge._handleMessage(JSON.parse(JSON.stringify(e.msg)), wsOf(e.client || 1)); break;
      case 'discord':
        if (!e.up && discordUp) for (const c of clients) { if (!c.open) continue; c.open = false; c._rejectAll(); c.emit('disconnected'); }
        discordUp = e.up; break;
      case 'reply': replyStall = e.mode === 'stall'; break;
      case 'settings': settings = JSON.parse(JSON.stringify(e.settings)); hub.applySettings(settings); break;
      case 'lang': hub.setLang(e.lang); break;
      case 'idle': hub.setIdleTimeout(e.ms); break;
      case 'level': hub.setResponseLevel(e.level); break;
      case 'rpc': rpcOn = e.on; if (rpcOn) hub.connect(); else { hub.clearActivity(); hub.disconnect(); } break;
      case 'reconnect':
        hub.disconnect();
        ctx.setTimeout(() => { if (!rpcOn) { logger.info('[RPC] 다시 연결 취소 — 그 사이 RPC 가 꺼졌습니다'); return; } hub.connect(); }, 1000);
        break;
      case 'hosted': {
        //  cacheThumbnail 의 뒷부분 (업로드가 끝난 뒤) 그대로
        hub._uploading.delete(e.url);
        logger.info('[RPC] 썸네일 재호스팅 OK →', e.hosted);
        while (hub._hosted.size >= 300) hub._hosted.delete(hub._hosted.keys().next().value);
        hub._hosted.set(e.url, { url: e.hosted, at: Date.now() === clock ? clock : clock, keepMs: e.keep });
        hub._saveHosted();
        for (const c of hub.clients.values()) c.adoptThumb(e.url, e.hosted, e.keep);
        break;
      }
    }
    await drain(); await drain();
  }
  return rec;
}

// ════════ C 쪽 실행기 ════════
function runC(events) {
  const r = spawnSync(EXE, [], { input: events.map((e) => JSON.stringify(e)).join('\n') + '\n', maxBuffer: 1 << 28 });
  if (r.status !== 0) throw new Error('core_cli exit ' + r.status + ' ' + r.stderr);
  return r.stdout.toString().split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
}

// ════════ 시나리오 만들기 ════════
function makeScenario(seed) {
  let s = seed >>> 0 || 1;
  const rnd = () => { s = (Math.imul(s, 1103515245) + 12345) >>> 0; return (s & 0x7fffffff) / 0x7fffffff; };
  const ri = (n) => Math.floor(rnd() * n);
  const pick = (a) => a[ri(a.length)];
  const chance = (p) => rnd() < p;
  const SITES = ['ttfc', 'imagination', 'youtube', 'disneyplus', 'chzzk', 'danime'];
  const TITLES = ['仮面ライダー響鬼', 'スーパー戦隊', 'Z', '가', ' ', '  우주  ', '😀', '鬼'.repeat(130), '가나다라'.repeat(40), 'ultraman 🎬 アーク', '', 'a\tb', '로봇'];
  const EPS = ['七之巻', '第1話', '1', '', '#12', '😀😀', '終'.repeat(140)];
  const THUMBS = ['https://d1.cloudfront.net/ep/1.jpg', 'https://d2.cloudfront.net/ep/2.jpg', 'https://disney.images.edge.bamgrid.com/x.jpg', 'https://imagination.m-78.jp/img/a.jpg',
    'https://pc.tokusatsu-fc.jp/a.png', 'kamen_rider_logo', 'javascript:alert(1)', '', 'http://x.y/z.png', 'https://xdisney.images.edge.bamgrid.com/a.jpg'];
  const URLS = { ttfc: ['https://pc.tokusatsu-fc.jp/watch/1', 'https://pc.tokusatsu-fc.jp/', ''], imagination: ['https://imagination.m-78.jp/video/2'], youtube: ['https://www.youtube.com/watch?v=abc&pp=비밀', 'https://www.youtube.com/@ch/search?query=x', 'https://www.youtube.com/watch?v=' + 'x'.repeat(600)],
    disneyplus: ['https://www.disneyplus.com/video/abc'], chzzk: ['https://chzzk.naver.com/live/0123456789abcdef0123456789abcdef', 'https://chzzk.naver.com/search?query=비밀'], danime: ['https://animestore.docomo.ne.jp/animestore/sc_d_pc?partId=12345001', 'ftp://x'] };
  const settingsOf = () => {
    const o = {};
    if (chance(0.7)) o.showTime = chance(0.85);
    if (chance(0.7)) o.timeMode = pick(['progress', 'remaining', 'none', undefined, 'weird']);
    if (o.timeMode === undefined) delete o.timeMode;
    for (const k of ['showSeries', 'showEpisode', 'showThumbnail', 'showButtons']) if (chance(0.5)) o[k] = chance(0.8);
    if (chance(0.5)) o.activityType = pick([0, 2, 3, 1, '2', null, 'x']);
    if (chance(0.5)) { o.statusDisplay = {}; for (const st of SITES) if (chance(0.5)) o.statusDisplay[st] = pick([0, 1, 2, 5, '1']); }
    return o;
  };
  const T0 = 1762500000000 + ri(1000000) * 1000;
  let t = T0;
  const ev = [];
  //  JS 는 TTFC·IMAGINATION·YouTube·Disney+ 의 ID 를 코드에 갖고 있다(내장). C 는 코드에 ID 를 두지 않는다(규칙) —
  //  그래서 'ID 없음' 은 내장 ID 가 없는 사이트로만 시험한다. 교체 전 secrets.json 에 6개가 다 있는지 확인 (PLAN.md)
  const noApp = chance(0.3) ? [pick(['chzzk', 'danime'])] : [];
  ev.push({ ev: 'init', t, noApp });
  ev.push({ t, ev: 'level', level: pick(['max', 'high', 'normal', 'low', 'turbo']) });
  ev.push({ t, ev: 'idle', ms: pick([300000, 0, 60000, 180000, 300000]) });
  ev.push({ t, ev: 'lang', lang: pick(['ko', 'en', 'ja', 'ko']) });
  ev.push({ t, ev: 'settings', settings: settingsOf() });
  if (chance(0.9)) ev.push({ t, ev: 'rpc', on: true });
  //  사이트별 재생 상태 (현실적인 흐름)
  const play = {};
  const N = 40 + ri(160);
  //  경계값 패턴 (돌연변이 검사에서 못 잡은 것 보강) — 한도 연타, 30초 경계, 진행 바 어긋남
  const patterns = [];
  if (chance(0.5)) patterns.push('burst');
  if (chance(0.5)) patterns.push('edge30');
  if (chance(0.5)) patterns.push('drift');
  for (let i = 0; i < N; i++) {
    t += pick([0, 1, 20, 21, 50, 150, 500, 1000, 2000, 2000, 2000, 3000, 3999, 4000, 4001, 5000, 8000, 15000, 30000, 61000, 400000]);
    if (patterns.length && chance(0.08)) {
      const pat = pick(patterns);
      const site = pick(SITES);
      const base = { type: 'VIDEO', site, siteName: '', seriesName: 'B' + ri(3), episodeNumber: 'E1', episodeTitle: '', duration: 1422, thumbnail: pick(THUMBS), url: pick(URLS[site]), isLive: false, liveStartedAt: 0, playbackRate: 1 };
      if (pat === 'burst') {
        //  20초 안에 6~12번 바뀜 → 토큰을 다 쓰고 기다렸다 보내는 경로
        let ct = ri(500);
        for (let j = 0; j < 6 + ri(7); j++) {
          t += pick([100, 300, 900, 1500, 3999, 4000, 4001]);
          ct += pick([10, 50, 200]);
          ev.push({ t, ev: 'msg', client: 1, msg: Object.assign({}, base, { currentTime: ct, isPlaying: chance(0.5) }) });
        }
      } else if (pat === 'edge30') {
        //  같은 페이지 둘러보기 → 영상 → 29~31초 뒤 같은 페이지로 (영상을 보다 왔는지 경계: 시간을 새로 세는가)
        const purl = 'https://example.com/p' + ri(2);
        const nav = { type: 'BROWSING', site, siteName: '', page: '作品ページ', detail: '', banner: '', url: purl };
        ev.push({ t, ev: 'msg', client: 1, msg: nav });
        t += pick([1000, 5000, 20000]);
        ev.push({ t, ev: 'msg', client: 1, msg: Object.assign({}, base, { currentTime: 10, isPlaying: true }) });
        t += pick([29000, 29500, 29999, 30000, 30001, 31000]);
        ev.push({ t, ev: 'msg', client: 1, msg: nav });
      } else {
        //  재생 중 위치가 카드보다 조금씩 처지거나 앞섬 (한 번에 3초 안이라 '탐색' 은 아님) → 허용치 경계에서 다시 잡기
        let ct = 100;
        ev.push({ t, ev: 'msg', client: 1, msg: Object.assign({}, base, { currentTime: ct, isPlaying: true }) });
        const step = pick([0.5, 1, 1.5, 2, -1]);
        for (let j = 0; j < 8; j++) {
          t += 2000;
          ct += 2 - step;
          ev.push({ t, ev: 'msg', client: 1, msg: Object.assign({}, base, { currentTime: ct, isPlaying: true }) });
        }
      }
      continue;
    }
    const k = rnd();
    const site = chance(0.05) ? pick(['netflix', undefined, '', 'ttfc']) : pick(SITES);
    if (k < 0.55) {
      const p = play[site] || (play[site] = { ep: 0, ct: ri(100), dur: pick([0, 1422, 1437, 600, 3600]), playing: false, at: t, rate: 1, live: false, ls: 0 });
      if (p.playing) p.ct += (t - p.at) / 1000 * p.rate;
      p.at = t;
      if (chance(0.1)) { p.ep++; p.ct = 0; p.dur = pick([0, 1422, 600]); }
      if (chance(0.15)) p.playing = !p.playing;
      if (chance(0.05)) p.ct = ri(1400);
      if (chance(0.05)) p.rate = pick([1, 1.5, 2, 0.5, 1.005, 20, 0]);
      if (chance(0.05)) { p.live = !p.live; p.ls = chance(0.5) ? T0 - ri(10000000) : 0; }
      const msg = { type: chance(0.03) ? 'SET_ACTIVITY' : 'VIDEO', site, siteName: chance(0.2) ? pick(['', 'Custom', '東映']) : '', seriesName: pick(TITLES), episodeNumber: pick(EPS),
        episodeTitle: chance(0.5) ? pick(EPS) : '', currentTime: Math.round(p.ct * 1000) / 1000, duration: p.dur, thumbnail: pick(THUMBS), isPlaying: p.playing,
        url: pick(URLS[site] || ['https://example.com/']), isLive: p.live, liveStartedAt: p.live ? p.ls : 0, playbackRate: p.rate };
      if (p.ep % 7 !== 0) { msg.seriesName = 'S' + (p.ep % 3); msg.episodeNumber = 'E' + p.ep; }
      if (chance(0.3)) msg.startedAtMs = t - Math.round(p.ct / (p.rate || 1) * 1000);
      if (chance(0.1)) delete msg.siteName;
      ev.push({ t, ev: 'msg', client: 1 + ri(2), msg });
    } else if (k < 0.75) {
      ev.push({ t, ev: 'msg', client: 1, msg: { type: 'BROWSING', site, siteName: '', page: pick(['作品ページ', '홈', 'H', '', 'シリーズ一覧']), detail: pick(['', '仮面ライダー', 'Z', '詳細'.repeat(80)]),
        banner: pick(THUMBS), url: pick(URLS[site] || ['https://example.com/a', 'https://example.com/b']) } });
    } else if (k < 0.80) ev.push({ t, ev: 'msg', client: 1, msg: chance(0.7) ? { type: 'CLEAR', site } : { type: 'CLEAR' } });
    else if (k < 0.82) ev.push({ t, ev: 'msg', client: 2, msg: { type: 'PING' } });
    else if (k < 0.86) ev.push({ t, ev: 'discord', up: chance(0.5) });
    else if (k < 0.88) ev.push({ t, ev: 'reply', mode: chance(0.7) ? 'ok' : 'stall' });
    else if (k < 0.90) ev.push({ t, ev: 'settings', settings: settingsOf() });
    else if (k < 0.91) ev.push({ t, ev: 'lang', lang: pick(['ko', 'en', 'ja', 'fr']) });
    else if (k < 0.92) ev.push({ t, ev: 'idle', ms: pick([300000, 0, 60000, 90000]) });
    else if (k < 0.93) ev.push({ t, ev: 'level', level: pick(['max', 'high', 'normal', 'low']) });
    else if (k < 0.95) ev.push({ t, ev: 'rpc', on: chance(0.6) });
    else if (k < 0.96) ev.push({ t, ev: 'reconnect' });
    else ev.push({ t, ev: 'hosted', url: pick(THUMBS.filter((x) => x.startsWith('http'))), hosted: 'https://h.example/' + ri(1000) + '.png', keep: pick([0, 3600000, 216000000, 7200000]) });
  }
  ev.push({ t: t + 20 * 60 * 1000, ev: 'end' });
  return ev;
}

(async () => {
  let fail = 0, totalRec = 0, totalReq = 0, totalEv = 0;
  for (let i = 0; i < NSCEN; i++) {
    const seed = SEED0 + i;
    const evs = makeScenario(seed);
    totalEv += evs.length;
    const a = await runJs(evs);
    const b = runC(evs);
    totalRec += a.length;
    totalReq += a.filter((x) => x.k === 'req').length;
    const n = Math.max(a.length, b.length);
    for (let k = 0; k < n; k++) {
      const x = JSON.stringify(a[k]), y = JSON.stringify(b[k]);
      if (x !== y) {
        fail++;
        console.log('X   시나리오 ' + seed + ' — ' + k + '번째 기록부터 다름');
        for (let j = Math.max(0, k - 3); j < Math.min(n, k + 3); j++) {
          console.log('   ' + (j === k ? '>>' : '  ') + ' JS: ' + JSON.stringify(a[j]).slice(0, 330));
          console.log('   ' + (j === k ? '>>' : '  ') + ' C : ' + JSON.stringify(b[j]).slice(0, 330));
        }
        break;
      }
    }
    if (fail >= 3) break;
  }
  console.log(fail ? '실패 ' + fail : '전부 같음 — 시나리오 ' + NSCEN + '개, 사건 ' + totalEv + '개, 비교한 기록 ' + totalRec + '줄 (Discord 요청 ' + totalReq + '건)');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log('테스트 오류', e && e.stack); process.exit(2); });
