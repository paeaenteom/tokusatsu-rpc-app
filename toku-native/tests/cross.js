// C 기반 코드(vec.exe) 와 JS 결과를 같은 입력으로 비교한다 — 지금 앱(JS)과 똑같이 동작하는지
//  node tests\cross.js build\vec.exe
'use strict';
const { spawnSync } = require('child_process');
const crypto = require('crypto');
const EXE = process.argv[2];
let fail = 0, total = 0;
const b64 = (s) => Buffer.from(s).toString('base64');
const unb64 = (s) => Buffer.from(s, 'base64');
function run(mode, lines) {
  const r = spawnSync(EXE, [mode], { input: lines.join('\n') + '\n', maxBuffer: 1 << 28 });
  if (r.status !== 0) throw new Error(mode + ' exit ' + r.status + ' ' + r.stderr);
  return r.stdout.toString().split(/\r?\n/).slice(0, lines.length);   // Windows 콘솔은 줄 끝에 \r 을 붙인다
}
function report(label, bad, n, sample) {
  total += n;
  console.log('  ' + (bad ? 'X   ' : 'OK  ') + label.padEnd(52) + (bad ? bad + '/' + n + ' 다름 ' + JSON.stringify(sample).slice(0, 300) : n + '건 같음'));
  fail += bad;
}
//  재현 가능한 난수
let seed = 20261107;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const ri = (n) => Math.floor(rnd() * n);
const CH = ['a', 'Z', '0', ' ', '"', '\\', '/', '\n', '\t', '\u0001', '\u001f', '가', '響', 'é', '…', '\u2028', '😀', '🎬', '\uffff', '\u0000'];
const rstr = (n) => { let s = ''; for (let i = 0; i < n; i++) s += CH[ri(CH.length)]; return s; };
function rval(d) {
  const k = ri(d > 3 ? 4 : 7);
  if (k === 0) return rstr(ri(12));
  if (k === 1) { const c = [0, -0, 1, -1, 0.1, 1.5, 1e21, 1e-7, 123456.789, 2 ** 53, 2 ** 53 + 2, 1e300, 5e-324, -0.000001, 1234.5, rnd() * 1e6, (rnd() - 0.5) * 1e-9, Math.round(rnd() * 1e12) / 1000]; return c[ri(c.length)]; }
  if (k === 2) return [true, false, null][ri(3)];
  if (k === 3) return ri(2) ? 'x' : 42;
  if (k === 4 || k === 5) { const o = {}; const n = ri(5); for (let i = 0; i < n; i++) o[rstr(ri(6))] = rval(d + 1); return o; }
  const a = []; const n = ri(5); for (let i = 0; i < n; i++) a.push(rval(d + 1)); return a;
}

console.log('=== JSON (C vs JSON.parse → JSON.stringify) ===');
{
  const cases = [];
  //  손으로 고른 정상 입력
  const fixed = ['{}', '[]', '0', '-0', '"x"', 'true', 'null', ' {"a" : [1, 2.5e3, -0.0, "\\u00e9\\ud83d\\ude00"] } ',
    '{"type":"VIDEO","seriesName":"仮面ライダー響鬼","episodeNumber":"七之巻","currentTime":1334.25,"isPlaying":false}',
    '1e21', '1e20', '1.5e20', '0.000001', '0.0000001', '123456789012345678901234567890', '1e400', '-1e400', '4.35', '0.1', '100', '1E2', '1e-2',
    '"\\"\\\\\\/\\b\\f\\n\\r\\t"', '"\\u0000"', '{"a":1,"a":2}', '[[[[[[[[[[1]]]]]]]]]]', '"\u2028\u2029"', '9007199254740993', '-9007199254740992', '5e-324'];
  for (const f of fixed) cases.push(f);
  for (let i = 0; i < 4000; i++) cases.push(JSON.stringify(rval(0)));
  const got = run('json', cases.map(b64));
  let bad = 0, sample = null;
  cases.forEach((c, i) => {
    const want = 'OK ' + b64(JSON.stringify(JSON.parse(c)));
    if (got[i] !== want) { bad++; if (!sample) sample = { in: c, want: JSON.stringify(JSON.parse(c)), got: got[i].startsWith('OK ') ? unb64(got[i].slice(3)).toString() : got[i] }; }
  });
  report('정상 입력 ' + cases.length + '건: 다시 쓴 결과가 JS 와 같음', bad, cases.length, sample);
}
{
  //  JS 도 거절하는 깨진 입력 → C 도 거절해야
  const bad = ['', ' ', '{', '}', '[1,]', '{"a":1,}', "{'a':1}", 'NaN', 'Infinity', '01', '+1', '.5', '1.', '1e', '-', '"a', '"\\x"', '"\\u12"',
    '"a\tb"', '"a\nb"', 'tru', 'nul', '{} x', '[1] [2]', '{"a"}', '{"a":}', '{1:2}', '[', '"\\', '--1', '1.2.3', '{"a":1 "b":2}', '//x\n1', '\u00a01'];
  const got = run('json', bad.map(b64));
  const wrong = bad.filter((c, i) => { let jsOk = true; try { JSON.parse(c); } catch (e) { jsOk = false; } return jsOk || got[i] !== 'ERR'; });
  report('깨진 입력 ' + bad.length + '건: JS·C 모두 거절', wrong.length, bad.length, wrong);
}
{
  //  C 만의 규칙 (일부러 JS 와 다르게 한 것)
  const deep = (n) => '['.repeat(n) + ']'.repeat(n);
  const cases = [
    [deep(64), 'OK', '깊이 64 는 받음'],
    [deep(65), 'ERR', '깊이 65 는 거절 (밖에서 온 입력이 스택을 못 넘게)'],
    ['"\\ud800"', 'OK ' + b64(JSON.stringify('�')), '짝 없는 서로게이트 → U+FFFD'],
    ['"\\udc00x"', 'OK ' + b64(JSON.stringify('�x')), '짝 없는 아래쪽 서로게이트 → U+FFFD'],
    ['"\\ud800\\u0041"', 'OK ' + b64(JSON.stringify('�A')), '서로게이트 뒤 일반 글자는 따로'],
  ];
  const rawBad = [Buffer.from([0x22, 0xff, 0x22]), Buffer.from([0x22, 0xc0, 0xaf, 0x22]), Buffer.from([0x22, 0xed, 0xa0, 0x80, 0x22]), Buffer.from([0x22, 0xe3, 0x81, 0x22]), Buffer.from([0xef, 0xbb, 0xbf, 0x7b, 0x7d])];
  const got = run('json', cases.map((c) => b64(c[0])).concat(rawBad.map((b) => b.toString('base64'))));
  let badN = 0; const s = [];
  cases.forEach((c, i) => { const ok = c[1] === 'OK' ? got[i].startsWith('OK ') : got[i] === c[1]; if (!ok) { badN++; s.push(c[2] + ' → ' + got[i]); } });
  rawBad.forEach((b, j) => { if (got[cases.length + j] !== 'ERR') { badN++; s.push('깨진 UTF-8/BOM ' + b.toString('hex') + ' → ' + got[cases.length + j]); } });
  report('C 규칙 (깊이 제한·서로게이트·깨진 UTF-8·BOM 거절)', badN, cases.length + rawBad.length, s);
}

