# gkNextRenderer

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)
![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)
![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)
![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)
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
    * Sample Reproject
    * RayQuery on PC & Android
* Non-Raytracing Pipeline
    * Visibiliy Buffer Rendering
    * Legacy Rendering
* Hybird Pipeline
    * Visibility Buffer
    * Indirect Hybird Shading
    * Sample Reproject
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
gkNextRenderer.exe --width=1920 --height=1080 --scene=0 --benchmark
```
下列指令可以进行所有场景遍历benchmark
```
gkNextRenderer.exe --width=1920 --height=1080 --benchmark --next-scenes
```
目前benchmark结果会上传我的个人网站，进行信息统计

[https://gameknife.site:60011/gpubenchmark?category=LuxBall&renderer=RayTracingRenderer](https://gameknife.site:60011/gpubenchmark?category=LuxBall&renderer=RayTracingRenderer)

## 后续计划

- Scene Management
    - ~~Element Instancing~~
    - Multi draw indirect
    - GLobal Dynamic Bindless Textures
- RayTracing Pipeline
    - ~~Temporal Reprojection~~
    - Ray Query Pipeline
    - MIS
- Non-RayTracing Pipeline
    - ~~Modern Deferred Shading~~
    - ~~Hybrid Rendering (Shadow & SkyVisibility)~~
    - ~~Reference Legacy Lighting~~
    - Referencing Legacy Indirect Lighting (Shadowmap, SSXX, etc)
- Common Rendering Feature
    - SVGF Denoise
- Platform
    - ~~MacOS moltenVK~~
    - ~~Android Vulkan ( RayTracing on 8Gen2 )~~
- Benchmark
    - ~~Online benchmark chart~~
    - Version Management
    - ~~Full render statstics~~
    - ~~GPU Timer based on Vulkan Query~~
- Others
    - ~~HDR display support~~
    - ~~HDR Env loading & apply to skylight (both RT & non-RT pipeline)~~
    - ~~GLTF Scene Support, with real scene management.~~
    - ~~Screenshot and save to avif hdr format~~
    - DebugViews

## Next Todolist

- [x] GLTF format load
- [x] HDR AVIF write
- [x] Benchmark Website & Ranking
- [x] Full stastics
- [x] Multi draw indirect
- [ ] GPU Frustum / Occulusion Culling
- [ ] GPU Lod Swtiching
- [x] Android Non-Raytracing Rendering
- [x] Android RayQuery Rendering
- [ ] Android Input Handling
- [x] Realtimg self statics system
- [ ] Auto release by Github action
- [ ] Global Dynamic Bindless Textures
- [x] Hybrid rendering with ray query
- [ ] Full scope refactor
- [ ] Dynamic Scene Management
- [ ] Multi Material Execution
- [ ] Huge Landscape

## 随感

- **[VCPKG]** 是一个好东西，2024年我才“了解”到，真的是相见恨晚。这是一个类似于npm / pip的针对cpp的包管理库，由微软维护，但支持的平台有win/linux/osx/android/ios甚至主机。通过vcpkg，很方便的就做到了windows, linux, macOS的跨平台。目前我的steamdeck和apple m3的mbp，均可以正常的跑起来. steamdeck在打开棋盘格渲染后甚至有40+的fps

- **[RayQuery]** 因为一开始是接触的metal3的hardware raytracing，当时的写法是在一个compute shader里调用rayquery的接口。而此demo使用的是khr_raytracing_pipeline，更加类似dx12的写法。而ray query其实也是可以用的。我在8gen2上写了一个vulkan初始化程序，他的光追也是只支持到ray query，因此感觉这个才是一个真正的跨平台方案。

- **[PathTracing]** SmallPT的思路是非常直观的，就是“模拟”真实世界。肆无忌惮的朝球面的各个方向发射射线，然后模拟光线的反弹。蒙特卡洛方法，通过无数多的样本，最终收敛到一个真实的结果。
所以基础的PT代码，是非常好看的，一个递归算法就解决了。
而PT之所以有这么多人研究，就是希望他能够更快，能够不发射这么多的样本，就可以得到一个真实的结果。

- **[HDR]** 一直对HDR显示挺陌生的，vulkan在swapchain创建时直接提供了可供选择的hdr surface和colorspace定义，渲染时按照PQ的定义，直接将线性颜色写入swapchain的10bit纹理即可，十分方便。对于hdr，怎么输出成纹理显示出来也是一个问题。avif可能是一个终极解决方案，现代浏览器对avif支持也很好，页面上的avif图片直接可以被hdr显示出来。因此摸索了下，通过aom库将backbuffer encode到avif输出。benchmark页面和gallery中的图片都是这样输出的。

- **[光追加速结构]** raytracing框架下的acceleration struct是一个非常好的抽象。他对场景管理实际上是一种非常好的概括。高速的trace性能，大胆设想一下，整个渲染管线完全抛弃光栅化也不是不行。一次primary ray的对pc来说代价其实很小。考虑可以做一种实验性的renderer，尝试下性能和开销。

- **[Visbiliy Buffer]** 其实10年前就提出了，最近翻出来研究，发现和现代渲染架构算是绑死的，目前这套bindless架构十分适合实现他，于是很快就写了一个modern deferred renderer，目前已经搬兼容了安卓平台。可以看出，几乎所有情况，visibility buffer都会比传统gbuffer的方式快，在场景overdraw严重的工况下，visibiliy buffer最多可以快到一倍。在安卓手机上尤为明显。看来带宽友好哈cache friendly的方式确实提效明显。

- **[Multi Draw Indirect]** 功能已经出来挺久了，基本上，可以将drawcall的消耗完全的在cpu释放，让drawcall的组织，通过cs移交给gpu。包括视锥裁剪，遮挡剔除，LOD切换等。这些工作，其实也非常时候GPU并行。为了验证MDI，我专门修复了baserendering，制作了一个多达2w+ instance node的场景，用于测试。目前在这个无剔除的渲染结构上，可以看出MDI相对于传统的CPU drawcall，已经快出好几倍了。帧耗时是0.7ms vs 2.5ms。后续准备把剔除和LOD也在这个场景上制作了，关注后续效果。
    - 安卓下面mid虽然可以正常工作，但发现了极大问题。在pc上，2w个drawcall的调用，和组成instance调用的开销区别非常小。但是在安卓下，天差地别，mid几乎和直接cpu裸掉2w次drawcall差不多了，需要详细的profile

- **[GPU Stats]** 安卓系统上没有一个很好的gpu profile工具，高通的snapdragon profiler有点ptsd。考虑先通过vulkan的timequery和native的timequery，自建一个stats系统。来发现一下gpu上的性能问题。借此机会，重新梳理下stats系统的搭建。之前gkEngine上做得有点浅了。当然，计划分几步，第一步先解决掉安卓上的profile问题。

- **[Android RayTracing]** 高通和mali从上一代的旗舰开始，都配备了RayTracing架构，虽然都支持RayQuery。一开始，我觉得也就是个噱头吧，RTX都还跑不明白。结果这次在我的SG 8gen2上实现了RayQuery，性能出乎意料。PrimaryRay加上bindless的材质shading，1920分辨率下，居然能拉到100-120fps，gputimer的结果在6-8ms之间。看起来，通过ray-tracing来做间接光照的hybird管线是没有任何问题的。考虑把skyvisibility, shadow, second bounce在hybird管线实现下，争取能在安卓上跑出一个可用的效果。
    - 初步实现了一个Hybird Rendering管线，Direction Light和Indirect light分别两条ray，通过32帧的reproject temporal sample，得到了一个较好的groundtruth效果。Indirect打到物件，目前是magic了一个0.2的能量衰减，改进可考虑reproject到上一帧的结果，有值的话直接复用。

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
brew install nasm
brew install molten-vk
brew install glslang
./vcpkg_macos.sh
./build_macos.sh
```

## References

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

