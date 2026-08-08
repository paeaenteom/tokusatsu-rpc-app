@echo off
title TOKU RPC

echo.
echo TOKU RPC starting...
echo.

if not exist node_modules (
    echo First run - installing dependencies...
    call npm install
    echo.
)

call npm start
