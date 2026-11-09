@echo off
rem TOKU RPC native - test build (console only, no windows)
rem  tests use ports 17695-17697 and fake pipe names only (never 7690 / discord-ipc-N)
rem  build.bat mut  = also run planted-bug check (slow, a few minutes)
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wshadow -Wformat=2 -Wno-unused-parameter -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
set BASE=src\base\sbuf.c src\base\json.c src\base\crypto.c src\base\utf.c
set CORE=src\core\rpc.c src\core\js.c src\core\timers.c src\core\url.c
gcc %CFLAGS% -o build\vec.exe tests\vec.c %BASE% || exit /b 1
gcc %CFLAGS% -o build\ws_srv.exe tests\ws_srv.c src\net\ws.c %BASE% -lws2_32 || exit /b 1
gcc %CFLAGS% -o build\dipc_cli.exe tests\dipc_cli.c src\discord\ipc.c %BASE% -lbcrypt || exit /b 1
gcc %CFLAGS% -o build\url_vec.exe tests\url_vec.c src\core\url.c %BASE% || exit /b 1
gcc %CFLAGS% -o build\core_cli.exe tests\core_cli.c %CORE% %BASE% || exit /b 1
node tests\cross.js build\vec.exe || exit /b 1
node tests\ws-test.js build\ws_srv.exe || exit /b 1
node tests\ipc-test.js build\dipc_cli.exe || exit /b 1
node tests\fuzz.js build\vec.exe build\ws_srv.exe || exit /b 1
node tests\url-test.js build\url_vec.exe || exit /b 1
node tests\core-diff.js build\core_cli.exe 300 1 || exit /b 1
node tests\core-fuzz.js build\core_cli.exe || exit /b 1
rem use-after-free check: freed memory filled with 0xDD and never reused -> any read-after-free shows up as a JS diff
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o build\core_cli_poison.exe tests\core_cli.c tests\poison_alloc.c %CORE% %BASE% || exit /b 1
node tests\core-diff.js build\core_cli_poison.exe 300 2 || exit /b 1
rem alpha app (tray). -B nomanifest = skip gcc default manifest, use res\app.manifest
if not exist build\nomanifest mkdir build\nomanifest
if not exist build\beta mkdir build\beta
echo.> build\nomanifest\empty.c
gcc -c build\nomanifest\empty.c -o build\nomanifest\default-manifest.o || exit /b 1
windres res\app.rc -O coff -o build\app.res || exit /b 1
node tools\gen-strings.js || exit /b 1
rem one exe, two modes: beta (default, same pacing as the JS app) / test version alpha (setting testChannel, pacing V2). src\app\channel.h
gcc %CFLAGS% -B build\nomanifest\ -mwindows -municode -static -o build\beta\TOKU-RPC-beta.exe src\app\main.c src\app\window.c src\app\img.c src\app\autostart.c src\app\keybinds.c src\app\binge.c src\app\accent.c src\app\hooks.c src\app\imgutil.c src\app\bg.c src\app\backfill.c src\app\canvas.c src\app\traymenu.c src\app\settings.c src\app\upload.c src\net\http.c src\net\ws.c src\discord\ipc.c %CORE% %BASE% build\app.res -lws2_32 -lwinhttp -lbcrypt -lshell32 -lole32 -loleaut32 -ladvapi32 -ltaskschd -lgdi32 -ldwmapi -luxtheme -lwindowscodecs -luuid -limm32 -lnormaliz -lcomdlg32 || exit /b 1
node tests\alpha-smoke.js build\beta\TOKU-RPC-beta.exe || exit /b 1
node tests\thumb-cache-test.js build\beta\TOKU-RPC-beta.exe || exit /b 1
rem key settings: store vs real keybinds.js (random commands, poisoned allocator) + extension pick flow
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -municode -o build\kb_cli.exe tests\kb_cli.c tests\poison_alloc.c src\app\keybinds.c src\app\settings.c src\core\js.c %BASE% -lbcrypt || exit /b 1
node tests\kb-diff.js build\kb_cli.exe 3000 1 || exit /b 1
node tests\kb-diff.js build\kb_cli.exe 3000 2 || exit /b 1
node tests\kb-diff.js build\kb_cli.exe 3000 3 || exit /b 1
node tests\kb-pick-test.js build\beta\TOKU-RPC-beta.exe build\snap-kb || exit /b 1
rem binge webhook: card color / target pick / characters vs real binge-webhook.js + secrets.js + thumb-color.js, then the real flow to a local http server
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -municode -o build\binge_cli.exe tests\binge_cli.c tests\poison_alloc.c src\app\binge.c src\app\accent.c src\app\settings.c src\net\http.c %CORE% %BASE% -lwinhttp -lbcrypt -lole32 -lwindowscodecs -luuid -lnormaliz -lshell32 || exit /b 1
node tests\binge-diff.js build\binge_cli.exe 2000 1 || exit /b 1
node tests\binge-diff.js build\binge_cli.exe 2000 2 || exit /b 1
node tests\binge-flow-test.js build\beta\TOKU-RPC-beta.exe || exit /b 1
rem webhook tab data: secrets.js titleWebhooks + main.js folders/characters vs C (temp files only)
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -municode -o build\hk_cli.exe tests\hk_cli.c tests\poison_alloc.c src\app\hooks.c src\app\binge.c src\app\accent.c src\app\settings.c src\net\http.c %CORE% %BASE% -lwinhttp -lbcrypt -lole32 -lwindowscodecs -luuid -lnormaliz -lshell32 || exit /b 1
node tests\hk-diff.js build\hk_cli.exe 1500 1 || exit /b 1
node tests\hk-diff.js build\hk_cli.exe 1500 2 || exit /b 1
rem icon / avatar crop: decode + square crop + PNG encode, checked by decoding the PNG
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -municode -o build\iu_cli.exe tests\iu_cli.c tests\poison_alloc.c src\app\imgutil.c -lole32 -lwindowscodecs -luuid || exit /b 1
node tests\iu-test.js build\iu_cli.exe || exit /b 1
rem background: sniff / sanitize / validate / name and the file flow vs real appearance.js
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o build\bg_cli.exe tests\bg_cli.c tests\poison_alloc.c src\app\bg.c src\app\settings.c src\app\imgutil.c src\core\js.c %BASE% -lbcrypt -lole32 -lwindowscodecs -luuid -lshell32 -lcomdlg32 || exit /b 1
node tests\bg-diff.js build\bg_cli.exe 3000 1 || exit /b 1
node tests\bg-diff.js build\bg_cli.exe 3000 2 || exit /b 1
node tests\bg-flow-diff.js build\bg_cli.exe 600 1 || exit /b 1
node tests\bg-flow-diff.js build\bg_cli.exe 600 2 || exit /b 1
rem window pictures: webhook tab (+ scrolled) and background
node tests\alpha-hook-snap.js build\beta\TOKU-RPC-beta.exe build\snap-hook || exit /b 1
node tests\alpha-bg-snap.js build\beta\TOKU-RPC-beta.exe build\snap-bg || exit /b 1
rem autostart: test names only ("TOKU RPC Alpha TEST ...") - real boot entries untouched
gcc %CFLAGS% "-DRUN_NAME=L\"TOKU RPC Alpha TEST\"" "-DTASK_NAME=L\"TOKU RPC Alpha TEST Fast Start\"" -o build\autostart_cli.exe tests\autostart_cli.c src\app\autostart.c -lole32 -loleaut32 -ladvapi32 -ltaskschd -luuid -lshell32 || exit /b 1
node tests\autostart-test.js build\autostart_cli.exe || exit /b 1
rem card images: download + WIC decode (needs network). jpg/webp ok, 404/html/http/bad asset/javascript: refused
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o build\img_cli.exe tests\img_cli.c tests\poison_alloc.c src\app\img.c src\net\http.c %BASE% -lwinhttp -lole32 -lwindowscodecs -luuid -lbcrypt || exit /b 1
build\img_cli.exe "https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg" "https://i.ytimg.com/vi/xxxxxxxxxxx_nope/hqdefault.jpg" "http://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg" "javascript:alert(1)"
rem window pictures without showing a window: build\snap\tab*.bmp
node tests\alpha-snap.js build\beta\TOKU-RPC-beta.exe build\snap || exit /b 1
rem tray menu picture (custom-drawn menu, memory DC only): build\snap-tray\tray@100/150.bmp
node tests\alpha-tray-snap.js build\beta\TOKU-RPC-beta.exe build\snap-tray || exit /b 1
rem backfill tab: RESOLVED filter / link / when / thumb decode vs real extension-bridge.js + rpc-window.js, then the real flow (fake extension + local webhook)
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o build\bf_cli.exe tests\bf_cli.c tests\poison_alloc.c src\app\backfill.c src\core\js.c %BASE% -lbcrypt || exit /b 1
node tests\bf-diff.js build\bf_cli.exe 3000 1 || exit /b 1
node tests\bf-diff.js build\bf_cli.exe 3000 2 || exit /b 1
node tests\backfill-flow-test.js build\beta\TOKU-RPC-beta.exe build\snap-bf || exit /b 1
rem latency: extension ping + Discord SET_ACTIVITY round trip must not be stuck to the 15.6ms timer tick (event-driven loop)
node tests\latency-bench.js build\beta\TOKU-RPC-beta.exe 20 --max-ext 3 --max-discord 3 || exit /b 1
rem latency display: fake extension / Discord answer 11ms late every 3rd time -> shown value (median of last 5) stays low
node tests\latency-jitter-test.js build\beta\TOKU-RPC-beta.exe || exit /b 1
rem extension: TTFC skip button guard (site flips play/pause on skip) on a fake player
node tests\ttfc-skip-guard-test.js || exit /b 1
rem presence pacing V2 (alpha): same scenarios through V1/V2 core, Discord client limit 5 per 20s, seek = one send, last state always sent
node tests\presence-v2-test.js build\core_cli.exe || exit /b 1
gcc %CFLAGS% -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o build\core_cli_poison_v2.exe tests\core_cli.c tests\poison_alloc.c %CORE% %BASE% || exit /b 1
set TOKU_CORE_V2=1
node tests\core-fuzz.js build\core_cli_poison_v2.exe || exit /b 1
set TOKU_CORE_V2=
rem mode switch: test hook flips testChannel -> app restarts itself into the other mode (test port only)
node tests\channel-switch-test.js build\beta\TOKU-RPC-beta.exe || exit /b 1
if /i "%1"=="mut" node tests\mutate.js || exit /b 1
rem setup exe (app + extension inside) and install / reinstall / uninstall into a temp folder (/TEST: real app, autostart and uninstall entry untouched)
call "%~dp0build-setup.bat" || exit /b 1
node tests\setup-test.js build\TOKU-RPC-Setup.exe build\TOKU-RPC-beta.exe ..\ttfc-chrome-ext || exit /b 1
echo build and tests OK
