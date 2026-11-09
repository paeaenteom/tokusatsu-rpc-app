// 창 문구 생성기 — 지금 앱의 locales/{ko,en,ja}.js 에서 필요한 키를 뽑아 src/app/strings.h 로 (실행 시 외부 도움 없음)
//  node tools\gen-strings.js
'use strict';
const fs = require('fs'); const path = require('path');
const SRC = 'C:/Users/paeae/Downloads/toku rpc/ttfc-app/src/locales/';
const KEYS = [
  'win.tabActivity', 'win.tabDisplay', 'win.tabApp', 'win.tabKeys', 'win.tabBg', 'win.tabHook',
  'win.current', 'win.badgeWatching', 'win.badgePaused', 'win.badgeBrowsing', 'win.nothing', 'win.nothingHint', 'win.openSite', 'win.timeUnknown',
  'win.connection', 'win.ext', 'win.extWaiting', 'win.extConnected', 'win.dcConnected', 'win.dcDisconnected',
  'win.recentLogs', 'win.noLogs', 'win.viewAll', 'win.quick', 'win.rpcOn', 'win.rpcOff', 'win.reconnect', 'win.reconnectHint', 'win.rpcToggleHint',
  'win.hdrOk', 'win.hdrNoExt', 'win.hdrNoDiscord', 'win.stOk', 'win.stOff', 'win.stNoDiscord', 'win.stNoExt',
  'win.settings', 'win.timeMode', 'win.timeProgress', 'win.timeRemaining', 'win.timeNone',
  'win.showSeries', 'win.showEpisode', 'win.showThumbnail', 'win.showButtons',
  'win.activityType', 'win.activityTypeHint', 'win.actWatching', 'win.actListening', 'win.actPlaying',
  'win.statusDisplay', 'win.statusDisplayHint', 'win.sdSeries', 'win.sdEpisode', 'win.sdAppName',
  'win.site_ttfc', 'win.site_imagination', 'win.site_youtube', 'win.site_disneyplus', 'win.site_chzzk', 'win.site_danime',
  'win.idleTimeout', 'win.idleTimeoutHint', 'win.idleNever', 'win.idleMin', 'win.idleHour',
  'win.responseLevel', 'win.responseLevelHint', 'win.language', 'win.langAuto', 'win.version',
  'win.autoStart', 'win.autoStartHint', 'win.fastStart', 'win.fastStartHint', 'win.timeLeft', 'win.timeElapsed',
  'hook.episode', 'hook.endTitle', 'hook.endDesc', 'hook.testDesc', 'win.binge', 'win.bingeOnHint', 'win.bingeOffHint', 'win.bingeNoExt', 'win.bingeOldExt', 'win.bingeFail',
  'win.hookTitle', 'win.hookIntro', 'win.hookName', 'win.hookNamePh', 'win.hookNameHint', 'win.hookUrl', 'win.hookUrlPh', 'win.hookWebhookName', 'win.hookWebhookNamePh', 'win.hookAdd', 'win.hookList', 'win.hookDefaultOn', 'win.hookDefaultOff', 'win.hookEmpty', 'win.hookCount', 'win.hookIcon', 'win.hookIconClear', 'win.hookAddHere', 'win.hookUnnamed', 'win.hookTest', 'win.hookRemove', 'win.hookRemoveConfirm', 'win.hookSending', 'win.hookTestOk', 'win.hookTestFail', 'win.hookAdded', 'win.hookRenamed', 'win.hookRemoved', 'win.hookErrTitle', 'win.hookErrUrl', 'win.hookErrDup', 'win.hookErrFull', 'win.hookErrFile', 'win.hookErrMissing', 'win.hookErrIcon', 'win.hookErrCharName', 'win.hookErrCharBad', 'win.hookErrCharFull', 'win.hookErrAvatarUrl', 'win.hookErrUpload', 'win.hookErrAvatarLoad', 'win.hookErrAvatarStale', 'win.hookChars', 'win.hookCharsCount', 'win.hookCharsNone', 'win.hookCharsHint', 'win.hookCharNamePh', 'win.hookCharAvatarPh', 'win.hookCharPick', 'win.hookCharAdd', 'win.hookCharSaved', 'win.hookCharOnMsg', 'win.hookCharOffMsg', 'win.hookCharExpired', 'win.hookCharTemp', 'win.hookCharUploading', 'win.hookCharUploaded', 'win.avatarAdjust', 'win.avatarLoading', 'win.hookIconDialog', 'win.hookCharDialog', 'win.imgDialogFilter', 'win.avatarTitle', 'win.avatarApply', 'win.avatarCancel', 'win.avatarHint', 'win.avatarMsgSample', 'win.avatarOther', 'win.avatarPreview', 'win.avatarReset', 'win.avatarZoom',
  'win.bgTitle', 'win.bgUnsaved', 'win.bgIntro', 'win.bgSample', 'win.bgNoImage', 'win.bgReset', 'win.bgEnable', 'win.bgEnableHint', 'win.bgFit', 'win.bgFitCover', 'win.bgFitContain', 'win.bgPosition', 'win.bgDragHint', 'win.bgCenter', 'win.bgZoom', 'win.bgOpacity', 'win.bgDim', 'win.bgBlur', 'win.bgCancel', 'win.bgApply', 'win.bgApplied', 'win.bgChange', 'win.bgPick', 'win.bgMeta', 'win.bgMissing', 'win.bgNeedImage', 'win.bgResetConfirm', 'win.bgResetDone', 'win.bgUnavailable', 'win.bgDialogTitle', 'win.bgDialogFilter', 'win.bgErr_tooLarge', 'win.bgErr_tooBig', 'win.bgErr_format', 'win.bgErr_animated', 'win.bgErr_corrupt', 'win.bgErr_read', 'win.bgErr_copy', 'win.bgErr_save', 'win.bgErr_busy', 'win.bgErr_stale', 'win.bgErr_invalid', 'win.bgErr_noImage',
  'tray.open', 'tray.rpc', 'tray.reconnect', 'tray.bingeOff', 'tray.autoStartOff', 'tray.quit', 'tray.latExt', 'tray.latExtNone', 'tray.latExtSlow', 'tray.latDiscord', 'tray.latDiscordNone', 'tray.latRpcOff',
  'win.tabBackfill', 'win.bfTitle', 'win.bfIntro', 'win.bfUrl', 'win.bfUrlPh', 'win.bfUrlHint', 'win.bfLoad', 'win.bfLoading', 'win.bfLoaded', 'win.bfSeries', 'win.bfEpNum', 'win.bfEpTitle', 'win.bfWhen', 'win.bfNow', 'win.bfWhenHint', 'win.bfPreview', 'win.bfNoThumb', 'win.bfToFolder', 'win.bfToFolderChars', 'win.bfToDefault', 'win.bfNoHook', 'win.bfSend', 'win.bfSending', 'win.bfSent', 'win.bfFail', 'win.bfErrNoext', 'win.bfErrOldext', 'win.bfErrBadurl', 'win.bfErrBlocked', 'win.bfErrEmpty', 'win.bfErrFetch', 'win.bfErrStale', 'win.bfErrSeries', 'win.bfErrWhen', 'win.bfErrNohook',
  'win.kbTitle', 'win.kbIntro', 'win.kbErrNoExt', 'win.kbErrOldExt', 'win.kbErrNoTab', 'win.kbErrTimeout', 'win.kbErrFull', 'win.kbErrKey', 'win.kbErrMissing', 'win.kbErrSave', 'win.kbChzzk', 'win.kbCount', 'win.kbOnMsg', 'win.kbOffTip', 'win.kbPick', 'win.kbPicking', 'win.kbCancel', 'win.kbEmpty', 'win.kbNamePh', 'win.kbNoKey', 'win.kbPressKey', 'win.kbKeyNow', 'win.kbUnnamed', 'win.kbRepick', 'win.kbRemove', 'win.kbRemoveConfirm', 'win.kbFsTag', 'win.kbRenamed', 'win.kbPicked', 'win.kbRepicked', 'win.kbCanceled', 'win.kbSaved', 'win.kbMoved', 'win.kbRemoved',
];
//  알파에만 있는 문구 (지금 앱 locales 에 없는 것) — 세 언어 모두
const EXTRA = {
  'na.bfWhenPh': ['YYYY-MM-DD HH:MM', 'YYYY-MM-DD HH:MM', 'YYYY-MM-DD HH:MM'],
  'na.alpha': ['C 알파 (테스트)', 'C alpha (test)', 'C アルファ (テスト)'],
  'na.beta': ['C 베타', 'C beta', 'C ベータ'],
  'na.bfModeLink': ['링크로 불러오기', 'From a link', 'リンクから'],
  'na.bfModeManual': ['직접 입력', 'Enter manually', '手動で入力'],
  'na.bfManualIntro': ['TTFC·IMAGINATION 에 없는 영상(DVD 특전·극장 한정 영상 등)을 직접 적어 정주행 웹훅에 기록합니다. 작품을 고르고 화·본 시각·이미지를 넣어 보내면 됩니다.', 'Record videos that are not on TTFC or IMAGINATION (DVD extras, theater-only clips, etc.) to the binge webhook by hand. Pick the title, then add the episode, watch time and an image.', 'TTFC・IMAGINATION にない映像（DVD特典・劇場限定映像など）を手動で一気見Webhookに記録します。作品を選び、話・視聴時刻・画像を入れて送ります。'],
  'na.bfPickTitle': ['작품 고르기', 'Pick a title', '作品を選ぶ'],
  'na.bfNoFolders': ['웹훅 탭에 만든 작품 폴더가 없습니다 — 작품명을 직접 적어 주세요', 'No title folders in the Webhook tab — type the title instead', 'Webhookタブに作品フォルダがありません — 作品名を直接入力してください'],
  'na.bfSource': ['출처 표시 (선택)', 'Source label (optional)', '出典表示（任意）'],
  'na.bfSourcePh': ['예: DVD 특전 영상 · 극장 한정', 'e.g. DVD extra · theater only', '例: DVD特典映像・劇場限定'],
  'na.bfImage': ['이미지', 'Image', '画像'],
  'na.bfImagePh': ['https://… 이미지 링크', 'https://… image link', 'https://… 画像リンク'],
  'na.bfImageFile': ['파일…', 'File…', 'ファイル…'],
  'na.bfImageClear': ['지우기', 'Clear', '消去'],
  'na.bfErrImgUrl': ['이미지 링크는 https 주소여야 합니다', 'The image link must be an https address', '画像リンクは https のアドレスにしてください'],
  'na.bfErrImgFile': ['이 그림은 쓸 수 없습니다 (png·jpg·gif·webp, 8MB 까지)', 'This image cannot be used (png/jpg/gif/webp, up to 8MB)', 'この画像は使えません（png・jpg・gif・webp、8MBまで）'],
  'na.bfImgLoading': ['그림 불러오는 중…', 'Loading image…', '画像を読み込み中…'],
  'na.bfImgPreviewFail': ['미리보기를 못 불러왔습니다 — 보낼 때 한 번 더 받아 보고, 안 되면 링크 그대로 보냅니다', 'Could not load the preview — it will be fetched again when sending, or sent as a link', 'プレビューを読み込めませんでした — 送信時にもう一度取得し、だめならリンクのまま送ります'],
  'na.bfNoImage': ['그림 없이 보냅니다', 'Sent without an image', '画像なしで送ります'],
  'na.testCh': ['테스트 버전 (알파)', 'Test version (alpha)', 'テスト版 (アルファ)'],
  'na.testChHint': ['새 전송 방식을 먼저 써 봐요 · 켜고 끄면 앱이 다시 시작돼요', 'Try the new send pacing early · the app restarts when switched', '新しい送信方式を先に試します · 切り替えるとアプリが再起動します'],
  'na.soon': ['C 알파에서는 아직 준비 중이에요', 'Not ready yet in the C alpha', 'C アルファではまだ準備中です'],
  'na.soonHint': ['지금 앱에서 저장해 둔 설정은 그대로 쓰고 있어요', 'Settings you saved in the current app are still used', '今のアプリで保存した設定はそのまま使っています'],
  'na.openLogs': ['로그 폴더 열기', 'Open log folder', 'ログフォルダーを開く'],
  'na.configPath': ['설정 파일', 'Settings file', '設定ファイル'],
  'na.info': ['정보', 'About', '情報'],
  'na.preparing': ['준비 중', 'Coming soon', '準備中'],
  'na.levelMax': ['MAX — 가장 빠름', 'MAX — fastest', 'MAX — 最速'],
  'na.levelHigh': ['HIGH', 'HIGH', 'HIGH'],
  'na.levelNormal': ['NORMAL', 'NORMAL', 'NORMAL'],
  'na.levelLow': ['LOW — 가장 가벼움', 'LOW — lightest', 'LOW — 最軽量'],
  'na.langKo': ['한국어', '한국어', '한국어'],
  'na.langEn': ['English', 'English', 'English'],
  'na.langJa': ['日本語', '日本語', '日本語'],
  'na.portWait': ['포트 7690 대기 중 — 기존 앱을 끄면 이어받아요', 'Waiting for port 7690 — takes over when the current app is closed', 'ポート 7690 待機中 — 今のアプリを閉じると引き継ぎます'],
  'na.discordUser': ['Discord', 'Discord', 'Discord'],
  'na.verbWatching': ['시청 중', 'Watching', '視聴中'],
  'na.verbListening': ['듣는 중', 'Listening to', '再生中'],
  'na.verbPlaying': ['플레이 중', 'Playing', 'プレイ中'],
};
const load = (l) => { const m = { exports: {} }; new Function('module', 'exports', fs.readFileSync(SRC + l + '.js', 'utf8'))(m, m.exports); return m.exports; };
const T = { ko: load('ko'), en: load('en'), ja: load('ja') };
const cstr = (s) => '"' + Buffer.from(s, 'utf8').toString('latin1').split('').map((ch) => {
  const c = ch.charCodeAt(0);
  if (ch === '"' || ch === '\\') return '\\' + ch;
  if (c === 10) return '\\n';
  if (c < 0x20 || c >= 0x7f) return '\\x' + c.toString(16).padStart(2, '0') + '""';
  return ch;
}).join('') + '"';
const ids = [], rows = { ko: [], en: [], ja: [] };
const missing = [];
for (const k of KEYS) {
  ids.push('S_' + k.replace(/^win\./, '').replace(/[^A-Za-z0-9]/g, '_'));
  for (const l of ['ko', 'en', 'ja']) {
    const v = T[l][k] || T.en[k] || T.ko[k];
    if (!T[l][k]) missing.push(l + ':' + k);
    if (v === undefined) throw new Error('없는 키: ' + k);
    rows[l].push(cstr(v));
  }
}
for (const [k, v] of Object.entries(EXTRA)) {
  ids.push('S_' + k.replace(/^na\./, 'NA_').replace(/[^A-Za-z0-9]/g, '_'));
  ['ko', 'en', 'ja'].forEach((l, i) => rows[l].push(cstr(v[i])));
}
let out = '// 자동 생성 — tools/gen-strings.js (지금 앱 locales 에서). 손으로 고치지 말 것\n#ifndef TOKU_STRINGS_H\n#define TOKU_STRINGS_H\n';
out += 'enum {\n' + ids.map((s) => '    ' + s + ',').join('\n') + '\n    S_COUNT\n};\n';
for (const l of ['ko', 'en', 'ja']) out += 'static const char *const STR_' + l.toUpperCase() + '[S_COUNT] = {\n' + rows[l].map((r) => '    ' + r + ',').join('\n') + '\n};\n';
out += '#endif\n';
fs.writeFileSync(path.join(__dirname, '..', 'src', 'app', 'strings.h'), out);
console.log('strings.h: ' + ids.length + '개 문구 × 3언어' + (missing.length ? ' (영어·한국어로 대신: ' + missing.join(', ') + ')' : ''));
