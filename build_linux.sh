#!/bin/sh
set -e

mkdir --parents build/linux
cd build/linux
cmake -G Ninja -D CMAKE_BUILD_TYPE=Release -D VCPKG_TARGET_TRIPLET=x64-linux -D CMAKE_TOOLCHAIN_FILE=../vcpkg.linux/scripts/buildsystems/vcpkg.cmake ../..
cmake --build . --config Release
