// ============================================================
//  TOKU RPC — TSUBURAYA IMAGINATION 추출기
//  imagination.m-78.jp 전용. content.js(공통 코어)가 사용하는
//  window.TOKU_SITE 인터페이스를 정의한다.
//
//  ── 패턴 DB(pattern-db.json) 분석 결과 반영 ──
//  영상 재생:   /video/{id}
//               og:title = "作品名第N話 「タイトル」｜TSUBURAYA IMAGINATION..."
//               썸네일 = og:image (static-constents imagecache)
//  작품 상세:   /series/{slug}   (slug = b00abc0 / Yomi_xxx / Series_xxx)
//               og:title / headings[0] = 작품명 (『』로 감싼 경우 있음)
//  プラネット:   /planet/{slug}   (slug = the_ultraman 등 영문)
//               og:title = "【プラネット】작품名", headings[0] = 작품名
//  プラネット목록: /planet
//  작품 목록:   /list/{type}     (new/rental/free/color_timer/soon_end/...)
//  마이리스트:  /mylist, /mylist/viewing
//  뉴스:        /news, /contents/{id}
//  추천:        /recommend
//  검색:        /search, /search/{category}, /search/video/{id}
// ============================================================

(() => {
  const SITE_NAME = 'TSUBURAYA IMAGINATION';
  const TITLE_SUFFIX = /[｜|]\s*TSUBURAYA[\s　]*IMAGINATION.*$/;
  const EP_SPLIT = /(第\s*[0-9０-９一二三四五六七八九十百千]+\s*[話章夜楽]|EPISODE\s*\d+|Episode\s*\d+|EP\.?\s*\d+|#\d+)/;

  function getMeta(prop) {
    const el = document.querySelector(`meta[property="${prop}"]`);
    return (el && el.content) ? el.content.trim() : '';
  }

  // 페이지 제목 (사이트명 suffix 제거)
  //  SPA로 다음 화가 시작되면 og:title이 낡은 값으로 남을 수 있어(TTFC에서 실측)
  //  document.title을 우선하고 og는 폴백으로 쓴다.
  function getPageTitle() {
    const clean = (s) => (s || '').replace(TITLE_SUFFIX, '').trim();
    const dt = clean(document.title);
    if (dt && !/^TSUBURAYA[\s　]*IMAGINATION$/i.test(dt)) return dt;
    const og = clean(getMeta('og:title'));
    if (og && !/^TSUBURAYA[\s　]*IMAGINATION$/i.test(og)) return og;
    return '';
  }

  function getH1() {
    const EXCLUDE = ['TSUBURAYA', 'IMAGINATION', 'ログイン', '会員', 'エラー', 'メニュー'];
    for (const h of document.querySelectorAll('h1, h2')) {
      const t = (h.textContent || '').trim();
      if (t && t.length >= 2 && t.length < 80 && !EXCLUDE.some(w => t.includes(w))) return t;
    }
    return '';
  }

  // 『작품명』 / 【プラネット】작품명 등 장식 제거
  function cleanName(s) {
    if (!s) return '';
    return s
      .replace(/^【[^】]*】\s*/, '')      // 【プラネット】
      .replace(/^[『「]\s*/, '').replace(/\s*[』」]$/, '')  // 『』「」
      .trim();
  }

  // 키 아트 이미지 (공통 OGP 제외)
  function keyArtImage() {
    const og = getMeta('og:image');
    return (og && !/assets\/images\/ogp/i.test(og)) ? og : '';
  }

  // プラネット 원형 아이콘 — alt가 프라네트명과 일치하는 정사각(60px+) 이미지
  //  (프라네트 페이지 구조: 1024×399 히어로 배너 + 160×160 원형 아이콘, alt 동일)
  function planetIcon(title) {
    if (!title) return '';
    for (const im of document.querySelectorAll('img')) {
      const w = im.offsetWidth, h = im.offsetHeight;
      if (w >= 60 && w === h && (im.alt || '').trim() === title) {
        return im.currentSrc || im.src || '';
      }
    }
    return '';
  }

  // "作品名第N話 「タイトル」" → { seriesName, episodeNumber, episodeTitle }
  function parseVideoTitle(raw) {
    let seriesName = raw || '', episodeNumber = '', episodeTitle = '';
    if (!raw) return { seriesName, episodeNumber, episodeTitle };

    const m = raw.match(EP_SPLIT);
    if (m) {
      episodeNumber = m[1].replace(/\s+/g, '');
      seriesName = raw.slice(0, m.index).trim();
      let rest = raw.slice(m.index + m[1].length).trim();
      const tm = rest.match(/^[「『]\s*(.+?)\s*[」』]/);
      episodeTitle = tm ? tm[1].trim() : rest.replace(/^[「『]|[」』]$/g, '').trim();
    } else {
      const tm = raw.match(/^(.*?)\s*[「『]\s*(.+?)\s*[」』]\s*$/);
      if (tm && tm[1].trim()) { seriesName = tm[1].trim(); episodeTitle = tm[2].trim(); }
      else seriesName = raw.trim();
    }
    return { seriesName: cleanName(seriesName), episodeNumber, episodeTitle };
  }

  // ════════════════════════════════════════
  //  인터페이스
  // ════════════════════════════════════════
  window.TOKU_SITE = {
    id: 'imagination',
    siteName: SITE_NAME,

    getVideoElement() {
      return document.querySelector('video');
    },

    // 영상 재생: /video/{id}  (검색 결과 /search/video/{id}는 제외)
    isPlaybackUrl() {
      return /^\/video\/[^/]+/.test(location.pathname);
    },

    extractVideo() {
      const parsed = parseVideoTitle(getPageTitle() || getH1());

      // 썸네일: og:image (SPA 내비로 og:url이 어긋나면 video.poster 폴백)
      let thumbnail = '';
      const ogUrl = getMeta('og:url');
      const ogImage = getMeta('og:image');
      const video = this.getVideoElement();
      const sameUrl = !ogUrl || ogUrl.includes(location.pathname);
      if (ogImage && sameUrl) thumbnail = ogImage;
      else if (video && video.poster) thumbnail = video.poster;
      else if (ogImage) thumbnail = ogImage;
      if (/assets\/images\/ogp/i.test(thumbnail)) thumbnail = '';

      return {
        seriesName: parsed.seriesName || SITE_NAME,
        episodeTitle: parsed.episodeTitle,
        episodeNumber: parsed.episodeNumber,
        thumbnail,
      };
    },

    rememberContext() { /* IMAGINATION은 영상 og:title에 작품명 포함 → 보강 불필요 */ },

    // 정주행: 영상 페이지의 에피소드 목록에서 현재 다음 항목
    //  (지역차단으로 원격 검증 불가 상태에서 작성 — VPN 환경에서 실측 보정 예정)
    findNextEpisode() {
      const cur = location.pathname;  // /video/{id}
      if (!/^\/video\/[^/]+/.test(cur)) return '';
      const hrefs = [...document.querySelectorAll('a')]
        .map(a => a.getAttribute('href') || '')
        .filter(h => /^\/video\/[^/]+/.test(h.split('?')[0]));
      const uniq = [...new Set(hrefs.map(h => h.split('?')[0]))];
      const idx = uniq.findIndex(h => h === cur);
      if (idx >= 0 && uniq[idx + 1]) return uniq[idx + 1];
      return '';
    },

    // ── 플레이어 자체 "다음 동영상" 오버레이 ──
    //  실측: imagination은 streaks(video.js) 플레이어가 자체 연속재생 기능을 갖고 있어
    //  "N秒後に次の動画を再生" 카운트다운 + [キャンセル][今すぐ再生] 버튼을 띄운다.
    //  즉 다음 화 이동은 플레이어가 담당하므로, 우리는 대기시간만 건너뛰면 된다.
    nextOverlay() {
      const t = document.body ? document.body.innerText || '' : '';
      if (!/秒後に次の動画/.test(t)) return null;
      const btns = [...document.querySelectorAll('button, a, [role="button"]')];
      const playNow = btns.find(b => /今すぐ再生/.test((b.textContent || '').trim()));
      return { playNow: playNow || null };
    },

    // 카운트다운을 건너뛰고 즉시 다음 화 재생 (있으면 true)
    clickPlayNow() {
      const ov = this.nextOverlay();
      if (ov && ov.playNow) { ov.playNow.click(); return true; }
      return false;
    },

    // 정주행: 유사 전체화면 — 플레이어 컨테이너를 화면 가득 고정
    //  (창 자체는 background의 chrome.windows fullscreen이 담당.
    //   진짜 Fullscreen API는 유저 제스처 없이는 호출 불가라 이 방식 사용)
    //  ※ 실측으로 확정한 사항 (2026-07-26)
    //   - 대상은 `[class*="video-container-"]` — 썸네일·플레이어·컨트롤을 모두 담은 박스.
    //     .video-js만 키우면 그건 투명 레이어라 화면이 그대로 보인다(실측 실패).
    //   - data-속성 방식은 React 재렌더가 속성을 지워서 무효 → 클래스 기반 CSS로 고정.
    //   - 브라우저 창 자체는 background의 chrome.windows fullscreen이 담당
    //     (Fullscreen API는 유저 제스처 없이 재요청 불가: "Permissions check failed" 실측).
    bingeFullscreen() {
      let st = document.getElementById('tokuBingeFs');
      if (!st) {
        st = document.createElement('style');
        st.id = 'tokuBingeFs';
        document.head.appendChild(st);
      }
      //  중간 컨테이너(streaks-player-container)가 원래 너비(856px)로 고정돼 있어
      //  바깥만 키우면 플레이어가 화면 절반만 차지한다 → 계층 전체를 100%로 늘린다.
      st.textContent =
        '[class*="video-container-"]{position:fixed !important;inset:0 !important;' +
        'width:100vw !important;height:100vh !important;max-width:none !important;' +
        'max-height:none !important;margin:0 !important;padding:0 !important;' +
        'z-index:2147480000 !important;background:#000 !important}' +
        '[class*="video-container-"] > *{width:100% !important;height:100% !important;' +
        'max-width:none !important;max-height:none !important}' +
        '[class*="video-container-"] [class*="streaks-player-container"]{' +
        'width:100% !important;height:100% !important;max-width:none !important}' +
        '[class*="video-container-"] .video-js{width:100% !important;height:100% !important;' +
        'max-width:none !important;max-height:none !important;padding-top:0 !important}' +
        '[class*="video-container-"] video{width:100% !important;height:100% !important;' +
        'object-fit:contain !important}' +
        'html,body{overflow:hidden !important}' +
        'html.toku-nocursor, html.toku-nocursor *{cursor:none !important}';
    },

    // 유사 전체화면이 걸려 있는지
    isBingeFullscreen() {
      return !!document.getElementById('tokuBingeFs');
    },

    extractBrowsing() {
      const path = location.pathname;
      let page = 'ブラウジング中';
      let detail = '';
      let banner = '';
      const keyArt = keyArtImage();

      // 영상 페이지인데 video 미준비 → isPlaybackUrl이 잡으므로 여기 안 옴

      // 작품 상세: /series/{slug}
      if (/^\/series\/[^/]+/.test(path)) {
        page = '作品ページ';
        detail = cleanName(getPageTitle() || getH1());
        banner = keyArt;
      }
      // プラネット 상세: /planet/{slug} (slug 영문 → 디코드 불가, 제목은 헤딩/og)
      //  배너: 큰 키아트 대신 프라네트 원형 아이콘 우선 (유빈 요청)
      else if (/^\/planet\/[^/]+/.test(path)) {
        page = 'プラネット';
        detail = cleanName(getH1() || getPageTitle());
        banner = planetIcon(detail) || keyArt;
      }
      // プラネット 목록
      else if (/^\/planet\/?$/.test(path)) {
        page = 'プラネット一覧';
      }
      // 작품 목록: /list/{type}
      else if (/^\/list\//.test(path)) {
        page = '作品一覧';
        detail = cleanName(getPageTitle() || getH1());
      }
      // 마이리스트
      else if (/^\/mylist/.test(path)) {
        page = 'マイリスト';
      }
      // 뉴스 목록
      else if (/^\/news\/?$/.test(path)) {
        page = 'ニュース';
      }
      // 뉴스/읽을거리 상세: /contents/{id}
      else if (/^\/contents\//.test(path)) {
        page = 'ニュース・読み物';
        detail = cleanName(getPageTitle() || getH1());
      }
      // 추천
      else if (/^\/recommend/.test(path)) {
        page = 'おすすめ';
      }
      else if (path.startsWith('/search')) page = '作品を検索中';
      else if (path.startsWith('/favorite')) page = 'お気に入り';
      else if (path.startsWith('/faq') || path.startsWith('/guide') || path.startsWith('/howtocube')) page = 'ヘルプ';
      else if (path.startsWith('/auth')) page = 'ログイン中';
      else if (path === '/' || path === '') page = 'ホーム';

      if (detail === page) detail = '';
      return { page, detail, banner };
    },
  };

  console.log('[TOKU RPC] IMAGINATION 추출기 로드됨');
})();
