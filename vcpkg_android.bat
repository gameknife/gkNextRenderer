@echo off

set PROJROOT=%CD%

mkdir build
cd build || goto :error

IF EXIST vcpkg.android (
	echo "vcpkg.android already exists."
	cd vcpkg.android || goto :error
) ELSE (
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.android || goto :error
	cd vcpkg.android || goto :error
)

git checkout 2024.08.23 || goto :error
call bootstrap-vcpkg.bat || goto :error

rem replace the triplets/arm64-android.cmake file with ours
copy /Y %PROJROOT%\android\custom-triplets\arm64-android.cmake %CD%\triplets\arm64-android.cmake

.\vcpkg.exe --recurse install ^
	boost-exception:arm64-android ^
	boost-program-options:arm64-android ^
	boost-stacktrace:arm64-android ^
	glm:arm64-android ^
	imgui[core,freetype,android-binding,vulkan-binding,docking-experimental]:arm64-android ^
	stb:arm64-android ^
	tinyobjloader:arm64-android ^
	tinygltf:arm64-android ^
	curl:arm64-android ^
	draco:arm64-android ^
	fmt:arm64-android ^
	meshoptimizer:arm64-android ^
	ktx:arm64-android ^
	joltphysics:arm64-android ^
	cpp-base64:arm64-android || goto :error

cd ..
cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%

