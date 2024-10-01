@echo off

IF "%1" == "clean" (
    rmdir /S /Q build\windows
)

call ./build_windows.bat
pushd %CD%
cd build/windows
copy /Y ..\..\package\*.bat %CD%
tar -a -cf gkNextRenderer-windows.zip ./bin ./assets/locale ./assets/shaders ./assets/textures ./assets/fonts ./assets/models ./*.bat
move /Y gkNextRenderer-windows.zip ..\..\
popd