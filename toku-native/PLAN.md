# TOKU RPC — C 버전 (toku-native)

지금 쓰는 앱(`ttfc-app`, Electron 28.3.3)을 **C + Windows 기본 API만으로** 다시 만든다.
외부 라이브러리 없음. 컴파일러는 WinLibs gcc(mingw64, UCRT).
크롬 확장(`ttfc-chrome-ext`)은 그대로 쓴다 — 확장이 보는 동작(포트 7690, 메시지)은 지금 앱과 똑같아야 한다.

## 교체 방식 (유빈 결정 2026-11-07)
- 지금 앱은 계속 쓴다. C 버전은 옆에서 따로 만들고 테스트한다.
- 유빈이 "된 것 같다" 하면 그때 교체한다. 그 전엔 설치본·포트 7690·Discord 를 건드리지 않는다.
- 테스트는 콘솔 프로그램 + Node 가짜 상대(가짜 확장·가짜 Discord 파이프)로. 포트는 17695~17699.
- 창을 띄우는 실행은 하지 않는다 (CLAUDE.md 규칙). 화면은 유빈이 직접 확인한다.

## 교체 전까지 절대 건드리지 않는 것 (유빈 확인 2026-11-07: "지금 작동하는 거에는 지장 안 가게")
- `ttfc-app\`·`ttfc-chrome-ext\` 소스와 설치본(`%LOCALAPPDATA%\Programs\TOKU RPC`) — C 작업 때문에 고치지 않는다
- 포트 7690 (C 테스트는 17695~17699 만)
- 진짜 Discord 파이프(`discord-ipc-0~9`) — C 테스트는 이름이 다른 가짜 파이프만. App ID 로 Discord 에 붙지 않는다
- `%APPDATA%\toku-rpc\` (config.json·secrets.json·배경·로그) — 읽지도 쓰지도 않는다. 테스트는 임시 폴더의 가짜 설정
- 작업 스케줄러 "TOKU RPC Fast Start", Run 키(자동 시작), ttfc:// 등록, 크롬 확장 사본
- C 결과물은 콘솔 테스트로만 돌린다 (창·트레이를 띄우지 않음)
- 교체는 유빈이 "된 것 같다" 한 뒤, 기존 설치본 백업 후에만

## 왜
| | Electron (실측 2026-11-07) | C 목표 |
|---|---|---|
| 설치 폴더 | 259MB | 수 MB |
| 메모리 | 277MB (프로세스 4개) | 수십 MB 이하 |
| 지원 종료·크롬 버그 | 28 은 2024-06 지원 종료, 투명 창 제목 띠 버그 | 없음 |

## 단계
1. **기반** — 문자열, JSON 읽기·쓰기, SHA-1, Base64, UTF-8↔UTF-16  ✅ JS 교차 검증 8,126건 + 퍼징 30,000건
2. **연결** — WebSocket 서버(확장), Discord IPC 파이프  ✅ ws 8 기준 서버와 64건 일치, discord-rpc 와 바이트 일치 26건, 퍼징 400연결
3. **핵심 동작** — 사이트별 Discord 카드(discord-rpc.js), 전송 한도, idle, 재연결, 썸네일 업로드
   3a ✅ 카드 엔진(src/core): 진짜 discord-rpc.js·extension-bridge.js 와 차이 검사 — 시나리오 1,000개·기록 77만 줄 일치,
      심은 버그 7개 모두 잡음(tests/mutate.js), 버튼 주소 4,066건 일치, 엉망인 메시지 17,403개에 안 죽음
   3b ✅ 썸네일 업로드 (Worker → litterbox·uguu·catbox, 재시도 15·30·60초, thumbGate). thumb-cache.json 은 읽기만 (알파는 저장 안 함)
   **알파 실사용 시작 (2026-11-07 20:20)**: `%LOCALAPPDATA%\Programs\TOKU RPC Alpha\TOKU-RPC-alpha.exe` (기존 앱은 유빈이 끔)
     · 기존 config.json·secrets.json·thumb-cache.json 은 읽기만, 로그는 `%APPDATA%\toku-rpc-native\logs\main.log`
     · 포트 7690 을 잡은 뒤에야 Discord 에 붙는다. `--quit` 으로 정상 종료
     · 시험 모드(TOKU_NATIVE_TEST_PORT/PIPE): 잠금·창 이름·로그 폴더를 따로 써서 진짜 알파를 건드리지 않는다 (tests/alpha-smoke.js 18건)
     · 설정: 알파 전용 %APPDATA%\toku-rpc-native\config.json (처음 켤 때 기존 앱 config.json 을 복사, 기존 파일은 읽기만). 시험 모드는 toku-rpc-native-test
     · 직접 켜면 창이 열림(--hidden 이면 트레이만), 이미 켜져 있으면 그 창을 연다. 트레이 클릭·'창 열기' 도 같음
     · 시험 모드 + TOKU_NATIVE_TEST_SNAP=폴더 → 끝낼 때 6개 탭을 100%·150% 로 메모리 DC 에 그려 BMP 저장 (화면에 안 띄움)
     · 창: Windows 제목 줄 없음(WM_NCCALCSIZE) — 머리줄에 최소화·최대화·닫기 직접 그림(HTMAXBUTTON 이라 스냅 레이아웃 됨)
     · 카드: Discord 활동 카드 모양. 그림은 src/app/img.c (https 만, 8MB·10초 제한, WIC 디코딩, 실패 30초 뒤 재시도), 에셋 키는 공개 API oauth2/applications/<id>/assets → cdn.discordapp.com/app-assets (⚠ 키에 App ID 가 있어 로그 안 남김)
     · 자동 시작(11/7): 알파 전용 이름 — 작업 "TOKU RPC Alpha Fast Start" / Run "TOKU RPC Alpha". 설치본 경로에서만 등록, 시험은 …TEST 이름(tests/autostart-test.js)
     · 11/7 유빈 결정 "알파만 부팅 시 켜기": 지금 앱 작업 "TOKU RPC Fast Start" 는 지우지 않고 Disabled 만. ⚠ 지금 앱을 한 번이라도 켜면 시작할 때 작업을 -Force 로 다시 등록해 되살아난다. 되돌리기: Enable-ScheduledTask "TOKU RPC Fast Start"
     · 키 설정(11/7): src/app/keybinds.c = keybinds.js 를 그대로 (tests/kb-diff.js 로 진짜 JS 와 무작위 명령 비교), 고르기 흐름 KB_PICK·KEYBIND_* (tests/kb-pick-test.js). 시험 모드 전용 메시지 TEST_KB_PICK·TEST_KB_CANCEL
       ⚠ 일부러 다른 점: 30·40·60·80자 자르기가 이모지 가운데면 JS 는 반쪽 서로게이트(�)를 두고 C 는 U+FFFD (C JSON 은 반쪽 서로게이트를 안 받으므로)
     · 정주행 기록(11/8): src/app/binge.c = binge-webhook.js (안정화 6초·재기록 10분·썸네일 대기 20초·첨부/주소·완주), 카드 색 src/app/accent.c = thumb-color.js.
       tests/binge-diff.js(진짜 JS 와 고르기·캐릭터·색 비교) · tests/binge-flow-test.js(로컬 HTTP 서버로 실제 전송). 시험 전용 환경 변수 TOKU_NATIVE_TEST_SECRETS
       ⚠ 지금 앱도 기록기에 isPlaying·duration 을 안 넘겨 '바로 보내기(1.5초)' 가 한 번도 안 탄다 — 알파도 같게 (늘 6초)
       ⚠ 일부러 다른 점: 색 고르기 전 그림 줄이기가 Skia→WIC 라 아주 조금 다를 수 있음. 작품명 소문자는 Windows 불변 로캘
     · 웹훅 탭(11/8): src/app/hooks.c = secrets.js 작품별 웹훅 + main.js 폴더·캐릭터 (tests/hk-diff.js). 저장은 알파 전용 toku-rpc-nativesecrets.json(처음 고칠 때 복사).
       프사 조절 창·올리기(Worker → catbox → litterbox) · 폴더 아이콘(64px PNG) — src/app/imgutil.c (tests/iu-test.js). 원본 사진은 toku-rpc-nativehook-avatars
       ⚠ 일부러 다른 점: 그림 읽기가 Chromium → WIC 라 AVIF 등은 Windows 코덱이 없으면 못 읽음 ('쓸 수 없는 그림'). 펼친 폴더는 앱을 끄면 잊음
     · 배경 꾸미기(11/8): src/app/bg.c = appearance.js (tests/bg-diff.js · bg-flow-diff.js). 그리기는 CSS(object-fit·위치·확대·blur·opacity·어둡게)를 C 로, 카드·줄 86% 반투명.
       폴더 toku-rpc-native\backgrounds — 지금 앱 사진은 처음 쓸 때 읽어서 복사 (원본 안 건드림). ⚠ 흐림은 가우스 → 상자 흐림 3번 근사
     · 보충 탭(11/8): src/app/backfill.c = extension-bridge.js _onResolved·BF_LINK·썸네일 풀기 (tests/bf-diff.js), main.c RESOLVE_PAGE 왕복·backfill-send 검사,
       binge.c binge_send_backfill (tests/backfill-flow-test.js). ⚠ 본 시각은 글 칸 'YYYY-MM-DD HH:MM' (지금 앱은 datetime-local 고르기)
     · 지연(11/8): 메인 반복이 확장 소켓(WSAEventSelect)·Discord 파이프(겹친 읽기) 이벤트를 기다려 받는 순간 깬다 — 예전엔 10ms 반복이
       Windows 타이머 칸(15.6ms)에 묶여 확장·Discord 둘 다 ~15.7ms. 지금 확장 ~0.3ms · Discord(가짜) ~0.7ms (tests/latency-bench.js, 기준 3ms)
       트레이 표시는 최근 5번 중간값 (src/base/median.h, 확장은 1분 안 값만) — 크롬·Discord 가 가끔 늦게 답해 11↔1ms 로 튀던 것 (tests/latency-jitter-test.js)
     · 베타/테스트 버전(11/8): exe 하나(Programs\TOKU RPC Beta\TOKU-RPC-beta.exe), 설정 testChannel 로 모드 — 스위치를 바꾸면 --restart 로 스스로 다시 시작.
       테스트 버전(알파 0.2.0) = 전송 V2: Discord 클라이언트와 같은 20초 5번 창(모든 사이트 공유·같은 내용은 안 셈) + 재생·정지·이동 0.35초 모으기
       (Discord 웹 JS 분석: ConnectionStore presence didCommit — 메모리 discord-presence-throttle). 베타(0.1.0) 는 지금 앱과 같은 방식(core-diff 대상)
       tests/presence-v2-test.js · channel-switch-test.js. 예전 알파 설치본·부팅 항목은 베타가 시작할 때 정리
     · 보충 직접 입력(11/8): TTFC·IMAGINATION 에 없는 특전 영상 — 작품 고르기(웹훅 폴더)·화·본 시각·출처 표시·이미지(https 링크는 보낼 때 받아 첨부,
       못 받으면 링크 그대로 / 파일 png·jpg·gif·webp 8MB). main.c app_bf_send_manual · tests/backfill-flow-test.js 직접 입력 9건
     · 설치 프로그램(11/9): src/setup/setup.c + build-setup.bat → build/TOKU-RPC-Setup.exe (앱·확장 내장, 크롬 정책 X, asInvoker). tests/setup-test.js
       GitHub: paeaenteom/tokusatsu-rpc-app main = C 버전 (Electron 은 v0.3.1-beta 태그), 릴리스 c-v0.1.0-beta 에 Setup·앱 exe
     · 아직 없는 것: 업데이트 확인 (자동 시작·썸네일 캐시·키 설정은 11/7 완료)
   ⚠ Claude 데스크톱(MSIX)에서 띄운 프로그램이 %APPDATA% 에 **새 폴더**를 만들면 Claude 전용 가상 폴더로 간다
     (`%LOCALAPPDATA%\Packages\Claude_…\LocalCache\Roaming`) — 진짜 알파는 explorer 로 띄워야 진짜 폴더를 쓴다
4. **트레이** — 지금 앱 tray-menu 와 같은 모양의 직접 그린 메뉴(11/8, src/app/traymenu.c: 머리·확장/Discord 지연 점·아이콘·스위치·빨간 종료, 스위치는 메뉴 유지, 못 띄우면 기본 메뉴).
   시험 그림 tests/alpha-tray-snap.js (TEST_TRAY_SNAP). 단일 실행·--quit·--hidden
5. **정주행 웹훅** — 작품별 웹훅·캐릭터·프사, 확인 시간, 썸네일
6. **설정 창** — 활동·표시 설정·앱 설정 완료(11/7, GDI 직접 그리기). 키 설정·배경·웹훅 탭은 남음
7. **나머지** — 빠른 시작(작업 스케줄러), 자동 시작, ttfc://, 업데이트 확인, 시청 기록, ko/en/ja
8. **교체** — 설치 프로그램, 기존 설정(config.json·secrets.json) 그대로 읽기

## 지금 앱과 똑같아야 하는 것 (확인 목록)
- WebSocket: `ws://127.0.0.1:7690`, Origin 없음 또는 `chrome-extension://[a-p]{32}` 만, 메시지 최대 16MB, ping→pong
- 확장 → 앱: VIDEO, BROWSING, CLEAR, SET_ACTIVITY, THUMB_BYTES, RESOLVED, PING, BINGE_STATE, BINGE_END,
  KEYBIND_PICK_START, KEYBIND_PICKED, KEYBIND_PICK_FAIL, KEYBIND_FS, KEYBINDS_LOCAL
