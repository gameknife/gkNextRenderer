# gkNextRenderer

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextRenderer)

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)
![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)
![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)
![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)
![iOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)

## 这是一个什么项目？

### 一句话简介

一个基于Vulkan的实时路径跟踪渲染器，目标是实现"**质量**"和"**效率**"能用于"**实时游戏**"的路径跟踪渲染。

### 技术特点

不同于目前游戏中实装的各种光追辅助技术，本项目的目标是最接近GroundTruth的路径跟踪，
不同于其他GPU PathTracer的实现，本项目的目标是实时，Benchmark使用全动态场景，
并提供最接近游戏的运行时环境（轻量级游戏引擎），验证实时光追的可行性，并实验最新的GPU特性，为下一代渲染架构作准备。

### 开发前提

本项目的初衷是：学习，验证，进步。因此，会激进的使用最新技术，有意的规避陈旧技术，利用新c++规范和标准库，全时跨平台开发。

## 贡献指南

如果你计划提交补丁或新特性，请先阅读仓库的贡献指引：[Repository Guidelines](AGENTS.md)。

<details>
<summary>### 子项目</summary>

- **gkNextRenderer**: 主项目，路径追踪渲染器
- **gkNextEditor**: 基于imgui的编辑器框架，用于编辑场景，完全依赖glb的读写
- **MagicaLego**: 类似MagicaVoxel的乐高搭建游戏，全实时路径追踪渲染，用以验证目标
- **gkNextBenchmark**: 专用的Benchmark程序，用于静态和实时场景的Benchmark
- **Packager**: 将资产打包成pkg文件，用于快速部署
- **Portal**: 子项目组合调用程序，提供可视化的各种部署，调试工具（计划中）

</details>

## 图库 (TrueHDR)

https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0

> *MegicaLego Games*

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

> *10 seconds Showcase Video*

<details>
<summary>点击展开/折叠更多图片</summary>

![Alt text](gallery/Qx50.avif?raw=true "Qx50")
> *RayTracing Renderer - QX50*

![Alt text](gallery/city.glb.avif?raw=true "City")
> *RayTracing Renderer - City*

![Alt text](gallery/Still.avif?raw=true "Still")
> *RayTracing Renderer - Still*

![Alt text](gallery/playground.glb.avif?raw=true "PlayGround")
> *RayTracing Renderer - PlayGround*

![Alt text](gallery/LuxBall.avif?raw=true "LuxBall")
> *RayTracing Renderer - LuxBall*

![Alt text](gallery/Kitchen.avif?raw=true "Kitchen")
> *RayTracing Renderer - Kitchen*

![Alt text](gallery/LivingRoom.avif?raw=true "Living Room")
> *RayTracing Renderer - Living Room*

![Alt text](gallery/Qx50_Android.avif?raw=true "Qx50Android")
> *Hybrid Renderer (Android) - QX50*

![Alt text](gallery/Complex_Android.avif?raw=true "ComplexAndroid")
> *Hybrid Renderer (Android) - Complex Cubes*

</details>

## 技术特性

<details>
<summary>点击展开/折叠技术特性列表</summary>

* Rendering
    * Importance Sampling (BRDF / Light)
    * VNDF Sampling for GGX, by [tigrazone](https://github.com/tigrazone)
    * Ground Truth Path Tracing
    * Temporal Reproject with MultiSample catchup
    * High Performance Bilateral Filter Denoiser
    * OpenImageDenoise Denoiser* (need sdk)
    * Visibiliy Buffer Rendering
    * Reference Legacy Deferred Rendering
    * RayTraced Hybrid Rendering
    * Realtime Renderer Switch
    * GPU Draw
    * GPU Raycast

* Engine
    * Multi-Platform support ( Windows / Linux / Mac / Android )
    * Global Bindless TexturePool
    * MultiThread Task Dispatcher ( Async Resource Loading and etc )
    * Full-Scope File Package System
    * Gpu scene updating
    * Aux Rendering
    * HDR Display Support
    * Screenshot HDR and encode to avif / jpg

* Scene Management
    * Full GLTF Scene File Support ( Mesh / Texture / Material / Animation)

* Editor
    * Full Imgui Pipeline
    * Node-based Material Editor

</details>

## 运行

1. 下载最新的MagicaLego游戏版本，通过bin/MagicaLego.exe启动
1. 下载最新Release版本，通过bin/*.exe直接启动
1. 从头构建运行

## 构建方式

首先，需要安装 [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)。各个平台根据lunarG的指引，完成安装。其他的依赖都基于 [Microsoft's vcpkg](https://github.com/Microsoft/vcpkg) 构建，执行后续的脚本即可完成编译。

项目的[Github Action](.github/workflows)包含windows，linux，android的自动ci脚本，作者会维护其正确性。如有任何环境问题可参阅解决。

<details>
<summary>点击展开/折叠各平台构建方式</summary>

本地开发环境部署完成后，各平台可按一下脚本构建

**Windows (Visual Studio 2022)**

```
vcpkg.bat windows
build.bat windows
```

**Windows (MinGW)**

init the MSYS2 MINGW64 shell with following packages，the MSYS2's cmake is not competible with vcpkg, so use cmake inside.
```
pacman -S --needed git base-devel mingw-w64-x86_64-toolchain ninja
```
cmake's module FindVulkan has a little bug on MingGW, try modified FindVulkan.cmake as below
set(_Vulkan_library_name vulkan) -> set(_Vulkan_library_name vulkan-1)
then, execute scripts bellow
```
vcpkg.sh mingw
build.sh mingw
```

**Android On Windows**

JAVA SDK should be JAVA17 or higher
Install Android Studio or Android SDK Tool, with NDK 25.1.8937393 installed
```
set ANDROID_NDK_HOME=\path\to\ndk
#like: set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\25.1.8937393
vcpkg.bat android
build.bat android
deploy_android.bat
```

**Linux**

各平台需要提前安装对应的依赖，vcpkg才可以正确运行。

例如，ubuntu
```
sudo apt-get install ninja-dev curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
./vcpkg.sh linux
./build.sh linux
```
SteamDeck Archlinux
```
sudo steamos-readonly disable
sudo pacman -S devel-base ninja
./vcpkg.sh linux
./build.sh linux
```

**MacOS**
```
brew install molten-vk
brew install glslang
brew install ninja
./vcpkg.sh macos
./build.sh macos
```

</details>

## Next Todolist
- [ ] GPU Frustum / Occulusion Culling
- [ ] GPU Lod Swtiching
- [ ] Huge Landscape

## 参考项目

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

## 随感

项目的发展，学习心得，一些随感，记录于 [Thoughts.md](doc/Thoughts.md)，随时更新。
