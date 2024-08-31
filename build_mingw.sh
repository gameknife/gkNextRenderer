#!/bin/sh
set -e

mkdir --parents build/mingw
cd build/mingw
cmake -G Ninja -D CMAKE_BUILD_TYPE=Release -D VCPKG_TARGET_TRIPLET=x64-mingw-static -D CMAKE_TOOLCHAIN_FILE=../vcpkg.mingw/scripts/buildsystems/vcpkg.cmake ../..
cmake --build . --config Release
