# gkNextEngine

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

## What is gkNextEngine?

A cross‑platform 3D game engine built with modern C++ and Vulkan. It focuses on learnable, verifiable implementations of modern rendering and game techniques.

## Highlights

- **Multi‑platform:** Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64
- **Modern rendering:** hardware/software ray tracing, real‑time global illumination, Visibility Buffer, GPU‑Driven, fully bindless
- **Temporal techniques:** temporal reprojection, bilateral denoising, OpenImageDenoise
- **Assets & scenes:** full glTF support (meshes/textures/materials/animation), Blender‑first workflow
- **Engine capabilities:** multithreaded tasking, packaged filesystem, HDR display and screenshots (AVIF/JPG)
- **Editor:** full ImGui pipeline with a node‑based material editor
- **Code philosophy:** core target < 50k LOC (current ~15k, 2025/09); prefer mature third‑party libraries over reinventing wheels

## Subprojects

- `gkNextRenderer`: main renderer (path tracing / hybrid)
- `gkNextEditor`: ImGui editor with GLB‑based scene IO
- `MagicaLego`: voxel/lego‑style prototype with real‑time path tracing validation scenes
- `gkNextBenchmark`: benchmarks for static and real‑time scenes
- `Packager`: packs assets into `.pkg` for distribution and loading
- `Portal`: (planned) consolidated launcher for visualization and debugging

## Gallery / Video

https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0

> MagicaLego clip

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

> 10‑second showcase video

- More screenshots in the `gallery/` directory (including Android hybrid rendering)

## Quick Start

The project uses CMake + Ninja, with dependencies managed by vcpkg. Ensure a reliable network (full GitHub access) and Git installed.

Windows (Visual Studio 2022):
``` bat
rem Windows prerequisites:
rem Install CMake 3.31 (do not install CMake 4.x)
rem Install Visual Studio 2022 with the C++ workload
rem Install Vulkan SDK 1.4.313.2
rem Enable "Use Unicode UTF-8 for worldwide language support" to avoid vcpkg tar extraction issues
vcpkg.bat windows
.\build.bat windows-dev
.\run.bat
```

Windows (MSYS2 MinGW):
``` shell
pacman -S --needed git mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain
./vcpkg.sh
./build.sh --preset default-mingw
./run.sh --preset default-mingw
```

Linux (example: Ubuntu):
``` shell
sudo apt install build-essential cmake ninja-build curl zip unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev autoconf autoconf-archive automake libtool
./vcpkg.sh
./build.sh --preset default-linux
./run.sh --preset default-linux
```

macOS:
``` shell
brew install molten-vk glslang ninja
./vcpkg.sh
./build.sh --preset default-macos-arm64
./run.sh --preset default-macos-arm64
```

Android (on Windows):
``` bat
rem Requires JDK 17+, Android SDK, and NDK r27 (e.g., 27.0.12077973)
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
vcpkg.bat
build.bat --android
run.bat --preset android
```

## Technical Highlights (Overview)

- Importance sampling (BRDF/Light), GGX VNDF
- Temporal reprojection (with multi‑sample reuse), fast bilateral denoising, OpenImageDenoise
- Visibility Buffer for primary hits; path tracing / software tracing / probe GI for secondary hits; hot‑swappable and reference/side‑by‑side renderers
- Fully GPU‑Driven pipeline with bindless resources

## References & Thanks

- RayTracingInVulkan — https://github.com/GPSnoopy/RayTracingInVulkan
- Vulkan Tutorial — https://vulkan-tutorial.com/
- Vulkan‑Samples — https://github.com/KhronosGroup/Vulkan-Samples

## Contributing

- Issues and PRs are welcome
- Notes and thoughts: `doc/Thoughts.md`
- Collaboration and naming conventions: `AGENTS.md`

## Third-Party Dependencies

[cpptrace](https://github.com/jeremy-rifkin/cpptrace), [cxxopts](https://github.com/jarro2783/cxxopts), [sdl3](https://github.com/libsdl-org/SDL), [glm](https://github.com/g-truc/glm), [imgui](https://github.com/ocornut/imgui), [stb](https://github.com/nothings/stb), [curl](https://github.com/curl/curl), [nlohmann-json](https://github.com/nlohmann/json), [tinygltf](https://github.com/syoyo/tinygltf), [draco](https://github.com/google/draco), [fmt](https://github.com/fmtlib/fmt), [meshoptimizer](https://github.com/zeux/meshoptimizer), [ktx](https://github.com/KhronosGroup/KTX-Software), [joltphysics](https://github.com/jrouwe/JoltPhysics), [xxhash](https://github.com/Cyan4973/xxHash), [spdlog](https://github.com/gabime/spdlog), [cpp-base64](https://github.com/ReneNyffenegger/cpp-base64), [catch2](https://github.com/catchorg/Catch2), [entt](https://github.com/skypjack/entt), [libwebp](https://github.com/webmproject/libwebp), [vulkan-loader](https://github.com/KhronosGroup/Vulkan-Loader), [libavif](https://github.com/AOMediaCodec/libavif)
