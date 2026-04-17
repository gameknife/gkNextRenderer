@echo off
setlocal

REM Wrapper for scripts/build.ps1

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\build.ps1" %*
exit /b %errorlevel%