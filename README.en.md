# gkNextEngine

**Real-Time Path Tracing at Game-Ready Performance**

[English](README.en.md) | [简体中文](README.md)

![Kitchen Scene](gallery/Kitchen.avif)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

---

## Performance

> Test scene: `city.glb`

| Platform | Hardware | Resolution | Render Mode | FPS |
|----------|----------|------------|-------------|-----|
| Windows | RTX 5070ti | 1080p | PathTracing (4spp + temporal) | **120 fps** |
| Windows | RTX 5070ti | 1080p | PathTracing + OIDN | **~80 fps** |
| Android | Snapdragon 8 Gen2 | 720p | Hardware RT Hybrid | **50 fps** |

---

## What is it?

gkNextEngine is a cross-platform 3D game engine built with modern C++ and Vulkan, featuring modern rendering techniques.

**Three Core Principles:**

- **Real-Time Photorealism** — Path tracing is no longer offline-only; Visibility Buffer + GPU-Driven + fully Bindless architecture delivers it at game framerates
- **Complete Dev Toolchain** — ImGui editor, node-based materials, QuickJS scripting with hot reload, game samples, plus modern CMake Preset + vcpkg build workflow
- **Learnable Codebase** — Target < 50k LOC (currently ~15k), no over-engineering, embrace mature libraries

**Platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## Core Technology

### Rendering
- **Real-Time Path Tracing** — 1spp + temporal reuse, JBF/OIDN/DLSS RR denoising
- **Hybrid Rendering** — Combines rasterization and ray tracing
- **Hot-Swappable Renderers** — Switch and compare at runtime

### GPU Architecture
- **Visibility Buffer** — Deferred material evaluation
- **Fully Bindless + GPU-Driven** — Reduced CPU overhead
- **Multi-Draw Indirect** — Batched draw calls

### Engine Features
- **ECS Architecture** — entt + reflection system
- **ImGui Editor** — Node-based material editing
- **QuickJS Scripting** — Hot reload support
- **Jolt Physics** — Physics simulation

---

## Gallery / Video

https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0

> MagicaLego clip

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

> 10-second showcase

<details>
<summary><b>More Screenshots</b></summary>

| Scene | Screenshot |
|-------|------------|
| LuxBall | ![LuxBall](gallery/LuxBall.avif) |
| Living Room | ![LivingRoom](gallery/LivingRoom.avif) |
| Qx50 | ![Qx50](gallery/Qx50.avif) |
| Cornell Box | ![CornellBox](gallery/CornellBox.avif) |
| Android Hybrid | ![Android](gallery/Qx50_Android.avif) |

</details>

---

## Quick Start

The project uses CMake + Ninja with vcpkg for dependencies. Requires GitHub access.

<details>
<summary><b>Windows (Visual Studio 2022)</b></summary>

**Prerequisites:**
- CMake 3.26+
- Visual Studio 2022 with C++ workload
- Vulkan SDK 1.4.313.2
- Enable "Use Unicode UTF-8 for worldwide language support"

```bat
vcpkg.bat windows
.\build.bat windows-dev
.\run.bat
```

</details>

<details>
<summary><b>Windows (MSYS2 MinGW)</b></summary>

```shell
pacman -S --needed git mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain
./vcpkg.sh
./build.sh --preset default-mingw
./run.sh --preset default-mingw
```

</details>

<details>
<summary><b>Linux (Ubuntu)</b></summary>

```shell
sudo apt install build-essential cmake ninja-build curl zip unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev autoconf autoconf-archive automake libtool
./vcpkg.sh
./build.sh --preset default-linux
./run.sh --preset default-linux
```

</details>

<details>
<summary><b>macOS</b></summary>

```shell
brew install molten-vk glslang ninja
./vcpkg.sh
./build.sh --preset default-macos-arm64
./run.sh --preset default-macos-arm64
```

</details>

<details>
<summary><b>Android (Build on Windows)</b></summary>

**Prerequisites:** JDK 17+, Android SDK, NDK r27

```bat
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
vcpkg.bat
build.bat --android
run.bat --preset android
```

</details>

---

## Subprojects

| Project | Description |
|---------|-------------|
| `gkNextRenderer` | Main renderer (path tracing / hybrid) |
| `gkNextEditor` | ImGui editor with GLB scene I/O |
| `MagicaLego` | Voxel/lego-style prototype, path tracing validation |
| `gkNextBenchmark` | Static and real-time scene benchmarks |
| `Packager` | Pack assets into `.pkg` |

---

## References & Thanks

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

## Contributing

Issues and PRs welcome · See `AGENTS.md` for guidelines · Notes in `doc/Thoughts.md`

## Third-Party Dependencies

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif
