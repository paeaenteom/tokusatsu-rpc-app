// 전송 V2 점검 (테스트 버전(알파) 모드) — 가짜 시계 위 코어(core_cli)에 같은 시나리오를 V1·V2(TOKU_CORE_V2=1) 로 넣고 비교
//  Discord 클라이언트 제한(웹 JS 실측): 서버로 20초에 5번, 내용이 같은 건 안 셈, 넘치면 마지막 상태만 늦게
//  ① 내용이 바뀐 전송이 어느 20초에도 5번을 넘지 않음 (Discord 가 늦출 일이 없음)
//  ② 위치 이동(정지+재생 0.1초 간격) 한 번 = 전송 한 번
//  ③ 마지막 상태는 반드시 간다 · ④ 새 영상은 기다리지 않고 바로 · ⑤ 여러 사이트가 한 창을 같이 씀
//  사용: node tests/presence-v2-test.js <core_cli.exe>
'use strict';
const { spawnSync } = require('child_process'); const path = require('path');
const EXE = path.resolve(process.argv[2]);
const V1 = false, V2 = true;
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(62) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 300) : '')); if (!c) fail++; };
const run = (v2, events) => {
  const env = Object.assign({}, process.env);
  if (v2) env.TOKU_CORE_V2 = '1'; else delete env.TOKU_CORE_V2;
  const r = spawnSync(EXE, [], { env, input: events.map((e) => JSON.stringify(e)).join('\n') + '\n', maxBuffer: 1 << 28 });
  if (r.status !== 0) throw new Error('core_cli exit ' + r.status + ' ' + r.stderr);
  return r.stdout.toString().split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
};
const T0 = 1762500000000;
const head = () => [{ ev: 'init', t: T0, noApp: [] }, { t: T0, ev: 'level', level: 'max' }, { t: T0, ev: 'idle', ms: 300000 }, { t: T0, ev: 'lang', lang: 'ko' }, { t: T0, ev: 'settings', settings: {} }, { t: T0, ev: 'rpc', on: true }];
const vid = (site, series, ct, dur, playing, anchor, t) => {
  const m = { type: 'VIDEO', site, siteName: '', seriesName: series, episodeNumber: '', episodeTitle: 'ch', duration: dur, currentTime: ct, isPlaying: !!playing, thumbnail: 'https://i.ytimg.com/vi/' + series + '/hqdefault.jpg', url: 'https://www.youtube.com/watch?v=' + series, isLive: false, liveStartedAt: 0, playbackRate: 1 };
  if (anchor) m.startedAtMs = t - ct * 1000;
  return m;
};
//  보낸 SET_ACTIVITY (지우기 포함) — 연결별 '내용이 바뀐' 것만 센다 (Discord 깊은 비교와 같게)
function sends(recs) {
  const last = {};
  const out = [];
  for (const r of recs) {
    if (r.k !== 'req') continue;
    const key = JSON.stringify(r.args);
    const changed = last[r.conn] !== key;
    last[r.conn] = key;
    out.push({ t: r.t - T0, conn: r.conn, act: r.args.activity || null, changed });
  }
  return out;
}
function maxIn20s(list) {
  const ts = list.filter((x) => x.changed).map((x) => x.t);
  let best = 0;
  for (let i = 0; i < ts.length; i++) { let k = i; while (k < ts.length && ts[k] < ts[i] + 20000) k++; best = Math.max(best, k - i); }
  return best;
}
const pos = (a, t) => (a && a.timestamps && a.timestamps.start ? Math.round(((T0 + t) / 1000 - a.timestamps.start) * 10) / 10 : null);

