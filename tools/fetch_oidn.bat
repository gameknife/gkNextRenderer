@echo off
setlocal enableextensions enabledelayedexpansion

REM ============================================================================
REM FETCH OIDN BINARY FOR WINDOWS
REM ============================================================================

REM Initialize variables
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%\.."
set "THIRDPARTY_DIR=%PROJECT_ROOT%\src\ThirdParty"
set "OIDN_DIR=%THIRDPARTY_DIR%\oidn"
set "OIDN_BIN_DIR=%OIDN_DIR%\bin"
set "OIDN_URL=https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x64.windows.zip"
set "TEMP_DIR=%PROJECT_ROOT%\temp_oidn"

echo [fetch_oidn] Downloading OpenImageDenoise (OIDN) for Windows...
echo [fetch_oidn] Source: %OIDN_URL%
echo [fetch_oidn] Target Directory: %OIDN_DIR%

REM Check if OIDN binaries already exist
if exist "%OIDN_BIN_DIR%\OpenImageDenoise.dll" (
    echo [fetch_oidn] OIDN binaries found at %OIDN_BIN_DIR%.
    REM Optional: Add version check or force update mechanism here if needed.
    echo [fetch_oidn] Skipping download.
    exit /b 0
)

REM Create temporary directory
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
mkdir "%TEMP_DIR%"

echo [fetch_oidn] Downloading from %OIDN_URL%...

REM Download using PowerShell
powershell -Command "Invoke-WebRequest -Uri '%OIDN_URL%' -OutFile '%TEMP_DIR%\oidn.zip'"
if %errorlevel% neq 0 (
    echo [fetch_oidn] Error: Download failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

echo [fetch_oidn] Extracting archive...
powershell -Command "Expand-Archive -Path '%TEMP_DIR%\oidn.zip' -DestinationPath '%TEMP_DIR%' -Force"
if %errorlevel% neq 0 (
    echo [fetch_oidn] Error: Extraction failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

echo [fetch_oidn] Installing OIDN to %OIDN_DIR%...
if not exist "%OIDN_DIR%" mkdir "%OIDN_DIR%"

REM Copy contents from extracted folder to target directory
REM Note: The zip contains a root folder 'oidn-2.4.0.x64.windows'
xcopy /E /I /Y "%TEMP_DIR%\oidn-2.4.0.x64.windows\*" "%OIDN_DIR%\"
if %errorlevel% neq 0 (
    echo [fetch_oidn] Error: Copying files failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

REM Cleanup
echo [fetch_oidn] Cleaning up temporary files...
rmdir /s /q "%TEMP_DIR%"

echo [fetch_oidn] Successfully installed OpenImageDenoise.
echo [fetch_oidn] Done!
exit /b 0
