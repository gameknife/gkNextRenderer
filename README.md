# gkNextEngine

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextRenderer)

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)
![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)
![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)
![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)
![iOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)

## 这是一个什么项目？

### 一句话简介

一个跨平台的3D游戏引擎，基于现代c++和vulkan api。实践现代渲染与现代游戏技术。

### 技术特点

**全平台支持** Windows(x86_64) / Linux(x86_64) / Mac(arm64) / Android(arm64) / iOS(arm64)

**现代渲染** 支持硬件光追，软件光追，支持不同效率的实时全局光照

**轻代码库** 核心引擎代码目标5万行以内，1.5万行@2025/09。激进重构，无冗余代码，拥抱成熟的跨平台第三方库，绝不造废轮子。

**直接工具链** 单纯基于gltf的资产管理，大部分资产管理工作前置于Blender

**AI辅助** 拥抱AI，使用各类AI工具辅助开发

### 开发前提

本项目的初衷是：学习，验证，进步。

因此，会激进的使用最新技术，有意的规避陈旧技术，利用新c++规范和标准库，全时跨平台开发。

## 子项目

本项目配套多个子项目，推动和验证引擎开发

<details>
<summary>展开查看</summary>

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
<summary>展开查看更多图片</summary>

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
<summary>展开查看</summary>

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

## 构建方式

项目基于[Microsoft's vcpkg](https://github.com/Microsoft/vcpkg)管理第三方依赖，使用cmake作为构建工具。提供各平台脚本实现一键部署依赖，但需要一定的环境前提：

- 公共前提: 良好的网络环境，能够完全访问github等站点，git已经安装并可用
- Windows: 已经安装Visual Studio 2022，包含C++工作负载
- Linux: 无需任何依赖
- Macos: 已经安装Xcode或者Command Line Tools (作者在Xcode 16.1测试)

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

## 第三方库和参考项目

* [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

## 随感

项目的发展，学习心得，一些随感，记录于 [Thoughts.md](doc/Thoughts.md)，随时更新。

## 贡献指南

欢迎贡献代码，指引文档暂缺

## AI指引

[Repository AI Guidelines](AGENTS.md)
