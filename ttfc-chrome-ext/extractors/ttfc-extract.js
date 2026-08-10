// ============================================================
//  TOKU RPC — TTFC (東映特撮ファンクラブ) 추출기
//  pc.tokusatsu-fc.jp 전용. content.js(공통 코어)가 사용하는
//  window.TOKU_SITE 인터페이스를 정의한다.
//
//  ── 패턴 DB(pattern-db.json) 분석 결과 반영 ──
//  영상 재생:    /movies/{id}/movie-stories/{epId}   (video.js)
//                → og:title = 에피소드명("第2話 盗まれた日本列島")
//                  작품명은 페이지에 없음 → 같은 movieId의 에피소드 선택
//                  페이지에서 본 작품명을 sessionStorage로 보강
//  라이브 시청:  /lives/{id}/live-deliveries/{epId}
//                → 제목은 headings[0]
//  작품 상세:    /works/{id}/contents          (og:title = 작품명)
//  에피소드 선택: /movies/{id}/movie-stories    (og:title = 작품/영화명)
//  특집:         /specials/{id}/contents
//  시리즈 목록:  /series/{1~6}/works            (배너 있음)
// ============================================================

(() => {
  // 페이지 종류 이름은 다국어 표에서 가져온다 (Discord 에 그대로 표시된다)
  const T = (key) => (self.TOKU_I18N ? self.TOKU_I18N.t(key) : key);

  // 시리즈 카테고리 → Discord Art Assets 키
  //  사이트의 banner_series_N.png는 외부 접근이 막혀 디스코드가 못 불러온다.
  //  → Dev Portal에 올린 에셋 키를 그대로 쓴다(재호스팅 불필요, 즉시 표시).
  //  에셋이 없는 카테고리(不思議コメディー·その他)는 사이트 배너 이미지를 그대로 쓴다.
  //  이 URL은 디스코드가 직접 못 불러오지만(외부 차단), 확장이 바이트를 뽑아
  //  앱이 재호스팅하는 경로를 타므로 정상 표시된다. (같은 오리진이라 추출 가능)
  const SERIES = {
    '1': { name: '仮面ライダー', banner: 'kamen_rider_logo' },
    '2': { name: 'スーパー戦隊', banner: 'super_sentai_series_logo' },
    '3': { name: 'メタルヒーロー', banner: 'metal_hero_series_logo' },
    '4': { name: '東映不思議コメディー', banner: 'https://pc.tokusatsu-fc.jp/assets/img/top/banner_series_4.png' },
    '5': { name: 'その他', banner: 'https://pc.tokusatsu-fc.jp/assets/img/top/banner_series_5.png' },
    '6': { name: 'PROJECT R.E.D.', banner: 'project_r_e_d_logo' },
  };

  // 에피소드 번호 패턴 (앞에서 시작)
  //  작품마다 화수 단위가 달라 실제로 겪은 사례를 반영:
  //   ・第4カイ (젠카이저 — 두 글자 단위라 [話章…]로는 'カ'만 잘렸음)
  //   ・第弍話 (오메가혼 — 한자 숫자)
  //   ・ＲＯＵＮＤ１ (고쥬저 다이제스트)
  //  ※ 여러 글자 단위(カイ)를 한 글자 단위보다 앞에 둬야 통째로 매치된다.
  const EP_NUMBER = /^(Case\s*\d+|ＲＯＵＮＤ\s*[0-9０-９]+|ROUND\s*\d+|第\s*[0-9０-９一二三四五六七八九十百千壱弐弍参伍拾]+\s*(?:カイ|話|章|夜|幕|楽|回|駅|忍)|[一二三四五六七八九十百千]+之巻|Ｍｉｓｓｉｏｎ\s*[0-9０-９]+(?:[－ー\-][0-9０-９]+)?|Mission\s*\d+(?:-\d+)?|EP\.?\s*\d+|Episode\s*\d+|#?\d+話)/i;

  // 작품명/안내문에서 제외할 UI 텍스트
  const UI_NOISE = ['専用', '購入', '限定', 'ログ', '絞り込み', 'カテゴリー', 'ジャンル',
    '会員', '視聴', 'エピソード', 'スマートフォン', '購入方法', 'ログアウト', '連携'];

  function getMetaT(prop) {
    const el = document.querySelector(`meta[property="${prop}"]`);
    return (el && el.content) ? el.content.trim() : '';
  }

  // 현재 페이지 제목 ("제목｜東映特撮ファンクラブ" 형태에서 앞부분)
  //  ⚠ 사이트 자체 연속재생으로 다음 화가 시작될 때 URL과 document.title은 갱신되지만
  //    og:title은 서버 렌더링 값 그대로 남는다(실측: URL 47960/4화인데 og는 3화).
  //    og만 읽으면 다음 화로 넘어가도 계속 1화로 표시되므로 document.title을 우선한다.
  function ogSeriesName() {
    const strip = (s) => (s || '').split(/[｜|]/)[0].trim();
    const dt = strip(document.title);
    if (dt && dt !== '東映特撮ファンクラブ') return dt;
    const og = strip(getMetaT('og:title'));
    if (og && og !== '東映特撮ファンクラブ') return og;
    return '';
  }

  // 헤딩에서 실제 제목 후보 (UI 텍스트 제외)
  function firstRealHeading() {
    for (const h of document.querySelectorAll('h1, h2, h3')) {
      const t = (h.textContent || '').trim();
      if (t && t.length >= 2 && t.length < 80 && !UI_NOISE.some(w => t.includes(w))) return t;
    }
    return '';
  }

  // 에피소드 텍스트 → { number, title }
  function parseEpisode(raw) {
    if (!raw) return { number: '', title: '' };
    const m = raw.match(EP_NUMBER);
    if (m) {
      const number = m[1].replace(/\s+/g, ' ').trim();
      let rest = raw.slice(m[0].length).trim();
      const tm = rest.match(/^[「『\[]\s*(.+?)\s*[」』\]]$/);
      const title = tm ? tm[1].trim() : rest.replace(/^[「『\[]|[」』\]]$/g, '').trim();
      return { number, title };
    }
    return { number: '', title: raw.trim() };
  }

  // movieId / liveId 기반 작품명 캐시 (작품→영상 이동 시 보강)
  function cacheKey() {
    const mv = location.pathname.match(/\/movies\/(\d+)/);
    if (mv) return 'toku_ttfc_mv_' + mv[1];
    const lv = location.pathname.match(/\/lives\/(\d+)/);
    if (lv) return 'toku_ttfc_lv_' + lv[1];
    return '';
  }
  function rememberSeries(name) {
    const k = cacheKey();
    if (k && name) { try { sessionStorage.setItem(k, name); } catch (e) {} }
  }
  function recallSeries() {
    const k = cacheKey();
    if (k) { try { return sessionStorage.getItem(k) || ''; } catch (e) {} }
    return '';
  }

  function detectSeries() {
    const sm = location.pathname.match(/\/series\/(\d+)/);
    if (sm && SERIES[sm[1]]) return SERIES[sm[1]];
    const imgs = document.querySelectorAll('img[src*="banner_series_"]');
    for (const img of imgs) {
      const m = (img.src || '').match(/banner_series_(\d+)\.png/);
      if (m && SERIES[m[1]]) return SERIES[m[1]];
    }
    return null;
  }

  // 현재 재생 중인 에피소드의 썸네일 — 에피소드 카드(href의 epId)와 URL 매칭
  //  ※ 구 방식(再生中 마커에서 부모로 걸어 올라가기)은 리스트 컨테이너까지 올라가
  //    "첫 번째" 이미지(=1화 썸네일)를 집는 버그가 있었음 (젠카이저 3화에서 재현 확인).
  let _epThumbCache = { key: '', src: '' };  // 에피소드 단위 캐시 (2초 주기 전체 스캔 방지)

  function currentEpisodeThumb() {
    const m = location.pathname.match(/\/(movie-stories|live-deliveries)\/(\d+)/);
    if (!m) return '';
    const key = m[1] + '/' + m[2];
    if (_epThumbCache.key === key && _epThumbCache.src) return _epThumbCache.src;
    const re = new RegExp('/' + m[1] + '/' + m[2] + '(\\?|$)');
    for (const a of document.querySelectorAll('a')) {
      const href = a.getAttribute('href') || '';
      if (re.test(href)) {
        const img = a.querySelector('img');
        const src = img ? (img.currentSrc || img.src || '') : '';
        if (src.includes('cloudfront.net')) {
          _epThumbCache = { key, src };
          return src;
        }
      }
    }
    return '';  // 못 찾으면 캐시 안 함 — 렌더 완료 후 재시도
  }

  // 폴백: "再生中" 마커가 붙은 카드(<a>) 내부의 이미지만 사용 (컨테이너로 안 올라감)
  function markerThumbnail() {
    for (const el of document.querySelectorAll('p, span, div')) {
      if (el.children.length === 0 && (el.textContent || '').trim() === '再生中') {
        const card = el.closest('a');
        const img = card && card.querySelector('img');
        const src = img ? (img.currentSrc || img.src || '') : '';
        if (src.includes('cloudfront.net')) return src;
        return '';
      }
    }
    return '';
  }

  // og:image — 컨텐츠 이미지(cloudfront)일 때만 (공통 사이트 OGP 제외)
  function ogContentImage() {
    const og = getMetaT('og:image');
    return (og && og.includes('cloudfront.net')) ? og : '';
  }

  // ════════════════════════════════════════
  //  인터페이스
  // ════════════════════════════════════════
  window.TOKU_SITE = {
    id: 'ttfc',
    siteName: '東映特撮ファンクラブ',

    getVideoElement() {
      return document.querySelector('.video-js video') || document.querySelector('video');
    },

    // 영상 재생 + 라이브 시청
    isPlaybackUrl() {
      return /\/movies\/\d+\/movie-stories\/\d+/.test(location.pathname) ||
             /\/lives\/\d+\/live-deliveries\/\d+/.test(location.pathname);
    },

    extractVideo() {
      const isLive = /\/lives\/\d+\/live-deliveries\/\d+/.test(location.pathname);

      // 썸네일 우선순위: 현재 에피소드 카드 매칭(항상 정확) → og:image(직접 진입 시 정확)
      //                → 再生中 마커 카드(폴백)
      const thumbnail = currentEpisodeThumb() || ogContentImage() || markerThumbnail();

      if (isLive) {
        // 라이브: 제목은 headings[0] (og:title은 generic)
        const title = firstRealHeading() || ogSeriesName();
        const ep = parseEpisode(title);
        const seriesName = recallSeries() || (ep.number ? '' : title) || 'ライブ配信';
        return {
          seriesName: seriesName || 'ライブ配信',
          episodeTitle: ep.number ? ep.title : (seriesName === title ? '' : title),
          episodeNumber: ep.number,
          thumbnail,
        };
      }

      // 영상: og:title = 에피소드명 → 번호/제목 분리, 작품명은 캐시 보강
      const epText = ogSeriesName() || firstRealHeading();
      const ep = parseEpisode(epText);
      const seriesName = recallSeries() || '';
      return {
        seriesName: seriesName || this.siteName,
        episodeTitle: ep.title || '',
        episodeNumber: ep.number || '',
        thumbnail,
      };
    },

    // 정주행: 현재 에피소드 카드의 다음 카드 URL (직접 진입 시 자동 재생됨 — 실측)
    findNextEpisode() {
      const m = location.pathname.match(/\/movie-stories\/(\d+)/);
      if (!m) return '';  // 라이브는 다음 화 개념 없음
      const re = new RegExp('/movie-stories/' + m[1] + '(\\?|$)');
      const cards = [...document.querySelectorAll('a')]
        .filter(a => /\/movie-stories\/\d+/.test(a.getAttribute('href') || ''));
      const idx = cards.findIndex(a => re.test(a.getAttribute('href') || ''));
      if (idx >= 0 && cards[idx + 1]) return cards[idx + 1].href;  // 절대 URL
      return '';
    },

    // 영상 진입 전 작품명 기억 (작품/에피소드 선택 페이지에서)
    rememberContext() {
      const path = location.pathname;
      // 에피소드 선택: /movies/{id}/movie-stories (og:title = 작품/영화명)
      if (/\/movies\/\d+\/movie-stories\/?$/.test(path)) {
        const name = ogSeriesName();
        if (name) rememberSeries(name);
      }
      // 라이브 목록/상세 진입 전 — 작품 상세에서 라이브 제목 기억은 생략
    },

    extractBrowsing() {
      const path = location.pathname.toLowerCase();
      let page = T('page.browsing');
      let detail = '';
      let banner = '';

      // 에피소드 선택: /movies/{id}/movie-stories — 배너는 페이지 메인 비주얼(og:image)
      if (/\/movies\/\d+\/movie-stories\/?$/.test(path)) {
        page = T('page.work');
        detail = ogSeriesName();
        banner = ogContentImage();
        if (!banner) { const s = detectSeries(); if (s) banner = s.banner; }
        rememberSeries(detail);
      }
      // 작품 상세: /works/{id}/contents — 배너는 작품 메인 비주얼(og:image)
      else if (/\/works\/\d+/.test(path)) {
        page = T('page.work');
        detail = ogSeriesName();
        banner = ogContentImage();
        if (!banner) { const s = detectSeries(); if (s) banner = s.banner; }
      }
      // 특집: /specials/{id}/contents (og:title generic → 라벨만)
      else if (/\/specials\/\d+/.test(path)) {
        page = T('page.special');
        detail = ogSeriesName();
        banner = ogContentImage();
      }
      // 시리즈 목록: /series/{id}/works
      else if (/\/series\/\d+/.test(path)) {
        const s = detectSeries();
        page = T('page.seriesList');
        detail = s ? s.name : ogSeriesName();
        if (s) banner = s.banner;
      }
      else if (/\/articles\/\d+/.test(path)) { page = T('page.article'); detail = ogSeriesName(); }
      else if (path.includes('/search')) page = T('page.search');
      else if (path.includes('/favorite')) page = T('page.favorite');
      else if (path.includes('/histories') || path.includes('/history')) page = T('page.history');
      else if (path.includes('/purchase')) page = T('page.purchase');
      else if (path.includes('/settings')) page = T('page.settings');
      else if (path.includes('/notice')) page = T('page.notice');
      else if (path.includes('/qa') || path.includes('/help') || path.includes('/faq')) page = T('page.help');
      else if (path === '/new') page = T('page.new');
      else if (path === '/' || path === '') page = T('page.home');

      return { page, detail, banner };
    },
  };

  console.log('[TOKU RPC] TTFC 추출기 로드됨');
})();
