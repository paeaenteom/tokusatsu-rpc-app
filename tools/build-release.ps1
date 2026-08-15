<#
  릴리스 자산 빌드
  ----------------
  GitHub Releases에 올릴 파일을 dist-release\ 에 만든다.

    TOKU-RPC-Setup-<버전>.exe   설치 프로그램 — 앱이 안에 들어 있어 이것만 받으면 된다
    toku-rpc-extension.crx      크롬 확장 (설치 프로그램이 등록한 정책이 받아가는 서명본)
    update.xml                  크롬이 확장 버전을 확인하는 매니페스트
    toku-rpc-extension.zip      확장 수동 로드용 (자동 설치가 막혔을 때)

  앱 본체 ZIP은 따로 올리지 않는다. Setup.exe 안에 이미 들어 있다.

  서명키(ttfc-chrome-ext-key.pem)는 저장소에 없다. 프로젝트 루트에 두면
  확장 ID가 항상 같게 유지된다. 키가 바뀌면 ID도 바뀌어 자동 설치가 끊긴다.
#>

$ErrorActionPreference = 'Stop'
$ROOT   = Split-Path $PSScriptRoot -Parent
$APP    = Join-Path $ROOT 'ttfc-app'
$EXT    = Join-Path $ROOT 'ttfc-chrome-ext'
$KEY    = Join-Path $ROOT 'ttfc-chrome-ext-key.pem'
$OUT    = Join-Path $ROOT 'dist-release'
$EXT_ID = 'dciaobllfdcegjcdmimclgglapnhggjm'
$REPO   = 'paeaenteom/tokusatsu-rpc-app'

function Step($m) { Write-Host "`n▶ $m" -ForegroundColor Cyan }

Remove-Item $OUT -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OUT | Out-Null

