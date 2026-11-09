// ============================================================
//  확장 파일 지문 (2026-10-15)
//
//  압축 해제로 로드한 확장은 파일을 고쳐도 다시 불러오기 전까지 서비스 워커·content script 가
//  옛 코드로 돈다. 팝업 같은 확장 페이지만 열 때마다 디스크에서 새로 읽힌다
//  (Chrome 문서 'When to reload the extension' 표: manifest·service worker·content scripts 는 다시 불러오기 필요,
//   popup 은 불필요).
//  그래서 불러온 순간의 지문을 저장해 두고(background.js), 지금 디스크의 지문과 다르면 새 버전이 있는 것이다.
//
//  대상: manifest.json · background.js(와 그것이 불러오는 파일) · 이 파일 · content script 전부.
//  팝업 파일은 늘 새로 읽히므로 넣지 않는다 (팝업만 고친 건 다시 불러올 일이 아니다).
//  background.js 와 popup.html 둘 다 이 파일을 불러 같은 계산을 쓴다.
// ============================================================
(() => {
  async function readText(f) {
    const r = await fetch(chrome.runtime.getURL(f), { cache: 'no-store' });
    if (!r.ok) throw new Error(f + ' ' + r.status);
    return r.text();
  }

  //  못 읽으면 '' — 쓰는 쪽은 '' 를 '모름' 으로 보고 아무것도 하지 않는다
  self.TOKU_PRINT = async function () {
    try {
      const mf = await readText('manifest.json');
      const files = new Set(['background.js', 'version-print.js', 'locales/ko.js', 'locales/en.js', 'locales/ja.js', 'i18n.js', 'page-resolve.js']);
      for (const cs of JSON.parse(mf).content_scripts || []) for (const f of cs.js || []) files.add(f);
      let all = mf;
      for (const f of [...files].sort()) all += '\n--- ' + f + '\n' + await readText(f);
      const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(all));
      return [...new Uint8Array(buf)].slice(0, 16).map((b) => b.toString(16).padStart(2, '0')).join('');
    } catch (e) {
      return '';
    }
  };
})();
