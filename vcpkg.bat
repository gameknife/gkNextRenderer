@echo off
setlocal enableextensions enabledelayedexpansion

REM ============================================================================
REM MAIN SCRIPT ENTRY POINT
REM ============================================================================

call :main %*
exit /b %errorlevel%

REM ============================================================================
REM MAIN LOGIC
REM ============================================================================

:main
    call :init_variables || goto :error
    call :parse_arguments %* || goto :error
    
    call :ensure_repo || goto :error
    call :ensure_bootstrap || goto :error
    call :select_triplet "%PLATFORM%" || goto :error
    call :install_manifest "%TRIPLET%" || goto :error
    
    echo [vcpkg] Done. 如果使用自定义路径，记得复用 VCPKG_ROOT=%VCPKG_ROOT%.
    exit /b %errorlevel%

REM ============================================================================
REM HELPER FUNCTIONS
REM ============================================================================

:init_variables
    set "SCRIPT_DIR=%~dp0"
    if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
    set "PROJECT_ROOT=%SCRIPT_DIR%"
    set "DEFAULT_VCPKG_ROOT=%PROJECT_ROOT%\.vcpkg"
    
    if not defined VCPKG_ROOT set "VCPKG_ROOT=%DEFAULT_VCPKG_ROOT%"
    set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
    exit /b 0

:parse_arguments
    if "%~1"=="" goto :usage
    
    set "PLATFORM=%~1"
    set "FEATURES=%~2"
    
    if defined FEATURES (
        set "VCPKG_MANIFEST_FEATURES=%FEATURES%"
    ) else (
        set "VCPKG_MANIFEST_FEATURES="
    )
    exit /b 0

:ensure_repo
    if not exist "%VCPKG_ROOT%\.git" (
        echo [vcpkg] Cloning vcpkg into %VCPKG_ROOT%...
        git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%" || goto :error
    ) else (
        echo [vcpkg] Updating vcpkg in %VCPKG_ROOT%...
        pushd "%VCPKG_ROOT%" >nul || goto :error
        git pull --ff-only
        if errorlevel 1 (
            echo [vcpkg] Warning: 无法访问远程仓库，继续使用现有 vcpkg 副本。>&2
        )
        popd >nul
    )
    exit /b 0

:ensure_bootstrap
    if not exist "%VCPKG_EXE%" (
        echo [vcpkg] Bootstrapping vcpkg...
        pushd "%VCPKG_ROOT%" >nul || goto :error
        call bootstrap-vcpkg.bat -disableMetrics || goto :error
        popd >nul
    )
    exit /b 0

:select_triplet
    set "TMP_PLATFORM=%~1"
    if /i "%TMP_PLATFORM%"=="windows" (
        set "TRIPLET=x64-windows-static"
    ) else if /i "%TMP_PLATFORM%"=="android" (
        set "TRIPLET=arm64-android"
    ) else if /i "%TMP_PLATFORM%"=="mingw" (
        set "TRIPLET=x64-mingw-static"
    ) else (
        echo Usage: vcpkg.bat ^<platform^> [manifest-features] 1>&2
        echo. 1>&2
        echo Platforms: windows^|android^|mingw 1>&2
        exit /b 1
    )
    exit /b 0

:install_manifest
    echo [vcpkg] Installing manifest dependencies for triplet %~1...
    pushd "%PROJECT_ROOT%" >nul || goto :error
    "%VCPKG_EXE%" install --triplet %~1 --feature-flags=manifests || goto :error
    popd >nul
    exit /b 0

REM ============================================================================
REM ERROR HANDLING & USAGE
REM ============================================================================

:usage
    echo Usage: vcpkg.bat ^<platform^> [manifest-features]
    echo.
    echo Platforms:
    echo   windows      (x64-windows-static)
    echo   android      (arm64-android)
    echo   mingw        (x64-mingw-static)
    echo.
    echo Examples:
    echo   vcpkg.bat windows
    echo   vcpkg.bat android avif
    exit /b 1

:error
    echo Failed with error #%errorlevel%.
    exit /b %errorlevel%