$appVer = (Get-Content (Join-Path $APP 'package.json') -Raw | ConvertFrom-Json).version
$extVer = (Get-Content (Join-Path $EXT 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
Write-Host "앱 v$appVer / 확장 v$extVer" -ForegroundColor White

# ── 1. 앱 빌드 ──────────────────────────────────────
#  NSIS 설치본은 코드서명 도구(winCodeSign) 압축 해제에 심볼릭 링크 권한이 필요해
#  환경에 따라 실패한다. 앱 패키징(win-unpacked) 자체는 항상 성공하므로
#  그 결과를 Setup.exe 안에 통째로 넣어 배포한다.
Step "앱 빌드"
Get-Process -Name 'TOKU RPC' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Push-Location $APP
npm run build 2>&1 | Select-String 'packaging|building block' | ForEach-Object { "  $_" }
Pop-Location

$unpacked = Join-Path $APP 'dist\win-unpacked'
if (-not (Test-Path (Join-Path $unpacked 'TOKU RPC.exe'))) { throw "앱 패키징 실패: $unpacked" }
Write-Host "  OK win-unpacked" -ForegroundColor Green

# electron-builder 가 이 환경에서 exe 아이콘을 못 갈아끼운다(위 NSIS 실패와 같은 원인).
# 그대로 두면 작업표시줄에 Electron 기본 아이콘이 뜬다.
& (Join-Path $PSScriptRoot 'set-exe-icon.ps1')

# 코드 서명 — Windows 11 Smart App Control 은 서명 없는 exe 를 무조건 차단한다.
#  인증서가 설정돼 있지 않으면 아무 일도 하지 않고 넘어간다 (tools\sign.ps1 주석 참고).
& (Join-Path $PSScriptRoot 'sign.ps1') -Path (Join-Path $unpacked 'TOKU RPC.exe') -Description 'TOKU RPC'

# ── 2. 확장 CRX (서명) ──────────────────────────────
Step "확장 패킹"
$chrome = @(
    "C:\Program Files\Google\Chrome\Application\chrome.exe",
    "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not (Test-Path $KEY)) { throw "서명키가 없습니다: $KEY (키가 없으면 확장 ID가 달라집니다)" }
if (-not $chrome) { throw "Chrome을 찾지 못해 CRX를 만들 수 없습니다" }

$stage = Join-Path $env:TEMP 'toku-ext-stage'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
robocopy $EXT $stage /E /NFL /NDL /NJH /NJS /NP | Out-Null
Remove-Item "$stage.crx" -Force -ErrorAction SilentlyContinue
& $chrome --pack-extension="$stage" --pack-extension-key="$KEY" --no-message-box | Out-Null
Start-Sleep -Seconds 5
if (-not (Test-Path "$stage.crx")) { throw "CRX 생성 실패" }
Copy-Item "$stage.crx" (Join-Path $OUT 'toku-rpc-extension.crx') -Force
Write-Host "  OK toku-rpc-extension.crx" -ForegroundColor Green

# ── 3. 확장 ZIP (수동 로드용) ───────────────────────
Compress-Archive -Path "$stage\*" -DestinationPath (Join-Path $OUT 'toku-rpc-extension.zip') -Force
Write-Host "  OK toku-rpc-extension.zip" -ForegroundColor Green
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue

# ── 3-2. 앱 무설치 ZIP ──────────────────────────────
#  서명 없는 설치 프로그램은 Windows Defender 의 머신러닝 판정에 걸려 다운로드가
#  통째로 삭제되는 일이 있다 (2026-08-15: Trojan:Win32/Sabsik.EN.B!ml — 오탐).
#  ZIP 은 실행 파일이 아니라 그 판정을 타지 않으므로, 막혔을 때 쓸 길을 같이 낸다.
#  압축을 풀고 'TOKU RPC.exe' 를 실행하면 된다. 확장은 위 zip 으로 수동 등록.
$portable = Join-Path $OUT ("TOKU-RPC-$appVer-portable.zip")
Remove-Item $portable -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $unpacked '*') -DestinationPath $portable -CompressionLevel Optimal -Force
Write-Host ("  OK TOKU-RPC-$appVer-portable.zip  {0:N1} MB" -f ((Get-Item $portable).Length/1MB)) -ForegroundColor Green

# ── 4. update.xml ───────────────────────────────────
Step "업데이트 매니페스트"
$crxUrl = "https://github.com/$REPO/releases/latest/download/toku-rpc-extension.crx"
$xml = @"
<?xml version='1.0' encoding='UTF-8'?>
<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>
  <app appid='$EXT_ID'>
    <updatecheck codebase='$crxUrl' version='$extVer' />
  </app>
</gupdate>
"@
# BOM 없이 저장한다 (브라우저의 XML 파서가 확장 자동 설치에 이 파일만 보고 판단한다)
[IO.File]::WriteAllText((Join-Path $OUT 'update.xml'), $xml, (New-Object Text.UTF8Encoding $false))
Write-Host "  OK update.xml (확장 v$extVer)" -ForegroundColor Green

# ── 5. 설치 프로그램 (앱 내장) ──────────────────────
Step "설치 프로그램 빌드"
& (Join-Path $PSScriptRoot 'build-installer.ps1')
& (Join-Path $PSScriptRoot 'sign.ps1') -Path (Join-Path $OUT "TOKU-RPC-Setup-$appVer.exe") -Description 'TOKU RPC 설치 프로그램'

# 릴리스 본문 (dist-release 는 매번 비우므로 tools 에 원본을 둔다)
# release-body.md 가 실제로 릴리스 설명란에 붙여넣을 파일이다.
# 한국어 본문 첫 문단 뒤에 영어·일본어를 접기 블록으로 끼워 한 본문에 세 언어를 담는다.
Copy-Item (Join-Path $PSScriptRoot 'RELEASE_NOTES*.md') $OUT -Force
node (Join-Path $PSScriptRoot 'build-release-body.js') (Join-Path $OUT 'release-body.md')

Write-Host "`n완료 — $OUT" -ForegroundColor White
Get-ChildItem $OUT | Sort-Object Length -Descending |
    ForEach-Object { "  {0,-34} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }
Write-Host "`n이 파일들을 GitHub 릴리스에 첨부하세요 (태그: v$appVer)" -ForegroundColor DarkGray
