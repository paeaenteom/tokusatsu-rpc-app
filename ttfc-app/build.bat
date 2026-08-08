@echo off
title TOKU RPC - Build

echo.
echo ========================================
echo   TOKU RPC - Build Script
echo ========================================
echo.

where node >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Node.js not installed!
    echo Download: https://nodejs.org/
    pause
    exit /b 1
)

echo [1/4] Node.js version:
node --version
echo.

echo [1.5/4] Installing dependencies...
call npm install
if %errorlevel% neq 0 (
    echo [ERROR] npm install failed!
    pause
    exit /b 1
)
echo OK
echo.

echo [2/4] Repairing Electron binary...
if not exist "node_modules\electron\dist\electron.exe" (
    echo   Electron binary missing - downloading...
    node node_modules\electron\install.js
    if %errorlevel% neq 0 (
        echo   First attempt failed - clean reinstall...
        rmdir /s /q node_modules\electron
        call npm install electron@^28.1.0
    )
) else (
    echo   OK - electron.exe present
)
echo.

echo [3/4] Building installer... (1-3 min)
call npm run build
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   BUILD COMPLETE!
echo   Check dist\ folder for Setup exe
echo ========================================
echo.

explorer dist
pause
