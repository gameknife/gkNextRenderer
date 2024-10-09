#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.linux exists
if [ -d "vcpkg.macos" ]; then
	cd vcpkg.macos
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.macos
	cd vcpkg.macos
fi

git checkout 2024.08.23
./bootstrap-vcpkg.sh

./vcpkg --recurse install \
	boost-exception:x64-osx \
	boost-program-options:x64-osx \
	boost-stacktrace:x64-osx \
	glfw3:x64-osx \
	glm:x64-osx \
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:x64-osx \
	stb:x64-osx \
	tinyobjloader:x64-osx \
	curl:x64-osx \
	tinygltf:x64-osx \
	draco:x64-osx \
	rapidjson:x64-osx \
	fmt:x64-osx \
	cpp-base64:x64-osx
