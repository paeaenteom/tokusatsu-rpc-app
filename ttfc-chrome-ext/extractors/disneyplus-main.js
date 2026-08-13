// ============================================================
//  디즈니+ — 페이지(MAIN) 월드 브리지   ※ 실험 기능
//
//  디즈니+ 플레이어는 시청 정보를 DOM 에 거의 안 남긴다.
//  대신 <disney-web-player-ui> 엘리먼트의 latestUiState 프로퍼티에
//  제목·에피소드·길이·재생상태·이미지가 전부 들어 있다 (실측 확인).
//
//  그런데 그건 페이지 자바스크립트의 프로퍼티라 확장의 격리 월드에서는 보이지 않는다.
//  그래서 이 파일만 world:"MAIN" 으로 올려 값을 읽고, postMessage 로 넘긴다.
//  (PreMiD 가 <script> 를 주입해서 같은 일을 하는데, MV3 는 공식 수단을 준다)
//
//  ⚠ 이 파일은 디즈니+ 페이지의 전역과 같은 공간에서 돈다.
//    사이트 코드를 건드리지 말 것. 읽기만 한다.
//
//  왜 DOM 을 안 쓰나:
//   · video.duration 이 Infinity (MSE 스트리밍) → 총 길이를 못 구한다
//   · seekable.end() 는 재생과 함께 늘어나므로 총 길이가 아니다
//   · 진행바(aria-valuemax)에는 총 길이가 있지만, 컨트롤이 숨겨지면
//     DOM 에서 아예 제거된다 → 마우스를 안 움직이면 못 읽는다
//   latestUiState 는 이 셋을 전부 피해 간다.
// ============================================================
(() => {
  const CHANNEL = 'TOKU_RPC_DP';
  // 상태 객체를 읽어 JSON 으로 비교하는 것뿐이라 매우 싸다.
  // 값이 그대로면 아무것도 보내지 않으므로 짧게 잡아 반응을 빠르게 한다.
  const POLL_MS = 400;

  function playerUi() {
    // 커스텀 엘리먼트 이름은 소문자 태그로 잡힌다
    return document.querySelector('disney-web-player-ui');
  }

  // images_experience 는 {용도:{종류:{화면비: "<uuid>"}}} 중첩이고 값은 UUID 문자열뿐이다.
  // 원하는 화면비가 없을 수 있어 가장 먼저 나오는 문자열을 집는다.
  function firstId(node, depth) {
    if (typeof node === 'string') return node;
    if (!node || typeof node !== 'object' || (depth || 0) > 4) return '';
    for (const k of Object.keys(node)) {
      const v = firstId(node[k], (depth || 0) + 1);
      if (v) return v;
    }
    return '';
  }

  function pickImage(ix) {
    if (!ix) return '';
    // 가로 이미지를 우선 (디스코드 큰 이미지는 정사각형에 가깝게 잘리지만
    // 타일보다 배경 쪽이 작품을 알아보기 쉽다)
    const order = [
      ix.standard && ix.standard.tile,
      ix.tile && ix.tile.background,
      ix.standard && ix.standard.background,
      ix.details && ix.details.background,
      ix.hero && ix.hero.background,
      ix.up_next && ix.up_next.background,
    ];
    for (const cand of order) {
      const id = firstId(cand);
      if (id) return id;
    }
    return firstId(ix);
  }

  function snapshot() {
    const ui = playerUi();
    const s = ui && ui.latestUiState;
    if (!s) return null;

    const md = (s.playbackCriteria && s.playbackCriteria.current && s.playbackCriteria.current.metadata) || {};
    const tl = (s.timeline && s.timeline.timeline) || {};
    const pb = s.playback || {};
    const sd = s.streamDetails || {};
    const upNext = Array.isArray(md.upNextData) ? md.upNextData[0] : null;

    return {
      title: (md.title && md.title.text) || '',
      subtitle: (md.subtitle && md.subtitle.text) || '',
      imageId: pickImage(md.images_experience),
      durationSec: Number(tl.plannedDurationSecs) || 0,
      positionSec: Number(tl.playheadPositionMs) / 1000 || 0,
      isPlaying: pb.playing === true && pb.isPaused !== true,
      ended: pb.ended === true,
      hasStarted: pb.hasStarted === true,
      isLive: sd.isLive === true || sd.isLinear === true,
      // 다음 화가 "이어지는 에피소드" 일 때만 정주행 대상으로 본다
      nextIsSequential: !!(upNext && upNext.sequentialEpisode),
      nextId: (upNext && upNext.item && upNext.item.id) || '',
      at: Date.now(),
    };
  }

  let lastJson = '';
  function tick() {
    let snap = null;
    try { snap = snapshot(); } catch (e) { /* 사이트 구조 변경 — 조용히 무시 */ }
    // 값이 그대로면 보내지 않는다 (at 은 비교에서 제외)
    const cmp = snap ? JSON.stringify({ ...snap, at: 0 }) : '';
    if (cmp === lastJson) return;
    lastJson = cmp;
    try { window.postMessage({ channel: CHANNEL, payload: snap }, location.origin); } catch (e) {}
  }

  setInterval(tick, POLL_MS);
  tick();

  // 격리 월드가 늦게 붙어도 첫 값을 받을 수 있게, 요청이 오면 즉시 한 번 더 보낸다
  window.addEventListener('message', (e) => {
    if (e.source !== window || !e.data || e.data.channel !== CHANNEL + '_REQ') return;
    lastJson = '';
    tick();
  });
})();
