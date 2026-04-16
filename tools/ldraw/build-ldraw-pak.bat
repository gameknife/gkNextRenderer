@echo off
setlocal

powershell -ExecutionPolicy Bypass -File "%~dp0build-ldraw-pak.ps1" %*
exit /b %errorlevel%
