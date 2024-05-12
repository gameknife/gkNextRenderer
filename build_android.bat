set CMAKE=cmake
cd build || goto :error
mkdir android
cd android || goto :error
%CMAKE% -D VCPKG_TARGET_TRIPLET=arm64-android -D CMAKE_TOOLCHAIN_FILE=../vcpkg.android/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_ANDROID=ON -DANDROID_ABI=arm64-v8a ../.. || goto :error
cd ..
cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%