- 앱 → 확장: CONNECTED{lang,booster,level}, KEYBINDS, KB_PICK, KB_PICK_CANCEL, SET_BINGE, RESOLVE_PAGE,
  NEED_THUMB, LANG, BOOSTER, PONG
- 사이트 6개: ttfc, imagination, youtube, disneyplus, chzzk, danime — 사이트별 App ID 는 secrets 경로로만 읽는다
- Discord 제한: details·state·large_text ≤128자(UTF-16), 버튼 URL ≤512자, SET_ACTIVITY 5회/20초
- 비밀값(App ID·웹훅·업로드 토큰)은 `%APPDATA%\toku-rpc\secrets.json` 에서만 읽고 로그·문서에 쓰지 않는다
- ⚠ 일부러 다른 점: 지금 앱은 TTFC·IMAGINATION·YouTube·Disney+ 의 App ID 를 코드에 갖고 있다(secrets 에 없으면 그걸 씀).
  C 는 코드에 ID 를 두지 않는다 → **교체 전 secrets.json 의 discordAppIds 에 6개 사이트가 모두 있는지 확인** (값은 출력하지 않고 있는지만)
- 일부러 다른 점: 버튼 주소의 비ASCII 도메인·xn--·IPv6 는 해석하지 않고 사이트 홈으로 (확장이 보내는 주소엔 나오지 않음)
- 일부러 다른 점: 확장 메시지의 글자 칸에 숫자·객체가 오면 JS 는 TypeError 로 멈추기도 한다 → C 는 글자로 바꾸거나 빈 값으로 (안 죽음)

## 보안 원칙 (C 라서 더 조심)
- 밖에서 들어오는 모든 입력(확장 메시지, Discord 응답, HTTP 응답, 설정 파일)은 길이·깊이 제한을 두고 읽는다
- 고정 크기 버퍼에 복사하지 않는다 — 늘어나는 버퍼(sbuf)만 쓴다. 할당 실패도 처리한다
- 모듈마다 경계값·깨진 입력 테스트를 둔다

## 빌드·테스트
- `build.bat` — 전체 테스트 빌드 후 실행 (콘솔)
