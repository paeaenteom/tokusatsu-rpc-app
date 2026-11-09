// 배경 꾸미기 교차 점검 — 진짜 appearance.js 의 sniff · sanitizeStored · validateOptions · cleanName 과 C(bg_cli.exe)
//  사용: node tests/bg-diff.js <bg_cli.exe> [수] [씨앗]
'use strict';
const { spawn } = require('child_process'); const zlib = require('zlib'); const path = require('path');
const ap = require('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/appearance.js');
const EXE = path.resolve(process.argv[2]);
const N = Number(process.argv[3] || 3000);
let seed = Number(process.argv[4] || 1) >>> 0;
const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const pick = (a) => a[Math.floor(rnd() * a.length)];
const int = (n) => Math.floor(rnd() * n);

// ── 진짜 모양의 그림들 ──
let crcT = null;
const crc = (b) => { if (!crcT) { crcT = []; for (let k = 0; k < 256; k++) { let c = k; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crcT[k] = c >>> 0; } } let c = 0xffffffff; for (const v of b) c = crcT[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty, 'latin1'), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cc]); };
function png(w, h, extra) {
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  const parts = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ih)];
  if (extra === 'acTL') parts.push(chunk('acTL', Buffer.alloc(8)));
  if (extra !== 'noidat') parts.push(chunk('IDAT', zlib.deflateSync(Buffer.alloc(Math.min(w * 4 + 1, 400)))));
  if (extra !== 'noend') parts.push(chunk('IEND', Buffer.alloc(0)));
  return Buffer.concat(parts);
}
function jpeg(w, h, opt) {
  const seg = (m, d) => { const l = Buffer.alloc(2); l.writeUInt16BE(d.length + 2); return Buffer.concat([Buffer.from([0xff, m]), l, d]); };
  const sof = Buffer.alloc(9); sof[0] = 8; sof.writeUInt16BE(h, 1); sof.writeUInt16BE(w, 3); sof[5] = 1;
  const parts = [Buffer.from([0xff, 0xd8]), seg(0xe0, Buffer.from('JFIF\0\x01\x01\0\0\x01\0\x01\0\0', 'latin1'))];
  if (opt === 'fill') parts.push(Buffer.from([0xff, 0xff, 0xff]));
  if (opt !== 'nosof') parts.push(seg(pick([0xc0, 0xc2, 0xc1]), sof));
  if (opt === 'dht') parts.push(seg(0xc4, Buffer.alloc(20)));
  parts.push(seg(0xda, Buffer.alloc(10)), Buffer.alloc(30, 0x11));
  if (opt !== 'noeoi') parts.push(Buffer.from([0xff, 0xd9]));
  if (opt === 'trail') parts.push(Buffer.alloc(9, 0x22));
  return Buffer.concat(parts);
}
function webp(kind, w, h) {
  const ck = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32LE(d.length); return Buffer.concat([Buffer.from(ty, 'latin1'), l, d, d.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0)]); };
  let body;
  if (kind === 'lossy') { const d = Buffer.alloc(14); d[3] = 0x9d; d[4] = 1; d[5] = 0x2a; d.writeUInt16LE(w & 0x3fff, 6); d.writeUInt16LE(h & 0x3fff, 8); body = ck('VP8 ', d); }
  else if (kind === 'lossless') { const d = Buffer.alloc(9); d[0] = 0x2f; d.writeUInt32LE(((w - 1) & 0x3fff) | (((h - 1) & 0x3fff) << 14), 1); body = ck('VP8L', d); }
  else { const x = Buffer.alloc(10); x[0] = kind === 'anim' ? 2 : 0; x.writeUIntLE(w - 1, 4, 3); x.writeUIntLE(h - 1, 7, 3); const d = Buffer.alloc(14); d[3] = 0x9d; d[4] = 1; d[5] = 0x2a; body = Buffer.concat([ck('VP8X', x), kind === 'anim' ? ck('ANIM', Buffer.alloc(6)) : Buffer.alloc(0), ck('VP8 ', d)]); }
  const hd = Buffer.alloc(12); hd.write('RIFF', 0, 'latin1'); hd.writeUInt32LE(body.length + 4, 4); hd.write('WEBP', 8, 'latin1');
  return Buffer.concat([hd, body]);
}
const SIZES = [[1, 1], [640, 480], [8192, 4096], [8193, 10], [6001, 6001], [0, 5], [16383, 2], [100, 0]];
function image() {
  const [w, h] = pick(SIZES);
  let b = pick([() => png(w, h, pick([null, null, 'acTL', 'noidat', 'noend'])), () => jpeg(w & 0xffff, h & 0xffff, pick([null, 'fill', 'nosof', 'dht', 'noeoi', 'trail'])),
    () => webp(pick(['lossy', 'lossless', 'x', 'anim']), Math.max(1, w), Math.max(1, h)), () => Buffer.from(Array.from({ length: int(60) }, () => int(256)))])();
  const r = rnd();
  if (r < 0.15) b = b.subarray(0, int(b.length + 1));                    //  잘림
  else if (r < 0.3) { b = Buffer.from(b); for (let k = 0; k < 1 + int(3); k++) b[int(b.length)] = int(256); }   //  바이트 바꿈
  else if (r < 0.35) b = Buffer.concat([b, Buffer.alloc(int(20), int(256))]);
  return b;
}
const junk = () => pick([null, 3, 3.5, -1, '7', true, [], {}, 'cover', 1000, 1001, 400, 99, 90, 91, 20, 21, 0, -0, NaN]);
function stored() {
  if (rnd() < 0.08) return junk();
  const o = {};
  const keys = ['enabled', 'image', 'name', 'width', 'height', 'fit', 'position', 'posX', 'posY', 'zoom', 'opacity', 'dim', 'blur'];
  for (const k of keys) if (rnd() < 0.75) o[k] = rnd() < 0.6 ? good(k) : junk();
  return o;
}
function good(k) {
  switch (k) {
    case 'enabled': return pick([true, false]);
    case 'image': return pick(['bg-0123456789abcdef.png', 'bg-0123456789abcdef.jpg', 'bg-0123456789abcdef.webp', 'bg-0123456789ABCDEF.png', 'tmp-0123456789abcdef.png', 'bg-012.png', '']);
    case 'name': return pick(['photo.png', 'C:\\Users\\x\\a b.jpg', 'dir/sub/p.webp', '\u0001\u0002ab', '  name  ', 'x'.repeat(130), '가'.repeat(121), '\ud83d\ude00'.repeat(70)]);
    case 'width': case 'height': return pick([1, 640, 8192, 8193, 0]);
    case 'fit': return pick(['cover', 'contain', 'fill']);
    case 'position': return pick(['center', 'top', 'bottom', 'left', 'right', 'middle']);
    case 'posX': case 'posY': return int(1100);
    case 'zoom': return 90 + int(320);
    case 'opacity': return int(110);
    case 'dim': return int(100);
    case 'blur': return int(25);
  }
}
function opts() {
  if (rnd() < 0.08) return junk();
  const o = {};
  for (const k of ['enabled', 'fit', 'posX', 'posY', 'zoom', 'opacity', 'dim', 'blur']) if (rnd() < 0.95) o[k] = rnd() < 0.8 ? good(k) : junk();
  return o;
}
function op() {
  const r = rnd();
  if (r < 0.45) return { op: 'sniff', b: image() };
  if (r < 0.75) return { op: 'sanitize', v: stored() };
  if (r < 0.92) return { op: 'validate', v: opts() };
  return { op: 'name', v: good('name') + pick(['', '\\x\\y.png', '/z']) };
}
function jsOf(m) {
  if (m.op === 'sniff') { const s = ap.sniff(m.b); return JSON.stringify(s.error ? { e: s.error } : { e: null, f: s.format, w: s.width, h: s.height }); }
  if (m.op === 'sanitize') { const s = ap.sanitizeStored(m.v); return JSON.stringify({ enabled: s.enabled, image: s.image, name: s.name.toWellFormed(), width: s.width, height: s.height, fit: s.fit, posX: s.posX, posY: s.posY, zoom: s.zoom, opacity: s.opacity, dim: s.dim, blur: s.blur }); }
  if (m.op === 'validate') { const s = ap.validateOptions(m.v); return JSON.stringify(s); }
  return JSON.stringify(ap.cleanName(m.v).toWellFormed());
}

