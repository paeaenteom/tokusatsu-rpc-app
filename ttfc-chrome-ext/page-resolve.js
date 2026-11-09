// ============================================================
//  TOKU RPC — 보충 기록용 페이지 읽기 (2026-11-04)
//
//  앱의 '보충' 탭에 에피소드 링크를 넣으면, 서비스 워커가 그 페이지를 받아(크롬 VPN 경로 그대로)
//  작품명·화 번호·화 제목·썸네일 주소를 뽑는다. 이 파일은 순수 함수만 둔다 (Node 테스트에서도 그대로 쓴다).
//
//  실측 (2026-11-04, 로그인 상태 fetch):
//   TTFC  /movies/{mid}/movie-stories/{eid}  og:title = "第1話  東京地底の怪要塞｜東映特撮ファンクラブ", og:image = cloudfront
//         /movies/{mid}/movie-stories        og:title = "宇宙刑事ギャバン｜東映特撮ファンクラブ"  ← 작품명은 여기서만
//   IMAG  /video/{id}  og:title = "ウルトラマンオメガ #01 「宇宙人がやってきた」｜TSUBURAYA IMAGINATION - ウルトラサブスク"
//  ⚠ 화 번호 규칙은 extractors/ttfc-extract.js (EP_NUMBER) · imagination-extract.js (EP_SPLIT) 와 같아야 한다 —
//    정주행 기록과 작품명이 같아야 같은 웹훅 폴더로 간다. 거기를 고치면 여기도 고친다.
// ============================================================