// ── ① ② ③ 실사용 로그(2026-11-08 12:42~12:45) 순서·간격 그대로 ──
const EV = [
  [0.0, 'A', 0, 394, 0, 1], [0.652, 'A', 0, 395, 1, 0], [0.699, 'A', 0, 394, 0, 1], [0.714, 'A', 0, 394, 1, 0],
  [2.948, 'A', 2, 394, 0, 1], [3.032, 'A', 69, 394, 1, 0], [5.463, 'B', 0, 331, 1, 0], [7.458, 'B', 1, 331, 0, 1], [7.561, 'B', 53, 331, 1, 0],
  [12.434, 'A', 0, 394, 0, 1], [12.546, 'A', 0, 394, 1, 0], [12.678, 'A', 0, 394, 0, 1], [12.81, 'A', 0, 394, 1, 0],
  [19.186, 'C', 0, 257, 1, 0], [24.696, 'C', 4, 256, 0, 1], [24.791, 'C', 21, 256, 1, 0], [26.201, 'C', 22, 256, 0, 1], [26.308, 'C', 119, 256, 1, 0],
  [38.065, 'D', 0, 217, 1, 0], [41.32, 'D', 2, 217, 0, 1], [41.404, 'D', 29, 217, 1, 0], [49.244, 'E', 0, 200, 1, 0],
  [137.087, 'S', 0, 1214, 1, 0], [141.642, 'S', 3, 1213, 0, 1], [141.743, 'S', 117, 1213, 1, 1], [144.163, 'S', 119, 1213, 0, 1], [144.264, 'S', 29, 1213, 1, 0],
  [156.944, 'S', 42, 1213, 0, 1], [159.028, 'S', 42, 1213, 1, 1], [165.47, 'S', 48, 1213, 0, 1], [168.683, 'S', 48, 1213, 1, 1], [171.616, 'S', 51, 1213, 0, 1], [196.378, 'S', 51, 1213, 1, 1],
];
const real = head();
for (const [s, ser, ct, dur, pl, an] of EV) { const t = T0 + 1000 + Math.round(s * 1000); real.push({ t, ev: 'msg', client: 1, msg: vid('youtube', ser, ct, dur, pl, an, t) }); }
real.push({ t: T0 + 1000 + 260000, ev: 'end' });
const r1 = sends(run(V1, real)), r2 = sends(run(V2, real));
console.log('=== 실사용 순서 그대로 (전송 수: V1 ' + r1.filter((x) => x.changed).length + ' · V2 ' + r2.filter((x) => x.changed).length + ') ===');
ok('① V1 은 20초에 5번을 넘김 (Discord 가 늦춘 원인 재현)', maxIn20s(r1) > 5, maxIn20s(r1));
ok('① V2 는 어느 20초에도 5번 이하', maxIn20s(r2) <= 5, maxIn20s(r2));
//  ② 이동 = 정지(⚓)+재생 0.1초 간격 → V2 는 그 사이에 정지 카드를 안 보냄
const seekPause = (rs, at) => rs.some((x) => x.t >= at && x.t < at + 300 && x.act && x.act.assets && x.act.assets.small_image === 'pause');
ok('② 이동(142.6초: 정지→재생 0.1초)에 V1 은 정지 카드를 보냄', seekPause(r1, 1000 + 141642));
ok('② V2 는 그 정지 카드를 안 보내고 재생 하나만', !seekPause(r2, 1000 + 141642) && r2.some((x) => x.t >= 1000 + 141743 && x.t < 1000 + 141743 + 600 && x.act && x.act.assets && x.act.assets.small_image === 'play'));
//  ③ 마지막(196.4초 재생 51초) 이 가고, 그 뒤 카드 위치가 51초 근처
const lastEv = 1000 + 196378;
const after = r2.filter((x) => x.t >= lastEv && x.act);
ok('③ 마지막 상태(재생 51초)가 감 — 20초 안에', after.length > 0 && after[0].t - lastEv <= 20000 && Math.abs(pos(after[0].act, after[0].t) - 51) <= 1.5, after[0] && { dt: after[0].t - lastEv, pos: pos(after[0].act, after[0].t) });
ok('③ 정지(171.6초)도 감 (중간 상태로 버려지지 않음 — 다음 소식이 24초 뒤)', r2.some((x) => x.t >= 1000 + 171616 && x.t < lastEv && x.act && x.act.assets && x.act.assets.small_image === 'pause'));
const firstS = r2.find((x) => x.t >= 1000 + 137087 && x.act && /S/.test(JSON.stringify(x.act)));
ok('④ 새 영상(137.1초)은 기다리지 않고 바로', firstS && firstS.t - (1000 + 137087) < 50, firstS && firstS.t - (1000 + 137087));

