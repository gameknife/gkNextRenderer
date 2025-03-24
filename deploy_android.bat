pushd android
adb uninstall com.gknextrenderer
adb install -r ./app/build/outputs/apk/release/app-release.apk
adb shell monkey -p com.gknextrenderer 1
popd