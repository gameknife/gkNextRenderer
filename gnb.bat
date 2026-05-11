@echo off
setlocal
set "ROOT=%~dp0"
set "GNB=%ROOT%tools\gnb-bin\windows-amd64\gnb.exe"
if exist "%ROOT%gnb.exe" set "GNB=%ROOT%gnb.exe"
if not exist "%GNB%" (
  if exist "%ProgramFiles%\Go\bin\go.exe" (
    pushd "%ROOT%tools\gnb"
    "%ProgramFiles%\Go\bin\go.exe" build -trimpath -ldflags "-s -w" -o "%ROOT%gnb.exe" .\cmd\gnb
    popd
    set "GNB=%ROOT%gnb.exe"
  )
)
if not exist "%GNB%" (
  mkdir "%ROOT%tools\gnb-bin\windows-amd64" >nul 2>nul
  curl -L -o "%GNB%" "https://github.com/gameknife/gkNextEngine/releases/download/paks-latest/gnb-windows-amd64.exe"
)
"%GNB%" %*
