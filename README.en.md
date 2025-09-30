# gknex trends

[English](README.en.md)\|[Simplified Chinese](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextRenderer)

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)

## What project is this?

### A brief introduction to one sentence

A Vulkan-based real-time path tracking renderer with the goal of implementing "**quality**"and"**efficiency**"Can be used"**Real-time game**"Path tracking rendering.

### Technical Features

Unlike the various ray tracing assistive technologies currently implemented in the game, the goal of this project is to track the path closest to GroundTruth.
Unlike other GPU PathTracer implementations, the goal of this project is to use full dynamic scenarios in real time.
It also provides the runtime environment closest to the game (lightweight game engine), verify the feasibility of real-time ray tracing, and experiments with the latest GPU features to prepare for the next generation of rendering architectures.

### Development premise

The original intention of this project is: learning, verification, and progress. Therefore, we will radically use the latest technologies, deliberately avoid outdated technologies, and use new C++ specifications and standard libraries to develop across the board.

## Contribution Guide

Planning to contribute? Read the repository onboarding notes in [Repository Guidelines](AGENTS.md).

### Subproject

-   **gknex trends**: Main project, path tracking renderer
-   **gkNextEditor**: Imgui-based editor framework for editing scenes, relying entirely on glb's reading and writing
-   **MagicaLego**: A LEGO built game similar to MagicaVoxel, with full real-time path tracking and rendering to verify the target
-   **gkNextBenchmark**: Dedicated Benchmark program for Benchmark for static and real-time scenarios
-   **Packager**: Package assets into pkg files for rapid deployment
-   **Portal**: Sub-project portfolio calls the program, providing various visual deployments and debugging tools (planned)

## Gallery (TrueHDR)

<https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0>

> _MegicaLego Games_

<https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7>

> _10 seconds Showcase Video_

![Alt text](gallery/Qx50.avif?raw=true "Qx50")

> _RayTracing Renderer - QX50_

![Alt text](gallery/city.glb.avif?raw=true "City")

> _RayTracing Renderer - City_

![Alt text](gallery/Still.avif?raw=true "Still")

> _RayTracing Renderer - Still_

![Alt text](gallery/playground.glb.avif?raw=true "PlayGround")

> _RayTracing Renderer - PlayGround_

![Alt text](gallery/LuxBall.avif?raw=true "LuxBall")

> _RayTracing Renderer - LuxBall_

![Alt text](gallery/Kitchen.avif?raw=true "Kitchen")

> _RayTracing Renderer - Kitchen_

![Alt text](gallery/LivingRoom.avif?raw=true "Living Room")

> _RayTracing Renderer - Living Room_

![Alt text](gallery/Qx50_Android.avif?raw=true "Qx50Android")

> _Hybrid Renderer (Android) - QX50_

![Alt text](gallery/Complex_Android.avif?raw=true "ComplexAndroid")

> _Hybrid Renderer (Android) - Complex Cubes_

## Technical Features

-   Rendering
    -   Importance Sampling (BRDF / Light)
    -   VNDF Sampling for GGX, by[Tigrazone](https://github.com/tigrazone)
    -   Ground Truth Path Tracing
    -   Temporal Reproject with MultiSample catchup
    -   High Performance Bilateral Filter Denoiser
    -   OpenImageDenoise Denoiser\* (need sdk)
    -   Visibiliy Buffer Rendering
    -   Reference Legacy Deferred Rendering
    -   RayTraced Hybrid Rendering
    -   Realtime Renderer Switch
    -   GPU Draw
    -   GPU Raycast

-   Engine
    -   Multi-Platform support ( Windows / Linux / Mac / Android )
    -   Global Bindless TexturePool
    -   MultiThread Task Dispatcher ( Async Resource Loading and etc )
    -   Full-Scope File Package System
    -   Gpu scene updating
    -   To the rendering
    -   HDR Display Support
    -   Screenshot HDR and encode to avif / jpg

-   Scene Management
    -   Full GLTF Scene File Support ( Mesh / Texture / Material / Animation)

-   Editor
    -   Full Imgui Pipeline
    -   Node-based Material Editor

## run

1.  Download the latest MagicaLego game version and start it through bin/MagicaLego.exe
2.  Download the latest Release version and start it directly through bin/\*.exe
3.  Build and run from scratch

## How to build

First, installation is required[Vulkan SDK](https://vulkan.lunarg.com/sdk/home). Each platform will complete the installation according to the guidance of lunarG. Other dependencies are based on[Microsoft's vcpkg](https://github.com/Microsoft/vcpkg)Build, execute subsequent scripts to complete the compilation.

Project[Github Action](.github/workflows)Automatic ci scripts containing windows, linux, android, and the author will maintain its correctness. If you have any environmental problems, please refer to the solution.

After the local development environment is deployed, each platform can be built by clicking the script

**Windows (Visual Studio 2022)**

    vcpkg.bat windows
    build.bat windows

**Windows (MinGW)**

init the MSYS2 MINGW64 shell with following packages，the MSYS2's cmake is not competible with vcpkg, so use cmake inside.

    pacman -S --needed git base-devel mingw-w64-x86_64-toolchain ninja

cmake's module FindVulkan has a little bug on MingGW, try modified FindVulkan.cmake as below
set(\_Vulkan_library_name vulkan) -> set(\_Vulkan_library_name vulkan-1)
then, execute scripts bellow

    vcpkg.sh mingw
    build.sh mingw

**Android On Windows**

JAVA SDK should be JAVA17 or higher
Install Android Studio or Android SDK Tool, with NDK 25.1.8937393 installed

    set ANDROID_NDK_HOME=\path\to\ndk
    #like: set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\25.1.8937393
    vcpkg.bat android
    build.bat android
    deploy_android.bat

**Linux**

Each platform needs to install the corresponding dependencies in advance before vcpkg can run correctly.

For example, ubuntu

    sudo apt-get install ninja-dev curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
    ./vcpkg.sh linux
    ./build.sh linux

SteamDeck Archlinux

    sudo steamos-readonly disable
    sudo pacman -S devel-base ninja
    ./vcpkg.sh linux
    ./build.sh linux

**MacOS**

    brew install molten-vk
    brew install glslang
    brew install ninja
    ./vcpkg.sh macos
    ./build.sh macos

## Next Todolist

-   [ ] Luc frustum / managel colling
-   [ ] GPU Lod Swtiching
-   [ ] Huge Landscape

## Reference project

-   [Raytracingin Dove](https://github.com/GPSnoopy/RayTracingInVulkan)
-   [Vulkan tutorial](https://vulkan-tutorial.com/)
-   [Volcano-samples](https://github.com/KhronosGroup/Vulkan-Samples)

## Feelings

The development of the project, learning experience, and some casual thoughts recorded in[Thoughts.md](doc/Thoughts.md), update at any time.