// ── 연타 (1초마다 정지·재생 30번) — 마지막 상태는 꼭 가고, 창은 안 넘음 ──
const burst = head();
let t = T0 + 1000;
burst.push({ t, ev: 'msg', client: 1, msg: vid('youtube', 'Z', 0, 600, 1, 0, t) });
for (let i = 1; i <= 30; i++) { t += 1000; burst.push({ t, ev: 'msg', client: 1, msg: vid('youtube', 'Z', 10 + i, 600, i % 2, 1, t) }); }
const burstEnd = t;
burst.push({ t: t + 60000, ev: 'end' });
const b2 = sends(run(V2, burst));
const lastB = b2.filter((x) => x.act).pop();
console.log('=== 1초마다 정지·재생 30번 ===');
ok('창 안 넘음 (20초에 5번 이하)', maxIn20s(b2) <= 5, maxIn20s(b2));
ok('마지막 상태(정지)가 끝내 감 · 20초 안에', lastB && lastB.act.assets.small_image === 'pause' && lastB.t - (burstEnd - T0) <= 20500, lastB && { t: lastB.t - (burstEnd - T0), img: lastB.act.assets.small_image });

// ── ⑤ 두 사이트가 번갈아 — 창 하나를 같이 씀 ──
const two = head();
t = T0 + 1000;
for (let i = 0; i < 16; i++) { t += 700; const site = i % 2 ? 'ttfc' : 'youtube'; two.push({ t, ev: 'msg', client: 1, msg: vid(site, 'S' + site, 5 + i, 900, i % 4 < 2, 1, t) }); }
two.push({ t: t + 60000, ev: 'end' });
const w2 = sends(run(V2, two));
console.log('=== 두 사이트 번갈아 ===');
ok('⑤ 두 연결 합쳐서 20초에 5번 이하', maxIn20s(w2) <= 5, maxIn20s(w2));
ok('⑤ 두 사이트 모두 카드가 감', new Set(w2.filter((x) => x.act).map((x) => x.conn)).size === 2);

// ── 같은 내용 재전송은 창을 안 씀 — 재생 3분 (MAX 15초 다시 잡기) 동안 한 번 정지해도 바로 감 ──
const calm = head();
t = T0 + 1000;
calm.push({ t, ev: 'msg', client: 1, msg: vid('youtube', 'Q', 0, 900, 1, 0, t) });
for (let i = 1; i <= 36; i++) { t += 5000; calm.push({ t, ev: 'msg', client: 1, msg: vid('youtube', 'Q', i * 5, 900, 1, 0, t) }); }
t += 1000;
const pauseAt = t;
calm.push({ t, ev: 'msg', client: 1, msg: vid('youtube', 'Q', 36 * 5 + 1, 900, 0, 1, t) });
calm.push({ t: t + 30000, ev: 'end' });
const c2 = sends(run(V2, calm));
const pz = c2.find((x) => x.t >= pauseAt - T0 && x.act && x.act.assets.small_image === 'pause');
console.log('=== 3분 재생 뒤 정지 ===');
ok('정지가 0.35초 모으기만 거치고 감 (재전송이 창을 다 쓰지 않음)', pz && pz.t - (pauseAt - T0) <= 400, pz && pz.t - (pauseAt - T0));

console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
process.exit(fail ? 1 : 0);
