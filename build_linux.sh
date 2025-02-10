#!/bin/sh
set -e

start_time=$(date +%s)

mkdir --parents build/linux
cd build/linux
cmake -Wno-dev -G Ninja -D CMAKE_BUILD_TYPE=Release -D VCPKG_TARGET_TRIPLET=x64-linux -D CMAKE_TOOLCHAIN_FILE=../vcpkg.linux/scripts/buildsystems/vcpkg.cmake ../..
cmake --build . --config Release

end_time=$(date +%s)
elapsed_time=$((end_time - start_time))

echo "Build completed in $elapsed_time seconds."