// ============================================================
//  다국어 (앱)
//
//  · 언어 설정은 'auto' | 'ko' | 'en' | 'ja'
//  · 'auto' 는 OS 언어를 따라간다. ko/en/ja 가 아니면 영어로 떨어진다.
//  · 한국어가 원본이라 키는 항상 채워져 있다. 다른 언어에 빠진 키가 있으면
//    영어 → 한국어 순으로 대체해 최소한 빈칸은 나오지 않게 한다.
// ============================================================

const LANGS = ['ko', 'en', 'ja'];
const FALLBACK = 'en';   // ko/en/ja 가 아닌 OS 언어일 때

const TABLES = {
    ko: require('./locales/ko'),
    en: require('./locales/en'),
    ja: require('./locales/ja'),
};

// 'ko-KR', 'ja_JP', 'en-US' → 'ko' / 'ja' / 'en'
function normalize(locale) {
    const base = String(locale || '').toLowerCase().replace('_', '-').split('-')[0];
    return LANGS.includes(base) ? base : '';
}

// 설정값 + OS 언어 → 실제로 쓸 언어
function resolve(setting, systemLocale) {
    if (LANGS.includes(setting)) return setting;      // 사용자가 직접 고른 값
    return normalize(systemLocale) || FALLBACK;       // 'auto' (또는 이상한 값)
}

function fill(str, vars) {
    if (!vars) return str;
    return str.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? String(vars[k]) : m));
}

function t(lang, key, vars) {
    const table = TABLES[lang] || TABLES[FALLBACK];
    const s = table[key] || TABLES[FALLBACK][key] || TABLES.ko[key];
    return s === undefined ? key : fill(s, vars);
}

// 렌더러로 통째로 넘길 때 사용
function table(lang) {
    return Object.assign({}, TABLES.ko, TABLES[FALLBACK], TABLES[lang] || {});
}

module.exports = { LANGS, normalize, resolve, t, table };
