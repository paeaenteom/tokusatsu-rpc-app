// 그림 디스크 캐시 시험 (img_cli.exe + TOKU_IMG_CACHE) — 임시 폴더만 쓴다
//  1) 처음: 받아서 캐시에 남김  2) 캐시 파일을 빨간 그림으로 바꾸면 다음엔 그걸 읽음(= 안 받음)
//  3) 깨진 캐시 → 다시 받아 고침  4) 정리: .tmp·한도 넘는 오래된 것 지움, 이 캐시 이름이 아닌 파일은 안 건드림
//  사용: node tests/img-cache-test.js <img_cli.exe>
'use strict';
const { spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os'); const zlib = require('zlib'); const crypto = require('crypto');
const EXE = path.resolve(process.argv[2] || 'build/img_cli.exe');
const URL = 'https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg';
const DIR = fs.mkdtempSync(path.join(os.tmpdir(), 'imgcache-'));
const FILE = path.join(DIR, crypto.createHash('sha1').update(URL).digest('hex') + '.img');
let fails = 0, n = 0;
const ok = (c, m) => { n++; console.log((c ? '  OK  ' : '  X   ') + m); if (!c) fails++; };
const run = () => {
  const r = spawnSync(EXE, [URL], { encoding: 'utf8', env: Object.assign({}, process.env, { TOKU_IMG_CACHE: DIR }) });
  return (r.stdout || '').trim();
};
function solidPng(rgb) {
  const rows = [];
  for (let y = 0; y < 32; y++) { const r = [0]; for (let x = 0; x < 32; x++) r.push(...rgb, 255); rows.push(Buffer.from(r)); }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(32, 0); ih.writeUInt32BE(32, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
}
const rgbOf = (line) => { const m = /rgb=(\d+),(\d+),(\d+)/.exec(line); return m ? m.slice(1).map(Number) : null; };

const o1 = run();
ok(/^1 ok/.test(o1) && fs.existsSync(FILE), '처음: 받고 캐시 파일 남김 (' + o1 + ')');
const real = rgbOf(o1);
fs.writeFileSync(FILE, solidPng([230, 20, 20]));
const o2 = run();
const red = rgbOf(o2);
ok(red && red[0] > 200 && red[1] < 40 && red[2] < 40, '두 번째: 캐시를 읽음 — 바꿔 둔 빨간 그림이 나옴 (' + o2 + ')');
fs.writeFileSync(FILE, Buffer.from('not an image'));
const o3 = run();
const back = rgbOf(o3);
ok(back && real && back.join() === real.join() && fs.statSync(FILE).size > 100, '깨진 캐시 → 다시 받아 고침 (' + o3 + ')');
//  정리
const old = new Date(Date.now() - 3600e3);
for (let i = 0; i < 410; i++) { const f = path.join(DIR, crypto.createHash('sha1').update('x' + i).digest('hex') + '.img'); fs.writeFileSync(f, 'x'); fs.utimesSync(f, old, old); }
const tmp = path.join(DIR, crypto.createHash('sha1').update('t').digest('hex') + '.tmp1234');
fs.writeFileSync(tmp, 'x');
const keep = path.join(DIR, 'keep-me.txt');
fs.writeFileSync(keep, 'x');
const o4 = run();
const imgs = fs.readdirSync(DIR).filter((f) => /^[0-9a-f]{40}\.img$/.test(f));
ok(imgs.length <= 400, '한도: .img ' + imgs.length + '개 (≤ 400)');
ok(fs.existsSync(FILE) && /^1 ok/.test(o4), '한도 정리 뒤에도 방금 쓴 그림은 남음');
ok(!fs.existsSync(tmp), '남은 .tmp 지움');
ok(fs.existsSync(keep), '캐시 이름이 아닌 파일은 안 건드림');
fs.rmSync(DIR, { recursive: true, force: true });
console.log((fails ? 'X ' : 'OK ') + 'img 캐시 ' + (n - fails) + '/' + n);
process.exit(fails ? 1 : 0);
