// ============================================================
//  TOKU RPC — Content Script 공통 코어
//
//  사이트별 추출기(extractors/*.js)가 먼저 로드되어
//  window.TOKU_SITE 를 정의하면, 이 코어가 영상/브라우징 상태를
//  통일 포맷으로 background 서비스 워커에 전송한다.
//
//   { type:'VIDEO',    site, siteName, seriesName, episodeTitle,
//     episodeNumber, currentTime, duration, thumbnail, isPlaying, url }
//   { type:'BROWSING', site, siteName, page, detail, banner, url }
// ============================================================

(() => {
  const SITE = window.TOKU_SITE;
  if (!SITE) {
    console.warn('[TOKU RPC] 추출기(TOKU_SITE)가 없음 — content.js 종료');
    return;
  }

  const LOG = (...a) => console.log('[TOKU RPC]', ...a);

  // ── 썸네일 바이트 추출 (디스코드가 직접 못 불러오는 이미지 전용) ──
  //  imagination CDN(static-constents)과 TTFC 메인 도메인(pc.tokusatsu-fc.jp)은
  //  외부 서버 페치를 차단해 디스코드 프록시가 이미지를 못 불러온다("?").
  //  TTFC의 *.cloudfront.net 이미지는 디스코드가 직접 로드 가능(실증) → 제외.
  //  또 catbox 등 업로드 호스트는 확장의 chrome-extension Origin을 거부하므로,
  //  여기선 브라우저 세션으로 바이트만 추출해(fetch: 같은 오리진 또는 CORS 허용 CDN)
  //  앱으로 보내고, 업로드(Origin 없는 Node)와 재호스팅 URL 사용은 앱이 담당한다.
  //  같은 이미지는 1회만 전송. URL 매핑은 원본 URL 그대로 보낸다(앱이 캐시 키로 씀).
  const sentThumbs = new Set();
  const thumbFails = new Map();  // URL → 실패 횟수 (3회 초과 시 포기)

  // 이 탭(사이트)이 가져올 수 있는 이미지인지 — NEED_THUMB 브로드캐스트 필터용
  function ownsImage(url) {
    try {
      const h = new URL(url, location.href).host;
      return h === location.host || h.endsWith('.' + location.host.split('.').slice(-3).join('.'))
        || (SITE.id === 'imagination' && /m-78\.jp$/.test(h))
        || (SITE.id === 'ttfc' && /tokusatsu-fc\.jp$/.test(h));
    } catch (e) { return false; }
  }

  function needsRehost(url) {
    if (!/^https?:/.test(url)) return false;
    if (/\.cloudfront\.net\//i.test(url)) return false;  // 디스코드 직접 로드 OK
    return true;
  }

  async function extractBytes(url) {
    const resp = await fetch(url, { mode: 'cors' });
    if (!resp.ok) throw new Error('img ' + resp.status);
    const blob = await resp.blob();
    return new Promise((res, rej) => {
      const fr = new FileReader();
      fr.onload = () => res(fr.result);
      fr.onerror = () => rej(new Error('read fail'));
      fr.readAsDataURL(blob);
    });
  }

  // 현재 썸네일/배너 바이트를 앱에 1회 보낸다 (앱이 업로드 + 사용)
  function ensureThumbBytes(url) {
    if (!needsRehost(url) || sentThumbs.has(url)) return;
    if ((thumbFails.get(url) || 0) >= 3) return;  // 반복 실패 → 포기 (로고 폴백)
    // 메모리 상한 — 초과 시 리셋 (재전송돼도 앱의 _thumbCache/_thumbPending이 중복 방지)
    if (sentThumbs.size > 100) sentThumbs.clear();
    if (thumbFails.size > 100) thumbFails.clear();
    sentThumbs.add(url);
    extractBytes(url)
      .then((dataUrl) => chrome.runtime.sendMessage({ action: 'THUMB_BYTES', url, dataUrl }))
      .then((r) => {
        if (r && r.ok) { LOG('썸네일 바이트 전송 OK'); }
        else { sentThumbs.delete(url); }  // 앱 미연결 등 → 다음 틱에 재시도
      })
      .catch((e) => {
        LOG('썸네일 바이트 추출/전송 실패:', e.message);
        thumbFails.set(url, (thumbFails.get(url) || 0) + 1);
        sentThumbs.delete(url);
      });
  }

  // ── 정주행 모드 ──
  //  ⚠ 진행 판정은 오직 video 'ended' 이벤트 — 일시정지/마우스 조작은 절대
  //    정주행을 종료시키지 않는다 (유빈 명시 요구). ON/OFF는 팝업 토글만.
  let bingeMode = false;
  let bingeMoving = false;      // 다음 화 이동 처리 중 (중복 트리거 차단)
  let bingeOverlaySeen = false; // 플레이어 다음화 오버레이 중복 클릭 방지
  let bingePendingFs = false;   // 전환 후 전체화면 복구 대기

  // ── 이중 스킵 방지 가드 (2026-07-26 버그 수정) ──
  //  증상: 다음 화로 한 번 넘어간 뒤 그 화를 다 보면 2화씩 건너뛰었다.
  //  원인: 종료 트리거가 3개(ended 이벤트 / 끝도달 안전망 / 오버레이 감시)인데
  //   ① 어느 것도 "이 화는 이미 처리했다"를 기록하지 않아 중복 실행됨
  //   ② imagination은 SPA 이동이라 문서가 유지돼, 다음 화로 넘어간 뒤에도
  //      이전 화에서 만든 대기 타이머가 살아남아 "오버레이 없음 → 직접 이동"을
  //      실행 → 플레이어가 이미 옮긴 화에서 또 한 칸 이동(= 2화 스킵).
  //  해결: 진행 동작은 "에피소드(URL) 단위로 단 한 번"만 허용하고,
  //        낡은 타이머는 URL이 바뀐 즉시 스스로 멈추게 한다.
  let bingeGuardPath = '';
  let bingeGuardAt = 0;
  function claimAdvance(why) {
    const p = location.pathname;
    const now = Date.now();
    if (bingeGuardPath === p && now - bingeGuardAt < 30000) {
      LOG('정주행: 중복 진행 차단 —', why);
      return false;
    }
    bingeGuardPath = p;
    bingeGuardAt = now;
    return true;
  }
  chrome.storage.local.get('bingeMode', (r) => {
    bingeMode = !!r.bingeMode;
    if (bingeMode) { LOG('정주행 모드 ON (저장값)'); bingeStatusBadge(); }
  });
  chrome.storage.onChanged.addListener((ch, area) => {
    if (area === 'local' && ch.bingeMode) {
      bingeMode = !!ch.bingeMode.newValue;
      LOG('정주행 모드:', bingeMode ? 'ON' : 'OFF');
      if (bingeMode) {
        bingeBadge('📺 정주행 모드 ON', { hold: 3000 });
      } else {
        bingeBadge('정주행 모드 OFF', { force: true, color: '#8a93a6', keep: false, hold: 2000 });
        bingeFsRestore();
      }
      sendUpdate(true);
    }
  });

  // 영상 페이지면 "정주행 대기 중" 상태를 배지로 표시
  function bingeStatusBadge() {
    if (!bingeMode) return;
    if (SITE.isPlaybackUrl()) bingeBadge('📺 정주행 ON · 끝나면 다음 화', { hold: 3500 });
    else bingeBadge('📺 정주행 ON', { hold: 3500 });
  }

  // ── 화면 표시 배지 (정주행이 켜졌는지·무엇을 하는지 눈으로 보이게) ──
  let badgeEl = null;
  function bingeBadge(text, opts) {
    const o = opts || {};
    if (!bingeMode && !o.force) { if (badgeEl) { badgeEl.remove(); badgeEl = null; } return; }
    if (!badgeEl) {
      badgeEl = document.createElement('div');
      badgeEl.id = 'tokuBingeBadge';
      badgeEl.style.cssText =
        'position:fixed;right:16px;bottom:16px;z-index:2147483600;pointer-events:none;' +
        'padding:9px 14px;border-radius:999px;font:600 13px/1 "Noto Sans KR",system-ui,sans-serif;' +
        'color:#12141a;background:#ffc233;box-shadow:0 6px 20px rgba(0,0,0,.45);' +
        'transition:opacity .4s;white-space:nowrap';
      (document.body || document.documentElement).appendChild(badgeEl);
    }
    badgeEl.textContent = text;
    badgeEl.style.background = o.color || '#ffc233';
    badgeEl.style.opacity = '1';
    clearTimeout(badgeEl._fade);
    // 잠깐 보여주고 완전히 사라진다 — 시청 중 상시 표시는 거슬린다(유빈 피드백).
    // 켜짐 여부는 확장 아이콘의 ON 배지가 담당.
    badgeEl._fade = setTimeout(() => { if (badgeEl) badgeEl.style.opacity = '0'; }, o.hold || 3000);
  }

  // ── 전체화면 유지 (IMAGINATION 전용) ──
  //  실측 원인: 다음 화로 넘어갈 때 플레이어가 <video>와 그 컨테이너를 통째로 교체한다.
  //  전체화면 대상 요소가 DOM에서 사라지면 브라우저가 전체화면을 자동 해제하므로
  //  "다음화 보기 → 전체화면 풀림"이 발생. 또 Fullscreen API 재요청은 유저 제스처가
  //  필요해(실측: "Permissions check failed") 자동 복구가 불가능하다.
  //  → 확장 권한으로 창 자체를 전체화면(chrome.windows)으로 만들고 플레이어를
  //    화면 가득 채우는 CSS를 입혀 시각적으로 동일하게 복구한다.
  let wasFullscreen = false;
  let fsLostAt = 0;

  function isPseudoFs() {
    return !!document.getElementById('tokuBingeFs');
  }

  // ── 유사 전체화면 중 마우스 커서 자동 숨김 ──
  //  진짜 전체화면과 달리 사이트의 커서 숨김이 작동하지 않아 포인터가 화면에
  //  남는다(유빈 피드백). 2.5초 무입력 시 숨기고, 움직이면 다시 보인다.
  let cursorTimer = null;
  function scheduleCursorHide() {
    if (!isPseudoFs()) return;
    document.documentElement.classList.remove('toku-nocursor');
    clearTimeout(cursorTimer);
    cursorTimer = setTimeout(() => {
      if (isPseudoFs()) document.documentElement.classList.add('toku-nocursor');
    }, 2500);
  }
  document.addEventListener('mousemove', scheduleCursorHide, { passive: true });

  function bingeApplyFullscreen(reason) {
    if (SITE.id !== 'imagination') return;
    if (typeof SITE.bingeFullscreen !== 'function') return;
    LOG('정주행: 전체화면 복구 —', reason);
    chrome.runtime.sendMessage({ action: 'BINGE_FULLSCREEN' }).catch(() => {});
    try { sessionStorage.setItem('toku_binge_fs', '1'); } catch (e) {}
    SITE.bingeFullscreen();
    scheduleCursorHide();   // 전체화면 진입 직후부터 커서 숨김 타이머 시작
    bingeBadge('⛶ 전체화면 유지', { force: true, hold: 2500 });
  }

  document.addEventListener('fullscreenchange', () => {
    if (document.fullscreenElement) {
      wasFullscreen = true;
      fsLostAt = 0;
      // 페이지가 이동해도 "전체화면으로 보던 중"임을 기억 (탭 단위 유지)
      try { sessionStorage.setItem('toku_was_fs', '1'); } catch (e) {}
    } else if (wasFullscreen) {
      // 전체화면이 풀림 — 사용자가 ESC로 끈 것인지, 영상 교체로 풀린 것인지는
      // 직후에 전환 신호(영상 교체/URL 변경)가 오는지로 구분한다.
      fsLostAt = Date.now();
      // 전환이 아니라 진짜 사용자가 끈 것이면 기억도 지운다 (3초 뒤 판정)
      setTimeout(() => {
        if (!document.fullscreenElement && !isPseudoFs() && Date.now() - fsLostAt >= 2500) {
          try { sessionStorage.removeItem('toku_was_fs'); } catch (e) {}
        }
      }, 3000);
    }
  });

  // 페이지 로드 시 복구 — 플레이어가 페이지를 갈아끼우며 다음 화로 넘어간 경우
  //  (실측: 전체화면 대상 요소가 사라지면 전체화면이 해제되고, JS 재요청은 불가)
  (function restoreFsOnLoad() {
    if (SITE.id !== 'imagination') return;
    let wanted = false;
    try { wanted = sessionStorage.getItem('toku_was_fs') === '1'; } catch (e) {}
    if (!wanted || !SITE.isPlaybackUrl()) return;
    chrome.storage.local.get('bingeMode', (r) => {
      if (!r.bingeMode) return;
      let tries = 0;
      const t = setInterval(() => {
        tries++;
        const v = SITE.getVideoElement();
        if (v && v.readyState >= 2) {
          clearInterval(t);
          bingeApplyFullscreen('다음 화 로드 — 전체화면 이어가기');
        }
        if (tries > 40) clearInterval(t);   // 20초 내 미로드 → 포기
      }, 500);
    });
  })();

  // 유사 전체화면에서 빠져나오기 (Esc) — 안 그러면 갇힌 느낌이 든다
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && isPseudoFs()) {
      LOG('Esc — 전체화면 해제');
      bingeFsRestore();
    }
  });

  // 영상 요소 교체 감시 — 전체화면이 방금 풀렸다면 그건 자동 전환이 원인
  let lastVideoEl = null;
  setInterval(() => {
    if (SITE.id !== 'imagination') return;
    const v = SITE.getVideoElement();
    if (v && v !== lastVideoEl) {
      const swapped = lastVideoEl !== null;
      lastVideoEl = v;
      const lostBySwap = fsLostAt && (Date.now() - fsLostAt < 6000);
      if (swapped && bingeMode && (lostBySwap || bingePendingFs)) {
        fsLostAt = 0;
        wasFullscreen = false;
        bingePendingFs = false;
        setTimeout(() => bingeApplyFullscreen('영상 교체로 전체화면 해제됨'), 400);
      }
    }
    // 오버레이가 떴다는 건 곧 전환된다는 뜻 → 전체화면 복구 예약만 한다.
    //  ⚠ 여기서 클릭까지 하면 onVideoEnded의 대기 루프와 이중 진행이 된다
    //    (2화 스킵 버그의 한 축) → 클릭은 onVideoEnded 한 곳에서만 수행.
    if (bingeMode && typeof SITE.nextOverlay === 'function') {
      const ov = SITE.nextOverlay();
      if (ov && !bingeOverlaySeen) {
        bingeOverlaySeen = true;
        if (isPseudoFs() || wasFullscreen || document.fullscreenElement) bingePendingFs = true;
      } else if (!ov) {
        bingeOverlaySeen = false;
      }
    }
  }, 700);

  // 유사 전체화면 해제 (윈도우 풀스크린 + CSS 원복)
  function bingeFsRestore() {
    const st = document.getElementById('tokuBingeFs');
    if (st) st.remove();
    document.documentElement.classList.remove('toku-nocursor');
    clearTimeout(cursorTimer);
    document.querySelectorAll('[data-toku-fs]').forEach((el) => el.removeAttribute('data-toku-fs'));
    let hadFs = false;
    try {
      hadFs = sessionStorage.getItem('toku_binge_fs') === '1';
      sessionStorage.removeItem('toku_binge_fs');
      sessionStorage.removeItem('toku_was_fs');   // 전체화면 이어가기 기억도 해제
    } catch (e) {}
    if (hadFs) chrome.runtime.sendMessage({ action: 'BINGE_RESTORE' }).catch(() => {});
  }

  // 영상이 끝까지 재생됨 → 다음 화 이동 / 마지막 화면 정주행 종료
  function onVideoEnded(opts) {
    const o = opts || {};
    if (!bingeMode && !o.test) return;
    if (bingeMoving) return;          // 이동 처리 중복 방지
    // 이 에피소드의 진행 권한을 선점 (중복 트리거·낡은 타이머 전부 여기서 걸러짐)
    if (!o.test && !o.forceMove && !claimAdvance('영상 종료')) return;

    // IMAGINATION: 플레이어가 자체 연속재생(카운트다운 오버레이)으로 다음 화를 튼다.
    //  우리가 또 이동하면 이중 진행이 되므로, 오버레이가 뜨면 그 버튼만 눌러
    //  플레이어에 맡기고(전체화면은 따로 복구), 안 뜨면 그때만 직접 이동한다.
    if (SITE.id === 'imagination' && !o.test && !o.forceMove && typeof SITE.nextOverlay === 'function') {
      if (document.fullscreenElement || isPseudoFs()) bingePendingFs = true;
      const startPath = location.pathname;
      let waited = 0;
      const wait = setInterval(() => {
        // 이미 다음 화로 넘어갔다면 이 타이머는 낡은 것 → 즉시 중단
        // (이게 없어서 2화씩 건너뛰는 버그가 났다)
        if (location.pathname !== startPath) {
          clearInterval(wait);
          LOG('정주행: 전환 완료 — 대기 타이머 종료');
          return;
        }
        waited += 500;
        const ov = SITE.nextOverlay();
        if (ov) {
          clearInterval(wait);
          LOG('정주행: 오버레이 감지 → 즉시 재생 (플레이어가 진행)');
          bingeBadge('▶ 다음 화 재생', { force: true, hold: 4000 });
          SITE.clickPlayNow();
        } else if (waited >= 6000) {        // 연속재생 꺼져 있음 → 직접 이동
          clearInterval(wait);
          LOG('정주행: 오버레이 없음 → 직접 이동');
          onVideoEnded({ forceMove: true });
        }
      }, 500);
      return;
    }

    // TTFC: 사이트 플레이어가 자체 연속재생으로 다음 화에 가며 전체화면도 유지한다.
    //  우리가 먼저 location 이동을 하면 전체화면이 풀려 창모드가 됐다(유빈 실사용 보고).
    //  → 10초 기다려 사이트가 진행하면 개입하지 않고, 안 하면 그때만 직접 이동.
    if (SITE.id === 'ttfc' && !o.test && !o.forceMove) {
      const startPath = location.pathname;
      const startVideo = SITE.getVideoElement();
      let waited = 0;
      const wait = setInterval(() => {
        waited += 500;
        const v = SITE.getVideoElement();
        const advanced =
          location.pathname !== startPath ||                     // URL이 다음 화로 변경
          (v && startVideo && v !== startVideo) ||               // 플레이어가 영상 교체
          (v && !v.ended && !v.paused && v.currentTime > 0 && v.currentTime < 120);  // 새 화 재생 시작
        if (advanced) {
          clearInterval(wait);
          LOG('정주행: TTFC 자체 연속재생 감지 — 개입하지 않음');
        } else if (waited >= 10000) {
          clearInterval(wait);
          LOG('정주행: 사이트가 진행하지 않음 → 직접 이동');
          onVideoEnded({ forceMove: true });
        }
      }, 500);
      return;
    }

    const next = (typeof SITE.findNextEpisode === 'function') ? SITE.findNextEpisode() : '';
    if (o.probe) return next;         // 테스트: 찾기만 하고 이동 안 함
    if (next) {
      LOG('정주행: 다음 화 이동 →', next);
      bingeMoving = true;
      bingeBadge('▶ 다음 화로 이동 중…', { force: true, hold: 8000 });
      try { sessionStorage.setItem('toku_binge_next', '1'); } catch (e) {}
      setTimeout(() => { location.href = next; }, 800);  // 배지가 보이도록 아주 짧게 지연
      return true;
    } else if (o.test) {
      // 테스트 실행은 실제 종료 처리(웹훅·모드 OFF)를 하지 않는다
      LOG('정주행 테스트: 다음 화를 찾지 못함');
      bingeBadge('다음 화 없음 (마지막 화로 보임)', { force: true, color: '#8a93a6', hold: 5000 });
      return false;
    } else {
      LOG('정주행: 마지막 화 완료 — 종료');
      bingeBadge('🏁 정주행 끝! (마지막 화)', { force: true, hold: 10000 });
      let seriesName = '';
      try { seriesName = (SITE.extractVideo() || {}).seriesName || ''; } catch (e) {}
      chrome.runtime.sendMessage({ action: 'BINGE_END', seriesName, site: SITE.id }).catch(() => {});
      chrome.storage.local.set({ bingeMode: false });
      bingeFsRestore();
      return false;
    }
  }

  // 다음 화 페이지 로드 후: 재생 보장 + (이매지네이션) 전체화면 복구
  (function bingeResume() {
    try {
      if (sessionStorage.getItem('toku_binge_next') !== '1') return;
      sessionStorage.removeItem('toku_binge_next');
    } catch (e) { return; }
    LOG('정주행: 다음 화 로드됨 — 재생 보장 시작');
    bingeBadge('▶ 다음 화 재생 준비 중…', { force: true, hold: 6000 });
    let tries = 0;
    const timer = setInterval(() => {
      tries++;
      const v = SITE.getVideoElement();
      if (v && v.readyState >= 2 && v.paused && !v.ended) {
        const p = v.play();
        if (p && p.catch) p.catch(() => {});
      }
      if (v && !v.paused && !v.ended) {
        clearInterval(timer);
        LOG('정주행: 재생 확인');
        bingeBadge('📺 정주행 ON · 재생 중', { hold: 3000 });
        if (SITE.id === 'imagination' && typeof SITE.bingeFullscreen === 'function') {
          chrome.runtime.sendMessage({ action: 'BINGE_FULLSCREEN' }).catch(() => {});
          try { sessionStorage.setItem('toku_binge_fs', '1'); } catch (e) {}
          SITE.bingeFullscreen();
        }
      }
      if (tries > 60) clearInterval(timer);  // 30초 내 재생 실패 → 포기 (수동 재생 대기)
    }, 500);
  })();

  let lastStateStr = '';
  let lastLogKey = '';
  let lastUrl = '';
  let lastSentAt = 0;
  let navStartedAt = Date.now();
  const BROWSING_HEARTBEAT = 10000; // 변경 없어도 10초마다 재전송 (SW keepalive 겸용)

  // ── 상태 수집 ──
  function collectState() {
    const video = SITE.getVideoElement();
    const hasRealVideo = video && video.readyState > 0 && video.duration > 0 && isFinite(video.duration);

    // 영상 페이지 URL이면 video 로드 전이라도 '시청 중'으로 표시
    //  → 들어가는 순간 작품/에피소드 정보(og:title 기반)가 뜨고,
    //    실제로 재생하면 시간바 + 재생상태가 채워진다.
    if (SITE.isPlaybackUrl()) {
      const info = SITE.extractVideo();
      let epTitle = info.episodeTitle || '';
      if (epTitle && epTitle === info.seriesName) epTitle = '';
      ensureThumbBytes(info.thumbnail || '');  // 앱에 바이트 1회 전송 (imagination)
      return {
        type: 'VIDEO',
        site: SITE.id,
        siteName: SITE.siteName,
        seriesName: info.seriesName || SITE.siteName,
        episodeTitle: epTitle,
        episodeNumber: info.episodeNumber || '',
        currentTime: hasRealVideo ? Math.floor(video.currentTime) : 0,
        duration: hasRealVideo ? Math.floor(video.duration) : 0,
        thumbnail: info.thumbnail || '',
        // video가 로드돼 실제 재생 중일 때만 재생 상태 true
        isPlaying: hasRealVideo && !video.paused && !video.ended,
        loaded: hasRealVideo,
        binge: bingeMode,
        url: location.href,
      };
    }

    // 영상 진입 시 보강용으로 작품명 기억 (추출기에 위임)
    if (typeof SITE.rememberContext === 'function') {
      try { SITE.rememberContext(); } catch (e) {}
    }

    const browsing = SITE.extractBrowsing();
    ensureThumbBytes(browsing.banner || '');  // 앱에 배너 바이트 1회 전송 (imagination)
    return {
      type: 'BROWSING',
      site: SITE.id,
      siteName: SITE.siteName,
      page: browsing.page || 'ブラウジング中',
      detail: browsing.detail || '',
      banner: browsing.banner || '',
      url: location.href,
    };
  }

  // ── 전송 ──
  function sendUpdate(force) {
    const state = collectState();
    const stateStr = JSON.stringify(state);
    const now = Date.now();

    if (!force && state.type === 'BROWSING' &&
        stateStr === lastStateStr && state.url === lastUrl &&
        now - lastSentAt < BROWSING_HEARTBEAT) {
      return;
    }

    const logKey = state.type === 'VIDEO'
      ? ['V', state.seriesName, state.episodeNumber, state.episodeTitle, state.isPlaying, state.loaded].join('|')
      : ['B', state.page, state.detail, state.url].join('|');
    const changed = logKey !== lastLogKey;
    lastLogKey = logKey;
    lastStateStr = stateStr;
    lastUrl = state.url;
    lastSentAt = now;

    chrome.runtime.sendMessage({ action: 'UPDATE_STATE', data: state })
      .then((res) => {
        if (changed) {
          LOG(`전송 → ${state.type}`,
            state.type === 'VIDEO'
              ? `${state.seriesName} | ${state.episodeNumber} ${state.episodeTitle} (${state.loaded ? state.currentTime + '/' + state.duration + 's ' + (state.isPlaying ? '재생' : '정지') : '로드 대기'})`
              : `${state.page}${state.detail ? ' / ' + state.detail : ''}`,
            res ? `[앱 연결: ${res.connected ? 'O' : 'X'}]` : '');
        }
      })
      .catch((e) => LOG('전송 실패 (서비스 워커 응답 없음):', e.message));
  }

  // 1초 주기 — timeupdate 제거·썸네일 캐시 덕에 스캔이 가벼워져 1초로 단축 (반응속도 ↑)
  setInterval(() => {
    sendUpdate(false);
    // 안전망: 일부 플레이어는 끝에서 'ended'를 안 쏘고 멈추기만 한다.
    //  끝 1초 이내 + 정지 상태일 때만 완주로 간주 (중간 일시정지는 절대 해당 없음)
    if (!bingeMode || bingeMoving) return;
    const v = SITE.getVideoElement();
    if (!v || !v.duration || !isFinite(v.duration)) return;
    if ((v.ended || v.paused) && v.duration - v.currentTime <= 1 && v.currentTime > 0) {
      LOG('정주행: 끝 도달 감지(안전망)');
      onVideoEnded();
    }
  }, 1000);

  // ── 비디오 이벤트 ──
  //  ※ timeupdate는 등록하지 않음 — 초당 ~4회 발생해 매번 풀 DOM 스캔+전송을
  //    유발하던 CPU 주범. 시간 진행은 2초 주기 전송이, 탐색은 seeked가 커버한다.
  function attachVideoEvents() {
    const video = SITE.getVideoElement();
    if (!video || video._tokuRpcAttached) return;
    ['play', 'pause', 'seeked', 'loadedmetadata', 'ended', 'playing'].forEach(evt => {
      video.addEventListener(evt, () => {
        LOG(`video 이벤트: ${evt}`);
        sendUpdate(true);
      });
    });
    // 정주행: 끝까지 재생 시에만 다음 화 (Event 객체가 옵션으로 새지 않게 래핑)
    video.addEventListener('ended', () => onVideoEnded());
    video._tokuRpcAttached = true;
    LOG('video 이벤트 리스너 부착됨');
  }

  attachVideoEvents();
  new MutationObserver(() => attachVideoEvents())
    .observe(document.body, { childList: true, subtree: true });

  // ── SPA URL 변경 즉시 감지 (history API 훅 — 폴링 제거로 반응속도 ↑, CPU ↓) ──
  //  라우팅 직후엔 DOM이 아직 이전 페이지일 수 있어 50ms/600ms 두 번 전송:
  //  첫 번째로 즉시 반영, 두 번째로 렌더 완료본 보정. (2초 주기 전송이 최종 안전망)
  function onUrlChange() {
    if (location.href === lastUrl) return;
    LOG('URL 변경 감지:', location.pathname);
    navStartedAt = Date.now();
    // 새 에피소드로 넘어왔으니 진행 상태 초기화 (SPA는 문서가 유지돼 수동 리셋 필요)
    bingeMoving = false;
    bingeOverlaySeen = false;
    sendUpdate(true);
    setTimeout(() => sendUpdate(true), 600);
    if (bingeMode) setTimeout(bingeStatusBadge, 900);  // 새 페이지에서도 상태 보이게
  }
  for (const fn of ['pushState', 'replaceState']) {
    const orig = history[fn].bind(history);
    history[fn] = function (...args) {
      const r = orig(...args);
      setTimeout(onUrlChange, 50);
      return r;
    };
  }
  window.addEventListener('popstate', () => setTimeout(onUrlChange, 50));

  // ── 앱 재시작 알림 수신 → 썸네일 재전송 (앱 캐시 소실 복구) ──
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg && msg.action === 'TEST_NEXT') {
      // 팝업의 테스트 버튼 — 영상 끝을 기다리지 않고 같은 경로를 즉시 실행
      if (!SITE.isPlaybackUrl()) { sendResponse({ ok: false, reason: '영상 페이지가 아닙니다' }); return true; }
      const ok = onVideoEnded({ test: true });
      sendResponse({ ok: !!ok, reason: ok ? '' : '다음 화를 찾지 못했습니다 (마지막 화?)' });
      return true;
    }
    if (msg && msg.action === 'RESET_THUMBS') {
      LOG('앱 재시작 감지 → 썸네일 기록 리셋');
      sentThumbs.clear();
      thumbFails.clear();
      sendUpdate(true);
    } else if (msg && msg.action === 'NEED_THUMB' && msg.url) {
      // 앱이 명시적으로 요청 — "이미 보냄" 기록을 무시하고 즉시 재추출.
      // 브로드캐스트라 자기 사이트 이미지일 때만 처리 (타 사이트는 CORS로 실패)
      if (!ownsImage(msg.url)) return;
      sentThumbs.delete(msg.url);
      thumbFails.delete(msg.url);
      ensureThumbBytes(msg.url);
    }
  });

  // ── 탭 표시/숨김 ──
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
      const video = SITE.getVideoElement();
      const playing = video && !video.paused && !video.ended;
      if (!playing) {
        LOG('탭 숨김 → CLEAR');
        chrome.runtime.sendMessage({ action: 'CLEAR' }).catch(() => {});
        lastStateStr = '';
      }
    } else {
      sendUpdate(true);
    }
  });

  LOG(`Content script 로드됨 — site: ${SITE.id} (${SITE.siteName})`);
})();
