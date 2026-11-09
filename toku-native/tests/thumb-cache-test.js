// 썸네일 캐시 저장·다시 읽기 왕복 점검 (시험 모드 — 포트 17696, 진짜 알파·기존 앱 파일은 읽기만)
//  1) 시험 폴더 캐시를 지우고 켜면 → 지금 앱 thumb-cache.json 을 읽고, 시험 훅으로 바로 시험 폴더에 쓴다
//  2) 쓴 파일 = 지금 앱 파일에서 만료 안 된 것 (순서·값까지 같아야)
//  3) 시험 파일에서 하나 빼고 다시 켜면 → 시험 파일을 읽어야 한다 (복원 건수 하나 적음)
//  사용: node tests/thumb-cache-test.js <exe>
'use strict';
const { spawn, spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path');
const EXE = path.resolve(process.argv[2]);
const PORT = 17696;
const OLD = path.join(process.env.APPDATA, 'toku-rpc', 'thumb-cache.json');
const DIR = path.join(process.env.APPDATA, 'toku-rpc-native-test');
const NEW = path.join(DIR, 'thumb-cache.json');
const LOG = path.join(DIR, 'logs', 'main.log');
let fail = 0;
const ok = (l, c, e) => { console.log('  ' + (c ? 'OK  ' : 'X   ') + l + (e !== undefined ? ' ' + JSON.stringify(e).slice(0, 160) : '')); if (!c) fail++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const env = (extra) => Object.assign({}, process.env, { TOKU_NATIVE_TEST_PORT: String(PORT), TOKU_NATIVE_TEST_PIPE: 'toku-thumb-test-' + process.pid + '-' }, extra);

async function run(extra) {
  const start = fs.existsSync(LOG) ? fs.statSync(LOG).size : 0;
  const app = spawn(EXE, [], { env: env(extra), stdio: 'ignore' });
  let line = '';
  for (let t = 0; t < 100 && !line; t++) {
    await wait(100);
    if (!fs.existsSync(LOG)) continue;
    const s = fs.readFileSync(LOG).slice(start).toString('utf8');
    const m = s.match(/썸네일 캐시 복원: (\d+)건/);
    if (m) line = m[1];
  }
  await wait(300);
  spawnSync(EXE, ['--quit'], { env: env({}) });
  for (let t = 0; t < 100 && app.exitCode === null; t++) await wait(100);
  return line === '' ? -1 : Number(line);
}

(async () => {
  if (!fs.existsSync(OLD)) { console.log('지금 앱 thumb-cache.json 없음 — 건너뜀'); process.exit(0); }
  const oldStat = fs.statSync(OLD).mtimeMs;
  const now = Date.now();
  const oldRaw = JSON.parse(fs.readFileSync(OLD, 'utf8'));
  const want = Object.entries(oldRaw).filter(([, e]) => e && e.url && e.at && now - e.at <= (e.keepMs || 2 * 3600 * 1000)).slice(-300);
  if (fs.existsSync(NEW)) fs.unlinkSync(NEW);   // 시험 폴더 파일만 지운다

  const n1 = await run({ TOKU_NATIVE_TEST_SAVE_HOSTED: '1' });
  ok('처음: 지금 앱 캐시에서 복원', n1 === want.length, [n1, want.length]);
  ok('시험 폴더에 thumb-cache.json 씀', fs.existsSync(NEW));
  const got = fs.existsSync(NEW) ? Object.entries(JSON.parse(fs.readFileSync(NEW, 'utf8'))) : [];
  const same = got.length === want.length && got.every(([k, e], i) => k === want[i][0] && e.url === want[i][1].url && e.at === want[i][1].at && e.keepMs === (want[i][1].keepMs || 0));
  ok('쓴 내용 = 만료 안 된 항목 (순서·주소·시각·보관 기간)', same, got.length);

  //  하나 빼고 다시 켜기 → 시험 파일을 읽었는지
  if (got.length) {
    const cut = Object.fromEntries(got.slice(1));
    fs.writeFileSync(NEW, JSON.stringify(cut));
    const n2 = await run({});
    ok('다음부터: 알파 파일에서 복원 (하나 뺀 수)', n2 === got.length - 1, [n2, got.length - 1]);
  }
  ok('지금 앱 thumb-cache.json 은 안 건드림', fs.statSync(OLD).mtimeMs === oldStat);
  console.log(fail ? '실패 ' + fail : '전부 통과');
  process.exit(fail ? 1 : 0);
})();
