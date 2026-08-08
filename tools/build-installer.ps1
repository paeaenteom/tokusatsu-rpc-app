<#
  설치 프로그램(exe) 빌드
  ------------------------
  tools\installer\Installer.cs 를 .NET Framework 컴파일러로 단일 exe 로 만든다.
  외부 도구·모듈이 필요 없다 (Windows에 기본 포함된 csc 사용).

  결과: dist-release\TOKU-RPC-Installer.exe
#>

$ErrorActionPreference = 'Stop'
$ROOT = Split-Path $PSScriptRoot -Parent
$SRC  = Join-Path $PSScriptRoot 'installer\Installer.cs'
$OUT  = Join-Path $ROOT 'dist-release'
$ICO  = Join-Path $ROOT 'ttfc-app\assets\icon.ico'
$EXE  = Join-Path $OUT 'TOKU-RPC-Installer.exe'

$csc = @(
    "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) { throw "C# 컴파일러(csc)를 찾지 못했습니다 (.NET Framework 4.x 필요)" }

New-Item -ItemType Directory -Force $OUT | Out-Null

$refs = @(
    'System.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll',
    'System.IO.Compression.dll', 'System.IO.Compression.FileSystem.dll'
) | ForEach-Object { "/r:$_" }

$args = @(
    '/target:winexe',
    "/out:$EXE",
    '/optimize+',
    '/nologo'
) + $refs
if (Test-Path $ICO) { $args += "/win32icon:$ICO" }
$args += $SRC

Write-Host "컴파일 중..." -ForegroundColor Cyan
& $csc $args
if ($LASTEXITCODE -ne 0) { throw "컴파일 실패 (코드 $LASTEXITCODE)" }

$f = Get-Item $EXE
Write-Host "OK  $($f.Name)  $([math]::Round($f.Length/1KB,1)) KB" -ForegroundColor Green
Write-Host "    $EXE" -ForegroundColor DarkGray
