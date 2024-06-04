set CMAKE=%ANDROID_SDK%\cmake\3.22.1\bin\cmake.exe
cd build || goto :error
mkdir android
cd android || goto :error
%CMAKE% -DVCPKG_TARGET_TRIPLET=arm64-android ^
-DCMAKE_MAKE_PROGRAM=%ANDROID_SDK%/cmake/3.22.1/bin/ninja ^
-DCMAKE_TOOLCHAIN_FILE=../vcpkg.android/scripts/buildsystems/vcpkg.cmake ^
-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=%ANDROID_NDK_HOME%/build/cmake/android.toolchain.cmake ^
-DANDROID_ABI=arm64-v8a ../.. ^
-DANDROID_PLATFORM=android-31 ^
-DCMAKE_SYSTEM_VERSION=31 ^
-G Ninja|| goto :error

%ANDROID_SDK%/cmake/3.22.1/bin/ninja.exe

cd ..
cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%