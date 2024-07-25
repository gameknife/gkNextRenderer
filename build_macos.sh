#!/bin/sh
set -e

mkdir -p build/macos
cd build/macos
cmake -G Ninja -D CMAKE_BUILD_TYPE=Release -D VCPKG_TARGET_TRIPLET=x64-osx -D CMAKE_TOOLCHAIN_FILE=../vcpkg.macos/scripts/buildsystems/vcpkg.cmake ../..
cmake --build . --config Release
