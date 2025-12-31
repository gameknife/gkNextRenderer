@echo off
setlocal EnableDelayedExpansion

set "root_dir=%~dp0"
set "target=gkNextRenderer.exe"
set "preset="
set "bin_dir="
set "list_only=0"
set "dry_run=0"
set "args="

:parse
if "%~1"=="" goto resolve
if "%~1"=="--" (
    shift
    goto passthrough
)
if "%~1"=="--list" set "list_only=1" & shift & goto parse
if "%~1"=="--dry-run" set "dry_run=1" & shift & goto parse
if "%~1"=="-h" goto help
if "%~1"=="--help" goto help

for %%O in (--target --preset --bin-dir --present-mode --scene) do (
    if /I "%%O"=="%~1" (
        if "%~2"=="" (
            echo Missing value for %~1 1>&2
            exit /b 1
        )
        if "%%O"=="--target" set "target=%~2"
        if "%%O"=="--preset" set "preset=%~2"
        if "%%O"=="--bin-dir" set "bin_dir=%~2"
        if "%%O"=="--present-mode" set "args=!args! --present-mode=%~2"
        if "%%O"=="--scene" set "args=!args! --load-scene=%~2"
        shift
        shift
        goto parse
    )
)

set "args=!args! %~1"
shift
goto parse

:passthrough
if "%~1"=="" goto resolve
set "args=!args! %~1"
shift
goto passthrough

:resolve

if /i "%preset%"=="android" (
    call :run_android
) else (
    call :run_native
)
exit /b 0

:run_native
REM Default preset if not specified
if not defined preset set "preset=windows-dev"

REM Check for bin directory locations
REM Priority 1: Explicit --bin-dir
if defined bin_dir (
    if not exist "%bin_dir%" (
        echo Bin directory not found: %bin_dir% 1>&2
        exit /b 1
    )
    goto :found_bin
)

REM Priority 2: New CMake Preset location (out/build/<preset>/bin)
set "bin_dir=%root_dir%out\build\%preset%\bin"
if exist "%bin_dir%" goto :found_bin

REM Priority 3: Fallback to old location (build/<platform>/bin)
REM Simple mapping for backward compatibility
if "%preset%"=="windows-dev" set "old_plat=windows"
if "%preset%"=="windows-release" set "old_plat=windows"
set "bin_dir=%root_dir%build\%old_plat%\bin"
if exist "%bin_dir%" goto :found_bin

echo Error: Could not find bin directory for preset '%preset%'. 
echo Checked: %root_dir%out\build\%preset%\bin
exit /b 1

:found_bin
if "%list_only%"=="1" (
    echo Entries in %bin_dir%:
    dir /b "%bin_dir%"
    exit /b 0
)

set "exe=%bin_dir%\%target%"
if not exist "%exe%" (
    if exist "%exe%.exe" (
        set "exe=%exe%.exe"
    ) else (
        echo Executable not found: %exe% 1>&2
        exit /b 1
    )
)

for %%F in ("%exe%") do set "exe_name=%%~nxF"
set "cmd=.\!exe_name!!args!"

echo Working dir: %bin_dir%
echo Command: !cmd!

if "%dry_run%"=="1" exit /b 0

pushd "%bin_dir%" >nul
call !cmd!
set "ec=%errorlevel%"
popd >nul
exit /b %ec%

:run_android
set "android_dir=%root_dir%android"
 
if not exist "%android_dir%" (
    echo Android project directory not found: %android_dir% 1>&2
    exit /b 1
)
 
if "%list_only%"=="1" (
    echo --list is not supported for android platform 1>&2
    exit /b 1
)
 
if not "%args%"=="" (
    echo Ignoring extra arguments for android platform: %args% 1>&2
)

set "cmd=gradlew.bat installAndLaunch"
echo Working dir: %android_dir%
echo Command: %cmd%

if "%dry_run%"=="1" exit /b 0
pushd "%android_dir%" >nul
call %cmd%
set "ec=%errorlevel%"
popd >nul
exit /b %ec%

:help
echo Usage: run.bat [options] [-- extra args]
echo   --target NAME         Executable to launch ^(default: gkNextRenderer.exe^)
echo   --preset NAME         CMake Preset name ^(default: windows-dev^)
echo   --bin-dir PATH        explicit bin directory
echo   --present-mode VALUE  append --present-mode=VALUE
echo   --scene PATH          append --load-scene=PATH
echo   --list                list entries in the bin directory
echo   --dry-run             print the command without running
echo   -h, --help            show this help
exit /b 0