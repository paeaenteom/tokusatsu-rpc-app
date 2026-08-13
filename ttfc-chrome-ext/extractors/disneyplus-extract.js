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

  // ── 페이지 월드에서 오는 재생 상태 ──
  let snap = null;
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.channel !== CHANNEL) return;
    snap = d.payload || null;
  });
  // 늦게 붙었을 때를 대비해 한 번 요청해 둔다
  try { window.postMessage({ channel: CHANNEL + '_REQ' }, location.origin); } catch (e) {}

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

  // 브랜드 허브 페이지의 로고 이미지 (alt 에 브랜드명이 들어있다)
  function brandLogoUrl() {
    const el = document.querySelector('[data-testid="he-title-treatment"]');
    const img = el && (el.tagName === 'IMG' ? el : el.querySelector('img'));
    return img ? (img.currentSrc || img.src || '') : '';
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
      return {
        seriesName: s.title || SITE_NAME,
        episodeTitle: ep.title,
        episodeNumber: ep.number,
        thumbnail: IMG(s.imageId),
      };
    },

    extractBrowsing() {
      const p = bare();
      let page = T('page.browsing');
      let detail = '';
      let banner = '';

      // 문서 제목이 "작품명 | 디즈니+" 형식 (og 태그·JSON-LD 는 없다)
      const docName = (document.title || '').split('|')[0].trim();
      const named = docName && !/^(Disney\+|디즈니\+)$/i.test(docName) ? docName : '';

      if (/^\/browse\/entity-/.test(p)) {
        page = T('page.work');
        detail = named;
        const img = document.querySelector('[data-testid="details-page-background-image-responsive"] img, [data-testid="details-page-background-image"] img');
        if (img) banner = img.currentSrc || img.src || '';
      }
      else if (/^\/browse\/page-/.test(p)) { page = T('page.workList'); detail = named; }
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
          banner = BRAND_ASSET[m[1]] || brandLogoUrl();     // 올린 에셋 우선, 없으면 사이트 로고
        }
      }

      if (detail === page) detail = '';
      return { page, detail, banner };
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
