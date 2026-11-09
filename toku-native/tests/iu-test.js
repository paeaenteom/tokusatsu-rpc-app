// 그림 자르기 점검 — 네 칸(빨·초·파·노) 200×100 그림을 만들어 여러 자리를 잘라 보고, 나온 PNG 를 직접 풀어 색을 확인
//  사용: node tests/iu-test.js <iu_cli.exe>
'use strict';
const { spawnSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os'); const zlib = require('zlib');
const EXE = path.resolve(process.argv[2]);
let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l + (c ? '' : ' ' + JSON.stringify(e))); if (!c) fail++; };
const COL = [[220, 30, 30], [30, 200, 60], [40, 70, 230], [240, 210, 30]];   // 왼위·오위·왼아래·오아래
function makePng(w, h) {
  const rows = [];
  for (let y = 0; y < h; y++) { const r = [0]; for (let x = 0; x < w; x++) { const q = (y < h / 2 ? 0 : 2) + (x < w / 2 ? 0 : 1); r.push(...COL[q], 255); } rows.push(Buffer.from(r)); }
  let t = null;
  const crc = (b) => { if (!t) { t = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = t[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
}
//  PNG 풀기 (8비트 RGBA·RGB, 필터 0~4)
function decodePng(buf) {
  let off = 8, w = 0, h = 0, ct = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off), ty = buf.toString('latin1', off + 4, off + 8), d = buf.slice(off + 8, off + 8 + len);
    if (ty === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; if (d[8] !== 8) throw new Error('depth ' + d[8]); }
    if (ty === 'IDAT') idat.push(d);
    off += 12 + len;
  }
  const bpp = ct === 6 ? 4 : ct === 2 ? 3 : 0;
  if (!bpp) throw new Error('color type ' + ct);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * bpp);
  const stride = w * bpp;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, b = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let p;
      if (f === 0) p = 0; else if (f === 1) p = a; else if (f === 2) p = b; else if (f === 3) p = (a + b) >> 1;
      else { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c); p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      out[y * stride + x] = (v + p) & 255;
    }
  }
  return { w, h, bpp, px: out };
}
const near = (p, c) => Math.abs(p[0] - c[0]) < 12 && Math.abs(p[1] - c[1]) < 12 && Math.abs(p[2] - c[2]) < 12;

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'iutest-'));
const src = path.join(tmp, 'q.png');
fs.writeFileSync(src, makePng(200, 100));
function crop(x, y, s, nn) {
  const out = path.join(tmp, 'o.png');
  const r = spawnSync(EXE, [src, out, String(x), String(y), String(s), String(nn)], { encoding: 'utf8' });
  if (r.status !== 0) return { err: r.status, out: r.stdout };
  const d = decodePng(fs.readFileSync(out));
  const at = (fx, fy) => { const i = (Math.floor(fy * d.h) * d.w + Math.floor(fx * d.w)) * d.bpp; return [d.px[i], d.px[i + 1], d.px[i + 2]]; };
  return { d, at, stdout: r.stdout };
}
let r = crop(50, 0, 100, 64);
ok('가운데 정사각 (아이콘 규칙: 가로 200 · 세로 100 → x=50, 크기 100) → 64×64', r.d && r.d.w === 64 && r.d.h === 64, r);
ok('  네 칸 색이 제자리 (왼위 빨 · 오위 초 · 왼아래 파 · 오아래 노)', r.at && near(r.at(0.1, 0.1), COL[0]) && near(r.at(0.9, 0.1), COL[1]) && near(r.at(0.1, 0.9), COL[2]) && near(r.at(0.9, 0.9), COL[3]), r.at && [r.at(0.1, 0.1), r.at(0.9, 0.1), r.at(0.1, 0.9), r.at(0.9, 0.9)]);
ok('  원래 크기 200×100 을 읽음', r.stdout && /orig 200 100/.test(r.stdout), r.stdout);
r = crop(0, 0, 50, 256);
ok('왼위 50×50 → 256 (프사 크기) · 전부 빨강', r.d && r.d.w === 256 && near(r.at(0.5, 0.5), COL[0]) && near(r.at(0.95, 0.95), COL[0]), r.at && r.at(0.95, 0.95));
r = crop(150, 50, 50, 32);
ok('오아래 50×50 → 전부 노랑', r.d && near(r.at(0.5, 0.5), COL[3]) && near(r.at(0.05, 0.05), COL[3]), r.at && r.at(0.05, 0.05));
r = crop(-30, 70, 400, 16);
ok('그림 밖으로 넘친 자리는 안으로 맞춤 (실패하지 않음)', r.d && r.d.w === 16, r);
fs.rmSync(tmp, { recursive: true, force: true });
console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
process.exit(fail ? 1 : 0);
