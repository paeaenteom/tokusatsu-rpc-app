// 보충 거르기 교차 점검 — 진짜 extension-bridge.js(_onResolved) · rpc-window.js(BF_LINK) 와 C(bf_cli.exe) 에 같은 입력을 넣고 비교
//  본 시각: 창의 datetime-local 값 → new Date → 카드 날짜·10분 검사 를 C 의 글 칸 해석과 비교
//  사용: node tests/bf-diff.js <bf_cli.exe> [수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const Module = require('module');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 3000);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

//  electron-log 만 흉내 (extension-bridge.js 가 불러온다)
const realLoad = Module._load;
Module._load = function (req, ...rest) { if (req === 'electron-log' || req === 'electron') return { info() {}, warn() {}, error() {}, debug() {} }; return realLoad.call(this, req, ...rest); };
const Bridge = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/extension-bridge.js');
const WIN = fs.readFileSync('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/rpc-window.js', 'utf8');
const m = /const BF_LINK = \/(.+)\/;\n/.exec(WIN);
if (!m) { console.log('X rpc-window.js 에서 BF_LINK 를 못 찾음'); process.exit(1); }
const BF_LINK = new RegExp(m[1]);

function jsResolve(msg) {
  let out = null;
  const fake = { _resolveWaits: new Map([['x', { left: 1, firstFail: null, timer: null, done: (r) => { out = r; } }]]) };
  Bridge.prototype._onResolved.call(fake, Object.assign({}, msg, { id: 'x' }));
  return out;
}

