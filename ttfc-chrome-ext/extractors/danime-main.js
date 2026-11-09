// ============================================================
//  dアニメストア — 페이지(MAIN) 월드 브리지 (재생 창 sc_d_pc 전용)
//
//  재생 창은 문서 제목이 '動画再生' 뿐이고, 화면 글자(.backInfoTxt1~3)에는 작품명·화·화 제목만 있다.
//  화 썸네일(장면 이미지)은 경로에 해시가 섞여 있어(…/36m8wA/001/2qB5Lw/29298001_1_3.png) partId 로 만들 수 없다.
//  플레이어가 시작·다음 화마다 부르는 /animestore/rest/WS010105 응답에 다 있다 (2026-11-04 확인):
//    data.partId · workTitle · partDispNumber · partTitle · mainScenePath(화 장면) · mainKeyVisualPath(작품)
//  → 페이지가 이미 받은 응답을 옆에서 읽기만 한다 (요청을 새로 보내지 않는다 — 그 응답에는 재생용
//    일회성 토큰도 있어 다시 부르면 재생에 영향을 줄 수 있다). 토큰 등 나머지 값은 버린다.
//  읽은 값은 <html data-toku-danime> 에 써 둔다 (격리 월드의 추출기가 동기적으로 읽는다).
//
//  ⚠ document_start 에 올라야 첫 응답을 잡는다. 페이지의 요청 동작은 바꾸지 않는다 (load 를 듣기만).
//  ⚠ 이 파일은 dアニメストア 페이지의 전역과 같은 공간에서 돈다. 읽기만 한다.
// ============================================================
(() => {
  const ATTR = 'data-toku-danime';
  const API = /\/animestore\/rest\/WS010105(\?|$)/;
  const str = (v) => (typeof v === 'string' ? v : '');
  const httpsOnly = (v) => (/^https:\/\/[^\s"'<>]+$/.test(str(v)) ? str(v).split('?')[0] : '');

  function publish(text) {
    let j;
    try { j = JSON.parse(text); } catch (e) { return; }
    const d = j && j.data;
    if (!d || !/^\d{8}$/.test(str(d.partId))) return;
    const info = {
      partId: d.partId,
      workTitle: str(d.workTitle),
      partDispNumber: str(d.partDispNumber),
      partTitle: str(d.partTitle),
      scene: httpsOnly(d.mainScenePath),
      keyVisual: httpsOnly(d.mainKeyVisualPath),
      at: Date.now(),
    };
    try { document.documentElement.setAttribute(ATTR, JSON.stringify(info)); } catch (e) { /* 무시 */ }
  }

  const XHR = window.XMLHttpRequest;
  if (!XHR || !XHR.prototype) return;
  const origOpen = XHR.prototype.open;
  XHR.prototype.open = function (method, url) {
    try {
      if (API.test(String(url)) && !this.__tokuDanime) {
        this.__tokuDanime = true;
        this.addEventListener('load', () => {
          try {
            if (this.status === 200 && (this.responseType === '' || this.responseType === 'text')) publish(this.responseText);
            else if (this.status === 200 && this.responseType === 'json' && this.response) publish(JSON.stringify(this.response));
          } catch (e) { /* 무시 */ }
        });
      }
    } catch (e) { /* 무시 */ }
    return origOpen.apply(this, arguments);
  };
})();
