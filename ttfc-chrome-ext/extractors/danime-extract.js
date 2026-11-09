// ============================================================
//  dアニメストア (animestore.docomo.ne.jp) 추출기 — 2026-11-04 구조 확인
//
//  주소
//    /animestore/tp_pc                        홈
//    /animestore/ci_pc?workId=<5자리>          작품 페이지 (og:title = 작품명, og:image = 작품 키아트 …/<workId>_1_d2.jpg)
//                      &partId=<8자리>         화 소개 창이 열린 상태 (닫으면 partId 가 빠진다)
//    /animestore/sc_pc?partId=…  →  /animestore/sc_d_pc?partId=…   PC 재생 창 (팝업). 다음 화는 새로고침 없이
//                                                                   주소의 partId 만 바뀐다 (같은 <video>)
//    partId = 작품 ID 5자리 + 화 번호 3자리 (29298001 = 29298 의 1화)
//  재생 창 화면
//    video#video (blob, 길이 유한) · .videoWrapper 클래스에 play-playing / play-pausing
//    .backInfoTxt1 작품명 · .backInfoTxt2 화 번호(第1話 · #01 · CHAPTER:1) · .backInfoTxt3 화 제목(없으면 전각 공백)
//  화 썸네일은 경로에 해시가 섞여 partId 로 만들 수 없다 → danime-main.js 가 재생 창이 받은 응답에서 읽어 둔다.
//  썸네일 CDN cs1.animestore.docomo.ne.jp 는 로그인 없이 200, CORS 없음 → 워커(호스트 권한)가 받는다.
// ============================================================
(() => {
  const T = (k, v) => (self.TOKU_I18N ? self.TOKU_I18N.t(k, v) : k);
  const SITE_NAME = 'dアニメストア';
  const ORIGIN = 'https://animestore.docomo.ne.jp';
  const ATTR = 'data-toku-danime';
  const PLAYER_PATH = /^\/animestore\/sc_d_pc\/?$/;
  //  재생 창에서 화가 바뀐 뒤 브리지 값이 따라올 때까지 기다리는 시간. 넘으면 화면 글자로 보낸다
  //  (확장을 재생 도중에 불러와 브리지가 첫 응답을 놓친 경우 등).
  const GRACE_MS = 1500;

  //  화 제목이 없으면 전각 공백(U+3000) 하나가 들어 있다 — JS 의 \s 가 전각 공백까지 잡으므로 이것으로 정리된다
  const clean = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const httpsUrl = (v) => (typeof v === 'string' && /^https:\/\/[^\s"'<>]+$/.test(v) ? v : '');

  function partIdOfUrl() {
    const p = new URLSearchParams(location.search).get('partId') || '';
    return /^\d{8}$/.test(p) ? p : '';
  }
  function workIdOfUrl() {
    const w = new URLSearchParams(location.search).get('workId') || '';
    return /^\d{5}$/.test(w) ? w : '';
  }

  // ── 브리지 값 (html[data-toku-danime]) ──
  let attrRaw = '';
  let attrVal = null;
  function bridge() {
    let raw = '';
    try { raw = document.documentElement.getAttribute(ATTR) || ''; } catch (e) { return null; }
    if (!raw) return null;
    if (raw !== attrRaw) {
      attrRaw = raw;
      try { attrVal = JSON.parse(raw); } catch (e) { attrVal = null; }
    }
    return attrVal;
  }
  //  지금 주소의 화 것일 때만 믿는다 (주소에 partId 가 없는 재생 — 플레이리스트 등 — 은 마지막 값)
  function fresh() {
    const b = bridge();
    if (!b || !/^\d{8}$/.test(String(b.partId || ''))) return null;
    const pid = partIdOfUrl();
    return (!pid || b.partId === pid) ? b : null;
  }

  // ── 화면 글자 (재생 창) ──
  const backTxt = (i) => clean((document.querySelector('.backInfoTxt' + i) || {}).textContent);

  //  주소의 화가 바뀐 시각 — 그 직후 화면 글자는 이전 화 것일 수 있다
  let seenPart = '';
  let seenAt = 0;
  function markPart() {
    const pid = partIdOfUrl();
    if (pid !== seenPart) { seenPart = pid; seenAt = Date.now(); }
    return pid;
  }

  function metaContent(prop) {
    const el = document.querySelector('meta[property="' + prop + '"]');
    return el ? clean(el.getAttribute('content')) : '';
  }

  function playerVideo() {
    return document.querySelector('.videoWrapper video#video') || document.querySelector('video#video');
  }

  self.TOKU_SITE = {
    id: 'danime',
    siteName: SITE_NAME,

    //  썸네일 CDN (cs1.animestore.docomo.ne.jp) — 앱의 다시 받기 요청(NEED_THUMB)을 이 탭이 받게
    ownsHost: (h) => /(^|\.)animestore\.docomo\.ne\.jp$/.test(h),

    isPlaybackUrl() {
      return PLAYER_PATH.test(location.pathname);
    },

    //  화가 바뀐 직후 브리지가 새 화 값을 줄 때까지는 보내지 않는다 (화면 글자가 이전 화일 수 있다)
    isReady() {
      if (!PLAYER_PATH.test(location.pathname)) return true;
      markPart();
      if (fresh()) return true;
      return Date.now() - seenAt > GRACE_MS && !!backTxt(1);
    },

    getVideoElement() {
      return playerVideo();
    },

    //  배속을 싣기 위해 재생 정보를 직접 준다 (앱이 진행 바를 배속에 맞춰 그린다)
    getPlayback() {
      const v = playerVideo();
      if (!v || !(v.duration > 0) || !isFinite(v.duration)) {
        return { currentTime: 0, duration: 0, isPlaying: false, loaded: false, startedAtMs: 0, playbackRate: 1 };
      }
      const rate = (v.playbackRate > 0 && isFinite(v.playbackRate)) ? v.playbackRate : 1;
      const exact = v.currentTime || 0;
      return {
        currentTime: Math.floor(exact),
        duration: Math.floor(v.duration),
        isPlaying: !v.paused && !v.ended,
        loaded: v.readyState >= 2,
        //  데이터가 모자라면(readyState<3) 위치가 흐르지 않으므로 기준점을 잡지 않는다
        startedAtMs: v.readyState >= 3 ? Date.now() - Math.round(exact * 1000 / rate) : 0,
        playbackRate: rate,
      };
    },

    // 카드: 윗줄 = 작품명, 아랫줄 = 화 번호 + 화 제목
    extractVideo() {
      markPart();
      const f = fresh();
      if (f) {
        return {
          seriesName: clean(f.workTitle) || backTxt(1),
          episodeNumber: clean(f.partDispNumber),
          episodeTitle: clean(f.partTitle),
          //  화 장면 이미지가 없으면 작품 키아트
          thumbnail: httpsUrl(f.scene) || httpsUrl(f.keyVisual),
        };
      }
      //  브리지 값이 없을 때(확장을 재생 도중에 불러옴 등) — 화면 글자만. 썸네일은 확인할 길이 없어 비운다(로고)
      return { seriesName: backTxt(1), episodeNumber: backTxt(2), episodeTitle: backTxt(3), thumbnail: '' };
    },

    extractBrowsing() {
      const p = location.pathname.replace(/\/+$/, '') || '/';
      let page = T('page.browsing');
      let detail = '';
      let banner = '';
      //  주소의 검색어·추적값은 싣지 않는다 (작품 페이지만 작품 번호를 남긴다)
      let url = ORIGIN + location.pathname;

      if (p === '/' || p === '/animestore' || p === '/animestore/tp_pc') {
        page = T('page.home');
      } else if (p === '/animestore/ci_pc') {
        page = T('page.work');
        detail = metaContent('og:title');
        banner = httpsUrl(metaContent('og:image').split('?')[0]);
        const w = workIdOfUrl();
        if (w) url = ORIGIN + '/animestore/ci_pc?workId=' + w;
      } else if (/^\/animestore\/(CF\/search_index|sch_pc)/.test(p)) {
        page = T('page.search');
      } else if (p.indexOf('/animestore/CR/') === 0) {
        page = T('danime.ranking');
      } else if (/^\/animestore\/mp/.test(p)) {
        page = T('danime.mypage');
      } else if (p.indexOf('/animestore/CN/') === 0) {
        page = T('page.notice');
      } else if (/^\/animestore\/CF\/(howto|faq)/.test(p)) {
        page = T('page.help');
      } else if (p === '/animestore/CF/user_setting') {
        page = T('page.settings');
      } else if (p.indexOf('/animestore/book') === 0) {
        page = T('danime.book');
      } else if (p.indexOf('/animestore/ec') === 0) {
        page = T('danime.goods');
      } else if (p.indexOf('/animestore/CF/') === 0 || p.indexOf('/animestore/CP/') === 0) {
        page = T('page.special');
      }
      return { page, detail, banner, url };
    },
  };
})();
