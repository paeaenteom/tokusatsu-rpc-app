// ============================================================
//  YouTube — 페이지(MAIN) 월드 브리지
//
//  DOM 만 보면 SPA 이동 후 값이 늦게 채워진다. 실측 2026-08-20 (이동 시점 기준):
//    URL 88ms → .ytp-title-link 764ms → ytd-watch-metadata h1 1114~2494ms
//    → #owner #channel-name a 1582ms
//  그 사이에 document.title 은 잠깐 "YouTube" 가 되고, 채널 페이지에서 넘어오면
//  이전 페이지의 채널명이 그대로 남는다. 그래서 카드에 "YouTube" 나 이전 채널명이
//  먼저 뜨고 1~2초 뒤에야 고쳐졌다.
//
//  플레이어 객체는 이 값을 훨씬 먼저, 한꺼번에 갖고 있다 (실측 339ms):
//    #movie_player.getVideoData() → { video_id, title, author, isLive }
//    getCurrentTime() / getDuration() / getPlayerState()
//  그런데 이건 페이지 자바스크립트가 엘리먼트에 붙인 메서드라 확장의 격리 월드에서는
//  보이지 않는다. 그래서 이 파일만 world:"MAIN" 으로 올려 읽고 postMessage 로 넘긴다.
//  (디즈니+ 브리지와 같은 방식)
//
//  video_id 를 같이 보내는 게 핵심이다. 받는 쪽이 지금 URL 의 영상과 짝이 맞을 때만
//  쓰면, 이동 중에 이전 영상 정보를 올리는 일이 원천적으로 없어진다.
//
//  ⚠ 이 파일은 YouTube 페이지의 전역과 같은 공간에서 돈다. 읽기만 한다.
// ============================================================
(() => {
  const CHANNEL = 'TOKU_RPC_YT';
  const POLL_MS = 400;
  const POLL_BOOST = 150;

  // /watch 는 #movie_player, Shorts 는 #shorts-player.
  //  Shorts 로 넘어가도 #movie_player 는 이전 영상 그대로 남아 있으므로 경로로 고른다.
  function player() {
    const shorts = location.pathname.indexOf('/shorts/') === 0;
    const el = document.querySelector(shorts ? '#shorts-player' : '#movie_player');
    return el && typeof el.getVideoData === 'function' ? el : null;
  }

  function num(fn) {
    try { const v = fn(); return typeof v === 'number' && isFinite(v) ? v : 0; } catch (e) { return 0; }
  }

  function snapshot() {
    const p = player();
    if (!p) return null;
    let d;
    try { d = p.getVideoData(); } catch (e) { return null; }
    if (!d || !d.video_id) return null;
    // getPlayerState: -1 미시작 · 0 종료 · 1 재생 · 2 일시정지 · 3 버퍼링 · 5 대기
    let state = -1;
    try { state = p.getPlayerState(); } catch (e) {}
    return {
      videoId: String(d.video_id),
      title: String(d.title || ''),
      author: String(d.author || ''),
      isLive: !!d.isLive,
      currentTime: Math.floor(num(() => p.getCurrentTime())),
      duration: Math.floor(num(() => p.getDuration())),
      // 버퍼링(3)도 재생 중으로 본다 — 잠깐 끊겼다고 일시정지로 뒤집히면 카드가 깜빡인다
      isPlaying: state === 1 || state === 3,
      ended: state === 0,
      //  아직 재생할 수 있는 데이터가 없으면 위치가 흐르지 않는다. 이때 '재생 중' 으로
      //  기준점을 잡으면 Discord 는 거기서부터 세는데 실제 재생은 나중에 시작되므로
      //  그 지연만큼 카드가 앞선다 (실측: readyState 0 인데 isPlaying true).
      ready: (function () { const v = document.querySelector('video'); return v ? v.readyState : 0; })(),
    };
  }

  //  ⚠ 시간은 postMessage 로 보내면 안 된다.
  //    "바뀔 때만 보내기" 로 아끼려고 비교에서 currentTime 을 빼면, 받는 쪽 스냅샷의
  //    시간이 처음 값에 그대로 굳는다. 그러면 30초마다 도는 갱신이 매번
  //    startTimestamp = 지금 - (굳은 시간) 으로 다시 세팅해, 진행 바가 계속 처음으로
  //    되돌아간다 (실측: 실제 2:34 인데 카드가 0:05 에 멈춰 있었다).
  //    → 시시각각 변하는 값은 <html> 의 data 속성에 써 둔다. 같은 DOM 이라
  //      격리 월드가 필요할 때 동기적으로 그때의 값을 읽어 간다.
  const ATTR = 'data-toku-yt';

  let lastJson = '';
  function tick() {
    const snap = snapshot();
    if (!snap) return;
    try {
      document.documentElement.setAttribute(ATTR, JSON.stringify(snap));
    } catch (e) { /* 무시 */ }
    // postMessage 는 '무엇이 달라졌다' 는 신호용이다 (즉시 갱신을 깨우는 용도).
    const cmp = [snap.videoId, snap.title, snap.author, snap.isLive,
                 snap.duration, snap.isPlaying, snap.ended].join('|');
    if (cmp === lastJson) return;
    lastJson = cmp;
    try { window.postMessage({ channel: CHANNEL, payload: snap }, location.origin); } catch (e) {}
  }

  //  재생/일시정지/탐색은 <video> 이벤트가 즉시 온다 (실측: play +3ms, pause +41ms).
  //  플레이어의 onStateChange 는 함수 참조로 등록되지 않아(구형 API 가 전역 함수 '이름' 을
  //  요구한다) 조용히 안 불린다 — 실측으로 확인. 그래서 미디어 이벤트를 쓴다.
  //  폴링은 이벤트가 없는 변화(영상 교체 등)를 위한 안전망으로 남긴다.
  const EVENTS = ['play', 'pause', 'seeked', 'playing', 'ended', 'loadedmetadata', 'durationchange', 'ratechange'];
  //  ⚠ 관찰은 <video> 를 찾을 때까지만 한다. 유튜브는 뮤테이션이 매우 잦아서
  //    계속 켜 두면 그때마다 콜백이 돌아 비용이 크다 (디즈니+ 브리지는 관찰을 쓰지 않는다).
  let mo = null;
  function attach() {
    const v = document.querySelector('video');
    if (!v) return false;
    if (!v.__tokuYtHooked) {
      v.__tokuYtHooked = true;
      for (const e of EVENTS) v.addEventListener(e, tick, { passive: true });
    }
    if (mo) { try { mo.disconnect(); } catch (e) {} mo = null; }
    return true;
  }
  if (!attach()) {
    try {
      mo = new MutationObserver(attach);
      mo.observe(document.documentElement, { childList: true, subtree: true });
    } catch (e) { /* 무시 */ }
  }

  let pollTimer = setInterval(tick, POLL_MS);
  function setPoll(ms) {
    clearInterval(pollTimer);
    pollTimer = setInterval(tick, ms);
  }

  window.addEventListener('message', (e) => {
    if (e.source !== window || !e.data) return;
    if (e.data.channel === CHANNEL + '_BOOST') { setPoll(e.data.on ? POLL_BOOST : POLL_MS); return; }
    if (e.data.channel !== CHANNEL + '_REQ') return;
    lastJson = '';          // 다음 tick 에서 무조건 다시 보낸다
    tick();
  });

  tick();
})();
