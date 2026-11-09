// 설치 프로그램 점검 — /S /TEST /NOLAUNCH /D=<임시 폴더> 로 설치 → 덮어쓰기 설치 → 제거
//  /TEST: 진짜 앱을 끄지 않고, 제거 항목(TOKURPCBetaTest)·바로 가기(설치 폴더 안 'TOKU RPC Test.lnk') 이름을 바꾸고, 부팅 항목은 안 건드린다
//  사용: node tests/setup-test.js <TOKU-RPC-Setup.exe> <넣은 앱 exe> <확장 폴더>
'use strict';
const { spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os'); const crypto = require('crypto');
const SETUP = path.resolve(process.argv[2]), APP = path.resolve(process.argv[3]), EXT = path.resolve(process.argv[4]);
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(56) + (e !== undefined && !c ? ' ' + JSON.stringify(e).slice(0, 300) : '')); if (!c) fail++; };
const sha = (f) => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const sleep = (ms) => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
const KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TOKURPCBetaTest';
const regVals = () => {
  const r = spawnSync('reg', ['query', KEY], { encoding: 'utf8' });
  if (r.status !== 0) return null;
  const o = {};
  for (const l of r.stdout.split(/\r?\n/)) { const m = /^\s+(\S+)\s+REG_(?:SZ|DWORD)\s+(.*)$/.exec(l); if (m) o[m[1]] = m[2].trim(); }
  return o;
};
const extFiles = (d, rel = '') => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.name.startsWith('.') || /\.pem$/i.test(e.name)) ? [] : e.isDirectory() ? extFiles(path.join(d, e.name), rel + e.name + '/') : [rel + e.name]);

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'toku-setup-'));
const DIR = path.join(TMP, 'TOKU RPC Beta');
const run = (exe, args) => spawnSync(exe, args, { timeout: 60000 });
spawnSync('reg', ['delete', KEY, '/f'], { stdio: 'ignore' });

console.log('=== 설치 ===');
let r = run(SETUP, ['/S', '/TEST', '/NOLAUNCH', '/D=' + DIR]);
ok('끝남 (0)', r.status === 0, r.status);
ok('앱 exe = 넣은 exe', fs.existsSync(path.join(DIR, 'TOKU-RPC-beta.exe')) && sha(path.join(DIR, 'TOKU-RPC-beta.exe')) === sha(APP));
const want = extFiles(EXT).sort();
const got = fs.existsSync(path.join(DIR, 'chrome-ext')) ? extFiles(path.join(DIR, 'chrome-ext')).sort() : [];
ok('확장 파일 ' + want.length + '개 그대로 (숨김·.pem 빼고)', JSON.stringify(got) === JSON.stringify(want) && want.every((f) => sha(path.join(EXT, f)) === sha(path.join(DIR, 'chrome-ext', f))), { want: want.length, got: got.length });
ok('제거 프로그램 = 설치 프로그램 사본', fs.existsSync(path.join(DIR, 'Uninstall TOKU RPC.exe')) && sha(path.join(DIR, 'Uninstall TOKU RPC.exe')) === sha(SETUP));
ok('바로 가기 (.lnk)', fs.existsSync(path.join(DIR, 'TOKU RPC Test.lnk')));
const v = regVals();
ok('제거 항목: 이름·버전·위치·제거 명령', v && v.DisplayName === 'TOKU RPC' && /^\d+\.\d+\.\d+$/.test(v.DisplayVersion) && v.InstallLocation === DIR &&
  v.UninstallString === '"' + path.join(DIR, 'Uninstall TOKU RPC.exe') + '" /uninstall /TEST' && /\/uninstall \/S \/TEST$/.test(v.QuietUninstallString) && v.NoModify === '0x1', v);
ok('남는 .new 임시 파일 없음', !fs.readdirSync(DIR).some((f) => f.endsWith('.new')));

console.log('=== 덮어쓰기 설치 (지난 버전 파일 정리) ===');
fs.writeFileSync(path.join(DIR, 'chrome-ext', 'stale-old.js'), 'old');
r = run(SETUP, ['/S', '/TEST', '/NOLAUNCH', '/D=' + DIR]);
ok('끝남 (0) · 지난 확장 파일은 지워짐', r.status === 0 && !fs.existsSync(path.join(DIR, 'chrome-ext', 'stale-old.js')), r.status);
ok('앱 exe 그대로', sha(path.join(DIR, 'TOKU-RPC-beta.exe')) === sha(APP));

console.log('=== 제거 ===');
r = run(path.join(DIR, 'Uninstall TOKU RPC.exe'), ['/uninstall', '/S', '/TEST']);
ok('끝남 (0)', r.status === 0, r.status);
ok('앱·확장·바로 가기·제거 항목 지워짐', !fs.existsSync(path.join(DIR, 'TOKU-RPC-beta.exe')) && !fs.existsSync(path.join(DIR, 'chrome-ext')) && !fs.existsSync(path.join(DIR, 'TOKU RPC Test.lnk')) && regVals() === null);
let gone = false;
for (let i = 0; i < 40 && !gone; i++) { sleep(250); gone = !fs.existsSync(DIR); }
ok('제거 프로그램·빈 설치 폴더도 잠시 뒤 지워짐', gone, fs.existsSync(DIR) ? fs.readdirSync(DIR) : []);

fs.rmSync(TMP, { recursive: true, force: true });
spawnSync('reg', ['delete', KEY, '/f'], { stdio: 'ignore' });
console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
process.exit(fail ? 1 : 0);
