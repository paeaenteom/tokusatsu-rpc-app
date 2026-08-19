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
    return txt('ytd-watch-metadata h1, h1.ytd-watch-metadata')
        || cleanTitle(document.title);
  }

  function channel() {
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

  self.TOKU_SITE = {
    id: 'youtube',
    siteName: SITE_NAME,

    // 썸네일은 i.ytimg.com 에서 온다 — 밝히지 않으면 NEED_THUMB 복구가 이 탭에 오지 않는다
    ownsHost: (h) => /(ytimg\.com|youtube\.com|googleusercontent\.com)$/.test(h),

    isPlaybackUrl() {
      return /^\/watch$/.test(location.pathname) || /^\/shorts\//.test(location.pathname);
    },

    //  ⚠ Shorts 에서는 <video> 가 2개다 (실측 2026-08-19).
    //    /watch 용 #movie_player 의 빈 video 가 src 도 없이 readyState 0 으로 함께 남아 있다.
    //    DOM 순서는 어느 쪽에서 넘어왔는지에 따라 바뀌므로 첫 번째를 집으면 안 된다 —
    //    죽은 쪽을 잡으면 currentTime 0 · paused true 라 '재생 중인데 일시정지' 로 굳는다.
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
        detail = channelName() || txt('#channel-name, ytd-channel-name') || cleanTitle(document.title);
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
