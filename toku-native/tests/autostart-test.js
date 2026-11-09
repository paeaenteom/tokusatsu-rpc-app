// 자동 시작 등록 점검 — 시험용 이름("TOKU RPC Alpha TEST …")으로 만든 autostart_cli.exe 로 등록→확인→해제
//  진짜 알파·지금 앱 부팅 항목은 건드리지 않는다
'use strict';
const { spawnSync } = require('child_process'); const path = require('path');
const EXE = path.resolve(process.argv[2]);
const steps = ['installed', 'task-off', 'task-exists', 'task-on', 'task-exists', 'task-on', 'task-off', 'task-exists', 'task-off',
  'run-off', 'run-exists', 'run-on', 'run-exists', 'run-on', 'run-off', 'run-exists', 'run-off'];
const want = [0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1];
const r = spawnSync(EXE, steps, { encoding: 'utf8' });
const got = (r.stdout || '').trim().split(/\r?\n/).map((l) => Number(l.split(' ')[1]));
let fail = 0;
steps.forEach((s, i) => { const ok = got[i] === want[i]; if (!ok) fail++; console.log('  ' + (ok ? 'OK  ' : 'X   ') + s.padEnd(12) + got[i] + (ok ? '' : ' (기대 ' + want[i] + ')')); });
console.log(fail ? '실패 ' + fail : '전부 통과 (' + steps.length + '건, 시험 항목은 다 지움)');
process.exit(fail ? 1 : 0);
