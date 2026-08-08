@echo off
title TOKU RPC - Publish Update

echo.
echo ========================================
echo   TOKU RPC - Publish Update
echo ========================================
echo.
echo Requirements:
echo   1. Update version in package.json
echo   2. Set GH_TOKEN environment variable
echo.

if "%GH_TOKEN%"=="" (
    echo [ERROR] GH_TOKEN not set!
    echo.
    echo How to set:
    echo   set GH_TOKEN=ghp_xxxxxxxxxxxxxxxxxxxx
    echo.
    echo Create token at:
    echo   https://github.com/settings/tokens
    echo   Required scope: repo
    echo.
    pause
    exit /b 1
)

echo Current version:
node -e "console.log(require('./package.json').version)"
echo.

set /p confirm="Publish now? (Y/N): "
if /i "%confirm%" neq "Y" (
    echo Cancelled.
    pause
    exit /b 0
)

echo.
echo Building and publishing...
call npm run publish

if %errorlevel% neq 0 (
    echo [ERROR] Publish failed!
    pause
    exit /b 1
)

echo.
echo Publish complete! Check GitHub Releases.
pause
