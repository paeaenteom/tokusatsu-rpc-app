<#
  릴리스 자산 빌드
  ----------------
  GitHub Releases에 올릴 파일 4개를 dist-release\ 에 만든다.

    TOKU-RPC-Setup-<버전>.exe   앱 설치 프로그램
    toku-rpc-extension.crx      크롬 확장 (자동 설치용, 서명본)
    toku-rpc-extension.zip      크롬 확장 (수동 로드용)
    update.xml                  크롬이 확장 버전을 확인하는 매니페스트
    install.ps1                 원클릭 설치 스크립트

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
$REPO   = 'paeaenteom/ttfc_app'

function Step($m) { Write-Host "`n▶ $m" -ForegroundColor Cyan }

Remove-Item $OUT -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OUT | Out-Null

$appVer = (Get-Content (Join-Path $APP 'package.json') -Raw | ConvertFrom-Json).version
$extVer = (Get-Content (Join-Path $EXT 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
Write-Host "앱 v$appVer / 확장 v$extVer" -ForegroundColor White

# ── 1. 앱 빌드 ──────────────────────────────────────
#  NSIS 설치 프로그램은 코드서명 도구(winCodeSign) 압축 해제에 심볼릭 링크 권한이
#  필요해 환경에 따라 실패한다. 앱 패키징(win-unpacked) 자체는 항상 성공하므로
#  그걸 ZIP으로 배포하고, 설치는 install.ps1 이 압축 해제 + 바로가기로 처리한다.
Step "앱 빌드"
Get-Process -Name 'TOKU RPC' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Push-Location $APP
npm run build 2>&1 | Select-String 'packaging|building block' | ForEach-Object { "  $_" }
Pop-Location

$unpacked = Join-Path $APP 'dist\win-unpacked'
if (Test-Path (Join-Path $unpacked 'TOKU RPC.exe')) {
    $zipPath = Join-Path $OUT "TOKU-RPC-$appVer-win.zip"
    Compress-Archive -Path (Join-Path $unpacked '*') -DestinationPath $zipPath -Force
    Write-Host "  OK $(Split-Path $zipPath -Leaf) ($([math]::Round((Get-Item $zipPath).Length/1MB,1))MB)" -ForegroundColor Green
} else {
    Write-Host "  앱 패키징 실패 — 앱 자산 없이 계속" -ForegroundColor Yellow
}

# 이번 버전의 설치 프로그램(.exe)이 만들어졌다면 함께 첨부 (환경에 따라 생성됨)
# ※ 와일드카드로 잡으면 dist에 남아 있는 옛 버전이 섞이므로 정확한 파일명만 본다
$setup = Get-Item (Join-Path $APP "dist\TOKU-RPC-Setup-$appVer.exe") -ErrorAction SilentlyContinue
if ($setup) { Copy-Item $setup.FullName $OUT; Write-Host "  OK $($setup.Name) (선택)" -ForegroundColor Green }

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
if (Test-Path "$stage.crx") {
    Copy-Item "$stage.crx" (Join-Path $OUT 'toku-rpc-extension.crx') -Force
    Write-Host "  OK toku-rpc-extension.crx" -ForegroundColor Green
} else { throw "CRX 생성 실패" }

# ── 3. 확장 ZIP (수동 로드용) ───────────────────────
Compress-Archive -Path "$stage\*" -DestinationPath (Join-Path $OUT 'toku-rpc-extension.zip') -Force
Write-Host "  OK toku-rpc-extension.zip" -ForegroundColor Green
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue

# ── 4. update.xml ───────────────────────────────────
Step "업데이트 매니페스트"
$crxUrl = "https://github.com/$REPO/releases/latest/download/toku-rpc-extension.crx"
@"
<?xml version='1.0' encoding='UTF-8'?>
<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>
  <app appid='$EXT_ID'>
    <updatecheck codebase='$crxUrl' version='$extVer' />
  </app>
</gupdate>
"@ | Set-Content (Join-Path $OUT 'update.xml') -Encoding UTF8
Write-Host "  OK update.xml (확장 v$extVer)" -ForegroundColor Green

# ── 5. 설치 스크립트 동봉 ───────────────────────────
Copy-Item (Join-Path $PSScriptRoot 'install.ps1') $OUT -Force
Copy-Item (Join-Path $PSScriptRoot 'install.bat') $OUT -Force

Write-Host "`n완료 — $OUT" -ForegroundColor White
Get-ChildItem $OUT | ForEach-Object { "  {0,-34} {1,8:N1} KB" -f $_.Name, ($_.Length / 1KB) }
Write-Host "`n이 파일들을 GitHub 릴리스에 첨부하세요 (태그: v$appVer)" -ForegroundColor DarkGray
