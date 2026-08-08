@echo off
chcp 65001 >nul
title TOKU RPC 설치
rem  더블클릭 한 번으로 앱 + 크롬 확장을 설치한다.
rem  실제 작업은 GitHub 최신 릴리스의 install.ps1 이 수행한다.

set "PS=powershell -NoProfile -ExecutionPolicy Bypass"

rem 같은 폴더에 install.ps1 이 있으면 그것을, 없으면 최신 릴리스에서 받아 실행
if exist "%~dp0install.ps1" (
    %PS% -File "%~dp0install.ps1"
) else (
    %PS% -Command "iwr 'https://github.com/paeaenteom/ttfc_app/releases/latest/download/install.ps1' -UseBasicParsing -OutFile \"$env:TEMP\toku-install.ps1\"; & \"$env:TEMP\toku-install.ps1\""
)
