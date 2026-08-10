// ============================================================
//  다국어 (확장)
//
//  · 언어 설정은 'auto' | 'ko' | 'en' | 'ja' — chrome.storage.local.lang
//  · 'auto' 는 이 순서로 정한다:
//      1. 앱이 알려준 언어 (앱과 확장이 따로 놀지 않게)
//      2. 브라우저 UI 언어
//    ko/en/ja 가 아니면 영어로 떨어진다.
//  · 팝업 · 콘텐트 스크립트 · 서비스 워커에서 모두 쓴다.
//    (locales/*.js 가 먼저 로드돼 self.TOKU_STRINGS 를 채워둔 상태여야 한다)
// ============================================================
(function () {
  const LANGS = ['ko', 'en', 'ja'];
  const FALLBACK = 'en';
  const S = self.TOKU_STRINGS || {};

  let lang = FALLBACK;              // 현재 적용 중인 언어
  const listeners = [];

  function normalize(locale) {
    const base = String(locale || '').toLowerCase().replace('_', '-').split('-')[0];
    return LANGS.includes(base) ? base : '';
  }

  function uiLanguage() {
    try { if (chrome.i18n && chrome.i18n.getUILanguage) return chrome.i18n.getUILanguage(); }
    catch (e) { /* 콘텐트 스크립트 외 환경 */ }
    return (self.navigator && navigator.language) || '';
  }

  function resolve(setting, appLang) {
    if (LANGS.includes(setting)) return setting;
    return normalize(appLang) || normalize(uiLanguage()) || FALLBACK;
  }

  function fill(str, vars) {
    if (!vars) return str;
    return str.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? String(vars[k]) : m));
  }

  function t(key, vars) {
    const s = (S[lang] && S[lang][key]) || (S[FALLBACK] && S[FALLBACK][key]) || (S.ko && S.ko[key]);
    return s === undefined ? key : fill(s, vars);
  }

  function setLang(next) {
    if (next === lang) return;
    lang = next;
    listeners.forEach((fn) => { try { fn(lang); } catch (e) {} });
  }

  // 저장된 설정 + 앱이 알려준 언어로 현재 언어를 다시 계산한다
  function refresh(cb) {
    try {
      chrome.storage.local.get(['lang', 'appLang'], (r) => {
        setLang(resolve(r && r.lang, r && r.appLang));
        if (cb) cb(lang);
      });
    } catch (e) {
      setLang(FALLBACK);
      if (cb) cb(lang);
    }
  }

  // 설정이나 앱 언어가 바뀌면 즉시 따라간다
  try {
    chrome.storage.onChanged.addListener((changes, area) => {
      if (area !== 'local') return;
      if (changes.lang || changes.appLang) refresh();
    });
  } catch (e) { /* 무시 */ }

  self.TOKU_I18N = {
    LANGS,
    t,
    refresh,
    normalize,
    get lang() { return lang; },
    onChange(fn) { listeners.push(fn); },
    // data-i18n="키" 가 붙은 요소를 한 번에 채운다
    apply(root) {
      const scope = root || self.document;
      if (!scope || !scope.querySelectorAll) return;
      scope.querySelectorAll('[data-i18n]').forEach((el) => {
        el.textContent = t(el.getAttribute('data-i18n'));
      });
    },
  };

  refresh();
})();
