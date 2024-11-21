@echo off

IF "%1" == "clean" (
    rmdir /S /Q build\windows
)

call ./build_windows_magicalego.bat
pushd %CD%
cd build/windows/bin
rem package all files needed for MagicaLego
Packager --out ../assets/paks/lego.pak --src assets --regex ".*.hdr|.*.png|.*.spv"
popd

pushd %CD%
cd build/windows
tar -a -cf MagicaLego-windows.zip ./bin/MagicaLego.exe ./bin/ffmpeg.exe ./bin/vulkan-1.dll ./assets/legos ./assets/sfx ./assets/paks ./assets/locale ./assets/fonts ./assets/models/legobricks.glb
move /Y MagicaLego-windows.zip ..\..\
popd