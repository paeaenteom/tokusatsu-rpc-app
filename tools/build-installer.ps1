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

$cscArgs = @('/target:winexe', "/out:$EXE", '/optimize+', '/nologo') + $refs
if (Test-Path $ICO) { $cscArgs += "/win32icon:$ICO" }
if ($embed) { $cscArgs += "/resource:$embed,app.zip" }   # 리소스 이름 = app.zip
$cscArgs += $SRC

Write-Host "컴파일 중..." -ForegroundColor Cyan
& $csc $cscArgs
if ($LASTEXITCODE -ne 0) { throw "컴파일 실패 (코드 $LASTEXITCODE)" }
if ($embed) { Remove-Item $embed -Force -ErrorAction SilentlyContinue }

$f = Get-Item $EXE
Write-Host "OK  $($f.Name)  $([math]::Round($f.Length/1MB,1)) MB" -ForegroundColor Green
Write-Host "    $EXE" -ForegroundColor DarkGray
