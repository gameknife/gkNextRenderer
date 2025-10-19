@echo off
setlocal enableextensions enabledelayedexpansion

REM ============================================================================
REM FETCH TSC BINARY FOR WINDOWS
REM ============================================================================

REM Initialize variables
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%\.."
set "TOOLS_DIR=%PROJECT_ROOT%\tools"
set "TSC_DIR=%TOOLS_DIR%\tsc"
set "TSC_TARGET=%TSC_DIR%\tsc.exe"
set "TSC_URL=https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-windows-amd64.exe"

echo [fetch_tsc] Downloading TSGO TypeScript compiler for Windows...
echo [fetch_tsc] Source: %TSC_URL%
echo [fetch_tsc] Target: %TSC_TARGET%

REM Create directories if they don't exist
if not exist "%TOOLS_DIR%" mkdir "%TOOLS_DIR%"
if not exist "%TSC_DIR%" mkdir "%TSC_DIR%"

REM Check if file already exists
if exist "%TSC_TARGET%" (
    echo [fetch_tsc] TSC already exists at %TSC_TARGET%
    for %%F in ("%TSC_TARGET%") do set "FILE_SIZE=%%~zF"
    echo [fetch_tsc] Existing file size: %FILE_SIZE% bytes

    REM Simple check: file should be larger than 1MB (use string length comparison)
    set "SIZE_CHECK=!FILE_SIZE:~6!"
    if defined SIZE_CHECK (
        echo [fetch_tsc] Existing file is valid, skipping download
        exit /b 0
    )
    echo [fetch_tsc] Existing file is too small, re-downloading...
)

echo [fetch_tsc] Downloading from %TSC_URL%...

REM Try curl first
curl --version >nul 2>&1
if %errorlevel% equ 0 (
    curl -L -o "%TSC_TARGET%" "%TSC_URL%"
    if %errorlevel% neq 0 (
        echo [fetch_tsc] Error: curl download failed
        exit /b 1
    )
) else (
    REM Fallback to PowerShell
    echo [fetch_tsc] curl not available, using PowerShell...
    powershell -Command "Invoke-WebRequest -Uri '%TSC_URL%' -OutFile '%TSC_TARGET%'"
    if %errorlevel% neq 0 (
        echo [fetch_tsc] Error: PowerShell download failed
        exit /b 1
    )
)

echo [fetch_tsc] Verifying download...
if not exist "%TSC_TARGET%" (
    echo [fetch_tsc] Error: Download failed - %TSC_TARGET% not found
    exit /b 1
)

for %%F in ("%TSC_TARGET%") do set "FILE_SIZE=%%~zF"
echo [fetch_tsc] Downloaded file size: %FILE_SIZE% bytes

REM Simple check: verify file size using string manipulation (should be at least 8 digits for ~15MB file)
set "SIZE_CHECK=!FILE_SIZE:~6!"
if defined SIZE_CHECK (
    echo [fetch_tsc] File size verification passed
) else (
    echo [fetch_tsc] Error: Downloaded file is too small (%FILE_SIZE% bytes)
    exit /b 1
)

echo [fetch_tsc] Successfully downloaded TSGO TypeScript compiler
echo [fetch_tsc] Done!
exit /b 0