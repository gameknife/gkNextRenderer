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
  ./bootstrap-vcpkg.sh
fi

./vcpkg --recurse install \
	cpptrace:arm64-osx \
	cxxopts:arm64-osx \
	glfw3:arm64-osx \
	glm:arm64-osx \
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:arm64-osx \
	stb:arm64-osx \
	tinyobjloader:arm64-osx \
	curl:arm64-osx \
	tinygltf:arm64-osx \
	draco:arm64-osx \
	rapidjson:arm64-osx \
	fmt:arm64-osx \
	meshoptimizer:arm64-osx \
	ktx:arm64-osx \
	joltphysics:arm64-osx \
	xxhash:arm64-osx \
	cpp-base64:arm64-osx
