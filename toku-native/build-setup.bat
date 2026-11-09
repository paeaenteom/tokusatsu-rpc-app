@echo off
rem TOKU RPC - setup exe (app + Chrome extension inside). Needs WinLibs GCC and Node.js.
rem  output: build\TOKU-RPC-Setup.exe   (runs build-app.bat first)
setlocal
cd /d "%~dp0"
call "%~dp0build-app.bat" || exit /b 1
node tools\pack-setup.js build\TOKU-RPC-beta.exe ..\ttfc-chrome-ext build\setup-payload.bin || exit /b 1
windres -I build -I res res\setup.rc -O coff -o build\setup.res || exit /b 1
gcc -std=c11 -O2 -Wall -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE -B build\nomanifest\ -mwindows -municode -static -o build\TOKU-RPC-Setup.exe src\setup\setup.c build\setup.res -lshell32 -lole32 -luuid -lcomctl32 -ladvapi32 || exit /b 1
echo built build\TOKU-RPC-Setup.exe
