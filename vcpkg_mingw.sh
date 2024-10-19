#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.linux exists
if [ -d "vcpkg.mingw" ]; then
	cd vcpkg.mingw
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.mingw
	cd vcpkg.mingw
fi

# handle vcpkg update
git checkout 2024.08.23
./bootstrap-vcpkg.sh

./vcpkg --recurse install \
	boost-exception:x64-mingw-static \
	boost-program-options:x64-mingw-static \
	boost-stacktrace:x64-mingw-static \
	glfw3:x64-mingw-static \
	glm:x64-mingw-static \
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:x64-mingw-static \
	stb:x64-mingw-static \
	tinyobjloader:x64-mingw-static \
	curl:x64-mingw-static \
	tinygltf:x64-mingw-static \
	draco:x64-mingw-static \
	rapidjson:x64-mingw-static \
	fmt:x64-mingw-static \
	cpp-base64:x64-mingw-static
