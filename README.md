**한국어** · [English](README.en.md) · [日本語](README.ja.md)

# TOKU RPC

특촬 스트리밍 시청 정보를 **Discord 프로필에 표시**하고, **정주행 기록**을 남기는 도구.

지원 사이트
- 東映特撮ファンクラブ (TTFC) — `pc.tokusatsu-fc.jp`
- TSUBURAYA IMAGINATION — `imagination.m-78.jp`
- Disney+ — `www.disneyplus.com` *(실험)*

> **beta 0.2.7** — 개인용으로 만든 도구입니다. 버그가 있을 수 있어요.

---

## 설치

[릴리스](https://github.com/paeaenteom/tokusatsu-rpc-app/releases/latest)에서
**`TOKU-RPC-Setup-<버전>.exe`** 하나만 받아 실행하세요. 앱이 안에 들어 있어 추가로 받을 파일이 없습니다.

설치 창에서 **확장을 넣을 브라우저를 골라** 체크하면 됩니다.
설치된 브라우저를 자동으로 찾아 목록에 보여줍니다.

| 지원 브라우저 |
|---|
| Google Chrome · Microsoft Edge · 네이버 웨일 · Brave · Vivaldi |

설치 프로그램이 하는 일
1. 앱 설치 (`%LOCALAPPDATA%\Programs\TOKU RPC`) — 내 계정 폴더, 권한 상승 없음
2. 선택한 브라우저에 확장 자동 등록
3. 바탕화면 바로가기 생성 · 앱 실행 *(옵션, 체크 해제 가능)*

2번에서 **관리자 승인 창이 한 번** 뜹니다 → **[예]**.
Windows가 확장 정책 영역(`SOFTWARE\Policies`)을 관리자에게만 열어두기 때문이고,
이 단계에서만 승격합니다. 거절해도 앱은 정상 설치되며, 확장만 아래 수동 방법으로 넣으면 됩니다.

설치 후 **브라우저를 완전히 종료했다가 다시 켜주세요.**
(트레이 아이콘까지 닫아야 확장이 적용됩니다)

> 조용히 설치하려면 `TOKU-RPC-Setup-<버전>.exe /S` — 감지된 모든 브라우저에 등록합니다.
> 설치 기록은 `%TEMP%\toku-rpc-install.log` 에 남습니다.

<details>
<summary>확장이 자동으로 안 잡힐 때 (수동 설치)</summary>

1. 릴리스에서 `toku-rpc-extension.zip` 을 받아 압축을 풉니다
2. `chrome://extensions` 접속 → 우측 상단 **개발자 모드** 켜기
3. **압축해제된 확장 프로그램을 로드** → 압축 푼 폴더 선택

</details>

---

## 쓰는 법

앱을 켜두고 지원 사이트에서 영상을 보면 됩니다. 나머지는 자동입니다.

- 시청 중에는 **작품명 · 에피소드 · 진행 시간 · 썸네일**이 Discord에 표시됩니다
- 둘러보는 중에는 페이지 종류(작품 페이지 · 시리즈 목록 등)가 표시됩니다
- 앱은 트레이에 상주하며, PC를 켜면 자동으로(창 없이) 실행됩니다
  - 트레이 아이콘 우클릭에서 RPC 켜기/끄기, 자동 시작 끄기 가능

### 정주행 모드

확장 아이콘을 눌러 **정주행 모드**를 켜면:

- 영상이 **끝까지 재생되면** 자동으로 다음 화로 넘어갑니다
- 전체화면으로 보고 있었다면 다음 화에서도 **전체화면을 유지**합니다
- 에피소드마다 Discord 웹훅으로 **시청 기록**을 남깁니다 (아래 설정 필요)
- 마지막 화까지 보면 "정주행 끝" 기록을 남기고 자동으로 꺼집니다

> **일시정지는 정주행을 끝내지 않습니다.** 잠깐 멈추든, 탭을 옮기든, 한참 뒤에 이어보든
> 정주행 모드는 그대로 유지됩니다. 종료는 *마지막 화 완주* 또는 *직접 토글 OFF* 뿐입니다.

### 언어

**한국어 · English · 日本語** 중에 고를 수 있습니다. 기본값은 **시스템 언어**입니다.
앱은 미니 창에서, 확장은 팝업에서 바꿉니다.

확장을 "시스템 언어"로 두면 **앱에서 고른 언어를 따라갑니다.** 한쪽만 바꿔도 둘 다 바뀝니다.
Discord에 표시되는 문구와 정주행 기록도 같은 언어로 나갑니다.

---

## 정주행 기록(웹훅) 설정 — 선택

정주행 기록을 Discord 채널에 남기고 싶다면, **본인의 웹훅 주소**를 등록하세요.
(등록하지 않아도 나머지 기능은 모두 정상 동작합니다.)

1. Discord에서 기록을 남길 채널 → **채널 편집 → 연동 → 웹후크 → 새 웹후크** → URL 복사
2. 아래 경로에 `secrets.json` 파일을 만들고 URL을 넣습니다

```
%APPDATA%\toku-rpc\secrets.json
```

```json
{
  "bingeWebhookUrl": "여기에_복사한_웹훅_URL"
}
```

3. 앱을 다시 실행합니다

> 이 파일은 **본인 PC에만** 저장되며 저장소·설치 파일에는 포함되지 않습니다.
> 웹훅 URL은 비밀번호와 같습니다. 아는 사람은 누구나 그 채널에 글을 쓸 수 있으니
> 공개된 곳(스크린샷·저장소 등)에 올리지 마세요.

<details>
<summary>Discord 애플리케이션을 직접 쓰고 싶다면</summary>

기본값으로 제작자의 Discord 애플리케이션을 사용합니다(표시 이름 `TTFC` / `IMAGINATION`).
본인 애플리케이션으로 바꾸려면 같은 `secrets.json` 에 추가하세요.

```json
{
  "discordAppIds": { "ttfc": "앱ID", "imagination": "앱ID" }
}
```

이 경우 [Discord Developer Portal](https://discord.com/developers/applications) 의
**Rich Presence → Art Assets** 에 아래 이름으로 이미지를 올려야 합니다.

| 앱 | 필요한 에셋 |
|---|---|
| TTFC | `ttfc_logo`, `play`, `pause`, `kamen_rider_logo`, `super_sentai_series_logo`, `metal_hero_series_logo`, `project_r_e_d_logo` |
| IMAGINATION | `tsuburaya_imagination_logo`, `play`, `pause` |

</details>

---

## 구성

```
ttfc-app/           Electron 앱 — Discord RPC 송출, 정주행 기록, 트레이 상주
ttfc-chrome-ext/    크롬 확장 — 사이트에서 시청 정보 추출
tools/              설치 스크립트 · 릴리스 빌드 스크립트
```

앱과 확장은 로컬 WebSocket(`127.0.0.1:7690`)으로만 통신합니다.
시청 정보가 외부로 나가는 곳은 **Discord**와, 설정했다면 **본인 웹훅 채널**뿐입니다.

### 직접 빌드

```powershell
cd ttfc-app; npm install          # 최초 1회
powershell -File tools\build-release.ps1
```

`dist-release\` 에 `TOKU-RPC-Setup-<버전>.exe` · 확장(CRX/ZIP) · `update.xml` 이 생성됩니다.
이 파일들을 그대로 릴리스에 첨부하면 됩니다.

> 확장 서명키 `ttfc-chrome-ext-key.pem` 은 저장소에 없습니다.
> 이 키가 확장 ID를 결정하므로 **잃어버리면 기존 사용자의 자동 업데이트가 끊깁니다.**

---


## 백신이 "바이러스가 발견됨" 이라고 할 때

**오탐입니다.** 코드 서명 인증서가 없는 개인 개발자의 설치 프로그램에서 흔히 일어납니다.

Windows Defender 가 붙이는 이름은 `Trojan:Win32/Sabsik.EN.B!ml` 이고, 끝의 **`!ml` 은
"머신러닝이 추측했다"** 는 표시입니다. 알려진 악성코드와 일치한 게 아니라,
*서명 없음 · 자체 압축 해제 · 레지스트리 수정 · 관리자 권한 요청* 조합이 통계적으로
수상해 보인다는 뜻입니다. 설치 프로그램이면 원래 다 하는 일입니다.

**의심스러우면 받지 않으셔도 됩니다.** 확인하거나 우회하는 방법:

- **설치 프로그램 코드까지 전부 공개돼 있습니다** — [`tools/installer/Installer.cs`](tools/installer/Installer.cs)
- **압축본을 쓰세요** — 릴리스의 `TOKU-RPC-<버전>-portable.zip` 은 실행 파일이 아니라 이 판정을
  타지 않습니다. 풀고 `TOKU RPC.exe` 실행 (확장은 `toku-rpc-extension.zip` 으로 수동 등록)
- **[Microsoft 에 오탐 신고](https://www.microsoft.com/en-us/wdsi/filesubmission)** — 보통 며칠 안에 풀립니다

서명 인증서를 사면 근본적으로 해결되지만, 개인 프로젝트라 아직 없습니다.
## 알아둘 점

- Windows 전용입니다 (Electron 기준으로는 macOS 빌드도 가능하지만 검증하지 않았습니다)
- IMAGINATION 썸네일은 사이트가 외부 접근을 막아 Discord가 직접 불러오지 못합니다.
  그래서 이미지를 임시 호스트에 올려 표시합니다 (litterbox → uguu 순으로 시도)
- 이 도구는 시청 정보를 표시할 뿐, 영상을 내려받거나 저작권 보호를 우회하지 않습니다

## 변경 이력

버전마다 달라진 점은 [CHANGELOG.md](CHANGELOG.md) 에 있습니다.

## 라이선스

MIT
