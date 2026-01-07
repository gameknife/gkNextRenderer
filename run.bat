@echo off
setlocal

REM Wrapper for run.ps1

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%run.ps1" %*
exit /b %errorlevel%