// ── 무작위 값 ──
const CH = ['a', 'Z', ' ', '\t', '\n', '\r', '\u0000', '\u0001', '\u001f', '\u007f', '\u0085', '\u00a0', '\u1680', '\u2000', '\u200a', '\u200b', '\u2028', '\u202f', '\u205f', '\u3000', '\ufeff', '가', '仮面ライダー', '😀', '"', "'", '<', '>', '/', '?', '=', '&', '#', '%20', '.'];
const rs = (k) => { let s = ''; const n = int(k); for (let i = 0; i < n; i++) s += pick(CH); return s; };
const longs = (n16) => { let s = ''; while (s.length < n16) s += pick(['a', '가', '😀', ' ', '仮']); return s; };
const junk = () => pick([null, 0, 1, true, false, '', 'x', [], ['https://a.b'], { a: 1 }, 2.5, undefined]);
const rstrf = (max) => { const r = rnd(); if (r < 0.1) return junk(); if (r < 0.25) return longs(max - 3 + int(8)); return rs(4) + pick(['仮面ライダー響鬼', 'ウルトラマン', 'Kamen Rider', '第1話', '七之巻', '']) + rs(4); };
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';
const rb64 = (n) => { let s = ''; for (let i = 0; i < n; i++) s += B64[int(B64.length)]; return s; };
function rthumb() {
  const r = rnd();
  if (r < 0.1) return junk();
  const head = pick(['data:image/jpeg;base64,', 'data:image/png;base64,', 'data:image/webp;base64,', 'data:image/gif;base64,', 'data:image/jpg;base64,', 'data:image/png;base64', 'DATA:image/png;base64,', 'data:image/png;charset=x;base64,']);
  return head + rb64(int(40)) + pick(['', '', '', ' ', '\n', '-', '_', '가', '%']) + rb64(int(3));
}
function rurl() {
  const r = rnd();
  if (r < 0.1) return junk();
  return pick(['https://', 'http://', 'HTTPS://', ' https://', 'https:/', '']) + pick(['pc.tokusatsu-fc.jp/movies/12/movie-stories/34', 'imagination.m-78.jp/video/abc_D-1', 'x']) + pick(['', '', '', rs(3), '?a=1', '#x', '"', "'", '<', '>', '\u00a0', '\u3000', '\u200b', '\u2028', '\ufeff', ' ']) + (rnd() < 0.05 ? longs(600) : '');
}
function rmsg() {
  const o = {};
  const add = (k, v) => { if (v !== undefined) o[k] = v; };
  add('ok', rnd() < 0.7 ? true : pick([false, 1, 0, 'x', '', null, undefined]));
  add('site', rnd() < 0.85 ? pick(['ttfc', 'imagination']) : pick(['youtube', 'TTFC', '', null, 1, ['ttfc'], undefined]));
  add('url', rurl());
  add('seriesName', rstrf(200));
  add('episodeNumber', rstrf(60));
  add('episodeTitle', rstrf(200));
  add('thumbDataUrl', rnd() < 0.8 ? rthumb() : undefined);
  add('error', pick(['badurl', 'blocked', 'empty', 'fetch', 'oldext', 'noext', 'BADURL', '', null, 1, undefined]));
  return JSON.parse(JSON.stringify(o));   //  WebSocket 로 오는 것과 같게
}
function rlink() {
  return pick(['', ' ', '\t', '\u00a0', '\u3000', '\ufeff', '\u200b', '\n ']) + pick(['https://', 'http://', 'https:/', 'HTTPS://']) +
    pick(['pc.tokusatsu-fc.jp/movies/', 'pc.tokusatsu-fc.jp/movie/', 'imagination.m-78.jp/video/', 'imagination.m-78.jp/videos/', 'www.tokusatsu-fc.jp/movies/']) +
    pick(['123', '', 'a1', '9']) + pick(['/movie-stories/', '/movie-story/', '/movie-stories', '']) + pick(['45', '', 'x', '0']) + pick(['', '/', '?x=1', ' ', '가']);
}
const pad = (n, w) => String(n).padStart(w, '0');
function rwhen(now) {
  const base = new Date(now + (int(3) === 0 ? int(30) * 60000 - 15 * 60000 : -int(400) * 86400000));
  let y = base.getFullYear(), mo = base.getMonth() + 1, d = base.getDate(), h = base.getHours(), mi = base.getMinutes();
  const r = rnd();
  if (r < 0.08) d = pick([29, 30, 31, 0, 32]);
  else if (r < 0.12) mo = pick([0, 13, 2]);
  else if (r < 0.16) h = pick([24, 23, 0]);
  else if (r < 0.2) mi = pick([60, 59, 0]);
  else if (r < 0.26) y = pick([1999, 2000, 1970, 2100, 2024]);
  return { y, mo, d, h, mi };
}
function jsWhen(t, now) {
  //  datetime-local 은 없는 날짜·시각을 빈 값으로 만든다 → bfWhen() 이 null
  const chk = new Date(t.y, t.mo - 1, t.d, t.h, t.mi);
  if (t.mo < 1 || t.mo > 12 || t.h > 23 || t.mi > 59 || chk.getFullYear() !== t.y || chk.getMonth() !== t.mo - 1 || chk.getDate() !== t.d) return null;
  const v = pad(t.y, 4) + '-' + pad(t.mo, 2) + '-' + pad(t.d, 2) + 'T' + pad(t.h, 2) + ':' + pad(t.mi, 2);
  const w = new Date(v);
  if (isNaN(w.getTime())) return null;
  //  카드 날짜 = bfCardDate, 보낼 수 있나 = main.js backfill-send 검사 (whenMs = w.getTime())
  const card = w.getFullYear() + '. ' + pad(w.getMonth() + 1, 2) + '. ' + pad(w.getDate(), 2) + '. ' + pad(w.getHours(), 2) + ':' + pad(w.getMinutes(), 2);
  const when = new Date(Number(w.getTime()));
  const ok = !(isNaN(when.getTime()) || when.getFullYear() < 2000 || when.getTime() > now + 10 * 60 * 1000);
  return { v: v.replace('T', ' '), card, ok, epoch: w.getTime() };
}

