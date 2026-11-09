// TTFC 스킵 버튼 보호 점검 — 확장 extractors/ttfc-extract.js 의 installSkipGuard 를 그대로 꺼내
//  '스킵을 누르면 재생이 뒤집히는' 사이트를 흉내 낸 가짜 문서·영상에 붙여 본다 (브라우저·크롬 안 띄움)
//  사용: node tests/ttfc-skip-guard-test.js
'use strict';
const fs = require('fs');
const SRC = fs.readFileSync('C:/Users/paeae/Downloads/toku rpc/ttfc-chrome-ext/extractors/ttfc-extract.js', 'utf8');
const m = /\/\/  @@skip-guard-start\n([\s\S]+?)\n\s*\/\/  @@skip-guard-end/.exec(SRC);
if (!m) { console.log('X ttfc-extract.js 에서 installSkipGuard 를 못 찾음'); process.exit(1); }
const installSkipGuard = new Function(m[1] + '\nreturn installSkipGuard;')();

let fail = 0, n = 0;
const ok = (l, c, e) => { n++; console.log('  ' + (c ? 'OK  ' : 'X   ') + l.padEnd(60) + (e !== undefined && !c ? ' ' + JSON.stringify(e) : '')); if (!c) fail++; };

//  가짜 문서: 잡기(capture) 단계 듣는 이만 흉내 — 확장이 쓰는 건 그것뿐
function makeEnv(opts = {}) {
  let t = 0;
  const L = { doc: {}, win: {} };
  const reg = (where) => ({ addEventListener: (type, fn) => { (L[where][type] = L[where][type] || []).push(fn); } });
  const doc = reg('doc'), win = reg('win');
  const emit = (where, type, target, isTrusted) => {
    const ev = { type, target, isTrusted: !!isTrusted, stopped: false, stopImmediatePropagation() { this.stopped = true; } };
    for (const fn of L[where][type] || []) { if (ev.stopped) break; fn(ev); }
    return ev;
  };
  const log = [];
  const video = {
    paused: !!opts.paused, ended: false,
    play() { log.push('play'); if (this.paused) { this.paused = false; emit('doc', 'play', this, false); } return Promise.resolve(); },
    pause() { log.push('pause'); if (!this.paused) { this.paused = true; emit('doc', 'pause', this, false); } },
  };
  const skipBtn = { closest: (s) => (s === '#player-skip-btn' ? skipBtn : null) };
  const skipLabel = { closest: (s) => (s === '#player-skip-btn' ? skipBtn : null) };   //  버튼 안 글자 (span)
  const playBtn = { closest: () => null };
  const env = { t: () => t, adv: (ms) => { t += ms; }, video, skipBtn, skipLabel, playBtn, log, emit };
  installSkipGuard(doc, win, () => video, () => t);
  //  사이트: 스킵 버튼 클릭이 플레이어 판까지 올라가 재생을 뒤집는다 (opts.bug=false 면 안 뒤집는 정상 사이트)
  env.click = (target, trusted, siteDelay) => {
    emit('doc', 'click', target, trusted);
    const isSkip = target === skipBtn || target === skipLabel;
    const toggle = () => { if (video.paused) video.play(); else video.pause(); };
    if (isSkip && opts.bug !== false) { if (siteDelay) env.later = toggle; else toggle(); }
    if (target === playBtn) toggle();
  };
  //  keybind.js: window 잡기 단계에서 키를 삼키고 버튼을 click() (신뢰 안 된 클릭)
  env.key = (code, binds) => {
    const ev = emit('win', 'keydown', { closest: () => null }, true);
    if (!ev.stopped && binds[code]) env.click(binds[code], false);
  };
  return env;
}

console.log('=== 사이트가 뒤집을 때 ===');
{ const e = makeEnv({ paused: false }); e.click(e.skipBtn, true); ok('재생 중 마우스로 스킵 → 계속 재생', e.video.paused === false, e.log); }
{ const e = makeEnv({ paused: true }); e.click(e.skipBtn, true); ok('멈춤 중 마우스로 스킵 → 계속 멈춤', e.video.paused === true, e.log); }
{ const e = makeEnv({ paused: false }); e.click(e.skipLabel, true); ok('버튼 안 글자를 눌러도 (closest)', e.video.paused === false, e.log); }
{ const e = makeEnv({ paused: false }); e.key('KeyS', { KeyS: e.skipBtn }); ok('키 설정(S)으로 스킵 — 신뢰 안 된 click 도', e.video.paused === false, e.log); }
{ const e = makeEnv({ paused: false }); e.click(e.skipBtn, true, true); e.adv(300); e.later(); ok('사이트가 0.3초 뒤에 뒤집어도 되돌림', e.video.paused === false, e.log); }

console.log('=== 손대면 안 되는 경우 ===');
{ const e = makeEnv({ paused: false, bug: false }); e.click(e.skipBtn, true); ok('정상 사이트 (안 뒤집음) → 아무것도 안 함', e.video.paused === false && e.log.length === 0, e.log); }
{ const e = makeEnv({ paused: false, bug: false }); e.click(e.skipBtn, true); e.adv(200); e.click(e.playBtn, true); ok('스킵 뒤 직접 멈춤 버튼 → 멈춘 채로', e.video.paused === true, e.log); }
{ const e = makeEnv({ paused: false, bug: false }); e.click(e.skipBtn, true); e.adv(200); e.key('Space', { Space: e.playBtn }); ok('스킵 뒤 재생 키(Space, 키 설정이 삼킴) → 멈춘 채로', e.video.paused === true, e.log); }
{ const e = makeEnv({ paused: false }); e.click(e.skipBtn, true, true); e.adv(1600); e.later(); ok('1.5초 지나서 뒤집힌 건 그대로', e.video.paused === true, e.log); }
{ const e = makeEnv({ paused: false, bug: false }); e.click(e.skipBtn, true); e.video.ended = true; e.video.pause(); ok('엔딩 스킵으로 끝(ended)에 가서 멈춤 → 다시 재생 안 함', e.video.paused === true && !e.log.slice(1).includes('play'), e.log); }
{ const e = makeEnv({ paused: false }); e.click(e.skipBtn, true); e.adv(100); e.video.pause(); ok('되돌린 뒤 또 멈춤 → 한 번만 되돌림', e.video.paused === true, e.log); }
{ const e = makeEnv({ paused: false, bug: false }); e.click(e.skipBtn, true); const other = { paused: true, ended: false }; e.emit('doc', 'play', other, false); e.video.pause(); ok('다른 영상 요소의 이벤트는 무시 (그 뒤 이 영상 멈춤은 되돌림)', e.video.paused === false, e.log); }

console.log(fail ? '실패 ' + fail + '/' + n : '전부 통과 (' + n + '건)');
process.exit(fail ? 1 : 0);
