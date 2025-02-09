@echo off

mkdir build
cd build || goto :error

IF EXIST vcpkg.windows (
	echo "vcpkg.windows already exists."
	cd vcpkg.windows || goto :error
) ELSE (
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.windows || goto :error
	cd vcpkg.windows || goto :error
	git checkout 2024.08.23 || goto :error
	call bootstrap-vcpkg.bat || goto :error
)

rem handle the vcpkg update, auto process
IF "%1" == "forceinstall" (
	git checkout 2024.08.23 || goto :error
	call bootstrap-vcpkg.bat || goto :error
)

rem add if want avif libavif[aom]:x64-windows-static ^
vcpkg.exe install --recurse ^
	cxxopts:x64-windows-static ^
	glfw3:x64-windows-static ^
	glm:x64-windows-static ^
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:x64-windows-static ^
	stb:x64-windows-static ^
	tinyobjloader:x64-windows-static ^
	curl:x64-windows-static ^
	tinygltf:x64-windows-static ^
	draco:x64-windows-static ^
	rapidjson:x64-windows-static ^
	fmt:x64-windows-static ^
	meshoptimizer:x64-windows-static ^
	ktx:x64-windows-static ^
	joltphysics:x64-windows-static ^
	cpp-base64:x64-windows-static || goto :error

IF "%1" == "avif" (
	vcpkg.exe install --recurse libavif[aom]:x64-windows-static || goto :error
)

cd ..
cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%

