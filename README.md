# gkNextRenderer

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)
![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)
![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)
![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)

## 这是一个什么项目？

这是一个基于VULKAN实现的实时路径跟踪渲染器，目标是实现能用于实时的路径跟踪渲染。

## 图库 (TrueHDR)

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

*10 seconds Showcase Video*

![Alt text](gallery/Qx50.avif?raw=true "Qx50")
*RayTracing Renderer - QX50*

![Alt text](gallery/city.glb.avif?raw=true "City")
*RayTracing Renderer - City*

![Alt text](gallery/Still.avif?raw=true "Still")
*RayTracing Renderer - Still*

![Alt text](gallery/playground.glb.avif?raw=true "PlayGround")
*RayTracing Renderer - PlayGround*

![Alt text](gallery/LuxBall.avif?raw=true "LuxBall")
*RayTracing Renderer - LuxBall*

![Alt text](gallery/Kitchen.avif?raw=true "Kitchen")
*RayTracing Renderer - Kitchen*

![Alt text](gallery/LivingRoom.avif?raw=true "Living Room")
*RayTracing Renderer - Living Room*

![Alt text](gallery/Qx50_Android.avif?raw=true "Qx50Android")
*Hybrid Renderer (Android) - QX50*

![Alt text](gallery/Complex_Android.avif?raw=true "ComplexAndroid")
*Hybrid Renderer (Android) - Complex Cubes*

## 特性

* Rendering
    * Importance Sampling
    * VNDF Sampling for GGX, by [tigrazone](https://github.com/tigrazone)
    * Adaptive Sampling, thanks [tigrazone](https://github.com/tigrazone)
    * Ground Truth Path Tracing
    * Phsyical Light Unit
    * Temporal Reproject
    * High Performance Bilateral Filter Denoiser
    * OpenImageDenoise Denoiser
    * RayQuery on Android
    * Visibiliy Buffer Rendering
    * Legacy Rendering
    * Hybird Shading
    * GPU Draw
    * GPU Raycast
    
* Scene Management
    * Wavefront OBJ File PBR Scene Support
    * Full GLTF Scene File Support

* System
    * CrossPlatform support for Windows/Linux/MacOS/Android
    * EditorApp including node based MaterialEditor
    * Global Bindless TexturePool
    * MultiThread Resource Loading
    * HDR Display Support
    * Benchmark System
    * Screenshot HDR and encode to avif / jpg

## 构建方式

首先，需要安装 [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)。各个平台根据lunarG的指引，完成安装。其他的依赖都基于 [Microsoft's vcpkg](https://github.com/Microsoft/vcpkg) 构建，执行后续的脚本即可完成编译。
github action包含windows和linux的自动ci，如有问题可参阅解决。

**Windows (Visual Studio 2022)** 

```
vcpkg_windows.bat
build_windows.bat
```

**Windows (MinGW)**

init the MSYS2 MINGW64 shell with following packages
```
pacman -S --needed git base-devel mingw-w64-x86_64-toolchain cmake ninja
```
cmake's module FindVulkan has a little bug on MingGW, try modified FindVulkan.cmake as below
set(_Vulkan_library_name vulkan) -> set(_Vulkan_library_name vulkan-1)
then, execute scripts bellow
```
vcpkg_mingw.sh
build_mings.sh
```

**Android On Windows**

JAVA SDK should be JAVA17 or higher
Install Android Studio or Android SDK Tool, with NDK 25.1.8937393 installed
```
set ANDROID_NDK_HOME=\path\to\ndk
#like: set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\25.1.8937393
vcpkg_android.bat
build_android.bat
deploy_android.bat
```

**Linux**

各平台需要提前安装对应的依赖，vcpkg才可以正确运行。

例如，ubuntu
```
sudo apt-get install ninja-dev curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
./vcpkg_linux.sh
./build_linux.sh
```
SteamDeck Archlinux
```
sudo steamos-readonly disable
sudo pacman -S devel-base ninja
./vcpkg_linux.sh
./build_linux.sh
```

**MacOS**
```
brew install molten-vk
brew install glslang
brew install ninja
./vcpkg_macos.sh
./build_macos.sh
```

## Next Todolist

- [ ] WireFrame Rendering
- [ ] Realtime Renderer Switch
- [ ] GPU Frustum / Occulusion Culling
- [ ] GPU Lod Swtiching
- [ ] Dynamic Scene Management
- [ ] Multi Material Execution
- [ ] Huge Landscape

## References

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

