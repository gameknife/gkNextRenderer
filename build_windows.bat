@echo off

for %%X in (cmake.exe) do (set FOUND=%%~$PATH:X)
if defined FOUND (
set CMAKE=cmake
) ELSE (
set CMAKE="%CD%\build\vcpkg.windows\downloads\tools\cmake-3.29.2-windows\cmake-3.29.2-windows-i386\bin\cmake.exe"
)

for %%X in (msbuild.exe) do (set FOUND=%%~$PATH:X)
if defined FOUND (
set MSBUILD=msbuild
) ELSE (
set MSBUILD="%programfiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\msbuild.exe"
)

cd build || goto :error
mkdir windows
cd windows || goto :error


call :SETTIMER

%CMAKE% -D WITH_OIDN=0 -D WITH_AVIF=0 -D WITH_SUPERLUMINAL=0 -D VCPKG_TARGET_TRIPLET=x64-windows-static -D CMAKE_TOOLCHAIN_FILE=../vcpkg.windows/scripts/buildsystems/vcpkg.cmake -G "Visual Studio 17 2022" -A "x64" ../.. || goto :error
%MSBUILD% gkNextRenderer.sln /p:Configuration=Release /verbosity:minimal /nologo || goto :error

call :STOPTIMER "Build"

cd ..
cd ..

exit /b


:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%

goto :EOF

REM Functions

:SETTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set StartTime=%%a
goto :EOF

:STOPTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set EndTime=%%a
for /f %%a in ('powershell -command "((Get-Date '%EndTime%') - (Get-Date '%StartTime%')).TotalSeconds"') do set Duration=%%a
echo %~1 cost %Duration% s.

goto :EOF