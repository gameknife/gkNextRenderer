# gkNextEngine

**实时路径追踪，游戏级性能**

[English](README.en.md) | [简体中文](README.md)

![Kitchen Scene](gallery/Kitchen.avif)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

---

## 性能数据

> 测试场景：`city.glb`

| 平台 | 硬件 | 分辨率 | 渲染模式 | 帧率 |
|------|------|--------|----------|------|
| Windows | RTX 5070ti | 1080p | PathTracing (4spp + temporal) | **120 fps** |
| Windows | RTX 5070ti | 1080p | PathTracing + OIDN | **~80 fps** |
| Android | Snapdragon 8 Gen2 | 720p | Hardware RT Hybrid | **50 fps** |

---

## 是什么？

gkNextEngine 是一个跨平台 3D 游戏引擎，基于现代 C++ 与 Vulkan，具备现代渲染特性。

**三个核心定位：**

- **真实感渲染的实时可用性** — 路径追踪不再是离线专属，Visibility Buffer + GPU-Driven + 全 Bindless 架构让它跑在游戏帧率
- **完整的开发工具链** — ImGui 编辑器、节点式材质、QuickJS 脚本热重载、游戏示例，加上 CMake Preset + vcpkg 的现代构建流程
- **可学习的代码库** — 核心目标 < 50k LOC（当前 ~15k），拒绝过度设计，拥抱成熟三方库

**支持平台：** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## 核心技术

### 渲染技术
- **实时路径追踪** — 1spp + 时域复用，支持 JBF/OIDN/DLSS RR 降噪
- **Hybrid Rendering** — 混合光栅化与光追
- **渲染器热切换** — 运行时切换对比

### GPU 架构
- **Visibility Buffer** — 延迟材质求值
- **全 Bindless + GPU-Driven** — 减少 CPU 开销
- **Multi-Draw Indirect** — 合并绘制调用

### 引擎能力
- **ECS 架构** — entt + 反射系统
- **ImGui 编辑器** — 节点式材质编辑
- **QuickJS 脚本** — 支持热重载
- **Jolt Physics** — 物理模拟

---

## 图库 / 视频

https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0

> MagicaLego 片段

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

> 10 秒展示视频

<details>
<summary><b>更多截图</b></summary>

| 场景 | 截图 |
|------|------|
| LuxBall | ![LuxBall](gallery/LuxBall.avif) |
| Living Room | ![LivingRoom](gallery/LivingRoom.avif) |
| Qx50 | ![Qx50](gallery/Qx50.avif) |
| Cornell Box | ![CornellBox](gallery/CornellBox.avif) |
| Android Hybrid | ![Android](gallery/Qx50_Android.avif) |

</details>

---

## 快速开始

项目使用 CMake + Ninja，依赖由 vcpkg 管理。需要可访问 GitHub 的网络环境。

<details>
<summary><b>Windows (Visual Studio 2022)</b></summary>

**前置条件：**
- CMake 3.26+
- Visual Studio 2022（C++ 工作负载）
- Vulkan SDK 1.4.313.2
- 启用「使用 Unicode UTF-8 提供全球语言支持」

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
<summary><b>Android (Windows 构建)</b></summary>

**前置条件：** JDK 17+、Android SDK、NDK r27

```bat
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
vcpkg.bat
build.bat --android
run.bat --preset android
```

</details>

---

## 子项目

| 项目 | 说明 |
|------|------|
| `gkNextRenderer` | 主渲染器（路径追踪 / 混合渲染） |
| `gkNextEditor` | ImGui 编辑器，GLB 场景读写 |
| `MagicaLego` | Voxel/乐高风格原型，路径追踪验证场景 |
| `gkNextBenchmark` | 静态与实时场景基准测试 |
| `Packager` | 资产打包为 `.pkg` |

---

## 参考与感谢

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

## 参与贡献

欢迎 Issue / PR · 开发协作见 `AGENTS.md` · 思考记录见 `doc/Thoughts.md`

## 第三方依赖

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif
