#!/bin/sh
set -e

start_time=$(date +%s)

mkdir -p build/macos
cd build/macos
cmake -Wno-dev -G Ninja -D CMAKE_BUILD_TYPE=Release -D VCPKG_TARGET_TRIPLET=arm64-osx -D CMAKE_TOOLCHAIN_FILE=../vcpkg.macos/scripts/buildsystems/vcpkg.cmake ../..
cmake --build . --config Release

end_time=$(date +%s)
elapsed_time=$((end_time - start_time))

echo "Build completed in $elapsed_time seconds."
