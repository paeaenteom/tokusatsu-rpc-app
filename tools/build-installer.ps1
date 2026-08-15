<#
  설치 프로그램(Setup exe) 빌드
  ------------------------------
  앱 본체(win-unpacked)를 exe 안에 통째로 넣어 한 파일로 만든다.
  받는 사람은 이 exe 하나만 실행하면 되고, 설치 중 추가 다운로드가 없다.

  결과: dist-release\TOKU-RPC-Setup-<버전>.exe

  -NoEmbed 를 주면 앱을 넣지 않고 릴리스에서 받아오는 경량 버전을 만든다.
#>
param([switch]$NoEmbed)

$ErrorActionPreference = 'Stop'
$ROOT = Split-Path $PSScriptRoot -Parent
$SRC  = Join-Path $PSScriptRoot 'installer\Installer.cs'
$APP  = Join-Path $ROOT 'ttfc-app'
$OUT  = Join-Path $ROOT 'dist-release'
$ICO  = Join-Path $APP 'assets\icon.ico'

$ver = (Get-Content (Join-Path $APP 'package.json') -Raw | ConvertFrom-Json).version
$EXE = Join-Path $OUT ("TOKU-RPC-Setup-$ver.exe")

$csc = @(
    "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) { throw "C# 컴파일러(csc)를 찾지 못했습니다 (.NET Framework 4.x 필요)" }

New-Item -ItemType Directory -Force $OUT | Out-Null

# 이전에 만든 설치 프로그램이 아직 떠 있으면 출력 파일이 잠겨 컴파일이 실패한다
Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $EXE } | ForEach-Object {
    Write-Host "  실행 중인 이전 설치 프로그램 종료 (PID $($_.Id))" -ForegroundColor DarkYellow
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Remove-Item $EXE -Force -ErrorAction SilentlyContinue

# ── 앱 본체를 임시 zip 으로 묶어 리소스로 첨부 ──
$embed = $null
if (-not $NoEmbed) {
    $unpacked = Join-Path $APP 'dist\win-unpacked'
    if (-not (Test-Path (Join-Path $unpacked 'TOKU RPC.exe'))) {
        throw "앱이 빌드되지 않았습니다: $unpacked (먼저 npm run build)"
    }
    $embed = Join-Path $env:TEMP 'app.zip'
    Remove-Item $embed -Force -ErrorAction SilentlyContinue
    Write-Host "앱 압축 중..." -ForegroundColor Cyan
    Compress-Archive -Path (Join-Path $unpacked '*') -DestinationPath $embed -CompressionLevel Optimal -Force
    Write-Host "  내장할 앱: $([math]::Round((Get-Item $embed).Length/1MB,1))MB" -ForegroundColor DarkGray
}

$refs = @(
    'System.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll',
    'System.IO.Compression.dll', 'System.IO.Compression.FileSystem.dll'
) | ForEach-Object { "/r:$_" }

# ── 버전 정보 리소스 ──
#  이게 없으면 exe 의 속성이 전부 백지로 나간다 (ProductName '' / FileVersion 0.0.0.0).
#  서명이 없는 상태에서 메타데이터까지 비어 있으면 Windows Defender 의 머신러닝 판정이
#  이를 "정체를 숨긴 자체 추출 프로그램"으로 보고 Trojan:Win32/Sabsik.EN.B!ml 로 잡는다
#  (2026-08-15 실제 발생 — 다운로드가 자동 삭제됨).
#  누가 만든 무엇인지 정확히 밝히는 것이 정상적인 배포 관행이자 오탐을 줄이는 방법이다.
$numVer = ($ver -replace '[^0-9.].*$', '')          # '0.2.6-beta' → '0.2.6'
while (($numVer -split '\.').Count -lt 4) { $numVer += '.0' }
$asmInfo = Join-Path $env:TEMP 'toku-rpc-installer-asm.cs'
@"
using System.Reflection;
[assembly: AssemblyTitle("TOKU RPC 설치 프로그램")]
[assembly: AssemblyDescription("TOKU RPC - 특촬 스트리밍 시청 정보를 Discord 에 표시하는 도구의 설치 프로그램")]
[assembly: AssemblyProduct("TOKU RPC")]
[assembly: AssemblyCompany("paeaenteom")]
[assembly: AssemblyCopyright("MIT License - https://github.com/paeaenteom/tokusatsu-rpc-app")]
[assembly: AssemblyVersion("$numVer")]
[assembly: AssemblyFileVersion("$numVer")]
[assembly: AssemblyInformationalVersion("$ver")]
"@ | Set-Content -LiteralPath $asmInfo -Encoding UTF8

$cscArgs = @('/target:winexe', "/out:$EXE", '/optimize+', '/nologo') + $refs
if (Test-Path $ICO) { $cscArgs += "/win32icon:$ICO" }
if ($embed) { $cscArgs += "/resource:$embed,app.zip" }   # 리소스 이름 = app.zip
$cscArgs += $SRC
$cscArgs += $asmInfo

Write-Host "컴파일 중..." -ForegroundColor Cyan
& $csc $cscArgs
if ($LASTEXITCODE -ne 0) { throw "컴파일 실패 (코드 $LASTEXITCODE)" }
if ($embed) { Remove-Item $embed -Force -ErrorAction SilentlyContinue }
Remove-Item $asmInfo -Force -ErrorAction SilentlyContinue

$f = Get-Item $EXE
Write-Host "OK  $($f.Name)  $([math]::Round($f.Length/1MB,1)) MB" -ForegroundColor Green
Write-Host "    $EXE" -ForegroundColor DarkGray

# 버전 정보가 실제로 박혔는지 확인 — 비어 있으면 백신 오탐의 큰 원인이 된다
$vi = $f.VersionInfo
if ([string]::IsNullOrWhiteSpace($vi.ProductName) -or $vi.FileVersion -eq '0.0.0.0') {
    Write-Host "  ★ 버전 정보가 비었습니다 — 백신 오탐 위험" -ForegroundColor Red
} else {
    Write-Host "    버전 정보: $($vi.ProductName) / $($vi.CompanyName) / $($vi.FileVersion)" -ForegroundColor DarkGray
}