console.log('=== 글자 수 자르기 (C vs discord-rpc.js clamp) ===');
{
  function clamp(str, max = 128) {
    if (!str || str.length <= max) return str;
    let cut = str.slice(0, max - 1);
    if (/[\uD800-\uDBFF]$/.test(cut)) cut = cut.slice(0, -1);
    return cut + '…';
  }
  const pool = ['a', '가', '響', '😀', '🎬', 'é', ' ', '…'];
  const cases = [];
  for (let i = 0; i < 3000; i++) {
    let s = ''; const n = ri(140);
    for (let j = 0; j < n; j++) s += pool[ri(pool.length)];
    cases.push([[128, 10, 2, 1][ri(4)], s]);
  }
  cases.push([128, 'a'.repeat(126) + '😀'], [128, 'a'.repeat(127) + '😀'], [128, 'a'.repeat(128)], [128, 'a'.repeat(129)], [128, ''], [3, '😀😀']);
  const got = run('clamp', cases.map(([m, s]) => m + ' ' + b64(s)));
  let bad = 0, sample = null;
  cases.forEach(([m, s], i) => {
    const want = clamp(s, m) || '';
    const [gb, gl] = got[i].split(' ');
    const g = unb64(gb).toString();
    if (g !== want || Number(gl) !== want.length) { bad++; if (!sample) sample = { m, s, want, g, gl }; }
  });
  report(cases.length + '건: 결과·UTF-16 길이가 JS 와 같음', bad, cases.length, sample);
}

console.log('=== SHA-1 · Base64 (C vs Node crypto/Buffer) ===');
{
  const bufs = [];
  for (const n of [0, 1, 2, 3, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 1000, 100000]) bufs.push(crypto.randomBytes(n));
  for (let i = 0; i < 500; i++) bufs.push(crypto.randomBytes(ri(300)));
  bufs.push(Buffer.from('dGhlIHNhbXBsZSBub25jZQ==' + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'));   // RFC 6455 예시
  const got = run('sha1', bufs.map((b) => b.toString('base64')));
  const bad = bufs.filter((b, i) => crypto.createHash('sha1').update(b).digest('hex') !== got[i]).length;
  report('SHA-1 ' + bufs.length + '건', bad, bufs.length, null);
  const rfc = Buffer.from(crypto.createHash('sha1').update(bufs[bufs.length - 1]).digest()).toString('base64');
  report('  RFC 6455 예시 키 → s3pPLMBiTxaQ9kYGzzhZRbK+xOo=', rfc === 's3pPLMBiTxaQ9kYGzzhZRbK+xOo=' && got[got.length - 1] === crypto.createHash('sha1').update(bufs[bufs.length - 1]).digest('hex') ? 0 : 1, 1, null);
  const g2 = run('b64', bufs.map((b) => b.toString('base64')));
  const bad2 = bufs.filter((b, i) => g2[i] !== b.toString('base64')).length;
  report('Base64 디코드→인코드 왕복 ' + bufs.length + '건', bad2, bufs.length, null);
  const invalid = ['abc', 'ab=c', '====', 'QR==', 'QUJ=', 'A===', 'ab\ncd', 'ab cd', '!!!!'];
  const g3 = run('b64', invalid);
  const bad3 = invalid.filter((s, i) => g3[i] !== 'ERR');
  report('Base64 깨진 입력 ' + invalid.length + '건 거절 (비정규 채움 포함)', bad3.length, invalid.length, bad3);
}

console.log(fail ? '실패 ' + fail : '전부 통과 (' + total + '건)');
process.exit(fail ? 1 : 0);
