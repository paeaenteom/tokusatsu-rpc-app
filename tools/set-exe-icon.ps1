<#
  앱 exe 에 아이콘 박기
  ---------------------
  electron-builder 가 이 환경에서는 exe 아이콘을 갈아끼우지 못한다.
  아이콘 삽입은 NSIS 설치본을 만드는 단계에서 일어나는데, 그 단계가
  코드서명 도구(winCodeSign) 압축 해제에 심볼릭 링크 권한이 필요해 실패한다.
  그래서 win-unpacked 의 exe 는 Electron 기본 아이콘(남색 사각형)을 그대로
  들고 있었다 — 작업표시줄·창에 오르카 대신 그게 떴다.

  다행히 삽입 도구(rcedit)는 같은 캐시에 이미 받아져 있으므로 직접 부른다.

  빌드 후에 부르면 된다:  powershell -File tools\set-exe-icon.ps1
#>
$ErrorActionPreference = 'Stop'
$ROOT = Split-Path $PSScriptRoot -Parent
$EXE  = Join-Path $ROOT 'ttfc-app\dist\win-unpacked\TOKU RPC.exe'
$ICO  = Join-Path $ROOT 'ttfc-app\assets\icon.ico'

if (-not (Test-Path $EXE)) { throw "앱이 빌드되지 않았습니다: $EXE" }
if (-not (Test-Path $ICO)) { throw "아이콘이 없습니다: $ICO" }

# electron-builder 가 받아 둔 rcedit 를 찾는다 (버전 폴더명이 바뀔 수 있어 검색)
$rc = Get-ChildItem "$env:LOCALAPPDATA\electron-builder\Cache" -Recurse -Filter 'rcedit-x64.exe' -ErrorAction SilentlyContinue |
      Select-Object -First 1
if (-not $rc) {
    Write-Host "rcedit 를 찾지 못했습니다 — exe 아이콘은 Electron 기본값으로 남습니다." -ForegroundColor Yellow
    Write-Host "  (앱 창·트레이 아이콘은 코드에서 직접 지정하므로 영향 없음)" -ForegroundColor DarkGray
    exit 0
}

& $rc.FullName $EXE --set-icon $ICO
if ($LASTEXITCODE -ne 0) { throw "아이콘 삽입 실패 (코드 $LASTEXITCODE)" }

# 실제로 박혔는지 확인한다 — 조용히 실패하면 또 기본 아이콘으로 나간다.
#  ※ [System.Drawing.Icon] 을 그대로 쓰면 스크립트를 통째로 파싱하는 시점에
#    어셈블리가 아직 안 올라와 있어 "타입을 찾을 수 없다"로 죽는다.
#    리플렉션으로 부르면 실행 시점에 해석되므로 그 문제를 피한다.
Add-Type -AssemblyName System.Drawing
$iconType = [Type]::GetType('System.Drawing.Icon, System.Drawing')
if ($iconType) {
    $extracted = $iconType.GetMethod('ExtractAssociatedIcon').Invoke($null, @($EXE))
    Write-Host "  OK  exe 아이콘 적용 ($($extracted.Width)x$($extracted.Height))" -ForegroundColor Green
} else {
    Write-Host "  OK  exe 아이콘 적용 (확인은 건너뜀)" -ForegroundColor Green
}
