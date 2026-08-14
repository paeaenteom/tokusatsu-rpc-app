// ============================================================
//  디즈니+ 추출기   ※ 실험 기능 (테스트 Discord 앱 사용)
//
//  window.TOKU_SITE 를 정의한다. 재생 정보는 disneyplus-main.js 가
//  페이지 월드에서 읽어 postMessage 로 넘겨준 것을 받아 쓴다.
//
//  ── URL (한국은 ko-kr 구 플랫폼) ──
//   /{lang}-{region}/play/{uuid}            영상 재생
//   /{lang}-{region}/browse/entity-{uuid}   작품 (영화·시리즈 공용)
//   /{lang}-{region}/browse/{movies|series|originals|watchlist|search}
//   /{lang}-{region}/browse/page-{uuid}     기획전
//   /{lang}-{region}/home                   홈
//
//  ⚠ 디즈니가 apps.disneyplus.com (Hulu 계열 /shows/{slug}/.../watch) 로
//    옮기는 중이다. 한국은 아직 구 플랫폼이지만 언젠가 바뀐다.
//    그때 이 파일의 URL 판정만 고치면 되도록 한곳에 모아 뒀다.
// ============================================================

(() => {
  const T = (key) => (self.TOKU_I18N ? self.TOKU_I18N.t(key) : key);
  const SITE_NAME = 'Disney+';
  const CHANNEL = 'TOKU_RPC_DP';

  // 아트워크: 페이지 월드가 UUID 만 주므로 여기서 URL 로 만든다.
  //  이 CDN 은 인증 없이 열려 있어(실측: Discordbot UA 로도 200)
  //  디스코드가 직접 불러올 수 있다 → 재호스팅 파이프라인을 타지 않는다.
  const IMG = (id) => id
    ? `https://disney.images.edge.bamgrid.com/ripcut-delivery/v2/variant/disney/${id}/compose?format=jpeg&width=600`
    : '';

  // ── 에피소드 썸네일 기억 ──
  //  재생 중 플레이어가 주는 images_experience 는 전부 시리즈 레벨이라
  //  그 화의 스틸이 들어있지 않다 (실측: 이미지 31개 전부 시리즈 것).
  //  다행히 에피소드 카드의 링크가 재생 URL 과 같은 /play/{uuid} 라서,
  //  목록을 지나갈 때 uuid → 썸네일 을 모아두면 재생할 때 꺼내 쓸 수 있다.
  //  디즈니+ 는 SPA 라 목록에서 재생으로 넘어가도 이 스크립트가 살아있다.
  //  직접 URL 로 들어온 경우엔 기억이 없으므로 시리즈 아트워크로 떨어진다.
  const epThumbs = new Map();
  const EP_CACHE_KEY = 'dpEpisodeThumbs';
  const EP_CACHE_MAX = 300;

  // 새로고침·다른 탭에서도 쓰도록 확장 저장소에 남긴다 (사이트 저장소는 건드리지 않는다)
  try {
    chrome.storage.local.get(EP_CACHE_KEY, (r) => {
      const saved = r && r[EP_CACHE_KEY];
      if (saved) for (const [k, v] of Object.entries(saved)) if (!epThumbs.has(k)) epThumbs.set(k, v);
    });
  } catch (e) { /* 저장소 없이도 동작한다 */ }

  let saveTimer = null;
  function persistThumbs() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      try {
        // 오래된 것부터 버려 상한을 지킨다
        const entries = [...epThumbs.entries()].slice(-EP_CACHE_MAX);
        epThumbs.clear();
        entries.forEach(([k, v]) => epThumbs.set(k, v));
        chrome.storage.local.set({ [EP_CACHE_KEY]: Object.fromEntries(entries) });
      } catch (e) {}
    }, 2000);
  }

  const playIdOf = (href) => {
    const m = String(href || '').match(/\/play\/([0-9a-f-]{8,})/i);
    return m ? m[1] : '';
  };

  // 지금 페이지에 있는 "에피소드 카드 → 썸네일" 을 전부 주워 담는다
  function harvestEpisodeThumbs() {
    let added = 0;
    for (const a of document.querySelectorAll('a[href*="/play/"]')) {
      const id = playIdOf(a.getAttribute('href'));
      if (!id || epThumbs.has(id)) continue;
      // 큰 "재생" 버튼도 /play/ 링크지만 이미지가 없다 — 그건 건너뛰고
      // 이미지를 가진 에피소드 카드만 담는다
      const img = a.querySelector('img');
      const src = img && (img.currentSrc || img.src);
      if (!src || !/bamgrid/.test(src)) continue;
      epThumbs.set(id, src);
      added++;
    }
    if (added) persistThumbs();
    return added;
  }

  // 카드가 화면에 붙는 "순간" 잡는다.
  //  1초 주기 수집만으로는 목록을 스쳐 지나가면 놓친다. 실제로 작품 페이지에서
  //  바로 재생을 누르면 에피소드 목록이 렌더되기 전에 넘어가 버려서
  //  아무것도 못 담은 채 시리즈 아트로 떨어졌다.
  //  디즈니+ 는 SPA 라 이 관찰자는 문서가 살아있는 동안 계속 동작한다.
  let harvestTimer = null;
  const observer = new MutationObserver(() => {
    if (harvestTimer) return;                 // 폭주하는 DOM 변경을 묶어서 처리
    harvestTimer = setTimeout(() => {
      harvestTimer = null;
      if (!harvestEpisodeThumbs()) return;
      // 지금 보고 있는 화의 썸네일을 방금 얻었다면 바로 갈아끼운다
      // bare() 는 아래에서 선언되므로 여기선 경로를 직접 본다 (선언 순서 의존 제거)
      if (/\/play\//.test(location.pathname) && epThumbs.has(playIdOf(location.pathname)) && self.TOKU_NUDGE) {
        self.TOKU_NUDGE('에피소드 썸네일 확보');
      }
    }, 300);
  });
  try {
    observer.observe(document.documentElement, { childList: true, subtree: true });
  } catch (e) { /* 관찰 실패해도 주기 수집이 남아 있다 */ }

  // ⚠ 관찰자는 "변경"에만 반응한다. 스크립트가 붙는 시점에 카드가 이미 그려져
  //   있으면 변경이 일어나지 않아 영영 못 잡는다(실측으로 확인). 처음 한 번은
  //   직접 훑어야 한다. 아직 안 그려졌으면 관찰자가 이어받는다.
  harvestEpisodeThumbs();

  // ── 페이지 월드에서 오는 재생 상태 ──
  let snap = null;
  let lastKey = '';
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.channel !== CHANNEL) return;
    snap = d.payload || null;
    // 작품·화·재생상태가 바뀌었으면 다음 주기를 기다리지 않고 바로 올린다
    const key = snap ? [snap.title, snap.subtitle, snap.isPlaying, snap.ended].join('|') : '';
    if (key !== lastKey) {
      lastKey = key;
      if (self.TOKU_NUDGE) self.TOKU_NUDGE('디즈니+ 플레이어 상태 변화');
    }
  });
  // 늦게 붙었을 때를 대비해 한 번 요청해 둔다
  try { window.postMessage({ channel: CHANNEL + '_REQ' }, location.origin); } catch (e) {}

  // 부스터를 MAIN 월드에 전달한다 (거긴 chrome.storage 를 못 본다)
  function pushBooster(on) {
    try { window.postMessage({ channel: CHANNEL + '_BOOST', on: !!on }, location.origin); } catch (e) {}
  }
  try {
    chrome.storage.local.get('booster', (r) => pushBooster(r && r.booster));
    chrome.storage.onChanged.addListener((ch, area) => {
      if (area === 'local' && ch.booster) pushBooster(ch.booster.newValue);
    });
  } catch (e) { /* 저장소 없이도 기본 주기로 동작한다 */ }

  const fresh = () => (snap && Date.now() - snap.at < 5000) ? snap : null;

  // ── URL 판정 ──
  const path = () => location.pathname;
  // 로케일 접두어(/ko-kr)를 걷어낸 경로
  const bare = () => path().replace(/^\/[a-z]{2}(-[a-z]{2})?(?=\/|$)/i, '') || '/';

  // /browse 아래에서 브랜드 허브가 아닌, 이름이 정해진 목록 경로들
  const LIST_ROUTES = ['movies', 'series', 'originals', 'watchlist', 'search'];

  // Discord 에 직접 올린 에셋이 있는 브랜드만 여기에 적는다.
  //  나머지는 사이트의 브랜드 로고 이미지를 그대로 쓴다 — 그 CDN 이 인증 없이
  //  열려 있어 디스코드가 바로 불러오므로 업로드할 이유가 없다.
  //  나중에 다른 브랜드를 올리면 한 줄만 추가하면 된다.
  const BRAND_ASSET = {
    marvel: 'marvel_logo',
  };

  // 브랜드 표시 이름은 영어로 고정한다.
  //  문서 제목은 로케일을 타서("마블"/"Marvel"/"マーベル") 보는 사람마다 달라지는데,
  //  브랜드는 원래 이름 그대로 두는 편이 알아보기 쉽다.
  const BRAND_NAME = {
    disney: 'Disney',
    pixar: 'PIXAR',
    marvel: 'MARVEL',
    'star-wars': 'STAR WARS',
    'national-geographic': 'National Geographic',
    disneyplus: 'Disney+',
    star: 'STAR',
  };
  // 목록에 없는 새 브랜드는 슬러그를 사람이 읽을 형태로 (star-wars → Star Wars)
  const prettySlug = (s) => s.split('-').map((w) => w.charAt(0).toUpperCase() + w.slice(1)).join(' ');

  // ⚠ 디즈니+ 는 같은 자리에 <img> 를 그대로 두기도 하고 <div> 로 감싸기도 한다.
  //   그래서 '[testid] img' 로 쓰면 요소 자체가 <img> 인 경우를 통째로 놓친다
  //   (2026-08-14: 이것 때문에 배경 아트워크가 저해상도로 잡히거나 아예 안 잡혔다).
  function imgSrcOf(el) {
    if (!el) return '';
    const img = el.tagName === 'IMG' ? el : el.querySelector('img');
    return img ? (img.currentSrc || img.src || '') : '';
  }

  // 타이틀 트리트먼트 = 그 페이지의 "로고 이미지". 페이지 종류마다 testid 가 다르다.
  //  ・작품 페이지(/browse/entity-…)          → details-title-treatment  실측 800x176 (4.55:1)
  //  ・목록·브랜드 페이지(/browse/page-·/browse/marvel) → he-title-treatment  실측 800x400 (2:1)
  //  한 페이지에 둘 중 하나만 존재하므로 한 번에 물어봐도 안전하다. alt 에 작품명이 들어 있다.
  function titleTreatmentUrl() {
    return imgSrcOf(document.querySelector('[data-testid="he-title-treatment"], [data-testid="details-title-treatment"]'));
  }

  // 배경 아트워크. -responsive 를 먼저 본다 — 1920x1080 로 나머지(400x225)보다 5배 크다.
  const BACKDROP_SELECTORS = [
    '[data-testid="details-page-background-image-responsive"]',        // 작품 페이지
    '[data-testid="high-emphasis-page-background-image-responsive"]',  // 목록·브랜드 페이지
    '[data-testid="details-page-background-image"]',
    '[data-testid="high-emphasis-page-background-image"]',
  ];
  function backdropUrl() {
    for (const sel of BACKDROP_SELECTORS) {
      const u = imgSrcOf(document.querySelector(sel));
      if (u) return u;
    }
    return '';
  }

  // 페이지 상단 히어로를 그대로 카드로. 확장 워커가 배경 위에 로고를 얹어 합성한다.
  //  단계별 폴백 — 어느 쪽이 없어도 표시가 죽지 않는다:
  //   로고+배경 → 합성 / 로고만 → 로고 / 배경만 → 배경 / 둘 다 없음 → 호출부가 사이트 로고로
  function heroBanner() {
    const logo = titleTreatmentUrl();
    const bg = backdropUrl();
    return { banner: logo || bg, bannerBg: logo && bg ? bg : '' };
  }

  // "시즌 1: 1회 새벽 3시" → { number: '시즌 1: 1회', title: '새벽 3시' }
  //  로케일마다 형식이 다르므로(영어는 "S1:E1 3 AM") 콜론 뒤 첫 덩어리를 번호로 본다.
  //  못 쪼개면 통째로 번호 자리에 넣는다 — 정보를 잃는 것보다 낫다.
  function splitEpisode(subtitle) {
    const s = (subtitle || '').trim();
    if (!s) return { number: '', title: '' };
    // 한국어: "시즌 1: 1회 제목"  /  영어: "S1:E1 Title"  /  일본어: "シーズン1:第1話 タイトル"
    let m = s.match(/^(.*?[:：]\s*\S+?[화회話]?)\s+(.+)$/);
    if (m) return { number: m[1].trim(), title: m[2].trim() };
    m = s.match(/^(S\d+\s*[:：]\s*E\d+)\s+(.+)$/i);
    if (m) return { number: m[1].trim(), title: m[2].trim() };
    return { number: s, title: '' };
  }

  window.TOKU_SITE = {
    id: 'disneyplus',
    siteName: SITE_NAME,

    isPlaybackUrl() {
      return /^\/play\//.test(bare());
    },

    getVideoElement() {
      // 디즈니+ 는 <video> 가 두 개 붙는다. 실제 재생은 hivePlayer 쪽.
      return document.querySelector('video[id^="hivePlayer"]');
    },

    // content.js 확장점: video.duration 이 Infinity(MSE)라 직접 못 쓴다.
    //  플레이어 상태 객체가 총 길이·현재 위치·재생 여부를 정확히 준다.
    getPlayback() {
      const s = fresh();
      if (!s) return null;
      return {
        currentTime: s.positionSec || 0,
        duration: s.durationSec || 0,
        isPlaying: !!s.isPlaying,
        loaded: !!s.hasStarted && s.durationSec > 0,
      };
    },

    extractVideo() {
      const s = fresh();
      if (!s) {
        // 플레이어가 아직 상태를 안 내놨다 — 최소한 작품명은 문서 제목에서
        const t = (document.title || '').split('|')[0].trim();
        return { seriesName: t && t !== 'Disney+' ? t : SITE_NAME, episodeTitle: '', episodeNumber: '', thumbnail: '' };
      }
      const ep = splitEpisode(s.subtitle);
      // 재생 화면에도 다음화 트레이 등에 /play/ 링크가 뜬다 — 지나갈 때마다 주워 둔다
      harvestEpisodeThumbs();
      // 그 화의 스틸을 기억해 뒀으면 그걸 쓰고, 없으면 시리즈 아트워크로 떨어진다
      const epThumb = epThumbs.get(playIdOf(location.pathname)) || '';
      return {
        seriesName: s.title || SITE_NAME,
        episodeTitle: ep.title,
        episodeNumber: ep.number,
        thumbnail: epThumb || IMG(s.imageId),
      };
    },

    extractBrowsing() {
      harvestEpisodeThumbs();   // 목록을 지나갈 때마다 에피소드 썸네일을 모아 둔다
      const p = bare();
      let page = T('page.browsing');
      let detail = '';
      let banner = '';
      let bannerBg = '';   // 있으면 확장 워커가 이 배경 위에 banner 를 얹어 합성한다

      // 문서 제목이 "작품명 | 디즈니+" 형식 (og 태그·JSON-LD 는 없다)
      const docName = (document.title || '').split('|')[0].trim();
      const named = docName && !/^(Disney\+|디즈니\+)$/i.test(docName) ? docName : '';

      if (/^\/browse\/entity-/.test(p)) {
        page = T('page.work');
        detail = named;
        ({ banner, bannerBg } = heroBanner());
      }
      else if (/^\/browse\/page-/.test(p)) {
        // 여기도 상단에 작품 로고가 크게 뜬다 (유빈 요청 — 예도 옮겨줘).
        page = T('page.workList');
        detail = named;
        ({ banner, bannerBg } = heroBanner());
      }
      else if (/^\/browse\/movies/.test(p)) page = T('page.movies');
      else if (/^\/browse\/series/.test(p)) page = T('page.seriesList');
      else if (/^\/browse\/originals/.test(p)) page = T('page.originals');
      else if (/^\/browse\/watchlist/.test(p)) page = T('page.watchlist');
      else if (/^\/browse\/search/.test(p)) page = T('page.search');
      else if (/^\/browse\/?$/.test(p)) page = T('page.workList');
      else if (p === '/home' || p === '/') page = T('page.home');
      else {
        // 남은 /browse/{슬러그} 는 브랜드 허브로 본다
        //  (disney · pixar · marvel · star-wars · national-geographic · disneyplus · star …)
        //  목록 경로는 위에서 이미 걸러졌으므로, 새 브랜드가 생겨도 자동으로 잡힌다.
        const m = p.match(/^\/browse\/([a-z0-9-]+)\/?$/);
        if (m && !LIST_ROUTES.includes(m[1])) {
          // 윗줄은 서비스 이름, 아랫줄은 브랜드 이름 (유빈 요청)
          //   Disney+
          //   PIXAR
          page = SITE_NAME;
          detail = BRAND_NAME[m[1]] || prettySlug(m[1]);
          // 올린 에셋(마블)이 있으면 그것, 없으면 페이지 히어로를 그대로 (로고+배경 합성)
          if (BRAND_ASSET[m[1]]) { banner = BRAND_ASSET[m[1]]; bannerBg = ''; }
          else ({ banner, bannerBg } = heroBanner());
        }
      }

      if (detail === page) detail = '';
      return { page, detail, banner, bannerBg };
    },

    // 정주행: 디즈니+ 는 연속 재생이 사이트 기본 기능이라 우리가 이동시키지 않는다.
    //  빈 문자열을 주면 content.js 가 "다음 화 없음"으로 보고 개입하지 않는다.
    //  (플레이어가 알아서 넘긴 뒤 우리는 새 상태를 그대로 표시하면 된다)
    findNextEpisode() {
      return '';
    },
  };

  console.log('[TOKU RPC] Disney+ 추출기 로드됨 (실험)');
})();
