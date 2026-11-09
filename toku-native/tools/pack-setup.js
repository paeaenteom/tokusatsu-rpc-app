// 설치 프로그램에 넣을 파일 묶음 만들기 — 앱 exe + 크롬 확장 폴더 → 한 파일 (setup.c 가 풀어 쓴다)
//  모양: "TOKUPAK1" · u32 개수 · [u16 경로 길이 · 경로(UTF-8, / 구분) · u32 크기 · 바이트]…  (모두 little-endian)
//  확장 서명키(*.pem)·디자인 작업 파일(.impeccable)·숨김 파일은 넣지 않는다
//  사용: node tools/pack-setup.js <앱 exe> <확장 폴더> <출력 파일>
'use strict';
const fs = require('fs'); const path = require('path');
const [exe, ext, out] = process.argv.slice(2);
if (!exe || !ext || !out) { console.log('사용: node tools/pack-setup.js <앱 exe> <확장 폴더> <출력 파일>'); process.exit(2); }
const entries = [['TOKU-RPC-beta.exe', fs.readFileSync(exe)]];
(function walk(dir, rel) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    if (e.name.startsWith('.') || /\.pem$/i.test(e.name)) continue;
    const p = path.join(dir, e.name), r = rel ? rel + '/' + e.name : e.name;
    if (e.isDirectory()) walk(p, r);
    else entries.push(['chrome-ext/' + r, fs.readFileSync(p)]);
  }
})(path.resolve(ext), '');
if (!entries.some(([p]) => p === 'chrome-ext/manifest.json')) { console.log('X 확장 폴더에 manifest.json 이 없음'); process.exit(1); }
const parts = [Buffer.from('TOKUPAK1', 'latin1')];
const n = Buffer.alloc(4); n.writeUInt32LE(entries.length); parts.push(n);
for (const [p, b] of entries) {
  const pb = Buffer.from(p, 'utf8');
  const h = Buffer.alloc(2); h.writeUInt16LE(pb.length);
  const s = Buffer.alloc(4); s.writeUInt32LE(b.length);
  parts.push(h, pb, s, b);
}
fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
fs.writeFileSync(out, Buffer.concat(parts));
console.log('묶음 ' + entries.length + '개 · ' + Buffer.concat(parts).length + '바이트 → ' + out);
