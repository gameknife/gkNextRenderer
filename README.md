# gkNextRenderer

2024主题：补课

实现基于[GPSnoopy的RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)fork而来，本身是一个非常工整的Vulkan渲染管线，作者在使用Vulkan的RayTracing管线实现了RayTracingInOneWeekend的GPU版本，性能很高。

基于其版本，修改了采样算法，Shading模型，Model的加载方案，以及降噪处理。使之更加走向标准的离线渲染器效果（Blender Cycles GPU）。

通过本项目，旨在补上在现代渲染上缺的课，同时更加深入的理解GPU光线跟踪，争取在下一个时代来临前做好准备。

是的，我认为将来光线跟踪一定会成为主流。

## 图库



## 特性

* Vulkan Raytracing pipeline
* Ground Truth Path Tracing
* Non-Raytracing pipeline
* Visibiliy Buffer Rendering
* obj / gltf PBR Scene Support

## 性能

Using a GeForce RTX 2080 Ti, the rendering speed is obscenely faster than using the CPU renderer. Obviously both implementations are still quite naive in some places, but I'm really impressed by the performance. The cover scene of the first book reaches ~140fps at 1280x720 using 8 rays per pixel and up to 16 bounces.

I suspect performance could be improved further. I have created each object in the scene as a separate instance in the top level acceleration structure, which is probably not the best for data locality. The same goes for displaying multiple [Lucy statues](http://graphics.stanford.edu/data/3Dscanrep/), where I have naively duplicated the geometry rather than instancing it multiple times.

## Benchmarking

Command line arguments can be used to control various aspects of the application. Use `--help` to see all modes and arguments. For example, to run the ray tracer in benchmark mode in 2560x1440 fullscreen for scene #1 with vsync off:
```
RayTracer.exe --benchmark --width 2560 --height 1440 --fullscreen --scene 1 --present-mode 0
```
To benchmark all the scenes, starting from scene #1:
```
RayTracer.exe --benchmark --width 2560 --height 1440 --fullscreen --scene 1 --next-scenes --present-mode 0
```
Here are my results with the command above on a few different computers.


## 后续计划

- Scene Management
    - Element Instancing
- RayTracing Pipeline
    - Temporal Reprojection
- Non-RayTracing Pipeline
    - Modern Deferred Shading
- Platform
    - MacOS moltenVK
    - Android Vulkan ( RayTracing on 8Gen2)

## Building

First you will need to install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). For Windows, LunarG provides installers. For Ubuntu LTS, they have native packages available. For other Linux distributions, they only provide tarballs. The rest of the third party dependencies can be built using [Microsoft's vcpkg](https://github.com/Microsoft/vcpkg) as provided by the scripts below.

If in doubt, please check the GitHub Actions [continuous integration configurations](.github/workflows) for more details.

**Windows (Visual Studio 2022 x64 solution)** [![Windows CI Status](https://github.com/GPSnoopy/RayTracingInVulkan/workflows/Windows%20CI/badge.svg)](https://github.com/GPSnoopy/RayTracingInVulkan/actions?query=workflow%3A%22Windows+CI%22)
```
vcpkg_windows.bat
build_windows.bat
```
**Linux (GCC 9+ Makefile)** [![Linux CI Status](https://github.com/GPSnoopy/RayTracingInVulkan/workflows/Linux%20CI/badge.svg)](https://github.com/GPSnoopy/RayTracingInVulkan/actions?query=workflow%3A%22Linux+CI%22)

For example, on Ubuntu 20.04 (same as the CI pipeline, build steps on other distributions may vary):
```
sudo apt-get install curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
./vcpkg_linux.sh
./build_linux.sh
```

Fedora Installation

```
sudo dnf install libXinerama-devel libXcursor-devel libX11-devel libXrandr-devel mesa-libGLU-devel pkgconfig ninja-build cmake gcc gcc-c++ vulkan-validation-layers-devel vulkan-headers vulkan-tools vulkan-loader-devel vulkan-loader glslang glslc
./vcpkg_linux.sh
./build_linux.sh
```

## References

### Initial Implementation (NVIDIA vendor specific extension)

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
