// 웹훅 캐릭터 한꺼번에 (모두 켜기 · 모두 끄기 · 무작위) 시험 — hk_cli.exe 의 charAll
//  임시 폴더·가짜 비밀 파일만 쓴다 (네트워크 X). 사용: node tests/hk-charall-test.js <hk_cli.exe>
'use strict';
const { spawn } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const EXE = path.resolve(process.argv[2] || 'build/hk_cli.exe');
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'hkall-'));
const SEC = path.join(TMP, 'secrets.json');
fs.writeFileSync(SEC, JSON.stringify({ discordAppIds: { ttfc: '123456789012345678' }, other: [1] }));

let fails = 0, oks = 0;
const ok = (c, m) => { if (c) oks++; else { fails++; console.log('X ' + m); } };

function cli(dir, real) {
  const env = Object.assign({}, process.env, { TOKU_NATIVE_TEST_SECRETS: SEC });
  if (real) env.HK_REAL_RNG = '1'; else delete env.HK_REAL_RNG;
  const c = spawn(EXE, [dir], { stdio: ['pipe', 'pipe', 'inherit'], env });
  let buf = '';
  const waits = [];
  c.stdout.on('data', (d) => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); waits.shift()(JSON.parse(l)); } });
  return {
    send: (m) => new Promise((res) => { waits.push(res); c.stdin.write(JSON.stringify(m) + '\n'); }),
    end: () => new Promise((res) => { c.on('exit', res); c.stdin.end(); }),
  };
}
const offs = (o, t) => (o.chars[t] || []).map((c) => c.off);
const rawChars = (o, t) => ((o.folders || {})[t] || {}).characters;

(async () => {
  const dir = fs.mkdtempSync(path.join(TMP, 'set-'));
  let c = cli(dir, false);
  const T = '仮面ライダー 시험';
  let o = await c.send({ op: 'add', t: T, url: 'https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz_-01', name: '채널' });
  ok(o.r === null, '웹훅 추가 ' + o.r);
  o = await c.send({ op: 'charAll', title: T, mode: 1 });
  ok(o.r === 'missing', '캐릭터 0명 → missing (' + o.r + ')');
  const NAMES = ['가', '나', '다', '라', '마', '바'];
  for (const n of NAMES) o = await c.send({ op: 'charAdd', title: T, name: n });
  ok(offs(o, T).length === 6, '캐릭터 6명');
  // 화면에 안 나오는 항목(이름 없는 것·숫자) 섞기 — 건드리지 않아야 한다
  const f = JSON.parse(JSON.stringify(o.folders));
  f[T].characters.push({ id: 'zz', note: 1 }, 7);
  o = await c.send({ op: 'folders', v: f });
  const extra = JSON.stringify(rawChars(o, T).slice(6));

  o = await c.send({ op: 'charAll', title: T, mode: 0 });
  ok(o.r === null && offs(o, T).every((x) => x === true), '모두 끄기 ' + JSON.stringify(offs(o, T)));
  ok(JSON.stringify(rawChars(o, T).slice(6)) === extra, '모두 끄기: 다른 항목 그대로');
  o = await c.send({ op: 'charAll', title: T, mode: 1 });
  ok(o.r === null && offs(o, T).every((x) => x === false), '모두 켜기 ' + JSON.stringify(offs(o, T)));
  ok(rawChars(o, T).slice(0, 6).every((x) => !('off' in x)), '모두 켜기: off 키 지움 (켜기 한 개와 같은 모양)');
  ok(JSON.stringify(rawChars(o, T).slice(6)) === extra, '모두 켜기: 다른 항목 그대로');
  o = await c.send({ op: 'charAll', title: T, mode: 2 });
  ok(o.r === null && offs(o, T).length === 6, '무작위 결과 ' + o.r);
  ok(rawChars(o, T).slice(0, 6).every((x) => x.off === true ? true : !('off' in x)), '무작위: off 는 true 또는 없음');
  ok(JSON.stringify(rawChars(o, T).slice(6)) === extra, '무작위: 다른 항목 그대로');
  const names = (o.chars[T] || []).map((x) => x.name).join('');
  ok(names === NAMES.join(''), '이름·순서 그대로 ' + names);
  const saved = JSON.stringify(offs(o, T));
  // 잘못된 값
  o = await c.send({ op: 'charAll', title: T, mode: 5 });
  ok(o.r === 'missing' && JSON.stringify(offs(o, T)) === saved, 'mode 5 → missing, 안 바뀜');
  o = await c.send({ op: 'charAll', title: '없는 작품', mode: 1 });
  ok(o.r === 'missing', '없는 작품 → missing');
  await c.end();
  // 다시 켜도 저장돼 있다
  c = cli(dir, false);
  o = await c.send({ op: 'charAll', title: T, mode: 9 });
  ok(JSON.stringify(offs(o, T)) === saved, '설정 파일에 저장됨');
  // null 이 섞인 깨진 파일 → badfile, 그대로
  const f2 = JSON.parse(JSON.stringify(o.folders));
  f2[T].characters.push(null);
  o = await c.send({ op: 'folders', v: f2 });
  const before = JSON.stringify(o.folders);
  o = await c.send({ op: 'charAll', title: T, mode: 1 });
  ok(o.r === 'badfile' && JSON.stringify(o.folders) === before, 'null 섞임 → badfile, 안 바뀜 (' + o.r + ')');
  await c.end();

  // 진짜 시스템 난수: 3명으로 300번 → 8가지(모두 켜짐·모두 꺼짐 포함) 다 나와야 한다
  const dir2 = fs.mkdtempSync(path.join(TMP, 'rng-'));
  c = cli(dir2, true);
  await c.send({ op: 'add', t: T, url: 'https://discord.com/api/webhooks/123456789012345678/abcdefghijklmnopqrstuvwxyz_-01', name: '채널' });
  for (const n of ['하나', '둘', '셋']) await c.send({ op: 'charAdd', title: T, name: n });
  const seen = {};
  let onSum = 0;
  for (let i = 0; i < 300; i++) {
    o = await c.send({ op: 'charAll', title: T, mode: 2 });
    const k = offs(o, T).map((x) => (x ? 0 : 1)).join('');
    seen[k] = (seen[k] || 0) + 1;
    onSum += [...k].filter((x) => x === '1').length;
  }
  await c.end();
  const keys = Object.keys(seen).sort();
  ok(keys.length === 8, '무작위 8가지 모두 나옴 ' + JSON.stringify(seen));
  ok(seen['111'] > 0 && seen['000'] > 0, '모두 켜짐·모두 꺼짐도 나옴');
  const rate = onSum / 900;
  ok(rate > 0.42 && rate < 0.58, '켜짐 비율 ' + rate.toFixed(3) + ' (≈0.5)');
  console.log('무작위 분포 (켜짐=1): ' + keys.map((k) => k + ':' + seen[k]).join(' ') + ' · 켜짐 비율 ' + rate.toFixed(3));

  fs.rmSync(TMP, { recursive: true, force: true });
  console.log((fails ? 'X ' : 'OK ') + 'hk-charall ' + oks + '/' + (oks + fails));
  process.exit(fails ? 1 : 0);
})();
