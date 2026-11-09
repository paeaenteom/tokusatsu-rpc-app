// ============================================================
//  치지직(CHZZK) 추출기
//
//  근거: 2026-10-11 공개 페이지가 내려받는 번들(chzzk.naver.com 의 index-*.js · player-vendor-*.js,
//  m.naver.com/shorts 의 main_*.js)을 직접 읽어 확인했다. 로그인·쿠키 없이 확인한 공개 구조다.
//
//   ・경로      /live/<채널ID 32자리>  /video/<번호>  /clips/<클립UID>  — 나머지는 둘러보기
//   ・플레이어  LIVE  div#live_player_layout.chzzk_player.type_live
//               VOD   div#player_layout.chzzk_player.type_vod
//     본 영상 = 레이아웃 안의 video.webplayer-internal-video (플레이어 라이브러리가 고정 이름으로 만든다)
//     광고    = 광고 SDK 가 따로 만드는 다른 <video> (GV 광고는 data-role="videoEl").
//     ⚠ 첫 번째 <video>·duration 이 유한/무한인지로 고르면 광고를 집는다 (실측: 라이브 페이지의 광고 23초).
//   ・메타      페이지가 og:title "<채널명> - <제목>" · og:image · og:url(지금 콘텐츠 주소) 를 갈아 끼운다.
//               SPA 이동 직후엔 이전 콘텐츠 값이 남아 있으므로 og:url 이 지금 주소와 같을 때만 믿는다.
//   ・LIVE 경과  정보 영역의 '<HH:MM:SS> 스트리밍 중' 은 페이지가 공식 openDate 로 1초마다 계산해 그린다
//               (방송 중일 때만 그린다). 이 숫자가 실제 시간과 같이 흐르는 걸 확인한 뒤에만 방송 시작 시각으로 쓴다.
//               플레이어 currentTime 은 타임머신(DVR) 창 안의 위치라 방송 경과가 아니다 — 쓰지 않는다.
//   ・클립      상위 문서엔 <video> 가 없다. iframe[title="CHZZK Clip Player"]
//               (https://m.naver.com/shorts/?serviceType=CHZZK&…&sdkTargetId=<임의값>) 가
//               부모 창으로 postMessage({ targetId, messageType:'shorts-viewer.event.*', payload }) 를 보낸다.
//                 content : { content:{ contentType:'VIDEO'|'AD', contentData:{ contentId, title, channelName, imageUrl, serviceType } } }
//                 playing : { status:'PLAY'|'PLAYING'|'PAUSE'|'ENDED', currentTime, duration(PLAYING 만), content }
//               m.naver.com 에는 스크립트를 넣지 않는다. 이 탭(치지직 부모)에서 출처·그 iframe·sdkTargetId·
//               serviceType·지금 주소의 클립UID 를 전부 맞춰 본 메시지만 쓴다 (일반 네이버 Shorts 는 안 받는다).
//
//  공식 Open API 는 Client ID/Secret 인증이 필요하다 — 쓰지 않는다 (Discord 애플리케이션 ID 와는 다른 것).
// ============================================================
(() => {
  const T = (k, v) => (self.TOKU_I18N ? self.TOKU_I18N.t(k, v) : k);
  const nudge = (why) => { if (self.TOKU_NUDGE) self.TOKU_NUDGE(why); };
  //  클립 iframe 메시지가 깨우는 즉시 전송은 한 박자 늦춰 한 번으로 모은다.
  //  ⚠ 우리 메시지 처리기가 페이지 것보다 먼저 돈다 — 다음 클립 소식을 받은 순간엔 페이지가 아직 주소를
  //    /clips/<새UID> 로 안 바꿨다. 바로 보내면 이전 클립 카드가 '재생 아님' 으로 한 번 나간다.
  let nudgeTimer = null;
  const nudgeSoon = (why) => {
    if (nudgeTimer) return;
    nudgeTimer = setTimeout(() => { nudgeTimer = null; nudge(why); }, 30);
  };

  const ORIGIN = 'https://chzzk.naver.com';
  const CLIP_ORIGIN = 'https://m.naver.com';       // 번들의 REACT_APP_CLIP_VIEWER_ORIGIN
  const CLIP_FRAME_SEL = 'iframe[title="CHZZK Clip Player"]';
  const CLIP_UID = /^[A-Za-z0-9_-]{4,64}$/;

  const GRACE_MS = 3000;          // 메타가 끝내 안 맞을 때 기다리는 한도 (그 뒤엔 아는 것만 보낸다)
  const OFFLINE_AFTER_OG_MS = 3000;  // 방송 정보가 뜬 뒤 이만큼 '스트리밍 중' 이 없으면 오프라인
  const OFFLINE_MAX_MS = 10000;   // 방송 정보조차 안 뜨면 이만큼 기다렸다가 오프라인으로 본다
  const ENDED_AFTER_MS = 5000;    // 보던 방송의 '스트리밍 중' 이 이만큼 사라지면 종료
  const UNAVAILABLE_AFTER_MS = 12000;  // 정보가 뜬 뒤 이만큼 재생할 영상이 안 생기면 '재생할 수 없음'
  const CLIP_STALE_MS = 8000;     // 클립 재생 소식(초당 ~4번)이 이만큼 끊기면 재생 중으로 보지 않는다

  // ── 경로 ──
  function routeOf(path) {
    let m;
    //  /live/<id>/simple 은 같은 라이브의 간단 보기다 (번들: ?simpleMode=true 가 여기로 옮긴다. 레이아웃·og 는 /live/<id> 와 같다)
    if ((m = /^\/live\/([0-9a-fA-F]{32})(?:\/simple)?\/?$/.exec(path))) return { kind: 'live', id: m[1] };
    if ((m = /^\/video\/(\d{1,15})\/?$/.exec(path))) return { kind: 'vod', id: m[1] };
    if ((m = /^\/clips\/([A-Za-z0-9_-]{4,64})\/?$/.exec(path)) && m[1] !== 'manual') return { kind: 'clip', id: m[1] };
    return null;
  }
  const pathOf = (r) => (r.kind === 'live' ? '/live/' : r.kind === 'vod' ? '/video/' : '/clips/') + r.id;

  //  콘텐츠가 바뀐 시각을 기억한다 (메타가 따라올 때까지의 대기·오프라인 판정 기준)
  let routeKey = '';
  let routeAt = 0;
  //  앞선 주소들에서 본 문서 제목 (공백 정리본). 다시보기 대체 제목이 '이전 영상 제목' 이 아님을 가리는 데 쓴다.
  //  ⚠ 가려진 탭에서 두 번 넘어가면 문서 제목은 바로 앞 영상, og 는 그보다 앞 영상 것이라 둘을 비교하는 것만으로는
  //    이전 제목을 걸러내지 못했다 (2026-10-12 검토).
  const prevTitles = [];
  let lastTitle = '';
  function mark() {
    const r = routeOf(location.pathname);
    const key = r ? r.kind + ':' + r.id : 'page:' + location.pathname;
    const nowTitle = normTitle(docTitleCore());
    if (key !== routeKey) {
      if (lastTitle && prevTitles.indexOf(lastTitle) < 0) {
        prevTitles.push(lastTitle);
        if (prevTitles.length > 8) prevTitles.shift();
      }
      routeKey = key;
      routeAt = Date.now();
      lsCache = null;
      liveObs = null;
      ogSince = { key: '', at: 0 };
      //  '방송 종료'·'재생할 수 없음' 판정은 이번 방문에서 본 것만으로 한다 —
      //  예전에 본 같은 방송으로 돌아온 직전(정보 영역이 아직 안 그려짐)을 종료로 오판하지 않게
      seenOpen = { cid: '', lastUpAt: 0 };
      noSrc = { key: '', since: 0 };
    }
    lastTitle = nowTitle;
    return r;
  }

  // ── og 메타 ──
  //  같은 속성이 둘 이상이면 어느 게 지금 것인지 모른다 → 믿지 않는다
  //  한 번의 상태 수집 안에서 15번쯤 불린다 — 그때마다 문서 전체(채팅 포함)를 훑지 않게 한 번 모아 두고,
  //  이번 작업이 끝나면(마이크로태스크) 버린다. 같은 작업 안에서는 페이지가 DOM 을 못 바꾸므로 값이 같고,
  //  다음 틱·이벤트는 항상 새로 읽는다.
  let metaMemo = null;
  function metaContent(prop) {
    if (!metaMemo) {
      metaMemo = new Map();
      for (const el of document.querySelectorAll('meta[property]')) {
        const p = el.getAttribute('property');
        metaMemo.set(p, metaMemo.has(p) ? null : el);   // 둘째부터는 null(믿지 않음)
      }
      Promise.resolve().then(() => { metaMemo = null; });
    }
    const el = metaMemo.get(prop);
    if (!el) return null;
    return (el.getAttribute('content') || '').trim();
  }
  function ogPath() {
    const url = metaContent('og:url');
    if (!url) return '';
    try {
      const u = new URL(url);
      return u.origin === ORIGIN ? (u.pathname.replace(/\/+$/, '') || '/') : '';
    } catch (e) { return ''; }
  }
  //  지금 주소의 콘텐츠 것일 때만 돌려준다
  function ogFor(r) {
    if (ogPath() !== pathOf(r)) return null;
    return { title: metaContent('og:title') || '', image: metaContent('og:image') || '' };
  }

  function httpsUrl(v) {
    return (typeof v === 'string' && /^https:\/\/[^\s"'<>]+$/.test(v)) ? v : '';
  }
  //  라이브 이미지 주소에는 크기 자리 '{type}' 가 있다 (페이지도 480 으로 채워 쓴다)
  const liveImage = (v) => httpsUrl(String(v || '').replace('{type}', '480'));
  const str = (v, max) => (typeof v === 'string' ? v.replace(/\s+/g, ' ').trim().slice(0, max || 300) : '');

  //  문서 제목 "<채널명> - <제목> - CHZZK" 의 앞부분 (번들: document.title 을 효과에서 바로 쓴다 —
  //  라이브는 상태 폴링 때마다 최신 제목으로, 다시보기는 영상 정보를 받으면). og 와 달리 가려진 탭에서도 바로 바뀐다.
  //  콘텐츠 주소가 안 실려 있으므로 단독으로 믿지 않는다 — 채널명이나 이전 og 와 맞춰 볼 때만 쓴다.
  function docTitleCore() {
    const t = String(document.title || '');
    if (!/ - CHZZK$/.test(t)) return '';
    const core = t.slice(0, -' - CHZZK'.length).trim();
    return core.indexOf(' - ') > 0 ? core : '';
  }
  //  비교용 — 브라우저는 문서 제목의 연속 공백을 하나로 줄여 돌려준다 (og:title 은 그대로)
  const normTitle = (s) => String(s || '').replace(/\s+/g, ' ').trim();

  // ── 화면 글자 ──
  //  가려진 안내문(.blind — '채널로 이동' 등)·아이콘은 빼고 보이는 글자만 모은다
  function visibleText(el) {
    let s = '';
    (function walk(n) {
      for (const c of n.childNodes) {
        if (c.nodeType === 3) s += c.nodeValue;
        else if (c.nodeType === 1) {
          const tag = c.localName;
          if (tag === 'svg' || tag === 'img' || tag === 'style' || tag === 'script' || tag === 'button') continue;
          if (c.classList && c.classList.contains('blind')) continue;
          walk(c);
        }
      }
    })(el);
    return s.replace(/\s+/g, ' ').trim();
  }

  //  채널 링크('/<32자리>')의 보이는 이름들. cid 를 주면 그 채널 것만.
  //  같은 콘텐츠에서 2초 안에 다시 물으면 앞의 결과를 쓴다 — 사이드바·채팅에 링크가 수백 개라
  //  틱(MAX 0.15초)마다 전부 읽는 게 CPU 를 꽤 썼다. 콘텐츠가 바뀌면(routeKey) 바로 새로 읽는다.
  let namesCache = { key: '', at: 0, v: [] };
  function channelNames(cid) {
    const ckey = routeKey + '|' + (cid || '');
    const nowMs = Date.now();
    if (namesCache.key === ckey && nowMs - namesCache.at < 2000) return namesCache.v;
    const names = [];
    const links = document.querySelectorAll('a[href^="/"], a[href^="' + ORIGIN + '/"]');
    let seen = 0;
    for (const a of links) {
      if (++seen > 600) break;
      let href = a.getAttribute('href') || '';
      if (href.indexOf(ORIGIN) === 0) href = href.slice(ORIGIN.length);
      const m = /^\/([0-9a-fA-F]{32})$/.exec(href);
      if (!m || (cid && m[1] !== cid)) continue;
      const t = visibleText(a);
      if (t && t.length <= 60 && names.indexOf(t) < 0) names.push(t);
    }
    //  채널 이름을 아직 못 찾았으면(페이지가 막 그리는 중) 다음 틱에 다시 읽게 둔다
    namesCache = names.length ? { key: ckey, at: nowMs, v: names } : { key: '', at: 0, v: [] };
    return names;
  }

  //  og:title = "<채널명> - <제목>" (번들에서 확인한 형식).
  //  화면에 있는 채널명 중 앞이 정확히 맞는 것으로 자른다 — 제목에 ' - ' 가 있어도 안전하다.
  //  채널명이 화면에 없으면 첫 ' - ' 에서 자른다 (채널명에 ' - ' 가 든 드문 경우만 틀린다).
  function splitOg(ogTitle, names) {
    const t = ogTitle || '';
    let best = '';
    for (const n of names) if (n && n.length > best.length && t.indexOf(n + ' - ') === 0) best = n;
    if (best) return { channel: best, title: t.slice(best.length + 3).trim() };
    const i = t.indexOf(' - ');
    if (i <= 0) return { channel: '', title: t.trim() };
    let ch = t.slice(0, i).trim();
    if (ch === 'undefined' || ch === 'null') ch = '';   // 페이지가 채널 정보 없이 만든 제목
    return { channel: ch, title: t.slice(i + 3).trim() };
  }

  // ══════════════════════════════════════
  //  LIVE
  // ══════════════════════════════════════

  //  정보 영역의 '<HH:MM:SS> 스트리밍 중'. React 가 시간·공백·문구를 각각 따로 텍스트 노드로 만든다
  //  (번들: <span>{uptime}{' '}{문구}</span>, uptime 은 글자만 그리는 Fragment). 채팅 글처럼 한 덩어리
  //  글자나 이모티콘(요소)이 섞인 줄은 걸리지 않는다. 경과 초를, 아니면 -1 을 돌려준다.
  const UPTIME_RE = /^(\d{2,}):([0-5]\d):([0-5]\d)$/;
  function uptimeOf(span) {
    const nodes = span.childNodes;
    if (!nodes || nodes.length < 2) return -1;
    for (const n of nodes) if (n.nodeType !== 3) return -1;
    const m = UPTIME_RE.exec(String(nodes[0].nodeValue || '').trim());
    if (!m) return -1;
    let rest = '';
    for (let i = 1; i < nodes.length; i++) rest += nodes[i].nodeValue || '';
    if (!rest.trim() || /\d:\d\d/.test(rest)) return -1;
    return Number(m[1]) * 3600 + Number(m[2]) * 60 + Number(m[3]);
  }

  //  제목(h2)이 가까이 있는 쪽을 고른다 — 같은 표시가 전체화면 머리글에도 있다
  function h2Near(el, levels) {
    let p = el.parentElement;
    for (let i = 0; p && i < levels; i++, p = p.parentElement) {
      const h = p.querySelector('h2');
      if (h) return h;
    }
    return null;
  }

  let upEl = null;
  let upScanAt = 0;
  //  매 틱 문서를 다 뒤지지 않는다. 찾아 둔 요소가 살아 있으면 그것만 다시 읽는다.
  function findUptime() {
    if (upEl && upEl.isConnected) {
      const sec = uptimeOf(upEl);
      if (sec >= 0) return { el: upEl, sec };
    }
    upEl = null;
    const now = Date.now();
    if (now - upScanAt < 700) return null;
    upScanAt = now;
    let first = null;
    for (const sp of document.querySelectorAll('span')) {
      const sec = uptimeOf(sp);
      if (sec < 0) continue;
      if (h2Near(sp, 4)) { upEl = sp; return { el: sp, sec }; }
      if (!first) first = { el: sp, sec };
    }
    if (first) upEl = first.el;
    return first;
  }

  //  방송 시작 시각 = 지금 - 경과. 경과 숫자가 실제 시간과 같은 속도로 늘어나는 것을
  //  2.5초 넘는 간격으로 두 번 확인한 뒤에만 확정한다. 멈춘 글자(가려진 탭의 타이머 지연·엉뚱한 글자)로
  //  시각을 지어내지 않는다. 한 번 확정하면 같은 방송인 동안 그대로 쓰고, 새 방송(크게 다름)이면 다시 확인한다.
  let liveStart = { cid: '', at: 0 };
  let liveObs = null;     // { cid, sec, t }
  function feedUptime(cid, sec) {
    if (!(sec > 0)) return;                    // 00:00:00 은 아직 계산 전일 수 있다
    const now = Date.now();
    //  표시는 초 단위로 버린 값이라 실제 시작은 (지금-경과-1초, 지금-경과] 안에 있다 → 가운데를 쓴다 (오차 ±0.5초)
    const est = now - sec * 1000 - 500;
    if (liveStart.cid === cid && Math.abs(est - liveStart.at) <= 3000) { liveObs = null; return; }
    //  확정값과 2분 넘게 다르면 같은 방송이 아니다(새 방송) — 새 시각을 확인할 때까지 옛 시각을 내보내지 않는다.
    if (liveStart.cid === cid && Math.abs(est - liveStart.at) > 120000) liveStart = { cid: '', at: 0 };
    //  2분 안쪽 차이는 같은 방송이다. 가려진 탭은 페이지의 1초 갱신이 1분까지 늦어져 경과가 일정하게 덜 나온다
    //  (= 시작이 늦게 계산된다). 그런 값으로 확정값을 덮지 않는다 — 더 이른 값(앞의 확정이 늦었던 것)만 고친다.
    if (liveStart.cid === cid && est > liveStart.at) { liveObs = null; return; }
    if (!liveObs || liveObs.cid !== cid) { liveObs = { cid, sec, t: now }; return; }
    const dt = (now - liveObs.t) / 1000;
    if (dt < 2.5) return;
    if (Math.abs((sec - liveObs.sec) - dt) <= 1.5) {
      liveStart = { cid, at: est };
      liveObs = null;
      nudge('치지직 방송 시작 시각 확인');
    } else {
      liveObs = { cid, sec, t: now };          // 흐름이 안 맞는다 — 이 관찰부터 다시
    }
  }

  // ── 플레이어 ──
  function layoutOf(kind) {
    const el = document.getElementById(kind === 'live' ? 'live_player_layout' : 'player_layout');
    if (!el || !el.classList.contains('chzzk_player')) return null;
    if (!el.classList.contains(kind === 'live' ? 'type_live' : 'type_vod')) return null;
    return el;
  }
  function mainVideo(kind) {
    const lay = layoutOf(kind);
    if (!lay) return null;
    let first = null;
    for (const v of lay.querySelectorAll('video.webplayer-internal-video')) {
      if (v.getAttribute('data-role') === 'videoEl') continue;   // 광고 SDK 표식 (방어)
      if (!first) first = v;
      if ((v.currentSrc || v.src) && v.readyState > 0) return v;
    }
    return first;
  }
  //  레이아웃 안에서 본 영상이 아닌 <video> 가 실제로 흐르고 있으면 광고 중이다
  function adPlaying(kind, main) {
    const lay = layoutOf(kind);
    if (!lay) return false;
    for (const v of lay.querySelectorAll('video')) {
      if (v === main || v.classList.contains('webplayer-internal-video')) continue;
      if (!v.paused && !v.ended && v.readyState > 2) return true;
    }
    return false;
  }

  //  재생 상태. 한 번 실제로 흐르기 시작한 콘텐츠는 잠깐 데이터가 모자라도(버퍼링) 재생 중으로 둔다 —
  //  그때마다 일시정지로 뒤집히면 카드 아이콘이 깜빡인다. 음소거는 보지 않는다(음소거 ≠ 일시정지).
  let playedKey = '';
  function mediaState(kind) {
    const v = mainVideo(kind);
    const ad = adPlaying(kind, v);
    if (!v) return { v: null, ad, loaded: false, playing: false };
    const moving = !v.paused && !v.ended;
    if (moving && !ad && v.readyState >= 3) playedKey = routeKey;
    const started = playedKey === routeKey;
    return {
      v, ad,
      loaded: !ad && (v.readyState >= 2 || started),
      playing: moving && !ad && (v.readyState >= 3 || started),
    };
  }

  // ── 재생할 수 없음 (연령·지역·유료 제한 안내 등) ──
  //  ⚠ 제한 안내 화면 자체는 해시 클래스·번역 문구라 고정해 찾지 않는다. 대신 확인할 수 있는 사실만 본다:
  //    이 콘텐츠의 정보(og)가 뜬 지 12초가 지나도 플레이어 레이아웃 안 어떤 <video>(광고 포함)에도
  //    소스가 없고 아무것도 흐르지 않는다. 소스가 생기거나 뭔가 흐르면 바로 풀린다.
  let noSrc = { key: '', since: 0 };
  function sourceless(kind) {
    const lay = layoutOf(kind);
    if (!lay) return true;
    for (const v of lay.querySelectorAll('video')) {
      if (v.currentSrc || v.src || v.readyState > 0 || (!v.paused && !v.ended)) return false;
    }
    return true;
  }
  function unavailable(r) {
    if (!ogFor(r) || !sourceless(r.kind)) { noSrc = { key: '', since: 0 }; return false; }
    const now = Date.now();
    if (noSrc.key !== routeKey) noSrc = { key: routeKey, since: now };
    return now - noSrc.since >= UNAVAILABLE_AFTER_MS;
  }

  // ── 방송 상태: open(방송 중) · ended(보던 방송이 끝남) · offline(꺼져 있음) · unavailable(재생할 수 없음) · unknown(아직 모름) ──
  let seenOpen = { cid: '', lastUpAt: 0 };   // 이 페이지에서 이 채널의 '스트리밍 중' 을 본 적이 있나
  let ogSince = { key: '', at: 0 };
  let lsCache = null;                        // 같은 틱 안에서 여러 번 불려도 문서를 한 번만 본다
  let lastLiveState = '';
  function liveState(r) {
    const now = Date.now();
    if (lsCache && lsCache.key === routeKey && now - lsCache.at < 250) return lsCache.v;
    let v = computeLiveState(r, now);
    //  방송 중인데 재생할 영상이 끝내 안 생긴다 → 볼 수 없는 방송 (제한 안내 등)
    if (v.state === 'open' && !v.ms.playing) {
      if (unavailable(r)) v = Object.assign({}, v, { state: 'unavailable' });
    } else {
      noSrc = { key: '', since: 0 };   // 재생 중이거나 판정 대상이 아니면 '소스 없음' 시계를 처음부터
    }
    lsCache = { key: routeKey, at: now, v };
    if (v.state !== lastLiveState) {
      const was = lastLiveState;
      lastLiveState = v.state;
      if (was) nudge('치지직 방송 상태 ' + was + ' → ' + v.state);
    }
    return v;
  }
  function computeLiveState(r, now) {
    const ms = mediaState('live');
    const og = ogFor(r);
    if (og && ogSince.key !== routeKey) ogSince = { key: routeKey, at: now };
    const up = findUptime();
    if (up) {
      seenOpen = { cid: r.id, lastUpAt: now };
      //  다른 방송에서 막 넘어온 순간엔 표시가 이전 방송 것일 수 있다 — 지금 방송 정보가 뜬 뒤에만 시각을 잰다
      if (og) feedUptime(r.id, up.sec);
      return { state: 'open', up, ms };
    }
    if (seenOpen.cid === r.id) {
      //  표시가 잠깐 사라진 것(화면 전환)과 방송 종료를 가른다: 재생도 멈춘 채 계속 없을 때만 종료
      if (!ms.playing && now - seenOpen.lastUpAt >= ENDED_AFTER_MS) {
        if (liveStart.cid === r.id) liveStart = { cid: '', at: 0 };   // 끝난 방송의 시작 시각은 다음 방송에 쓰지 않는다
        return { state: 'ended', up: null, ms };
      }
      return { state: 'open', up: null, ms };
    }
    if (ms.playing) return { state: 'open', up: null, ms };
    const ogLong = og && ogSince.key === routeKey && now - ogSince.at >= OFFLINE_AFTER_OG_MS;
    if (ogLong || now - routeAt >= OFFLINE_MAX_MS) return { state: 'offline', up: null, ms };
    return { state: 'unknown', up: null, ms };
  }

  // ══════════════════════════════════════
  //  클립 (iframe 메시지)
  // ══════════════════════════════════════
  const clips = new Map();     // 클립UID → { title, channel, image, status, ct, dur, at }
  let clipCurrent = null;      // iframe 이 지금 띄운 것 { type:'VIDEO'|'AD'|'OTHER', uid }

  function clipFrame() {
    const el = document.querySelector(CLIP_FRAME_SEL);
    if (!el) return null;
    let u;
    try { u = new URL(el.src, location.href); } catch (e) { return null; }
    if (u.origin !== CLIP_ORIGIN || !/^\/shorts\/?$/.test(u.pathname)) return null;
    if (u.searchParams.get('serviceType') !== 'CHZZK') return null;
    const targetId = u.searchParams.get('sdkTargetId') || '';
    return targetId ? { el, targetId } : null;
  }

  function clipRec(uid) {
    let rec = clips.get(uid);
    if (!rec) {
      if (clips.size >= 30) clips.delete(clips.keys().next().value);
      rec = { title: '', channel: '', image: '', status: '', ct: 0, dur: 0, at: 0 };
      clips.set(uid, rec);
    }
    return rec;
  }

  //  iframe 이 알려 준 콘텐츠 → 치지직 클립이면 { uid, rec }, 광고면 'AD', 그 밖은 null
  function readContent(content) {
    if (!content || typeof content !== 'object') return null;
    if (content.contentType === 'AD') return 'AD';
    if (content.contentType !== 'VIDEO') return null;
    const cd = (content.contentData && typeof content.contentData === 'object') ? content.contentData : {};
    if (cd.serviceType !== undefined && cd.serviceType !== 'CHZZK') return null;   // 치지직 클립이 아니다
    const uid = typeof cd.contentId === 'string' ? cd.contentId : '';
    if (!CLIP_UID.test(uid)) return null;
    const rec = clipRec(uid);
    const title = str(cd.title, 200), channel = str(cd.channelName, 80), image = httpsUrl(cd.imageUrl);
    if (title) rec.title = title;
    if (channel) rec.channel = channel;
    if (image) rec.image = image;
    return { uid, rec };
  }

  function setCurrent(next) {
    const prev = clipCurrent;
    clipCurrent = next;
    if (!prev || prev.type !== next.type || prev.uid !== next.uid) nudgeSoon('치지직 클립 전환');
  }

  const MOVING = { PLAY: true, PLAYING: true };
  window.addEventListener('message', (e) => {
    if (e.origin !== CLIP_ORIGIN) return;
    const d = e.data;
    if (!d || typeof d !== 'object' || typeof d.messageType !== 'string'
        || d.messageType.indexOf('shorts-viewer.event.') !== 0) return;
    const r = routeOf(location.pathname);
    if (!r || r.kind !== 'clip') return;
    const fr = clipFrame();
    if (!fr || e.source !== fr.el.contentWindow || d.targetId !== fr.targetId) return;
    const p = (d.payload && typeof d.payload === 'object') ? d.payload : {};

    if (d.messageType === 'shorts-viewer.event.content') {
      const c = readContent(p.content);
      if (c === 'AD') setCurrent({ type: 'AD', uid: '' });
      else if (c) setCurrent({ type: 'VIDEO', uid: c.uid });
      else if (p.content) setCurrent({ type: 'OTHER', uid: '' });
      return;
    }
    if (d.messageType !== 'shorts-viewer.event.playing') return;
    const c = readContent(p.content);
    if (c === 'AD') { setCurrent({ type: 'AD', uid: '' }); return; }
    if (!c) return;
    const status = p.status;
    if (status !== 'PLAY' && status !== 'PLAYING' && status !== 'PAUSE' && status !== 'ENDED') return;
    const rec = c.rec;
    const t = Number(p.currentTime);
    if (isFinite(t) && t >= 0) rec.ct = t;
    const dur = Number(p.duration);
    if (isFinite(dur) && dur > 0) rec.dur = dur;
    const was = !!MOVING[rec.status];
    rec.status = status;
    rec.at = Date.now();
    setCurrent({ type: 'VIDEO', uid: c.uid });
    if (was !== !!MOVING[status]) nudgeSoon('치지직 클립 ' + status);
  });

  function clipPlayback(uid) {
    const rec = clips.get(uid);
    const cur = clipCurrent;
    //  광고로 넘어갔으면 광고 시간은 싣지 않는다. 클립 정보는 남기고 재생 아님으로 둔다
    if (!rec || !rec.dur || !cur || cur.type !== 'VIDEO' || cur.uid !== uid) {
      return { currentTime: 0, duration: 0, isPlaying: false, loaded: false, startedAtMs: 0, playbackRate: 1 };
    }
    const now = Date.now();
    const moving = !!MOVING[rec.status] && now - rec.at <= CLIP_STALE_MS;
    let ct = rec.ct;
    if (moving) ct = Math.min(rec.dur, rec.ct + (now - rec.at) / 1000);
    if (rec.status === 'ENDED') ct = rec.dur;
    return {
      currentTime: Math.floor(ct),
      duration: Math.floor(rec.dur),
      isPlaying: moving,
      loaded: true,
      //  마지막 소식을 받은 순간의 위치로 0초 시각을 잡는다 (소식은 초당 ~4번 온다)
      startedAtMs: moving ? rec.at - Math.round(rec.ct * 1000) : 0,
      playbackRate: 1,
    };
  }

  // ══════════════════════════════════════
  //  TOKU_SITE
  // ══════════════════════════════════════
  self.TOKU_SITE = {
    id: 'chzzk',
    get siteName() { return T('chzzk.siteName'); },

    // 썸네일 CDN (2026-10-11 확인: 라이브 스냅샷 · 다시보기 · 클립)
    ownsHost: (h) => /(^|\.)(livecloud-thumb\.akamaized\.net|nng-phinf\.pstatic\.net|video-phinf\.pstatic\.net)$/.test(h),

    //  방송이 꺼져 있거나 끝났거나 재생할 수 없는 주소는 '시청' 이 아니다 → 둘러보기 카드로 보낸다
    isPlaybackUrl() {
      const r = mark();
      if (!r) return false;
      if (r.kind === 'live') {
        const st = liveState(r).state;
        return st === 'open' || st === 'unknown';
      }
      if (r.kind === 'vod') return !unavailable(r);
      return true;
    },

    //  지금 콘텐츠의 정보가 확인되기 전에는 보내지 않는다 (이전 콘텐츠 제목·썸네일이 남아 있다)
    isReady() {
      const r = mark();
      if (!r) return true;
      const late = Date.now() - routeAt > GRACE_MS;
      if (r.kind === 'live') {
        const ls = liveState(r);
        if (ls.state === 'unknown') return false;   // 켜졌는지 꺼졌는지 모른 채 틀린 카드를 먼저 띄우지 않는다
        //  ⚠ '스트리밍 중' 이 보인다는 것만으로는 안 된다 — 다른 방송에서 막 넘어오면 이전 방송 표시가 남아 있다.
        return !!ogFor(r) || late;
      }
      if (r.kind === 'clip') {
        const rec = clips.get(r.id);
        if (rec && rec.title) return true;
      }
      return !!ogFor(r) || late;
    },

    getVideoElement() {
      const r = routeOf(location.pathname);
      if (!r || r.kind === 'clip') return null;   // 클립은 iframe 안이라 볼 수 없다
      return mainVideo(r.kind);
    },

    //  공통 코어의 hasRealVideo 는 길이가 유한해야 통과한다 — 라이브(무한/0)·클립(요소 없음)은 여기서 준다
    getPlayback() {
      const r = mark();
      if (!r) return null;
      if (r.kind === 'live') {
        //  재생 상태는 그때그때 새로 읽는다 — 일시정지·재생 이벤트로 즉시 보낼 때 250ms 묶음(lsCache)의
        //  이전 값을 싣지 않게. (방송 열림·종료 판정만 묶음을 쓴다)
        const ms = mediaState('live');
        //  위치·길이는 싣지 않는다: 라이브의 currentTime 은 DVR 창 안의 위치다
        return { currentTime: 0, duration: 0, isPlaying: ms.playing, loaded: ms.loaded, startedAtMs: 0, playbackRate: 1 };
      }
      if (r.kind === 'vod') {
        const ms = mediaState('vod');
        const v = ms.v;
        if (!v || ms.ad || !(v.duration > 0) || !isFinite(v.duration)) {
          return { currentTime: 0, duration: 0, isPlaying: false, loaded: false, startedAtMs: 0, playbackRate: 1 };
        }
        const rate = (v.playbackRate > 0 && isFinite(v.playbackRate)) ? v.playbackRate : 1;
        const exact = v.currentTime || 0;
        return {
          currentTime: Math.floor(exact),
          duration: Math.floor(v.duration),
          isPlaying: ms.playing,
          loaded: true,
          //  진행 바 기준점 — 배속이면 '실제 시간' 으로 환산한다 (앱이 끝 시각도 같은 비율로 잡는다).
          //  데이터가 모자라면(readyState<3) 위치가 흐르지 않으므로 잡지 않는다.
          startedAtMs: v.readyState >= 3 ? Date.now() - Math.round(exact * 1000 / rate) : 0,
          playbackRate: rate,
        };
      }
      return clipPlayback(r.id);
    },

    // 카드: 윗줄(details)=방송·영상·클립 제목, 아랫줄(state)=채널명. 회차 번호는 없다.
    extractVideo() {
      const r = mark();
      const empty = { seriesName: '', episodeNumber: '', episodeTitle: '', thumbnail: '' };
      if (!r) return empty;

      if (r.kind === 'live') {
        const ls = liveState(r);
        const og = ogFor(r);
        const names = channelNames(r.id);
        const sp = og ? splitOg(og.title, names) : { channel: '', title: '' };
        //  방송 중 제목을 바꾸면 og:title 은 처음 값 그대로다. 정보 영역 제목이 최신이다.
        //  og 가 이 방송 것으로 바뀐 뒤에만 정보 영역 제목을 믿는다 (그 전엔 이전 방송 제목이 남아 있을 수 있다)
        const h = (og && ls.up) ? h2Near(ls.up.el, 4) : null;
        let fresh = h ? visibleText(h) : '';
        //  정보 영역 제목은 3줄을 넘으면 '...' 로 잘려 그려진다 — 같은 제목이면 og 의 온전한 제목을 쓴다
        const cut = fresh.replace(/(\.\.\.|…)$/, '');
        if (cut !== fresh && sp.title && sp.title.indexOf(cut) === 0) fresh = sp.title;
        //  전체화면·넓은 화면에선 정보 영역(h2)이 없다. 그때는 문서 제목(상태 폴링 때마다 최신)을 쓴다 —
        //  이 채널명으로 시작할 때만 (다른 방송 것이 아님을 확인)
        if (!fresh && og && sp.channel) {
          const core = docTitleCore();
          if (core.indexOf(sp.channel + ' - ') === 0) fresh = core.slice(sp.channel.length + 3).trim();
        }
        return {
          seriesName: fresh || sp.title,
          episodeNumber: '',
          episodeTitle: sp.channel || names[0] || '',
          thumbnail: og ? liveImage(og.image) : '',
          isLive: true,
          liveStartedAt: liveStart.cid === r.id ? liveStart.at : 0,
        };
      }

      if (r.kind === 'vod') {
        const og = ogFor(r);
        if (og) {
          const sp = splitOg(og.title, channelNames(''));
          return { seriesName: sp.title, episodeNumber: '', episodeTitle: sp.channel, thumbnail: httpsUrl(og.image) };
        }
        //  가려진 탭에서 다음 동영상으로 자동 이동하면 페이지가 og 갱신을 미룬다(화면이 그려질 때까지).
        //  문서 제목은 바로 바뀐다 → og 가 아직 '이전 다시보기' 것이고 문서 제목이 그 이전 것과 다를 때만 쓴다.
        //  썸네일은 확인할 길이 없어 비운다(로고).
        //  이전 영상들의 제목(og·앞선 주소에서 본 문서 제목)과 같으면 아직 새 영상 정보가 아니다
        const core = normTitle(docTitleCore());
        if (core && ogPath().indexOf('/video/') === 0 && core !== normTitle(metaContent('og:title'))
            && prevTitles.indexOf(core) < 0) {
          const sp = splitOg(core, channelNames(''));
          return { seriesName: sp.title, episodeNumber: '', episodeTitle: sp.channel, thumbnail: '' };
        }
        return empty;
      }

      // 클립: iframe 이 알려 준 지금 클립 정보가 우선, 상위 문서 og 는 같은 클립일 때만 보탠다
      const rec = clips.get(r.id);
      const og = ogFor(r);
      let title = rec ? rec.title : '';
      let channel = rec ? rec.channel : '';
      if (og && (!title || !channel)) {
        const sp = splitOg(og.title, channel ? [channel] : []);
        title = title || sp.title;
        channel = channel || sp.channel;
      }
      return {
        seriesName: title,
        episodeNumber: '',
        episodeTitle: channel,
        thumbnail: (og && httpsUrl(og.image)) || (rec ? rec.image : '') || '',
      };
    },

    extractBrowsing() {
      const p = location.pathname.replace(/\/+$/, '') || '/';
      let page = T('page.browsing');
      let detail = '';
      let banner = '';
      let url = '';            // 비워 두면 content.js 가 location.href 를 쓴다

      const r = routeOf(location.pathname);
      if (r && r.kind === 'live') {
        //  라이브 주소인데 방송이 꺼져 있거나·끝났거나·재생할 수 없다
        const st = liveState(r).state;
        page = T(st === 'ended' ? 'chzzk.liveEnded' : st === 'unavailable' ? 'chzzk.unavailable' : 'chzzk.offline');
        const og = ogFor(r);
        const names = channelNames(r.id);
        detail = (og ? splitOg(og.title, names).channel : '') || names[0] || '';
      } else if (r && r.kind === 'vod') {
        //  다시보기 주소인데 재생할 영상이 안 생긴다 (제한 안내 등)
        page = T('chzzk.unavailable');
        const og = ogFor(r);
        detail = og ? splitOg(og.title, channelNames('')).channel : '';
      } else if (p === '/' || p === '/home' || p.indexOf('/home/') === 0) {
        page = T('page.home');
      } else if (p === '/lives' || p.indexOf('/lives/') === 0) {
        page = T('chzzk.lives');
      } else if (p === '/videos' || p.indexOf('/videos/') === 0) {
        page = T('chzzk.videos');
      } else if (p === '/clips' || p.indexOf('/clips/') === 0) {
        page = T('chzzk.clips');
      } else if (p === '/following') {
        page = T('chzzk.following');
      } else if (p === '/category' || p.indexOf('/category/') === 0) {
        page = T('chzzk.category');
      } else if (p === '/search' || p.indexOf('/search/') === 0) {
        //  ⚠ 검색어는 싣지 않는다 — 카드·버튼 주소로 프로필을 보는 모두에게 공개된다 (유튜브와 같은 규칙)
        page = T('chzzk.search');
        url = ORIGIN + '/';
      } else {
        const m = /^\/([0-9a-fA-F]{32})(?:\/(?:videos|clips|community|about))?$/.exec(p);
        if (m) {
          page = T('chzzk.channel');
          //  채널 페이지의 og:title 은 채널명 그대로, og:image 는 채널 프로필 사진이다 (og:url 이 이 채널일 때만)
          if (ogPath() === '/' + m[1]) {
            detail = str(metaContent('og:title'), 80);
            banner = httpsUrl(metaContent('og:image'));
          }
        }
      }
      if (detail === page) detail = '';
      return { page, detail, banner, url };
    },

    //  치지직 다시보기는 플레이어가 '다음 동영상 자동 재생' 을 스스로 한다. 우리가 또 옮기면 두 칸을 건너뛴다
    //  (IMAGINATION 2화 건너뛰기와 같은 구조). 다음 영상 정보도 페이지 내부 상태에만 있어 이동 대상을 확인할 수 없다.
    findNextEpisode() { return ''; },
  };

  console.log('[TOKU RPC] 치지직 추출기 로드됨');
})();
