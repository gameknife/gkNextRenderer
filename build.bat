@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM gkNextRenderer Build Script (Windows)
REM Wrapper for build.ps1 to provide a standard CLI.
REM ==============================================================================

set "PRESET=windows-dev"
set "CONFIG="
set "TARGET="
set "CLEAN=0"
set "ANDROID=0"

set "SCRIPT_DIR=%~dp0"

:arg_loop
if "%~1"=="" goto end_arg_loop

if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help
if /i "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto arg_loop
)
if /i "%~1"=="--android" (
    set "ANDROID=1"
    shift
    goto arg_loop
)
if /i "%~1"=="--preset" (
    set "PRESET=%~2"
    shift
    shift
    goto arg_loop
)
if /i "%~1"=="--config" (
    set "CONFIG=%~2"
    shift
    shift
    goto arg_loop
)
if /i "%~1"=="--target" (
    set "TARGET=%~2"
    shift
    shift
    goto arg_loop
)

REM Backward compatibility for positional preset
if not "%~1"=="" (
    if "!PRESET!"=="windows-dev" (
        set "PRESET=%~1"
        shift
        goto arg_loop
    )
)

shift
goto arg_loop

:end_arg_loop

set "PS_ARGS="
if "!ANDROID!"=="1" (
    set "PS_ARGS=-Android"
) else (
    set "PS_ARGS=-Preset !PRESET!"
    if not "!CONFIG!"=="" set "PS_ARGS=!PS_ARGS! -Config !CONFIG!"
    if not "!TARGET!"=="" set "PS_ARGS=!PS_ARGS! -Target !TARGET!"
)

if "!CLEAN!"=="1" set "PS_ARGS=!PS_ARGS! -Clean"

powershell -NoProfile -ExecutionPolicy Bypass -File "!SCRIPT_DIR!build.ps1" !PS_ARGS!
exit /b %errorlevel%

:help
echo Usage: build.bat [options]
echo Options:
echo   --preset ^<name^>  CMake preset to use (default: windows-dev)
echo   --config ^<type^>  Build configuration (Debug, Release, etc.)
echo   --target ^<name^>  Specific target to build
echo   --clean          Clean build directory before building
echo   --android        Build for Android
echo   -h, --help       Show this help
exit /b 0