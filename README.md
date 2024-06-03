# gkNextRenderer
---
![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)
![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)

## 简介

**2024主题：补课**

本项目开始于[GPSnoopy的RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan) 工程的fork，本身是一个非常工整的Vulkan渲染管线，作者在使用Vulkan的RayTracing管线实现了RayTracingInOneWeekend的GPU版本，性能很高。

基于其版本，增加了重要性采样，Shading模型，更全面的Model的加载，以及Reproject和降噪处理。使之更加走向标准的离线渲染器效果（Blender Cycles GPU）。

通过本项目，旨在补上在现代渲染上缺的课，同时更加深入的理解GPU光线跟踪，争取在下一个时代来临前做好准备。

> 是的，我认为将来光线跟踪一定会成为主流。

同时，基于vulkan维护一个尽量简洁的渲染流水线，快速的实现多种现代渲染管线，并方便的部署于多个平台，调试性能。

## 图库


![Alt text](gallery/LuxBall.avif?raw=true "LuxBall")
![Alt text](gallery/Kitchen.avif?raw=true "Kitchen")
![Alt text](gallery/LivingRoom.avif?raw=true "LivingRoom")
![Alt text](gallery/Still.avif?raw=true "still")
![Alt text](gallery/CornellBox.avif?raw=true "Cornell Box")

## 特性

* Vulkan Raytracing pipeline
    * Importance Sampling
    * Ground Truth Path Tracing
    * Phsyical Light Unit
* Non-Raytracing Pipeline
    * Visibiliy Buffer Rendering
    * Legacy Rendering
* Common Rendering Feature
    * Compute Checkerbox Rendering
    * Temporal Reproject
* File Format support
    * Wavefront OBJ File PBR Scene Support
    * GLTF Scene File Support
* CrossPlatform support for Windows/Linux/MacOS
* HDR Display Support
* Screenshot HDR and encode to avif

## 性能

在我的RTX4070上，1920x1080下，1spp + 多帧sample的情况下，图库内的场景基本都能跑出400-500fps的帧率。并且在后期完成reproject后，这个帧率是有可能在真实的实时渲染环境内达到的。rtx的性能出乎了我的意料

目前很多实现的细节还没有深究，应该还有一定的优化空间

原作者在benchmark方面做了一些框架型的工作，我引入了libcurl来上传benchmark分数，后期可以针对固定版本，作一些benchmark收集，也许做成一个光追性能的标准测试程序也有可能

## Benchmarking

使用下列指令可以对单场景进行benchmark
```
gkNextRenderer.exe --width=1920 --height=1080 --scene=6 --benchmark
```
下列指令可以进行所有场景遍历benchmark
```
gkNextRenderer.exe --width=1920 --height=1080 --scene=6 --benchmark --next-scenes
```
目前benchmark结果会上传我的个人网站，进行信息统计

[https://gameknife.site:60011/gpubenchmark?category=LuxBall&renderer=RayTracingRenderer](https://gameknife.site:60011/gpubenchmark?category=LuxBall&renderer=RayTracingRenderer)

## 后续计划

- Scene Management
    - ~~Element Instancing~~
    - GLobal Bindless Textures
- RayTracing Pipeline
    - ~~Temporal Reprojection~~
    - Ray Query Pipeline
    - MIS
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
    - ~~GLTF Scene Support, with real scene management.~~
    - ~~Screenshot and save to avif hdr format~~

## Next Todolist

- [x] GLTF format load
- [x] HDR AVIF write
- [x] Benchmark Website & Ranking
- [ ] Global Bindless Textures
- [ ] Hybrid rendering with ray query
- [ ] Android Hybrid Rendering
- [ ] Full scope refactor

## 随感

- vcpkg是一个好东西，2024年我才“了解”到，真的是相见恨晚。这是一个类似于npm / pip的针对cpp的包管理库，由微软维护，但支持的平台有win/linux/osx/android/ios甚至主机。通过vcpkg，很方便的就做到了windows, linux, macOS的跨平台。目前我的steamdeck和apple m3的mbp，均可以正常的跑起来. steamdeck在打开棋盘格渲染后甚至有40+的fps

- 因为一开始是接触的metal3的hardware raytracing，当时的写法是在一个compute shader里调用rayquery的接口。而此demo使用的是khr_raytracing_pipeline，更加类似dx12的写法。而ray query其实也是可以用的。我在8gen2上写了一个vulkan初始化程序，他的光追也是只支持到ray query，因此感觉这个才是一个真正的跨平台方案。

- SmallPT的思路是非常直观的，就是“模拟”真实世界。肆无忌惮的朝球面的各个方向发射射线，然后模拟光线的反弹。蒙特卡洛方法，通过无数多的样本，最终收敛到一个真实的结果。
所以基础的PT代码，是非常好看的，一个递归算法就解决了。
而PT之所以有这么多人研究，就是希望他能够更快，能够不发射这么多的样本，就可以得到一个真实的结果。

- 一直对HDR显示挺陌生的，vulkan在swapchain创建时直接提供了可供选择的hdr surface和colorspace定义，渲染时按照PQ的定义，直接将线性颜色写入swapchain的10bit纹理即可，十分方便。对于hdr，怎么输出成纹理显示出来也是一个问题。avif可能是一个终极解决方案，现代浏览器对avif支持也很好，页面上的avif图片直接可以被hdr显示出来。因此摸索了下，通过aom库将backbuffer encode到avif输出。benchmark页面和gallery中的图片都是这样输出的。

- raytracing框架下的acceleration struct是一个非常好的抽象。他对场景管理实际上是一种非常好的概括。高速的trace性能，大胆设想一下，整个渲染管线完全抛弃光栅化也不是不行。一次primary ray的对pc来说代价其实很小。考虑可以做一种实验性的renderer，尝试下性能和开销。

## Building

首先，需要安装 [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)。各个平台根据lunarG的指引，完成安装。其他的依赖都基于 [Microsoft's vcpkg](https://github.com/Microsoft/vcpkg) 构建，执行后续的脚本即可完成编译。
github action包含windows和linux的自动ci，如有问题可参阅解决。

**Windows (Visual Studio 2022)** 
```
vcpkg_windows.bat
build_windows.bat
```
**Linux**

各平台需要提前安装对应的依赖，vcpkg才可以正确运行。

例如，ubuntu
```
sudo apt-get install curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
./vcpkg_linux.sh
./build_linux.sh
```
SteamDeck Archlinux
```
sudo steamos-readonly disable
sudo pacman devel-base
./vcpkg_linux.sh
./build_linux.sh
```

**MacOS**
```
./vcpkg_macos.sh
./build_macos.sh
```

## References

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

