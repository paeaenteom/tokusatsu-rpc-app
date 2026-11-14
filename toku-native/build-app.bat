@echo off
rem TOKU RPC - app only build (no tests). Needs WinLibs GCC (mingw-w64 UCRT) on PATH.
rem  output: build\TOKU-RPC-beta.exe
setlocal
cd /d "%~dp0"
if not exist build\nomanifest mkdir build\nomanifest
rem gcc adds its own default manifest - an empty one here so res\app.manifest is the only manifest
echo.> build\nomanifest\empty.c
gcc -c build\nomanifest\empty.c -o build\nomanifest\default-manifest.o || exit /b 1
windres res\app.rc -O coff -o build\app.res || exit /b 1
gcc -std=c11 -O2 -Wall -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE -B build\nomanifest\ -mwindows -municode -static -o build\TOKU-RPC-beta.exe src\app\main.c src\app\window.c src\app\img.c src\app\autostart.c src\app\keybinds.c src\app\binge.c src\app\accent.c src\app\hooks.c src\app\hksearch.c src\app\imgutil.c src\app\bg.c src\app\backfill.c src\app\canvas.c src\app\traymenu.c src\app\settings.c src\app\upload.c src\net\http.c src\net\ws.c src\discord\ipc.c src\core\rpc.c src\core\js.c src\core\timers.c src\core\url.c src\base\sbuf.c src\base\json.c src\base\crypto.c src\base\utf.c build\app.res -lws2_32 -lwinhttp -lbcrypt -lshell32 -lole32 -loleaut32 -ladvapi32 -ltaskschd -lgdi32 -ldwmapi -luxtheme -lwindowscodecs -luuid -limm32 -lnormaliz -lcomdlg32 || exit /b 1
echo built build\TOKU-RPC-beta.exe
