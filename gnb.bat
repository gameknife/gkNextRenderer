@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "CACHE_DIR=%ROOT%tools\gnb-bin\windows-amd64"
set "CACHE_GNB=%CACHE_DIR%\gnb.exe"
set "CACHE_VERSION=%CACHE_DIR%\gnb-version.txt"
set "REMOTE_BASE_URL=https://github.com/gameknife/gkNextEngine/releases/download/paks-latest"
set "REMOTE_GNB_URL=%REMOTE_BASE_URL%/gnb-windows-amd64.exe"
set "REMOTE_VERSION_URL=%REMOTE_BASE_URL%/gnb-version.txt"
set "LOCAL_GNB=%ROOT%gnb.exe"
set "GNB=%CACHE_GNB%"
set "LOCAL_VERSION=%GNB_VERSION%"
set "GOEXE="
for /f "delims=" %%I in ('where go 2^>nul') do if not defined GOEXE set "GOEXE=%%I"
if not defined GOEXE if exist "%ProgramFiles%\Go\bin\go.exe" set "GOEXE=%ProgramFiles%\Go\bin\go.exe"
if not defined LOCAL_VERSION for /f "usebackq delims=" %%I in (`git -C "%ROOT:~0,-1%" rev-parse HEAD 2^>nul`) do if not defined LOCAL_VERSION set "LOCAL_VERSION=%%I"
if not defined LOCAL_VERSION set "LOCAL_VERSION=dev"

if exist "%ROOT%tools\gnb\go.mod" if defined GOEXE (
  set "NEED_BUILD=1"
  if exist "%LOCAL_GNB%" (
    for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$binary = Get-Item '%LOCAL_GNB%'; $newer = Get-ChildItem -Path '%ROOT%tools\gnb' -Recurse -File | Where-Object { $_.Name -match '\.go$|^go\.mod$|^go\.sum$|\.html$' -and $_.LastWriteTimeUtc -gt $binary.LastWriteTimeUtc } | Select-Object -First 1; if ($newer) { '1' } else { '0' }"`) do set "NEED_BUILD=%%I"
  )
  pushd "%ROOT%tools\gnb"
  if "!NEED_BUILD!"=="1" "%GOEXE%" build -tags "desktop,production,wv2runtime.embed" -trimpath -ldflags "-s -w -X main.version=%LOCAL_VERSION%" -o "%LOCAL_GNB%" .\cmd\gnb
  if errorlevel 1 exit /b 1
  popd
  set "GNB=%LOCAL_GNB%"
) else (
  call :sync_remote_gnb
  if errorlevel 1 exit /b 1
  set "GNB=%CACHE_GNB%"
)
"%GNB%" %*
exit /b %errorlevel%

:sync_remote_gnb
mkdir "%CACHE_DIR%" >nul 2>nul
set "NEED_DOWNLOAD=0"
set "HAD_CACHE=0"
if not exist "%CACHE_GNB%" set "NEED_DOWNLOAD=1"
if exist "%CACHE_GNB%" set "HAD_CACHE=1"

set "REMOTE_VERSION_VALUE="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; try { (Invoke-WebRequest -UseBasicParsing '%REMOTE_VERSION_URL%').Content.Trim() } catch { exit 1 }" 2^>nul`) do if not defined REMOTE_VERSION_VALUE set "REMOTE_VERSION_VALUE=%%I"

set "LOCAL_VERSION_VALUE="
if exist "%CACHE_VERSION%" set /p LOCAL_VERSION_VALUE=<"%CACHE_VERSION%"
if defined REMOTE_VERSION_VALUE if /I not "%REMOTE_VERSION_VALUE%"=="%LOCAL_VERSION_VALUE%" set "NEED_DOWNLOAD=1"

if "%NEED_DOWNLOAD%"=="1" (
  echo [gnb] download %REMOTE_GNB_URL%
  powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; $dst='%CACHE_GNB%'; $tmp=$dst + '.part'; try { Invoke-WebRequest -UseBasicParsing '%REMOTE_GNB_URL%' -OutFile $tmp; Move-Item -Force $tmp $dst } catch { Remove-Item -Force -ErrorAction SilentlyContinue $tmp; exit 1 }"
  if errorlevel 1 (
    if "%HAD_CACHE%"=="1" (
      echo [gnb] warning: failed to update bootstrap binary, using cached copy 1>&2
    ) else (
      echo [gnb] bootstrap binary download failed 1>&2
      exit /b 1
    )
  ) else (
    if defined REMOTE_VERSION_VALUE >"%CACHE_VERSION%" echo %REMOTE_VERSION_VALUE%
  )
)

if not exist "%CACHE_GNB%" (
  echo [gnb] bootstrap binary missing and download failed 1>&2
  exit /b 1
)
exit /b 0
