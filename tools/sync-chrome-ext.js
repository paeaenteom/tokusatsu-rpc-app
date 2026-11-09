// ============================================================
//  확장 수정 → 크롬이 실제로 쓰는 사본에 바로 반영 (2026-11-07 유빈 지시: 앞으로 확장은 자동으로 크롬 쪽까지)
//
//  유빈은 rpc-chrome-ext.zip 을 chrome://extensions 에 끌어 넣어 설치한다 → 크롬은 그 압축을
//  User Data\<프로필>\UnpackedExtensions\rpc-chrome-ext_… 에 풀어 거기서 불러온다 (작업 폴더가 아니다).
//  그래서 확장을 고친 뒤 이 스크립트로 그 사본을 작업 폴더와 똑같이 맞춘다. 파일만 바꾸면
//  확장의 지문(version-print.js)이 달라져 팝업에 '새 버전 적용' 이 뜨고, 열린 사이트 탭이 없으면 스스로 적용한다.
//
//  하는 일: ① Secure Preferences 에서 이 확장(ID 고정)의 경로 항목만 읽는다 (다른 값은 읽거나 출력하지 않는다)
//          ② 그 경로가 UnpackedExtensions 안일 때만 진행 (다른 곳이면 멈추고 알린다)
//          ③ 사본을 .ext-backup\<시각> 에 백업 (최근 5개만 남김) ④ 바뀐 파일만 덮어쓰고, 작업 폴더에 없는 파일은 지운다
//          ⑤ 모든 파일 해시 비교 ⑥ rpc-chrome-ext.zip 다시 만들기 (manifest 가 최상위)
//  쓰는 법:  node tools\sync-chrome-ext.js          (반영 + zip)
//            node tools\sync-chrome-ext.js --dry    (무엇이 바뀌는지만)
//            node tools\sync-chrome-ext.js --no-zip
//  크롬·확장을 다시 불러오거나 창을 건드리지 않는다. 키 파일(.pem)·점(.)으로 시작하는 파일은 옮기지 않는다.
// ============================================================
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');

const EXT_ID = 'dciaobllfdcegjcdmimclgglapnhggjm';
const ROOT = path.resolve(__dirname, '..');
const SRC = path.join(ROOT, 'ttfc-chrome-ext');
const ZIP = path.join(ROOT, 'rpc-chrome-ext.zip');
const BACKUP = path.join(ROOT, '.ext-backup');
const USER_DATA = path.join(process.env.LOCALAPPDATA || '', 'Google', 'Chrome', 'User Data');
const DRY = process.argv.includes('--dry');
const NO_ZIP = process.argv.includes('--no-zip');

const sha = (f) => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const skip = (name) => name.startsWith('.') || name.endsWith('.pem');
function listFiles(dir, base = dir, keepUnderscore = false) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (skip(e.name)) continue;
    //  크롬이 사본 안에 만드는 '_metadata' 같은 폴더는 건드리지 않는다
    if (!keepUnderscore && e.name.startsWith('_')) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listFiles(p, base, keepUnderscore));
    else if (e.isFile()) out.push(path.relative(base, p));
  }
  return out.sort();
}

//  ① 크롬이 불러오는 경로 — 프로필마다 이 확장 항목의 path 만 본다
function findInstalled() {
  const found = [];
  if (!fs.existsSync(USER_DATA)) return found;
  for (const prof of fs.readdirSync(USER_DATA)) {
    if (!/^(?:Default|Profile \d+)$/.test(prof)) continue;
    const sp = path.join(USER_DATA, prof, 'Secure Preferences');
    if (!fs.existsSync(sp)) continue;
    let p = '';
    try {
      const j = JSON.parse(fs.readFileSync(sp, 'utf8'));
      const s = j && j.extensions && j.extensions.settings && j.extensions.settings[EXT_ID];
      p = s && typeof s.path === 'string' ? s.path : '';
    } catch (e) { continue; }
    if (p) found.push({ prof, path: path.isAbsolute(p) ? p : path.join(USER_DATA, prof, 'Extensions', EXT_ID, p) });
  }
  return found;
}

