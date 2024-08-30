pushd android
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
adb shell monkey -p com.gknextrenderer 1
popd