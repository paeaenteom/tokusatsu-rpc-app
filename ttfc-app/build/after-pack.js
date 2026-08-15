// electron-builder 가 앱을 풀어놓은 직후(afterPack) 호출된다.
//
//  왜 필요한가
//  ----------
//  electron-builder 는 원래 스스로 exe 에 아이콘을 박는다. 그런데 이 환경에서는
//  그 단계가 코드서명 도구(winCodeSign) 압축 해제에서 심볼릭 링크 권한 때문에
//  실패한다. 그러면 exe 는 Electron 기본 아이콘을 그대로 들고 나간다.
//
//  예전엔 tools\set-exe-icon.ps1 을 릴리스 스크립트에서 따로 불러 메웠는데,
//  `electron-builder --win dir` 처럼 릴리스 스크립트를 거치지 않는 빌드에서는
//  그 단계가 통째로 빠져 아이콘 없는 exe 가 설치되곤 했다 (2026-08-15 실제 발생).
//  → 빌드 방식과 무관하게 항상 적용되도록 여기로 옮긴다.
//
//  rcedit 가 없으면 경고만 하고 빌드는 그대로 진행한다.
//  (창·트레이 아이콘은 코드에서 직접 지정하므로 앱 동작에는 영향이 없다)

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

function findRcedit() {
  const cache = path.join(process.env.LOCALAPPDATA || '', 'electron-builder', 'Cache');
  if (!fs.existsSync(cache)) return '';
  const stack = [cache];
  while (stack.length) {
    const dir = stack.pop();
    let entries = [];
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (e) { continue; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) stack.push(p);
      else if (e.name.toLowerCase() === 'rcedit-x64.exe') return p;
    }
  }
  return '';
}

// icon.ico 안의 가장 큰 이미지 조각이 exe 안에 실제로 들어갔는지 확인한다.
// 조용히 실패하면 또 기본 아이콘으로 나가므로, 눈으로 볼 수 있게 검증까지 한다.
function iconEmbedded(exePath, icoPath) {
  try {
    const ico = fs.readFileSync(icoPath);
    const count = ico.readUInt16LE(4);
    let best = { size: 0, off: 0 };
    for (let i = 0; i < count; i++) {
      const e = 6 + i * 16;
      const size = ico.readUInt32LE(e + 8), off = ico.readUInt32LE(e + 12);
      if (size > best.size) best = { size, off };
    }
    const needle = ico.subarray(best.off + 64, best.off + 64 + 48);
    return fs.readFileSync(exePath).indexOf(needle) >= 0;
  } catch (e) {
    return null;   // 확인 불가 — 실패로 치지는 않는다
  }
}

exports.default = async function afterPack(context) {
  if (context.electronPlatformName !== 'win32') return;

  const projectDir = context.packager.info.projectDir;
  const exeName = context.packager.appInfo.productFilename + '.exe';
  const exePath = path.join(context.appOutDir, exeName);
  const icoPath = path.join(projectDir, 'assets', 'icon.ico');

  if (!fs.existsSync(exePath)) { console.log('  [icon] exe 없음, 건너뜀: ' + exePath); return; }
  if (!fs.existsSync(icoPath)) { console.log('  [icon] icon.ico 없음, 건너뜀'); return; }

  const rc = findRcedit();
  if (!rc) {
    console.log('  [icon] rcedit 를 못 찾음 — exe 아이콘은 Electron 기본값으로 남습니다');
    return;
  }

  try {
    execFileSync(rc, [exePath, '--set-icon', icoPath], { stdio: 'pipe' });
  } catch (e) {
    console.log('  [icon] 삽입 실패: ' + e.message);
    return;
  }

  const okEmbed = iconEmbedded(exePath, icoPath);
  console.log('  [icon] ' + (okEmbed === false ? '★ 넣었는데 확인 실패' : 'exe 아이콘 적용 완료'));

  // ── 버전 정보 ──
  //  electron-builder 가 이걸 못 박아서 앱 exe 가 Electron 기본값을 그대로 들고 있었다
  //  (ProductName "Electron" / CompanyName "GitHub, Inc.").
  //  자기 정체를 안 밝히는 실행 파일은 백신 머신러닝 판정에 불리하고, 애초에 사실도 아니다.
  const pkg = require(path.join(projectDir, 'package.json'));
  const numVer = (String(pkg.version).match(/^\d+(\.\d+){0,3}/) || ['0.0.0'])[0]
    .split('.').concat(['0', '0', '0']).slice(0, 4).join('.');
  const strings = [
    'ProductName', 'TOKU RPC',
    'FileDescription', 'TOKU RPC',
    'CompanyName', 'paeaenteom',
    'LegalCopyright', 'MIT License - https://github.com/paeaenteom/tokusatsu-rpc-app',
    'OriginalFilename', exeName,
  ];
  try {
    const args = [exePath, '--set-file-version', numVer, '--set-product-version', numVer];
    for (let i = 0; i < strings.length; i += 2) args.push('--set-version-string', strings[i], strings[i + 1]);
    execFileSync(rc, args, { stdio: 'pipe' });
    console.log('  [icon] 버전 정보 적용: TOKU RPC ' + numVer);
  } catch (e) {
    console.log('  [icon] 버전 정보 적용 실패: ' + e.message);
  }
};