(async () => {
  const c = spawn(EXE, [], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  const ask = async (o) => { c.stdin.write(JSON.stringify(o) + '\n'); return next(); };
  let fail = 0;
  const count = { resolved: 0, link: 0, when: 0, fixed: 0 };
  const bad = (what, inp, want, got) => { fail++; if (fail <= 6) { console.log('X 다름 ' + what + ' ' + JSON.stringify(inp).slice(0, 300)); console.log('   JS: ' + want.slice(0, 400)); console.log('   C : ' + got.slice(0, 400)); } };
  const now = Date.now();
  for (let i = 0; i < N; i++) {
    const r = rnd();
    if (r < 0.6) {
      const msg = rmsg();
      //  slice 가 이모지 가운데서 자르면 JS 는 반쪽 서로게이트가 남는다 — UTF-8 로 쓸 때(웹훅·화면) U+FFFD 가 되는 것과 같게 맞춰 비교
      const jr = jsResolve(msg);
      for (const k of Object.keys(jr || {})) if (typeof jr[k] === 'string') jr[k] = Buffer.from(jr[k], 'utf8').toString('utf8');
      const want = JSON.stringify(jr);
      const raw = await ask({ op: 'resolved', msg });
      const got = JSON.stringify(JSON.parse(raw));
      count.resolved++;
      if (got !== want) bad('resolved', msg, want, got);
      //  main.js backfill-resolve 의 썸네일 바이트 (거른 값 + 거르기 전 아무 값)
      for (const s of [jr && jr.thumbDataUrl, typeof msg.thumbDataUrl === 'string' ? msg.thumbDataUrl : null]) {
        if (!s) continue;
        const mm = /^data:(image\/(?:jpeg|png|webp));base64,(.+)$/.exec(s);
        const tw = JSON.stringify(mm ? { type: mm[1], hex: Buffer.from(mm[2], 'base64').toString('hex') } : null);
        const tg = JSON.stringify(JSON.parse(await ask({ op: 'thumb', s })));
        count.thumb = (count.thumb || 0) + 1;
        if (tg !== tw) bad('thumb', s, tw, tg);
      }
    } else if (r < 0.8) {
      const s = rlink();
      const want = String(BF_LINK.test(s));
      const got = await ask({ op: 'link', s });
      count.link++;
      if (got !== want) bad('link', s, want, got);
    } else {
      const t = rwhen(now);
      const want = JSON.stringify(jsWhen(t, now));
      const s = pick(['%Y-%M-%D %h:%m', '%Y-%M-%DT%h:%m', '%Y. %M. %D. %h:%m', ' %Y-%M-%D  %h:%m ']).replace('%Y', pad(t.y, 4)).replace('%M', pad(t.mo, 2)).replace('%D', pad(t.d, 2)).replace('%h', pad(t.h, 2)).replace('%m', pad(t.mi, 2));
      const got = JSON.stringify(JSON.parse(await ask({ op: 'when', s, now })));
      count.when++;
      if (got !== want) bad('when', s, want, got);
    }
  }
  //  글 칸에서만 쓰는 모양 (지금 앱엔 없음) — 정해진 답
  const FIX = [['2026-11-08 18:49', true], ['2026-11-8 8:5', true], ['2026/11/08 18:49', true], ['20261108 1849', false], ['2026-11-08', false], ['2026-11-08 18:49:30', false], ['2026-11-08 18:49 x', false], ['', false], ['가 2026-11-08 18:49', false], ['2026-02-29 10:00', false], ['2028-02-29 10:00', true], ['12345-01-01 00:00', false], ['2026-11-08\u300018:49', false]];
  for (const [s, ok] of FIX) {
    const got = JSON.parse(await ask({ op: 'when', s, now }));
    count.fixed++;
    if ((got !== null) !== ok) bad('fixed', s, String(ok), JSON.stringify(got));
  }
  c.stdin.end();
  await new Promise((res) => c.on('exit', res));
  console.log(fail ? '실패 ' + fail : '전부 같음 — ' + Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · '));
  process.exit(fail ? 1 : 0);
})();
