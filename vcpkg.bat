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
    set "VCPKG_DEFAULT_BINARY_CACHE=%PROJECT_ROOT%\.vcpkg_bincache"
    set "VCPKG_GIT_REF=2025.10.17"
    if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" (
        mkdir "%VCPKG_DEFAULT_BINARY_CACHE%"
    )
    
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
        echo [vcpkg] Updating vcpkg in %VCPKG_ROOT%...
        pushd "%VCPKG_ROOT%" >nul || goto :error
        git fetch origin --tags --force
        git -c advice.detachedHead=false checkout --force "%VCPKG_GIT_REF%"
        git reset --hard "%VCPKG_GIT_REF%"
        popd >nul
    ) else (
        echo [vcpkg] Updating vcpkg in %VCPKG_ROOT%...
        pushd "%VCPKG_ROOT%" >nul || goto :error
        git fetch origin --tags --force
        git -c advice.detachedHead=false checkout --force "%VCPKG_GIT_REF%"
        git reset --hard "%VCPKG_GIT_REF%"
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