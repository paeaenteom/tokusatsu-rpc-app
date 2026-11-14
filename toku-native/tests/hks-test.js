// 웹훅 폴더 검색 시험 — hks_cli.exe (진짜 Windows 일본어 읽기 분석기 사용, 네트워크 X)
//  사용: node tests/hks-test.js <hks_cli.exe>
'use strict';
const { spawnSync } = require('child_process'); const path = require('path');
const EXE = path.resolve(process.argv[2] || 'build/hks_cli.exe');

const HIBIKI = '仮面ライダー響鬼', FOURZE = '仮面ライダーフォーゼ', NEXUS = 'ウルトラマンネクサス', BLAZAR = 'ウルトラマンブレーザー';
//  [검색어, [제목·캐릭터...], 기대]
const CASES = [
  // 유빈 예시
  ['히비키', [HIBIKI], 1], ['가면 라이더 히비키', [HIBIKI], 1], ['가면라이더히비키', [HIBIKI], 1], ['가면 라이더', [HIBIKI], 1],
  ['포제', [FOURZE], 1], ['포 제', [FOURZE], 1], ['가면라이더 포제', [FOURZE], 1],
  // 일본어 그대로
  ['響鬼', [HIBIKI], 1], ['ひびき', [HIBIKI], 1], ['ヒビキ', [HIBIKI], 1], ['ﾋﾋﾞｷ', [HIBIKI], 1], ['フォーゼ', [FOURZE], 1], ['ふぉーぜ', [FOURZE], 1], ['ライダー', [FOURZE], 1],
  // 울트라맨 · 표기 흔들림
  ['넥서스', [NEXUS], 1], ['울트라맨 넥서스', [NEXUS], 1], ['울트라', [NEXUS], 1], ['네쿠사스', [NEXUS], 1], ['블레이저', [BLAZAR], 1], ['울트라맨 블레이저', [BLAZAR], 1], ['데카레인저', ['特捜戦隊デカレンジャー'], 1], ['레인저', ['特捜戦隊デカレンジャー'], 1], ['빌드', ['仮面ライダービルド'], 1], ['배트맨', ['バットマン'], 1], ['블루', ['ブルー'], 1], ['고쥬저', ['ゴジュウジャー'], 1], ['세이버', ['仮面ライダーセイバー'], 1], ['부레자', [BLAZAR], 1],
  ['쥬오쟈', ['動物戦隊ジュウオウジャー'], 1], ['주오우자', ['動物戦隊ジュウオウジャー'], 1], ['쥬우오우쟈', ['動物戦隊ジュウオウジャー'], 1], ['전대', ['動物戦隊ジュウオウジャー'], 1],
  ['덴오', ['仮面ライダー電王'], 1], ['가이무', ['仮面ライダー鎧武'], 1], ['류우키', ['仮面ライダー龍騎'], 1], ['류키', ['仮面ライダー龍騎'], 1],
  ['젯츠', ['仮面ライダーゼッツ'], 1], ['제츠', ['仮面ライダーゼッツ'], 1], ['고바스타즈', ['特命戦隊ゴーバスターズ'], 1], ['고버스터즈', ['特命戦隊ゴーバスターズ'], 1],
  // 캐릭터 이름으로
  ['겐타로', [FOURZE, '如月弦太朗'], 1], ['如月', [FOURZE, '如月弦太朗'], 1], ['켄고', [FOURZE, '如月弦太朗', '歌星賢吾'], 1],
  // 치는 중 (낱자)
  ['히비ㅋ', [HIBIKI], 1], ['ㅎ', [HIBIKI], 1], ['포ㅈ', [FOURZE], 1], ['넥ㅅ', [NEXUS], 1],
  // 영문 · 빈 검색
  ['w', ['仮面ライダーW'], 1], ['W', ['仮面ライダーW'], 1], ['', [HIBIKI], 1], ['   ', [HIBIKI], 1],
  // 안 맞아야 하는 것
  ['히비키', [NEXUS], 0], ['히비키', [FOURZE, '如月弦太朗'], 0], ['넥서스', [HIBIKI], 0], ['포제', [BLAZAR], 0], ['포제', [HIBIKI], 0],
  ['가면 히비키', [NEXUS], 0], ['히비키 넥서스', [HIBIKI], 0], ['키비', [HIBIKI], 0], ['이비', [HIBIKI], 0], ['zzqq', [HIBIKI, FOURZE], 0],
  ['울트라맨', [HIBIKI], 0], ['블레이저', [NEXUS], 0], ['빌드', [HIBIKI], 0], ['響', [FOURZE], 0],
];
//  분석기가 없는 PC: 한자 읽기는 못 하지만 글 그대로·가나 부분은 된다
const FAKE = [['響鬼', [HIBIKI], 1], ['라이더', [HIBIKI], 1], ['포제', [FOURZE], 1], ['히비키', [HIBIKI], 0], ['넥서스', [NEXUS], 1]];

const lines = [];
lines.push(JSON.stringify({ read: HIBIKI }));
for (const [q, hay] of CASES) lines.push(JSON.stringify({ q, hay }));
lines.push(JSON.stringify({ fake: 1 }));
for (const [q, hay] of FAKE) lines.push(JSON.stringify({ q, hay }));
const r = spawnSync(EXE, [], { input: lines.join('\n') + '\n', encoding: 'utf8' });
if (r.status !== 0) { console.log('X hks_cli 종료 코드 ' + r.status + ' ' + (r.stderr || '')); process.exit(1); }
const out = r.stdout.trim().split(/\r?\n/).map((l) => JSON.parse(l));
let fails = 0, n = 0;
const ok = (c, m) => { n++; if (!c) { fails++; console.log('X ' + m); } };
let i = 0;
const rd = out[i++].read;
ok(rd === 'かめんらいだーひびき', '읽기 분석기 響鬼 → ' + rd);
for (const [q, hay, want] of CASES) { const g = out[i++].m; ok(g === want, JSON.stringify(q) + ' / ' + hay.join(',') + ' → ' + g + ' (기대 ' + want + ')'); }
i++;
for (const [q, hay, want] of FAKE) { const g = out[i++].m; ok(g === want, '[분석기 없음] ' + JSON.stringify(q) + ' / ' + hay.join(',') + ' → ' + g + ' (기대 ' + want + ')'); }
console.log((fails ? 'X ' : 'OK ') + 'hks 검색 ' + (n - fails) + '/' + n);
process.exit(fails ? 1 : 0);
