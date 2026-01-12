@echo off
setlocal enableextensions enabledelayedexpansion

REM ============================================================================
REM FETCH NVIDIA STREAMLINE SDK FOR WINDOWS
REM ============================================================================

REM Initialize variables
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%\.."
set "THIRDPARTY_DIR=%PROJECT_ROOT%\src\ThirdParty"
set "STREAMLINE_DIR=%THIRDPARTY_DIR%\streamline"
set "STREAMLINE_LIB=%STREAMLINE_DIR%\lib\x64\sl.interposer.lib"
set "SDK_URL=https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.0/streamline-sdk-v2.10.0.zip"
set "TEMP_DIR=%PROJECT_ROOT%\temp_streamline"

echo [fetch_streamline] Downloading NVIDIA Streamline SDK...
echo [fetch_streamline] Source: %SDK_URL%
echo [fetch_streamline] Target Directory: %STREAMLINE_DIR%

REM Check if binaries already exist
if exist "%STREAMLINE_LIB%" (
    echo [fetch_streamline] Streamline SDK found at %STREAMLINE_DIR%.
    echo [fetch_streamline] Skipping download.
    exit /b 0
)

REM Create temporary directory
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
mkdir "%TEMP_DIR%"

echo [fetch_streamline] Downloading from %SDK_URL%...

REM Download using PowerShell
powershell -Command "Invoke-WebRequest -Uri '%SDK_URL%' -OutFile '%TEMP_DIR%\streamline.zip'"
if %errorlevel% neq 0 (
    echo [fetch_streamline] Error: Download failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

echo [fetch_streamline] Extracting archive...
powershell -Command "Expand-Archive -Path '%TEMP_DIR%\streamline.zip' -DestinationPath '%TEMP_DIR%' -Force"
if %errorlevel% neq 0 (
    echo [fetch_streamline] Error: Extraction failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

echo [fetch_streamline] Installing Streamline to %STREAMLINE_DIR%...
if not exist "%STREAMLINE_DIR%" mkdir "%STREAMLINE_DIR%"

REM Copy contents
REM The zip structure for Streamline SDK usually has 'include', 'lib', 'bin' at root or under a folder.
REM Let's check if there is a root folder in the extracted zip. 
REM Assuming standard packaging (usually direct, but sometimes nested).
REM Safest is to check for 'include' in temp root.

if exist "%TEMP_DIR%\include" (
    xcopy /E /I /Y "%TEMP_DIR%\*" "%STREAMLINE_DIR%\"
) else (
    REM Maybe inside a subfolder?
    for /d %%D in ("%TEMP_DIR%\*") do (
        if exist "%%D\include" (
            xcopy /E /I /Y "%%D\*" "%STREAMLINE_DIR%\"
        )
    )
)

if %errorlevel% neq 0 (
    echo [fetch_streamline] Error: Copying files failed.
    rmdir /s /q "%TEMP_DIR%"
    exit /b 1
)

REM Cleanup
echo [fetch_streamline] Cleaning up temporary files...
rmdir /s /q "%TEMP_DIR%"

echo [fetch_streamline] Successfully installed NVIDIA Streamline SDK.
echo [fetch_streamline] Done!
exit /b 0
