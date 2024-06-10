pushd android
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
adb shell mkdir -p /sdcard/Android/data/com.gknextrenderer/files/assets/fonts
adb shell mkdir -p /sdcard/Android/data/com.gknextrenderer/files/assets/shaders
adb shell mkdir -p /sdcard/Android/data/com.gknextrenderer/files/assets/textures
adb shell mkdir -p /sdcard/Android/data/com.gknextrenderer/files/assets/models
adb push ./assets /sdcard/Android/data/com.gknextrenderer/files
adb shell monkey -p com.gknextrenderer 1
popd