@echo off
setlocal

REM Wrapper for fetch-paks.ps1 (co-located in scripts/)

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%fetch-paks.ps1" %*
exit /b %errorlevel%
