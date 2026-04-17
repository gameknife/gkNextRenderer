@echo off
setlocal

REM Wrapper for vcpkg.ps1 (co-located in scripts/)

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%vcpkg.ps1" %*
exit /b %errorlevel%
