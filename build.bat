@echo off
setlocal

REM This script is a wrapper for build.ps1 to allow execution from a standard command prompt.

set "TARGET=windows-dev"
set "CLEAN_ARG="
set "ARGS="

:arg_loop
if "%~1"=="" goto end_arg_loop
if /i "%~1"=="--clean" (
    set "CLEAN_ARG=-Clean"
) else if /i "%~1"=="--android" (
    set "TARGET=android"
) else (
    REM Assuming the first non-flag argument is the target
    if /i "%~1"=="windows" (
        set "TARGET=windows-dev"
    ) else (
        set "TARGET=%~1"
    )
)
shift
goto arg_loop
:end_arg_loop

set "ARGS=-Target %TARGET% %CLEAN_ARG%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\build.ps1" %ARGS%

exit /b %errorlevel%