function main() {
  const inst = findInstalled();
  if (!inst.length) { console.log('크롬에 TOKU RPC 확장이 설치돼 있지 않습니다 — zip 을 한 번 끌어 넣어 주세요'); process.exit(2); }
  const srcFiles = listFiles(SRC);
  if (!srcFiles.includes('manifest.json')) throw new Error('작업 폴더에 manifest.json 이 없습니다');
  let bad = 0;
  for (const it of inst) {
    const dest = path.resolve(it.path);
    const allowed = path.join(USER_DATA, it.prof, 'UnpackedExtensions') + path.sep;
    console.log('[' + it.prof + '] 크롬이 쓰는 사본: ' + dest);
    if (path.resolve(dest) === path.resolve(SRC)) { console.log('  작업 폴더를 직접 불러오고 있음 — 따로 맞출 것 없음'); continue; }
    if (!dest.startsWith(allowed) || !fs.existsSync(path.join(dest, 'manifest.json'))) {
      console.log('  ⚠ zip 으로 넣은 사본(UnpackedExtensions)이 아닙니다 — 건드리지 않습니다'); bad++; continue;
    }
    const destFiles = listFiles(dest);
    const changed = srcFiles.filter((f) => !fs.existsSync(path.join(dest, f)) || sha(path.join(SRC, f)) !== sha(path.join(dest, f)));
    const extra = destFiles.filter((f) => !srcFiles.includes(f));
    console.log('  바뀔 파일 ' + changed.length + '개' + (changed.length ? ': ' + changed.join(', ') : '') +
      (extra.length ? ' / 지울 파일 ' + extra.length + '개: ' + extra.join(', ') : ''));
    if (DRY || (!changed.length && !extra.length)) continue;
    //  ③ 백업 (사본 전체, 최근 5개)
    const d = new Date();
    const p2 = (n) => String(n).padStart(2, '0');
    const stamp = d.getFullYear() + p2(d.getMonth() + 1) + p2(d.getDate()) + '-' + p2(d.getHours()) + p2(d.getMinutes()) + p2(d.getSeconds());   // 이 PC 시각
    const bdir = path.join(BACKUP, stamp + '-' + it.prof.replace(/\s+/g, ''));
    for (const f of listFiles(dest, dest, true)) {
      fs.mkdirSync(path.dirname(path.join(bdir, f)), { recursive: true });
      fs.copyFileSync(path.join(dest, f), path.join(bdir, f));
    }
    const olds = fs.readdirSync(BACKUP).sort();
    for (const o of olds.slice(0, Math.max(0, olds.length - 5))) fs.rmSync(path.join(BACKUP, o), { recursive: true, force: true });
    console.log('  백업: ' + bdir);
    //  ④ 덮어쓰기·지우기
    for (const f of changed) {
      fs.mkdirSync(path.dirname(path.join(dest, f)), { recursive: true });
      fs.copyFileSync(path.join(SRC, f), path.join(dest, f));
    }
    for (const f of extra) fs.rmSync(path.join(dest, f), { force: true });
    //  ⑤ 확인
    const after = listFiles(dest);
    const diff = srcFiles.filter((f) => !after.includes(f) || sha(path.join(SRC, f)) !== sha(path.join(dest, f)));
    if (diff.length || after.length !== srcFiles.length) { console.log('  ✗ 맞추기 실패: ' + diff.join(', ')); bad++; }
    else console.log('  ✓ ' + srcFiles.length + '개 파일 모두 작업 폴더와 같음 → 팝업에 \'새 버전 적용\' (사이트 탭이 없으면 몇 분 안에 스스로 적용)');
  }
  //  ⑥ zip (다시 설치할 때·다른 PC 용)
  if (!DRY && !NO_ZIP) {
    const top = fs.readdirSync(SRC).filter((n) => !skip(n));
    execFileSync('C:\\Windows\\System32\\tar.exe', ['-a', '-c', '-f', ZIP, ...top], { cwd: SRC });
    console.log('zip 다시 만듦: ' + ZIP + ' (' + top.length + '개 항목)');
  }
  process.exit(bad ? 1 : 0);
}
main();
