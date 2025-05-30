# gkNextRenders

[English](README.en.md)\|[Simplified Chinese](README.md)

![windows ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/windows.yml/badge.svg)![linux ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/linux.yml/badge.svg)![macOS ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/macos.yml/badge.svg)![android ci](https://github.com/gameknife/gkNextRenderer/actions/workflows/android.yml/badge.svg)

## What kind of project is this?

### One sentence introduction

A real-time path tracing renderer based on Vulkan with the goal of "**quality**"and"**efficiency**"Can be used for"**real time gaming**"Path tracing rendering.

### Technical features

Different from various ray tracing auxiliary technologies currently implemented in games, the goal of this project is the path tracking closest to GroundTruth.
Different from other GPU PathTracer implementations, the goal of this project is real-time, Benchmark uses full dynamic scenes,
It also provides a runtime environment (lightweight game engine) closest to the game, verifies the feasibility of real-time ray tracing, and experiments with the latest GPU features to prepare for the next generation of rendering architecture.

### Development premise

The original intention of this project is: learning, verification, and progress. Therefore, we will radically use the latest technology, intentionally avoid old technologies, use new C++ specifications and standard libraries, and develop cross-platform full-time.

### Subproject

-   **gkNextRenders**: Main project, path tracing renderer
-   **gkNextEditor**: Editor framework based on imgui, used for editing scenarios, completely dependent on glb reading and writing
-   **MagicaLego**: A Lego building game similar to MagicaVoxel, with full real-time path tracing rendering to verify goals
-   **gkNextBenchmark**: Dedicated Benchmark program for benchmarking static and real-time scenes
-   **Packager**: Package assets into pkg files for rapid deployment
-   **Portal**: Sub-project combination caller, providing various visual deployment and debugging tools (planned)

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

## Technical characteristics

-   Rendering
    -   Importance Sampling (BRDF / Light)
    -   VNDF Sampling for GGX, by[tigrazone](https://github.com/tigrazone)
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
    -   Aux Rendering
    -   HDR Display Support
    -   Screenshot HDR and encode to avif / jpg

-   Scene Management
    -   Full GLTF Scene File Support ( Mesh / Texture / Material / Animation)

-   Editor
    -   Full Imgui Pipeline
    -   Node-based Material Editor

## run

1.  Download the latest MagicaLego game version and launch it through bin/MagicaLego.exe
2.  Download the latest Release version and start it directly through bin/\*.exe
3.  Build and run from scratch

## How to build

First, you need to install[Vulkan SDK](https://vulkan.lunarg.com/sdk/home). Each platform completes the installation according to lunarG's instructions. Other dependencies are based on[Microsoft's vcpkg](https://github.com/Microsoft/vcpkg)Build and execute subsequent scripts to complete the compilation.

project[Github Action](.github/workflows)Contains automatic ci scripts for windows, linux, android, and the author will maintain their correctness. If you have any environmental problems, please refer to the solution.

After the local development environment is deployed, each platform can click the script to build

**Windows (Visual Studio 2022)**

    vcpkg_windows.bat
    build_windows.bat

**Windows (MinGW)**

init the MSYS2 MINGW64 shell with following packages，the MSYS2's cmake is not competible with vcpkg, so use cmake inside.

    pacman -S --needed git base-devel mingw-w64-x86_64-toolchain ninja

cmake's module FindVulkan has a little bug on MingGW, try modified FindVulkan.cmake as below
set(\_Vulkan_library_name vulkan) -> set(\_Vulkan_library_name vulkan-1)
then, execute scripts bellow

    vcpkg_mingw.sh
    build_mings.sh

**Android On Windows**

JAVA SDK should be JAVA17 or higher
Install Android Studio or Android SDK Tool, with NDK 25.1.8937393 installed

    set ANDROID_NDK_HOME=\path\to\ndk
    #like: set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\25.1.8937393
    vcpkg_android.bat
    build_android.bat
    deploy_android.bat

**Linux**

Each platform needs to install the corresponding dependencies in advance so that vcpkg can run correctly.

For example, ubuntu

    sudo apt-get install ninja-dev curl unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev
    ./vcpkg_linux.sh
    ./build_linux.sh

SteamDeck Archlinux

    sudo steamos-readonly disable
    sudo pacman -S devel-base ninja
    ./vcpkg_linux.sh
    ./build_linux.sh

**MacOS**

    brew install molten-vk
    brew install glslang
    brew install ninja
    ./vcpkg_macos.sh
    ./build_macos.sh

## Next Todolist

-   [ ] GPU Frustum / Occlusion Culling
-   [ ] GPU Lod Swtiching
-   [ ] Huge Landscape

## Reference project

-   [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
-   [Volcano tutorial](https://vulkan-tutorial.com/)
-   [Volcano Samples](https://github.com/KhronosGroup/Vulkan-Samples)

## Random thoughts

The development of the project, learning experience, and some random thoughts are recorded in[Thoughts.md](doc/Thoughts.md), updated at any time.