(function (root) {
  const TTFC_EP = /^https:\/\/pc\.tokusatsu-fc\.jp\/movies\/(\d+)\/movie-stories\/(\d+)\/?$/;
  const IMAG_EP = /^https:\/\/imagination\.m-78\.jp\/video\/([A-Za-z0-9_-]+)\/?$/;

  const TTFC_EP_NUMBER = /^(Case\s*\d+|ＲＯＵＮＤ\s*[0-9０-９]+|ROUND\s*\d+|第\s*[0-9０-９一二三四五六七八九十百千壱弐弍参伍拾]+\s*(?:カイ|話|章|夜|幕|楽|回|駅|忍)|[一二三四五六七八九十百千]+之巻|Ｍｉｓｓｉｏｎ\s*[0-9０-９]+(?:[－ー\-][0-9０-９]+)?|Mission\s*\d+(?:-\d+)?|EP\.?\s*\d+|Episode\s*\d+|#?\d+話)/i;
  const IMAG_EP_SPLIT = /(第\s*[0-9０-９一二三四五六七八九十百千]+\s*[話章夜楽]|EPISODE\s*\d+|Episode\s*\d+|EP\.?\s*\d+|#\d+)/;
  const IMAG_SUFFIX = /[｜|]\s*TSUBURAYA[\s　]*IMAGINATION.*$/;

  //  링크 → { site, url(정규화), parts } 또는 null. 쿼리·# 는 버린다
  function classify(raw) {
    let u;
    try { u = new URL(String(raw || '').trim()); } catch (e) { return null; }
    if (u.protocol !== 'https:') return null;
    const clean = u.origin + u.pathname.replace(/\/+$/, '');
    let m = TTFC_EP.exec(clean);
    if (m) return { site: 'ttfc', url: clean, listUrl: u.origin + '/movies/' + m[1] + '/movie-stories' };
    m = IMAG_EP.exec(clean);
    if (m) return { site: 'imagination', url: clean };
    return null;
  }

  function decode(s) {
    return String(s || '')
      .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16)))
      .replace(/&#(\d+);/g, (_, d) => String.fromCodePoint(Number(d)))
      .replace(/&quot;/g, '"').replace(/&#39;|&apos;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>')
      .replace(/&nbsp;/g, ' ').replace(/&amp;/g, '&');
  }

  //  <meta property|name="og:…" content="…"> → { 'og:title': …, … } (+ _title = <title>)
  function metaOf(html) {
    const out = {};
    const h = String(html || '').slice(0, 400000);
    for (const m of h.matchAll(/<meta\b[^>]*>/gi)) {
      const tag = m[0];
      const k = (/\b(?:property|name)\s*=\s*["']([^"']+)["']/i.exec(tag) || [])[1];
      const v = (/\bcontent\s*=\s*"([^"]*)"/i.exec(tag) || /\bcontent\s*=\s*'([^']*)'/i.exec(tag) || [])[1];
      if (k && v !== undefined && !(k in out)) out[k] = decode(v).trim();
    }
    const t = /<title[^>]*>([\s\S]*?)<\/title>/i.exec(h);
    out._title = t ? decode(t[1]).replace(/\s+/g, ' ').trim() : '';
    return out;
  }

  const oneSpace = (s) => String(s || '').replace(/[\s　]+/g, ' ').trim();

  //  TTFC 화 텍스트 → { number, title } (ttfc-extract.js parseEpisode 와 같다)
  function ttfcEpisode(raw) {
    raw = oneSpace(raw);
    if (!raw) return { number: '', title: '' };
    const m = raw.match(TTFC_EP_NUMBER);
    if (m) {
      const number = m[1].replace(/\s+/g, ' ').trim();
      const rest = raw.slice(m[0].length).trim();
      const tm = rest.match(/^[「『\[]\s*(.+?)\s*[」』\]]$/);
      return { number, title: tm ? tm[1].trim() : rest.replace(/^[「『\[]|[」』\]]$/g, '').trim() };
    }
    return { number: '', title: raw };
  }

  //  TTFC: 에피소드 페이지 meta + 작품(에피소드 목록) 페이지 meta
  function fromTtfc(epMeta, listMeta) {
    const strip = (s) => oneSpace(String(s || '').split(/[｜|]/)[0]);
    const epText = strip(epMeta['og:title'] || epMeta._title);
    let series = strip((listMeta && (listMeta['og:title'] || listMeta._title)) || '');
    if (series === '東映特撮ファンクラブ') series = '';
    const ep = ttfcEpisode(epText === '東映特撮ファンクラブ' ? '' : epText);
    const img = epMeta['og:image'] || '';
    return { seriesName: series, episodeNumber: ep.number, episodeTitle: ep.title, thumbnail: /^https:\/\/[^/]*cloudfront\.net\//.test(img) ? img : '' };
  }

  //  IMAGINATION: "作品名 #01 「タイトル」" (imagination-extract.js parseVideoTitle 와 같다)
  function fromImagination(meta) {
    const raw = oneSpace(String(meta['og:title'] || meta._title || '').replace(IMAG_SUFFIX, ''));
    let seriesName = raw;
    let episodeNumber = '';
    let episodeTitle = '';
    const m = raw.match(IMAG_EP_SPLIT);
    if (m) {
      episodeNumber = m[1].replace(/\s+/g, '');
      seriesName = raw.slice(0, m.index).trim();
      const rest = raw.slice(m.index + m[1].length).trim();
      const tm = rest.match(/^[「『]\s*(.+?)\s*[」』]/);
      episodeTitle = tm ? tm[1].trim() : rest.replace(/^[「『]|[」』]$/g, '').trim();
    } else {
      const tm = raw.match(/^(.*?)\s*[「『]\s*(.+?)\s*[」』]\s*$/);
      if (tm && tm[1].trim()) { seriesName = tm[1].trim(); episodeTitle = tm[2].trim(); }
    }
    seriesName = seriesName.replace(/^【[^】]*】\s*/, '').replace(/^[『「]\s*/, '').replace(/\s*[』」]$/, '').trim();
    if (/^TSUBURAYA[\s　]*IMAGINATION$/i.test(seriesName)) seriesName = '';
    const img = meta['og:image'] || '';
    return { seriesName, episodeNumber, episodeTitle, thumbnail: /^https:\/\/[^/]*\.m-78\.jp\//.test(img) && !/assets\/images\/ogp/i.test(img) ? img : '' };
  }

  //  이미지 바이트의 실제 형식 (CDN 이 octet-stream 으로 줄 때가 있다)
  function sniffImage(u8) {
    if (u8.length > 3 && u8[0] === 0xff && u8[1] === 0xd8 && u8[2] === 0xff) return 'image/jpeg';
    if (u8.length > 8 && u8[0] === 0x89 && u8[1] === 0x50 && u8[2] === 0x4e && u8[3] === 0x47) return 'image/png';
    if (u8.length > 12 && u8[0] === 0x52 && u8[1] === 0x49 && u8[2] === 0x46 && u8[3] === 0x46 && u8[8] === 0x57 && u8[9] === 0x45 && u8[10] === 0x42 && u8[11] === 0x50) return 'image/webp';
    return '';
  }

  root.TOKU_RESOLVE = { classify, metaOf, fromTtfc, fromImagination, ttfcEpisode, sniffImage };
  if (typeof module !== 'undefined' && module.exports) module.exports = root.TOKU_RESOLVE;
})(typeof self !== 'undefined' ? self : globalThis);
