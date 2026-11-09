// 카드 엔진 퍼징 — 모양이 엉망인 확장 메시지(모든 칸에 아무 종류 값)를 넣어도 C 가 죽지 않는지
//  (JS 원본은 이런 입력에 TypeError 로 멈추기도 해서 비교 대상이 아니다 — 여기선 '안 죽음' 만 본다)
'use strict';
const { spawnSync } = require('child_process');
const EXE = process.argv[2];
let seed = 4242;
const rnd = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return (seed & 0x7fffffff) / 0x7fffffff; };
const ri = (n) => Math.floor(rnd() * n);
const pick = (a) => a[ri(a.length)];
const anyVal = (d = 0) => {
  const k = ri(d > 2 ? 6 : 8);
  return [() => null, () => true, () => false, () => pick([0, -1, 1e308, -1e308, 1.5, 2 ** 53, -0, 0.1, 1e-300, 2147483648000]),
    () => pick(['', 'ttfc', 'youtube', '__proto__', 'constructor', 'http://x', 'https://a.cloudfront.net/x', '😀', 'a'.repeat(5000), '\u0000', '가'.repeat(200)]),
    () => undefined, () => { const o = {}; for (let i = 0; i < ri(4); i++) o[pick(['type', 'site', 'x', 'url'])] = anyVal(d + 1); return o; },
    () => { const a = []; for (let i = 0; i < ri(4); i++) a.push(anyVal(d + 1)); return a; }][k]();
};
const TYPES = ['VIDEO', 'BROWSING', 'CLEAR', 'PING', 'SET_ACTIVITY', 'THUMB_BYTES', 'BINGE_STATE', 'NOPE', 5, null];
const FIELDS = ['site', 'siteName', 'seriesName', 'episodeTitle', 'episodeNumber', 'currentTime', 'duration', 'thumbnail', 'isPlaying', 'url', 'isLive', 'liveStartedAt', 'startedAtMs', 'playbackRate', 'page', 'detail', 'banner'];
let total = 0, fails = 0;
for (let run = 0; run < 40; run++) {
  let t = 1762500000000;
  const ev = [{ ev: 'init', t, noApp: [] }, { t, ev: 'rpc', on: true }];
  for (let i = 0; i < 400; i++) {
    t += pick([0, 1, 100, 2000, 30000]);
    const msg = { type: pick(TYPES) };
    for (const f of FIELDS) if (rnd() < 0.6) { const v = anyVal(); if (v !== undefined) msg[f] = v; }
    ev.push({ t, ev: 'msg', client: 1, msg });
    if (rnd() < 0.05) ev.push({ t, ev: 'settings', settings: { timeMode: anyVal(), showTime: anyVal(), statusDisplay: anyVal(), activityType: anyVal(), showButtons: anyVal() } });
    if (rnd() < 0.03) ev.push({ t, ev: 'discord', up: rnd() < 0.5 });
  }
  ev.push({ t: t + 600000, ev: 'end' });
  total += ev.length;
  const r = spawnSync(EXE, [], { input: ev.map((e) => JSON.stringify(e)).join('\n') + '\n', maxBuffer: 1 << 28 });
  if (r.status !== 0) { fails++; console.log('  X   실행 ' + run + ' 종료 코드 ' + r.status + ' ' + String(r.stderr).slice(0, 200)); }
}
console.log('  ' + (fails ? 'X   ' : 'OK  ') + ('엉망인 메시지 ' + total + '개 → C 엔진 죽지 않음').padEnd(50) + (fails ? fails + '번 죽음' : ''));
process.exit(fails ? 1 : 0);
