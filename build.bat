@echo off
setlocal enableextensions enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%"
set "BUILD_ROOT=%PROJECT_ROOT%\build"

if not defined VCPKG_ROOT set "VCPKG_ROOT=%PROJECT_ROOT%\.vcpkg"
set "TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=windows"

if /i "%PLATFORM%"=="windows" (
    call :build_windows %2
) else if /i "%PLATFORM%"=="android" (
    call :build_android
) else (
    echo [build] Unsupported platform "%PLATFORM%"
    echo [build] Supported: windows, android
    goto :error
)

exit /b %errorlevel%

:log
echo [build] %~1
exit /b 0

:warn
echo [build] Warning: %~1>&2
exit /b 0

:require_toolchain
if not exist "%TOOLCHAIN_FILE%" (
    call :warn "vcpkg toolchain not found at %TOOLCHAIN_FILE%"
    call :warn "请先运行 vcpkg.bat windows"
    goto :error
)
exit /b 0

:ensure_cmake
for %%X in (cmake.exe) do (set "FOUND=%%~$PATH:X")
if defined FOUND (
    set "CMAKE=cmake"
) else (
    call :warn "cmake.exe 未在 PATH 中找到"
    goto :error
)
exit /b 0

:ensure_msbuild
for %%X in (msbuild.exe) do (set "FOUND=%%~$PATH:X")
if defined FOUND (
    set "MSBUILD=msbuild"
) else (
    if exist "%programfiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\msbuild.exe" (
        set "MSBUILD=%programfiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\msbuild.exe"
    ) else if exist "%programfiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\msbuild.exe" (
        set "MSBUILD=%programfiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\msbuild.exe"
    ) else (
        call :warn "msbuild.exe 未找到，请安装 Visual Studio 2022"
        goto :error
    )
)
exit /b 0

:prepare_build_dir
set "TARGET_DIR=%~1"
set "CACHED_TOOLCHAIN="
set "NORMALIZED_TOOLCHAIN="
set "NORMALIZED_CACHED="
if exist "%TARGET_DIR%\CMakeCache.txt" (
    for /f "usebackq tokens=2 delims==" %%A in (`findstr /b "CMAKE_TOOLCHAIN_FILE:FILEPATH=" "%TARGET_DIR%\CMakeCache.txt"`) do set "CACHED_TOOLCHAIN=%%A"
)
for %%A in ("%TOOLCHAIN_FILE%") do set "NORMALIZED_TOOLCHAIN=%%~fA"
if defined CACHED_TOOLCHAIN (
    for %%A in ("%CACHED_TOOLCHAIN%") do set "NORMALIZED_CACHED=%%~fA"
    if not defined NORMALIZED_CACHED set "NORMALIZED_CACHED=%CACHED_TOOLCHAIN%"
)
if not defined NORMALIZED_TOOLCHAIN set "NORMALIZED_TOOLCHAIN=%TOOLCHAIN_FILE%"
if defined NORMALIZED_CACHED (
    if /i not "%NORMALIZED_CACHED%"=="%NORMALIZED_TOOLCHAIN%" (
        call :log "Detected outdated vcpkg toolchain, cleaning %TARGET_DIR%"
        rmdir /s /q "%TARGET_DIR%"
    )
)
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
exit /b 0

:SETTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set StartTime=%%a
exit /b 0

:STOPTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set EndTime=%%a
for /f %%a in ('powershell -command "((Get-Date '%EndTime%') - (Get-Date '%StartTime%')).TotalSeconds"') do set Duration=%%a
echo %~1 cost %Duration% s.
exit /b 0

:build_windows
set "BUILD_CONFIG=%~1"
if "%BUILD_CONFIG%"=="" set "BUILD_CONFIG=RelWithDebInfo"
call :require_toolchain || goto :error
call :ensure_cmake || goto :error
call :ensure_msbuild || goto :error
call :prepare_build_dir "%BUILD_ROOT%\windows"
cd "%BUILD_ROOT%\windows" || goto :error
call :SETTIMER
%CMAKE% -D WITH_OIDN=0 -D WITH_AVIF=0 -D WITH_SUPERLUMINAL=0 -D VCPKG_TARGET_TRIPLET=x64-windows-static -D VCPKG_MANIFEST_MODE=ON -D VCPKG_MANIFEST_DIR="%PROJECT_ROOT%" -D CMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_FILE%" -G "Visual Studio 17 2022" -A "x64" "%PROJECT_ROOT%" || goto :error
%CMAKE% --build . --config %BUILD_CONFIG% -j12 || goto :error
call :STOPTIMER "Windows build"
cd "%PROJECT_ROOT%"
exit /b 0

:build_android
cd "%PROJECT_ROOT%\android" || goto :error
gradlew.bat build || goto :error
cd "%PROJECT_ROOT%"
exit /b 0

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
