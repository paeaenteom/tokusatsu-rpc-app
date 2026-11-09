**한국어** · [English](README.en.md) · [日本語](README.ja.md)

# TOKU RPC

특촬 스트리밍 시청 정보를 **Discord 프로필 카드**로 보여 주고, **정주행 기록**을 Discord 웹훅으로 남기는 Windows 도구.

지원 사이트
- 東映特撮ファンクラブ (TTFC) — `pc.tokusatsu-fc.jp`
- TSUBURAYA IMAGINATION — `imagination.m-78.jp`
- YouTube — `www.youtube.com`
- Disney+ — `www.disneyplus.com` *(실험)*
- 치지직 (CHZZK) — `chzzk.naver.com`
- dアニメストア — `animestore.docomo.ne.jp`

> **C 버전** — C11 + Win32 API 만으로 다시 만든 앱입니다 (외부 라이브러리·런타임 없음, exe 하나 약 1.1MB).
> 베타 0.1.0 · 테스트 버전(알파) 0.2.0. 개인용으로 만든 도구라 버그가 있을 수 있어요.
> 예전 Electron 버전(0.3.1 까지)은 [`v0.3.1-beta` 태그](https://github.com/paeaenteom/tokusatsu-rpc-app/tree/v0.3.1-beta)에 남아 있습니다.

---

## 구성

```
toku-native/        C 앱 — Discord RPC 송출, 정주행 기록, 설정 창, 트레이
  src/base          바이트 버퍼 · JSON · SHA-1/Base64 · UTF-8
  src/net           WebSocket 서버(127.0.0.1:7690) · WinHTTP
  src/discord       Discord IPC (이름 있는 파이프)
  src/core          카드 엔진 (사이트별 클라이언트 · 전송 대기열 · 자동 해제)
  src/app           창(GDI 직접 그리기) · 트레이 메뉴 · 웹훅 · 배경 · 키 설정 · 보충 기록
  src/setup         설치 프로그램 (앱·확장을 담아 풀어 설치 · 제거)
  tests/            Node 시험 하네스 (가짜 Discord · 가짜 확장)
ttfc-chrome-ext/    크롬 확장 — 사이트에서 시청 정보 추출, 영상 버튼 키
tools/              sync-chrome-ext.js — 압축 해제로 로드한 확장 사본을 작업 폴더와 맞춤
```

앱과 확장은 로컬 WebSocket(`127.0.0.1:7690`)으로만 통신합니다.
시청 정보가 외부로 나가는 곳은 **Discord**, 설정했다면 **본인 웹훅 채널**, 그리고 Discord 가 못 여는 썸네일을 올리는 이미지 호스트(본인 Worker 또는 litterbox·uguu·catbox)뿐입니다.

---

## 빌드

필요한 것: Windows 10/11 x64, [WinLibs](https://winlibs.com/) GCC (mingw-w64 UCRT, `windres` 포함)

```bat
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

`toku-native\build-app.bat` 을 실행하면 `toku-native\build\TOKU-RPC-beta.exe` 가 만들어집니다 (시험 없이 앱만).

> `build.bat` 은 빌드 + 전체 시험입니다. 시험 일부는 예전 Electron 버전의 JS 코드와 결과를 한 글자씩 비교하므로
> Node.js 24 와 `v0.3.1-beta` 의 `ttfc-app` 소스가 필요합니다 (시험 파일 안의 경로 참고).
> 화면 문구 `src/app/strings.h` 는 생성된 파일이 들어 있어 따로 만들 필요가 없습니다.

---

## 설치

1. [릴리스](https://github.com/paeaenteom/tokusatsu-rpc-app/releases/latest)에서 **`TOKU-RPC-Setup.exe`** 를 받아 실행합니다. 관리자 권한은 필요 없어요.
   - 앱을 `%LOCALAPPDATA%\Programs\TOKU RPC Beta` 에 넣고, 시작 메뉴 바로 가기와 '앱 및 기능' 제거 항목을 만든 뒤 앱을 실행합니다.
   - 이미 돌고 있으면 정상 종료한 뒤 바꿉니다. 설정·웹훅 데이터는 그대로 남습니다.
   - 서명되지 않은 exe 라 Windows SmartScreen 이 경고할 수 있어요 → **추가 정보 → 실행**.
2. 아래 `secrets.json` 을 만듭니다 (없으면 설치 끝 화면에서 알려 줍니다).
3. 크롬 확장: 설치 끝 화면의 **확장 폴더 열기** → 크롬 `chrome://extensions` → 개발자 모드 → **압축해제된 확장 프로그램 로드** → 그 폴더(`chrome-ext`).
   확장 파일이 바뀐 뒤에는 확장 팝업의 **새 버전 적용**을 누르면 됩니다.

제거는 '설정 → 앱 → 설치된 앱' 의 TOKU RPC, 또는 설치 폴더의 `Uninstall TOKU RPC.exe`. 설정·웹훅 데이터는 고른 경우에만 휴지통으로 갑니다.
직접 빌드했다면 `toku-native\build-setup.bat` 으로 같은 설치 파일을 만들 수 있습니다 (Node.js 필요).

### secrets.json (필수)

C 버전은 Discord 애플리케이션 ID 를 코드에 넣지 않습니다. 본인의 애플리케이션을 만들어 넣어 주세요.

```
%APPDATA%\toku-rpc-native\secrets.json
```

```json
{
  "discordAppIds": {
    "ttfc": "앱ID", "imagination": "앱ID", "youtube": "앱ID",
    "disneyplus": "앱ID", "chzzk": "앱ID", "danime": "앱ID"
  },
  "bingeWebhookUrl": "선택 — 정주행 기록 기본 웹훅 URL"
}
```

[Discord Developer Portal](https://discord.com/developers/applications) 의 각 앱 **Rich Presence → Art Assets** 에 아래 이름으로 이미지를 올립니다.

| 사이트 | 에셋 |
|---|---|
| ttfc | `ttfc_logo`, `play`, `pause` (+ 시리즈 배너 `kamen_rider_logo` 등) |
| imagination | `tsuburaya_imagination_logo`, `play`, `pause` |
| youtube | `logo`, `play`, `pause`, `live` |
| disneyplus | `disney__logo`, `play`, `pause` |
| chzzk | `chzzk_logo`, `play`, `pause`, `live` |
| danime | `danime_logo`, `play`, `pause` |

> 이 파일은 **본인 PC에만** 둡니다. 웹훅 URL 은 비밀번호와 같아서, 아는 사람은 누구나 그 채널에 글을 쓸 수 있어요.
> 작품별 웹훅·캐릭터는 앱의 **웹훅** 탭에서 추가하면 같은 파일(`titleWebhooks`)과 설정에 저장됩니다.
> 썸네일을 본인 Cloudflare Worker 로 올리려면 `"avatarUpload": { "url": "...", "token": "..." }` 를 더합니다 (없으면 공개 호스트 사용).

---

## 쓰는 법

설정 창 탭
- **활동** — 지금 Discord 에 보이는 카드 (Discord 와 같은 모양), 연결 상태, 정주행 모드 스위치
- **표시 설정** — 진행 시간 모양, 작품·화·썸네일·버튼 보이기, 활동 종류, 사이트별 상태 표시
- **앱 설정** — 언어, RPC 켜고 끄기, 로그온 시 시작, **테스트 버전 (알파)** 스위치 (바꾸면 앱이 다시 시작)
- **키 설정** — 영상 플레이어 버튼마다 키 (크롬 확장이 그 버튼을 누름)
- **배경 꾸미기** — 창 바탕 사진 (맞춤·위치·확대·흐림·밝기)
- **웹훅** — 작품별 정주행 웹훅 폴더 · 아이콘 · 캐릭터(이름·프로필 사진)
- **보충** — 확장 없이 본 화를 나중에 기록. **링크로 불러오기**(TTFC·IMAGINATION 에피소드 링크) 또는 **직접 입력**(DVD 특전·극장 한정 영상 등: 작품·화·본 시각·출처·이미지)

트레이 아이콘을 오른쪽 클릭하면 확장·Discord 응답 시간(ms)과 스위치가 있는 메뉴가 열립니다.

### 베타 / 테스트 버전 (알파)

exe 하나에 두 모드가 있습니다. 평소엔 **베타**, 앱 설정의 스위치를 켜면 앱이 다시 시작하며 **테스트 버전(알파)** 으로 뜹니다.
알파는 Discord 클라이언트의 presence 전송 한도(20초에 5번)에 맞춰 보내는 새 방식을 먼저 써 봅니다
(같은 내용은 세지 않고, 재생·일시정지·위치 이동이 잇따르면 0.35초 모아 마지막 상태만 보냄).

### 데이터 위치

`%APPDATA%\toku-rpc-native\` — `config.json`(설정) · `secrets.json` · `logs\main.log` · `backgrounds\` · `hook-avatars\` · `thumb-cache.json`

---

## 변경 이력

[CHANGELOG.md](CHANGELOG.md)

## 라이선스

MIT
