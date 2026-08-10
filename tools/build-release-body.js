/*
 * 릴리스 본문 조립
 * ----------------
 * GitHub 릴리스 설명에는 언어 전환 기능이 없다. 대신 한국어 본문 첫 문단 바로 뒤에
 * 영어·일본어를 접기(<details>) 블록으로 넣어, 접힌 상태에서는 한 줄씩만 차지하고
 * 클릭하면 펼쳐지게 만든다.
 *
 *   node tools/build-release-body.js [출력경로]
 *
 * 출력은 이 스크립트가 직접 쓴다. 셸 리다이렉트(>)를 쓰면 PowerShell 이 BOM 을 붙이고,
 * 그게 릴리스 본문 첫 글자로 들어간다.
 */
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const read = (f) => fs.readFileSync(path.join(DIR, f), 'utf8').replace(/\r\n/g, '\n').trim();

const ko = read('RELEASE_NOTES.md');
const alt = [
  { label: 'English', hint: 'release notes in English', body: read('RELEASE_NOTES.en.md') },
  { label: '日本語', hint: '日本語のリリースノート', body: read('RELEASE_NOTES.ja.md') },
];

// <summary> 다음의 빈 줄이 없으면 GitHub 이 안쪽 마크다운을 렌더링하지 않는다
const fold = ({ label, hint, body }) =>
  `<details>\n<summary><b>${label}</b> — ${hint}</summary>\n\n${body}\n\n</details>`;

// 첫 문단(요약 한 줄) 뒤에 끼워 넣는다. 한국어 독자는 한 줄씩만 더 보이고,
// 다른 언어 독자는 스크롤 없이 바로 찾을 수 있다.
const lines = ko.split('\n');
const cut = lines.indexOf('');
if (cut < 0) throw new Error('RELEASE_NOTES.md 에서 첫 문단을 찾지 못했습니다');

const out = [
  lines.slice(0, cut).join('\n'),
  '',
  alt.map(fold).join('\n\n'),
  '',
  lines.slice(cut + 1).join('\n'),
].join('\n') + '\n';

const dest = process.argv[2] || path.join(DIR, '..', 'dist-release', 'release-body.md');
fs.mkdirSync(path.dirname(dest), { recursive: true });
fs.writeFileSync(dest, out, 'utf8');   // BOM 없이
// stdout 으로 낸다 — PowerShell 은 네이티브 명령의 stderr 를 오류로 취급한다
console.log(`릴리스 본문: ${dest} (${out.split('\n').length}줄)`);
