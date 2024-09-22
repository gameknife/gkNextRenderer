@echo off

rem call ./build_windows_oidn.bat
pushd %CD%
cd build/windows
copy /Y ..\..\package\*.bat .\
tar -a -cf gkNextRenderer-windows.zip ./bin ./assets/locale ./assets/shaders ./assets/textures ./assets/fonts ./assets/models ./*.bat
move /Y gkNextRenderer-windows.zip ..\..\
popd