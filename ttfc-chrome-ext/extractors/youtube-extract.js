// ============================================================
//  YouTube 추출기
//
//  PreMiD 의 YouTube presence 를 참고하되, 실제 페이지에서 실측해 다시 짰다.
//  PreMiD 의 선택자 중 상당수는 옛 DOM 기준이라 그대로 쓰면 위험하다.
//
//  실측 (2026-08-18, youtube.com/watch):
//   ・제목   ytd-watch-metadata h1              ← 살아 있음. SPA 이동에도 갱신된다
//   ・채널   ytd-channel-name a                 ← 살아 있음
//   ・재생   /watch 는 <video> 1개. currentTime/duration/paused 전부 정상
//            Shorts 는 2개다 — 빈 #movie_player 쪽을 거르고 실제 재생 중인 것을 고른다
//   ・ID     URL 의 v= / shorts/<id> / #page-manager [video-id]
//
//  ⚠ meta[name=title] · og:image 는 **SPA 이동 후 갱신되지 않는다**(실측:
//    Shorts 에서 자동으로 다음 영상으로 넘어갔는데 meta 는 이전 영상 그대로였다).
//    그래서 제목은 DOM/document.title 에서, 썸네일은 **URL 의 영상 ID로 직접 조립**한다.
//
//  ⚠ .ytp-live-badge 는 라이브가 아니어도 DOM 에 존재한다(숨겨진 채).
//    보이는지(offsetParent)로 판정해야 한다.
// ============================================================
(() => {
  const SITE_NAME = 'YouTube';
  const T = (k, v) => (self.TOKU_I18N ? self.TOKU_I18N.t(k, v) : k);

  const txt = (sel) => {
    const el = document.querySelector(sel);
    return el ? (el.textContent || '').replace(/\s+/g, ' ').trim() : '';
  };

  // 영상 ID — URL 이 가장 믿을 만하다 (자동 재생으로 넘어가도 URL 은 따라간다)
  function videoId() {
    const m = /^\/shorts\/([\w-]{5,})/.exec(location.pathname);
    if (m) return m[1];
    const v = new URLSearchParams(location.search).get('v');
    if (v) return v;
    const el = document.querySelector('#page-manager > [video-id]');
    return el ? el.getAttribute('video-id') || '' : '';
  }

  // 썸네일은 ID 로 조립한다. i.ytimg.com 은 인증 없이 열려 있어 그대로 쓸 수 있다.
  //  maxres 가 없는 영상이 아주 많다(실측: 살아 있는 영상 9개 중 5개가 404).
  //  404 면 워커(background.js 의 fetchImage)가 mqdefault 로 다시 받아 온다.
  function thumbUrl() {
    const id = videoId();
    return id ? `https://i.ytimg.com/vi/${id}/maxresdefault.jpg` : '';
  }

  function cleanTitle(s) {
    return (s || '').replace(/\s*-\s*YouTube\s*$/, '').replace(/^\(\d+\)\s*/, '').trim();
  }

  const isShorts = () => location.pathname.startsWith('/shorts/');

  //  ⚠ /watch 에서 Shorts 로 SPA 이동하면 /watch 의 DOM 이 지워지지 않고 남는다(실측
  //    2026-08-19: 경로는 /shorts/... 인데 ytd-watch-metadata h1 과 #owner 가 이전
  //    영상 값을 그대로 돌려줬다). 그래서 경로로 갈라 본다 — 순서 폴백으로는 못 막는다.
  //    document.title 은 Shorts 에서도 제때 갱신되므로 마지막 보루로 쓸 수 있다.
  function title() {
    if (isShorts()) {
      return txt('.ytShortsVideoTitleViewModelShortsVideoTitle')
          || cleanTitle(document.title);
    }
    const f = fresh();
    if (f && f.title) return f.title;      // 페이지 월드 — 가장 빠르고 짝까지 확인됨
    //  플레이어가 띄우는 제목이 가장 먼저 바뀐다. 실측 2026-08-20 (SPA 이동 기준):
    //    URL 99ms → .ytp-title-link 235ms → ytd-watch-metadata h1 / document.title 965ms
    //    (같은 측정을 다시 했을 때 h1 은 2494ms 까지도 걸렸다)
    //  텍스트는 h1 과 글자까지 동일하다. 먼저 보는 것만으로 체감이 크게 준다.
    //  이동 중 document.title 은 잠깐 그냥 "YouTube" 가 된다. 그건 제목이 아니다.
    const dt = cleanTitle(document.title);
    return txt('.ytp-title-link')
        || txt('ytd-watch-metadata h1, h1.ytd-watch-metadata')
        || (dt === SITE_NAME ? '' : dt);
  }

  function channel() {
    const f = fresh();
    if (f && f.author) return f.author;    // DOM 채널명은 1.5초까지 늦다 (실측)
    if (isShorts()) {
      // 못 찾으면 빈 값이 낫다 — /watch 선택자로 내려가면 이전 영상의 채널이 붙는다
      return txt('.ytReelChannelBarViewModelChannelName a')
          || txt('.ytReelChannelBarViewModelChannelName')
          || '';
    }
    return txt('#owner #channel-name a')
        || txt('ytd-channel-name a')
        || '';
  }

  // 라이브 정보는 <meta itemprop> 에 있는데, SPA 이동 후에도 이전 영상 값이 남는다
  //  (실측 2026-08-19: 경로는 새 영상인데 identifier 가 이전 것이었다).
  //  meta[itemprop=identifier] 가 지금 영상 ID 와 짝이 맞을 때만 믿는다.
  function liveMeta() {
    const idEl = document.querySelector('meta[itemprop="identifier"]');
    if (!idEl || !idEl.content || idEl.content !== videoId()) return null;
    const liveEl = document.querySelector('meta[itemprop="isLiveBroadcast"]');
    if (!liveEl || liveEl.content !== 'True') return null;
    const startEl = document.querySelector('meta[itemprop="startDate"]');
    const t = startEl ? Date.parse(startEl.content) : NaN;
    return { startedAt: isFinite(t) ? t : 0 };
  }

  //  ⚠ duration 으로는 라이브를 못 가른다 — DVR 창 때문에 유한하다(실측 50399초).
  //    배지도 플레이어 컨트롤이 숨으면 offsetParent 가 null 이라 안 보인다.
  //    재생 중이면 .ytp-live 가 확실하고, 그게 없으면 짝 맞는 meta 로 판단한다.
  function isLive() {
    const f = fresh();
    if (f) return f.isLive;                // 플레이어가 직접 알려준다
    if (document.querySelector('.ytp-live')) return true;
    const b = document.querySelector('.ytp-live-badge');
    if (b && b.offsetParent !== null) return true;
    return !!liveMeta();
  }

  // 광고 중에는 제목/시간이 광고 것이라 그대로 올리면 엉뚱한 게 뜬다
  function isAd() {
    if (isShorts()) return false;   // Shorts 의 #movie_player 는 남아 있는 /watch 것이라 오판한다
    const p = document.querySelector('#movie_player');
    return !!(p && p.classList.contains('ad-showing'));
  }

  // 채널 프로필 이미지. link[rel=image_src] 에 정사각(s900-c)으로 들어 있다.
  //  ⚠ 이 meta 블록은 SPA 이동 후에도 이전 페이지 것이 남는다 — 실측 2026-08-19:
  //    영상에서 채널로 넘어가자 image_src 가 i.ytimg.com 의 영상 썸네일 그대로였다.
  //    og:title 이 화면의 채널명과 같을 때만 믿고, 호스트도 한 번 더 확인한다.
  function channelName() {
    return txt('#page-header h1, yt-page-header-renderer h1');
  }
  function channelAvatar() {
    const name = channelName();
    if (!name) return '';
    const og = document.querySelector('meta[property="og:title"]');
    if (!og || og.content !== name) return '';
    const link = document.querySelector('link[rel="image_src"]');
    const url = link ? link.href : '';
    return /^https:\/\/yt3\.googleusercontent\.com\//.test(url) ? url : '';
  }

  // ── 페이지(MAIN) 월드에서 오는 플레이어 정보 ──
  //  DOM 보다 훨씬 먼저 확정된다 (실측: 339ms vs 채널명 1582ms).
  //  videoId 가 지금 URL 과 짝맞을 때만 쓴다 — 이동 중 이전 영상 정보를 올리지 않는다.
  const CHANNEL = 'TOKU_RPC_YT';
  let snap = null;
  let lastKey = '';
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.channel !== CHANNEL) return;
    snap = d.payload || null;
    const key = snap ? [snap.videoId, snap.title, snap.author, snap.isPlaying, snap.ended].join('|') : '';
    if (key !== lastKey) {
      lastKey = key;
      if (self.TOKU_NUDGE) self.TOKU_NUDGE('YouTube 플레이어 상태 변화');
    }
  });
  try { window.postMessage({ channel: CHANNEL + '_REQ' }, location.origin); } catch (e) {}

  // 부스터를 MAIN 월드에 전달 (거긴 chrome.storage 를 못 본다)
  function pushBooster(on) {
    try { window.postMessage({ channel: CHANNEL + '_BOOST', on: !!on }, location.origin); } catch (e) {}
  }
  try {
    chrome.storage.local.get('booster', (r) => pushBooster(r && r.booster));
    chrome.storage.onChanged.addListener((ch, area) => {
      if (area === 'local' && ch.booster) pushBooster(ch.booster.newValue);
    });
  } catch (e) { /* 무시 */ }

  //  최신 상태는 <html data-toku-yt> 에서 그때그때 읽는다 (시간이 굳지 않게).
  //  속성이 없으면 postMessage 로 받아 둔 마지막 스냅샷을 쓴다.
  const ATTR = 'data-toku-yt';
  let attrRaw = '';
  let attrVal = null;
  function readAttr() {
    let raw = '';
    try { raw = document.documentElement.getAttribute(ATTR) || ''; } catch (e) { return null; }
    if (!raw) return null;
    if (raw !== attrRaw) {
      attrRaw = raw;
      try { attrVal = JSON.parse(raw); } catch (e) { attrVal = null; }
    }
    return attrVal;
  }

  // 지금 보고 있는 영상의 것일 때만 돌려준다
  function fresh() {
    const id = videoId();
    if (!id) return null;
    const a = readAttr();
    if (a && a.videoId === id) return a;
    return (snap && snap.videoId === id) ? snap : null;
  }

  //  이동 직후 DOM 은 한동안 이전 영상 값을 그대로 들고 있다 (실측: 87~464ms 내내
  //  이전 제목·채널이 나왔다). 브리지가 지금 영상으로 확정되기 전에는 보내지 않는다.
  //  다만 브리지가 아예 안 붙는 경우(페이지 구조 변경 등)에 영영 막히면 안 되므로,
  //  영상이 바뀐 지 GRACE_MS 가 지나면 DOM 값이라도 내보낸다.
  const GRACE_MS = 1500;
  //  브리지가 아직 한 번도 답을 안 준 상태의 대기 시간. 300ms 로 뒀더니 홈에서 영상으로
  //  들어갈 때 플레이어가 막 만들어지는 사이를 못 기다려, DOM 의 전이 값(document.title 이
  //  잠깐 "YouTube" 가 된다)이 그대로 카드에 나갔다 (실측 19:47:40 로그).
  const FIRST_WAIT_MS = 1200;
  let idSeen = '';
  let idSeenAt = 0;
  function markId() {
    const id = videoId();
    if (id !== idSeen) {
      idSeen = id;
      idSeenAt = Date.now();
      // 영상이 바뀌었으니 브리지에 지금 값을 달라고 바로 청한다 (다음 폴링까지 안 기다리게)
      try { window.postMessage({ channel: CHANNEL + '_REQ' }, location.origin); } catch (e) {}
    }
    return id;
  }

  self.TOKU_SITE = {
    id: 'youtube',
    siteName: SITE_NAME,

    // 썸네일은 i.ytimg.com 에서 온다 — 밝히지 않으면 NEED_THUMB 복구가 이 탭에 오지 않는다
    ownsHost: (h) => /(ytimg\.com|youtube\.com|googleusercontent\.com)$/.test(h),

    //  content.js 가 영상 상태를 보내기 전에 물어본다 (다른 사이트엔 없는 훅)
    isReady() {
      const id = markId();
      if (!id) return true;                       // 영상 페이지가 아니면 판단할 게 없다
      if (fresh()) return true;                   // 브리지가 지금 영상으로 확정했다
      if (!snap && !readAttr()) return Date.now() - idSeenAt > FIRST_WAIT_MS;  // 브리지가 아직 안 붙었다
      return Date.now() - idSeenAt > GRACE_MS;    // 브리지는 살아 있는데 늦다 → 안전 밸브
    },

    isPlaybackUrl() {
      return /^\/watch$/.test(location.pathname) || /^\/shorts\//.test(location.pathname);
    },

    //  ⚠ Shorts 에서는 <video> 가 2개다 (실측 2026-08-19).
    //    /watch 용 #movie_player 의 빈 video 가 src 도 없이 readyState 0 으로 함께 남아 있다.
    //    DOM 순서는 어느 쪽에서 넘어왔는지에 따라 바뀌므로 첫 번째를 집으면 안 된다 —
    //    죽은 쪽을 잡으면 currentTime 0 · paused true 라 '재생 중인데 일시정지' 로 굳는다.
    //  플레이어가 알려주는 시간을 우선 쓴다. <video> 를 직접 읽으면, 채널 페이지의
    //  미리보기 플레이어처럼 요소가 여럿일 때 매 틱 다른 것을 집어 길이·재생상태가
    //  진동한다 (실측 로그: 294초 ↔ 588초 를 왕복하며 ⏸/▶ 가 번갈아 찍혔다).
    getPlayback() {
      const f = fresh();
      if (!f) return null;
      let ct = f.currentTime || 0;
      let playing = !!f.isPlaying;
      //  브리지는 폴링(150~400ms)이라 재생/일시정지가 그만큼 늦다. <video> 는 즉시 바뀐다.
      //  다만 아무 <video> 나 믿으면 안 된다(채널 미리보기 등) — 길이가 브리지와 같을 때만,
      //  즉 같은 콘텐츠일 때만 쓴다. 그러면 요소가 여럿이어도 엉뚱한 걸 집지 않는다.
      let dur = f.duration || 0;
      //  startedAtMs: "이 영상의 0초가 몇 시각이었나". 읽는 순간 소수점까지 써서 계산한다.
      //  앱이 '지금 - 받은 초' 로 만들면, 소수점 버림 + 전송 지연이 그대로 오차가 되고
      //  갱신될 때마다 값이 달라져 진행 바가 ±2초로 흔들렸다 (실측 스크린샷).
      let startedAtMs = 0;
      const v = this.getVideoElement();
      //  HAVE_FUTURE_DATA(3) 미만이면 위치가 흐르지 않는다 — 기준점을 만들지 않는다.
      const canPlay = v ? v.readyState >= 3 : (f.ready || 0) >= 3;
      if (canPlay && v && dur > 0 && Math.abs((v.duration || 0) - dur) <= 2) {
        const exact = v.currentTime || 0;
        ct = Math.floor(exact);
        playing = !v.paused && !v.ended;
        startedAtMs = Date.now() - Math.round(exact * 1000);
        // 화면 하단 표시와 같은 값이 되도록 요소의 길이를 쓴다 (플레이어 API 는 1초 위로 반올림될 때가 있다)
        if (isFinite(v.duration) && v.duration > 0) dur = Math.floor(v.duration);
      }
      return { currentTime: ct, duration: dur, isPlaying: playing, loaded: dur > 0, startedAtMs };
    },

    getVideoElement() {
      const vids = document.querySelectorAll('video');
      if (vids.length <= 1) return vids[0] || null;
      for (const v of vids) {
        if ((v.currentSrc || v.src) && v.readyState > 0 && v.videoWidth > 0) return v;
      }
      return vids[0];
    },

    extractVideo() {
      const ad = isAd();
      const live = !ad && isLive();
      const lm = live ? liveMeta() : null;
      return {
        // 카드 구성: 윗줄(details)=영상 제목, 아랫줄(state)=채널
        seriesName: ad ? T('yt.ad') : (title() || SITE_NAME),
        episodeNumber: '',
        episodeTitle: ad ? '' : channel(),
        thumbnail: ad ? '' : thumbUrl(),
        // 라이브는 currentTime 이 DVR 창 안의 위치라 경과 시간이 아니다.
        //  방송 시작 시각을 넘겨 앱이 거기서부터 세어 올리게 한다.
        isLive: live,
        liveStartedAt: lm ? lm.startedAt : 0,
      };
    },

    extractBrowsing() {
      const p = location.pathname;
      let page = T('page.browsing');
      let detail = '';
      let banner = '';
      let url = '';           // 비워 두면 content.js 가 location.href 를 쓴다

      if (p === '/' || p === '') page = T('page.home');
      else if (p.startsWith('/results')) {
        //  ⚠ 검색어는 싣지 않는다. detail 은 카드 4줄째·이미지 툴팁으로 나가고
        //    pageUrl 은 '사이트에서 보기' 버튼이 되어, 입력한 질의가 프로필을 보는
        //    모든 사람에게 그대로 공개된다. 나머지 3개 사이트도 사용자 입력은 안 싣는다.
        page = T('yt.search');
        url = 'https://www.youtube.com/';
      }
      else if (p.startsWith('/feed/subscriptions')) page = T('yt.subscriptions');
      else if (p.startsWith('/feed/history')) page = T('yt.history');
      else if (p.startsWith('/playlist')) { page = T('yt.playlist'); detail = cleanTitle(document.title); }
      else if (p.startsWith('/@') || p.startsWith('/channel/') || p.startsWith('/c/')) {
        page = T('yt.channel');
        //  ytd-channel-name 컨테이너는 이름을 두 번 담고 있어 "이름 이름" 이 된다
        //  (실측 로그: '채널 / 오버롤즈 - Brick studio 오버롤즈 - Brick studio'). 링크만 읽는다.
        detail = channelName() || txt('#channel-name a, ytd-channel-name a') || cleanTitle(document.title);
        banner = channelAvatar();      // 프로필 이미지를 카드에 띄운다
      }
      else if (p.startsWith('/feed/')) page = T('page.browsing');

      if (detail === page) detail = '';
      // 채널 페이지만 배너(프로필 이미지)를 쓴다. 나머지는 사이트 로고면 충분하다.
      return { page, detail, banner, url };
    },

    // 유튜브는 자동 재생이 사이트 기본 기능이라 우리가 개입하지 않는다
    findNextEpisode() { return ''; },
  };

  console.log('[TOKU RPC] YouTube 추출기 로드됨');
})();
