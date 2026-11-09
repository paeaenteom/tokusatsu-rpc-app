// C 버튼 주소(url_vec.exe) vs discord-rpc.js 의 ytButtonUrl·chzzkButtonUrl·danimeButtonUrl (원본 함수를 그대로 읽어 쓴다)
'use strict';
const fs = require('fs'); const vm = require('vm'); const { spawnSync } = require('child_process');
const EXE = process.argv[2];
const src = fs.readFileSync('C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/discord-rpc.js', 'utf8');
const pick = (name) => { const a = src.indexOf('function ' + name + '('); const b = src.indexOf('\n}\n', a); return src.slice(a, b + 3); };
const ctx = { URL, encodeURIComponent };
vm.createContext(ctx);
vm.runInContext(pick('ytButtonUrl') + pick('chzzkButtonUrl') + pick('danimeButtonUrl') + 'this.f={yt:ytButtonUrl,chzzk:chzzkButtonUrl,danime:danimeButtonUrl};', ctx);
const HOME = { yt: 'https://www.youtube.com/', chzzk: 'https://chzzk.naver.com/', danime: 'https://animestore.docomo.ne.jp/animestore/' };
let seed = 99;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const ri = (n) => Math.floor(rnd() * n);
const pickOf = (a) => a[ri(a.length)];
const cases = [];
const fixed = {
  yt: ['https://www.youtube.com/watch?v=dQw4w9WgXcQ&pp=검색어&t=10s', 'https://www.youtube.com/watch?v=a%20b&list=PL1', 'https://www.youtube.com/watch', 'https://www.youtube.com/watch?v=',
    'https://www.youtube.com/shorts/abc_-1?feature=share', 'https://www.youtube.com/live/XYZ', 'https://www.youtube.com/@채널/search?query=비밀', 'https://www.youtube.com/@abc',
    'https://www.youtube.com/channel/UC123/videos', 'https://www.youtube.com/c/name/featured', 'https://www.youtube.com/playlist?list=PL%2Fx', 'https://www.youtube.com/results?search_query=비밀',
    'https://m.youtube.com/watch?v=1', 'https://www.youtube.com:443/feed/subscriptions', 'https://WWW.YouTube.com/Watch?v=1', 'https://www.youtube.com/./a/../watch?v=q', 'https://www.youtube.com/%2e%2e/x',
    'https://www.youtube.com/watch?v=1+2', 'https://www.youtube.com/watch?v=%E3%81%82', 'https://www.youtube.com/watch?v=%ZZ', 'https://www.youtube.com/a b/"c"', 'https://user:pw@www.youtube.com/x',
    'https://www.youtube.com:8080/x', 'https://127.1/x', 'https://0x7f.0.0.1/x', 'https://www.youtube.com\\watch?v=1', 'https:///www.youtube.com/x', 'https://www.youtube.com/watch?v=1#frag', 'http://', 'https://[::1]/x',
    'https://한글.com/x', 'https://xn--bj0bj06e.com/x', 'https://www.youtube.com/watch?v=😀', 'https://www.youtube.com/😀?v=1', 'https://a..b/x', 'https://a.1/x', 'https://999.1.1.1/x', 'https://www.youtube.com/watch?%76=7'],
  chzzk: ['https://chzzk.naver.com/live/0123456789abcdef0123456789ABCDEF', 'https://chzzk.naver.com/live/0123456789abcdef0123456789ABCDEF/extra', 'https://chzzk.naver.com/live/0123456789abcdef0123456789ABCDEFx',
    'https://chzzk.naver.com/video/12345?t=3', 'https://chzzk.naver.com/video/12a', 'https://chzzk.naver.com/clips/Ab_-9', 'https://chzzk.naver.com/0123456789abcdef0123456789abcdef/community',
    'https://chzzk.naver.com/search?query=비밀', 'https://CHZZK.naver.com/video/1', 'https://chzzk.naver.com:443/video/1', 'https://chzzk.naver.com:444/video/1', 'http://chzzk.naver.com/video/1', 'https://evil.com/video/1',
    'https://chzzk.naver.com', 'https://chzzk.naver.com/', 'https://chzzk.naver.com/clips/', 'https://chzzk.naver.com/live/'],
  danime: ['https://animestore.docomo.ne.jp/animestore/sc_d_pc?partId=12345001&ref=x', 'https://animestore.docomo.ne.jp/animestore/ci_pc?workId=12345', 'https://animestore.docomo.ne.jp/animestore/ci_pc?workId=12345&partId=99999001',
    'https://animestore.docomo.ne.jp/animestore/sc_pc?workId=1234', 'https://animestore.docomo.ne.jp/animestore/sc_pc_l?partId=1234500', 'https://animestore.docomo.ne.jp/animestore/tp_pc', 'https://animestore.docomo.ne.jp/animestore/sch_pc?searchKey=비밀',
    'https://animestore.docomo.ne.jp/animestore/ci_pc?workId=１２３４５', 'https://animestore.docomo.ne.jp/animestore/ci_pc?partId=12345001&workId=54321', 'https://evil.jp/animestore/ci_pc?workId=12345', 'https://animestore.docomo.ne.jp/animestore/ci_pc/?workId=12345'],
};
for (const m of Object.keys(fixed)) for (const u of fixed[m]) cases.push([m, u]);
//  무작위 조합
const hostOf = { yt: ['www.youtube.com', 'm.youtube.com', 'WWW.YOUTUBE.COM', 'youtube.com:443', 'a.b'], chzzk: ['chzzk.naver.com', 'CHZZK.NAVER.COM', 'chzzk.naver.com:443', 'naver.com'], danime: ['animestore.docomo.ne.jp', 'ANIMESTORE.docomo.ne.jp', 'x.jp'] };
const pathBits = ['watch', 'shorts', 'live', '@x', 'channel', 'c', 'playlist', 'video', 'clips', '0123456789abcdef0123456789abcdef', 'animestore', 'ci_pc', 'sc_d_pc', 'sc_pc', 'sc_pc_l', '..', '.', '%2e', '한', 'a b', '"', '😀', '123', 'Ab_-', ''];
const qBits = ['v=1', 'v=a%20b', 'list=PL1', 'partId=12345001', 'workId=12345', 'workId=1', 'partId=1', 'pp=비밀', 'x', '', '=', 'v', 'v=😀', 'v=%FF', 'v=+'];
for (let i = 0; i < 4000; i++) {
  const m = pickOf(['yt', 'chzzk', 'danime']);
  let u = pickOf(['https://', 'http://', 'HTTPS://']) + pickOf(hostOf[m]);
  const np = ri(4);
  for (let k = 0; k < np; k++) u += pickOf(['/', '/', '\\']) + pickOf(pathBits);
  if (ri(2)) { u += '?'; const nq = 1 + ri(3); for (let k = 0; k < nq; k++) u += (k ? '&' : '') + pickOf(qBits); }
  if (ri(5) === 0) u += '#' + pickOf(pathBits);
  cases.push([m, u]);
}
const r = spawnSync(EXE, [], { input: cases.map(([m, u]) => m + ' ' + Buffer.from(u).toString('base64')).join('\n') + '\n', maxBuffer: 1 << 26 });
const out = r.stdout.toString().split(/\r?\n/);
let bad = 0, unsupported = 0;
const samples = [];
cases.forEach(([m, u], i) => {
  const want = ctx.f[m](u, HOME[m]);
  const got = Buffer.from(out[i] || '', 'base64').toString();
  if (want === got) return;
  //  일부러 지원하지 않는 것: 비ASCII 도메인(IDNA)·xn--·IPv6 — C 는 홈으로 보낸다
  let host = '';
  try { host = new URL(u).hostname; } catch (e) {}
  if (got === HOME[m] && (/[^\x00-\x7f]/.test(u.split('/')[2] || '') || /(^|\.)xn--/i.test(host) || host.startsWith('['))) { unsupported++; return; }
  bad++;
  if (samples.length < 6) samples.push({ m, u, want, got });
});
console.log('  ' + (bad ? 'X   ' : 'OK  ') + ('버튼 주소 ' + cases.length + '건 JS 와 같음').padEnd(50) + (bad ? bad + '건 다름 ' + JSON.stringify(samples) : '') + (unsupported ? ' (일부러 지원 안 함 ' + unsupported + '건: 비ASCII·xn--·IPv6 → 홈)' : ''));
process.exit(bad ? 1 : 0);
