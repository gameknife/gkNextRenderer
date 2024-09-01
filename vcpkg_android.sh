#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.linux exists
if [ -d "vcpkg.android" ]; then
	cd vcpkg.android
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.android
	cd vcpkg.android
	git checkout 2024.08.23
	./bootstrap-vcpkg.sh
fi

#replace the triplets/arm64-android.cmake file with ours
cp -f ../../android/custom-triplets/arm64-android.cmake ./triplets/arm64-android.cmake

./vcpkg --recurse install \
	boost-exception:arm64-android \
	boost-program-options:arm64-android \
	boost-stacktrace:arm64-android \
	glm:arm64-android \
	imgui[core,freetype,android-binding,vulkan-binding,docking-experimental]:arm64-android \
	stb:arm64-android \
	tinyobjloader:arm64-android \
	curl:arm64-android \
	tinygltf:arm64-android \
	draco:arm64-android \
	fmt:arm64-android \
	cpp-base64:arm64-android
