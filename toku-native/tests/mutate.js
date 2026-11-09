// 차이 검사가 진짜로 잡아내는지 — rpc.c 에 작은 버그를 하나씩 심은 사본을 빌드해 core-diff 가 '다름' 을 내는지 본다
//  (원본 rpc.c 는 건드리지 않는다. build/mut/ 안에서만)
'use strict';
const fs = require('fs'); const { spawnSync } = require('child_process'); const path = require('path');
const ROOT = path.join(__dirname, '..');
const SRC = fs.readFileSync(path.join(ROOT, 'src/core/rpc.c'), 'utf8');
const muts = [
  ['128자 → 127자 자르기', 'utf8_clamp(&b, s, strlen(s), 128)', 'utf8_clamp(&b, s, strlen(s), 127)'],
  ['토큰 회복 4000 → 4001ms', '#define RPC_REFILL_MS 4000', '#define RPC_REFILL_MS 4001'],
  ['2글자 보정 구분자 · → -', 'sb_puts(&r, " \\xC2\\xB7 ");', 'sb_puts(&r, " - ");'],
  ['영상→둘러보기 30초 → 29초', '#define WATCH_ENDS_BROWSING_MS 30000', '#define WATCH_ENDS_BROWSING_MS 29000'],
  ['어긋남 판정 > → >=', 'if (off > tuning_of(p->level)->drift)', 'if (off >= tuning_of(p->level)->drift)'],
  ['재연결 간격 최대 2분 → 3분', 'fmin(wait * 2, 120000)', 'fmin(wait * 2, 180000)'],
  ['버튼 라벨 빠뜨림', 'if (jstrict_false(sget(p, "showButtons"))) return;', 'if (1) return;'],
];
fs.mkdirSync(path.join(ROOT, 'build/mut'), { recursive: true });
let caught = 0;
for (const [label, a, b] of muts) {
  if (!SRC.includes(a)) { console.log('  X   바꿀 자리를 못 찾음: ' + label); continue; }
  const file = path.join(ROOT, 'build/mut/rpc.c');
  //  #include 경로가 맞게 원래 자리 기준으로 고친다
  fs.writeFileSync(file, SRC.replace(a, b).replace(/#include "\.\.\/base\//g, '#include "../../src/base/').replace(/#include "(js|url|rpc|timers)\.h"/g, '#include "../../src/core/$1.h"'));
  const exe = path.join(ROOT, 'build/mut/core_cli.exe');
  const cc = spawnSync('gcc', ['-std=c11', '-O2', '-w', '-o', exe, 'tests/core_cli.c', file, 'src/core/js.c', 'src/core/timers.c', 'src/core/url.c', 'src/base/sbuf.c', 'src/base/json.c', 'src/base/utf.c'], { cwd: ROOT });
  if (cc.status !== 0) { console.log('  X   빌드 실패: ' + label + ' ' + cc.stderr); continue; }
  const r = spawnSync('node', ['tests/core-diff.js', exe, '40', '1'], { cwd: ROOT, maxBuffer: 1 << 26 });
  const found = r.status === 1;
  if (found) caught++;
  console.log('  ' + (found ? 'OK  ' : 'X   ') + ('심은 버그: ' + label).padEnd(40) + (found ? '→ 잡음' : '→ 못 잡음!'));
}
console.log(caught === muts.length ? '심은 버그 ' + muts.length + '개 모두 잡음' : '못 잡은 것 ' + (muts.length - caught) + '개');
process.exit(caught === muts.length ? 0 : 1);