(async () => {
  const c = spawn(EXE, [], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buf = Buffer.alloc(0);
  const lines = [];
  let wake = null;
  c.stdout.on('data', (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(10)) >= 0) { lines.push(buf.subarray(0, i).toString('utf8').replace(/\r$/, '')); buf = buf.subarray(i + 1); } if (wake) wake(); });
  const next = () => new Promise((res) => { if (lines.length) return res(lines.shift()); wake = () => { if (lines.length) { wake = null; res(lines.shift()); } }; });
  let fail = 0;
  const count = {};
  for (let i = 0; i < N; i++) {
    const m = op();
    //  JS 는 C 가 받는 것(JSON 을 거친 값)으로 — NaN·-0·undefined 가 JSON 에서 바뀌는 것까지 같게
    const wire = m.op === 'sniff' ? { op: 'sniff', hex: m.b.toString('hex') } : JSON.parse(JSON.stringify(m));
    const want = jsOf(m.op === 'sniff' ? m : wire);
    c.stdin.write(JSON.stringify(wire) + '\n');
    const got = await next();
    count[m.op] = (count[m.op] || 0) + 1;
    if (got !== want) { fail++; if (fail <= 4) { console.log('X 다름 #' + i + ' ' + m.op + '  ' + (m.op === 'sniff' ? m.b.toString('hex').slice(0, 120) : JSON.stringify(wire).slice(0, 300))); console.log('   JS: ' + want); console.log('   C : ' + got); } }
  }
  c.stdin.end();
  console.log(fail ? '실패 ' + fail + '/' + N : '전부 같음 — ' + N + '건 (' + Object.entries(count).map(([k, v]) => k + ' ' + v).join(' · ') + ')');
  process.exit(fail ? 1 : 0);
})();
