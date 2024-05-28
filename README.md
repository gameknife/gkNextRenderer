# gkNextRenderer

**2024主题：补课**

实现基于[GPSnoopy的RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan) fork而来，本身是一个非常工整的Vulkan渲染管线，作者在使用Vulkan的RayTracing管线实现了RayTracingInOneWeekend的GPU版本，性能很高。

基于其版本，修改了采样算法，Shading模型，Model的加载方案，以及Reproject和降噪处理。使之更加走向标准的离线渲染器效果（Blender Cycles GPU）。

通过本项目，旨在补上在现代渲染上缺的课，同时更加深入的理解GPU光线跟踪，争取在下一个时代来临前做好准备。

> 是的，我认为将来光线跟踪一定会成为主流。

同时，基于vulkan维护一个尽量简洁的渲染流水线，快速的实现多种现代渲染管线，并方便的部署于多个平台，调式性能。

## 图库

![Alt text](gallery/luxball.jpg?raw=true "luxball")
![Alt text](gallery/kitchen.jpg?raw=true "kitchen")
![Alt text](gallery/livingroom.jpg?raw=true "livingroom")
![Alt text](gallery/still.jpg?raw=true "still")

## 特性

* Vulkan Raytracing pipeline
    * Importance Sampling
    * Ground Truth Path Tracing
* Non-Raytracing Pipeline
    * Visibiliy Buffer Rendering
    * Legacy Rendering
* Common Rendering Feature
    * Compute Checkerbox Rendering
    * Temporal Reproject
* obj / gltf PBR Scene Support
* CrossPlatform support for Windows/Linux/MacOS
* HDR Display Support
* Phsyical Light Unit

## 性能

在我的RTX4070上，1920x1080下，1spp + 多帧sample的情况下，图库内的场景基本都能跑出400-500fps的帧率。并且在后期完成reproject后，这个帧率是有可能在真实的实时渲染环境内达到的。rtx的性能出乎了我的意料

目前很多实现的细节还没有深究，应该还有一定的优化空间

原作者在benchmark方面做了一些框架型的工作，我引入了libcurl来上传benchmark分数，后期可以针对固定版本，作一些benchmark收集，也许做成一个光追性能的标准测试程序也有可能

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
    - ~~Temporal Reprojection~~
    - Ray Query Pipeline

- Non-RayTracing Pipeline
    - ~~Modern Deferred Shading~~
    - Hybrid Rendering
    - ~~Reference Legacy Lighting~~

- Common Rendering Feature
    - SVGF Denoise

- Platform
    - ~~MacOS moltenVK~~
    - Android Vulkan ( RayTracing on 8Gen2 )

- Benchmark
    - Online benchmark chart
    - Version Management

- Others
    - ~~HDR display support~~
    - ~~HDR Env loading & apply to skylight (both RT & non-RT pipeline)~~


## 随感

- vcpkg是一个好东西，2024年我才“了解”到，真的是相见恨晚。这是一个类似于npm / pip的针对cpp的包管理库，由微软维护，但支持的平台有win/linux/osx/android/ios甚至主机。通过vcpkg，很方便的就做到了windows, linux, macOS的跨平台。目前我的steamdeck和apple m3的mbp，均可以正常的跑起来. steamdeck在打开棋盘格渲染后甚至有40+的fps

- 因为一开始是接触的metal3的hardware raytracing，当时的写法是在一个compute shader里调用rayquery的接口。而此demo使用的是khr_raytracing_pipeline，更加类似dx12的写法。而ray query其实也是可以用的。我在8gen2上写了一个vulkan初始化程序，他的光追也是只支持到ray query，因此感觉这个才是一个真正的跨平台方案。

## Building

First you will need to install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). For Windows, LunarG provides installers. For Ubuntu LTS, they have native packages available. For other Linux distributions, they only provide tarballs. The rest of the third party dependencies can be built using [Microsoft's vcpkg](https://github.com/Microsoft/vcpkg) as provided by the scripts below.

**Windows (Visual Studio 2022 x64 solution)** 
```
vcpkg_windows.bat
build_windows.bat
```
**Linux (GCC 9+ Makefile)**

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

SteamDeck Archlinux

```
sudo steamos-readonly disable
sudo pacman devel-base
```

**MacOS**

macOS using moltenVK, you should install it. then just
```
./vcpkg_macos.sh
./build_macos.sh
```

## References

### Initial Implementation (NVIDIA vendor specific extension)

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
