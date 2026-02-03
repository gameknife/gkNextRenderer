# gkNextEngine

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

## gkNextEngine是什么？

一个跨平台 3D 游戏引擎，基于现代 C++ 与 Vulkan。聚焦“可学习、可验证”的现代渲染与游戏技术实践。

## 亮点

- **多平台：** Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64
- **现代渲染：** 硬件/软件光追，实时全局光照，Visibility Buffer，GPU‑Driven，全 Bindless
- **时域技术：** 时域重投影、双边滤波去噪，OpenImageDenoise
- **资产与场景：** 完整 glTF（网格/纹理/材质/动画），以 Blender 为主的前置流程
- **引擎能力：** 多线程任务调度、打包文件系统、HDR 显示与截图（AVIF/JPG）
- **编辑器：** 全ImGui管线，节点式材质编辑器
- **编码哲学：** 核心目标 < 50k 行（当前 ~15k，2025/09）；拥抱成熟三方库，拒绝重复造轮子

## 子项目

- `gkNextRenderer`：主渲染器（路径追踪/混合渲染）
- `gkNextEditor`：ImGui 编辑器，基于 GLB 读写的场景编辑
- `MagicaLego`：Voxel/乐高风格原型，实时路径追踪验证场景
- `gkNextBenchmark`：静态与实时场景基准程序
- `Packager`：资产打包为 `.pkg`，便于分发与加载
- `Portal`：（规划）可视化部署与调试入口

## 图库 / 视频

https://github.com/user-attachments/assets/2d1c61ab-8daa-4f1f-ad14-1f211fca19b0 

> MagicaLego 片段

https://github.com/user-attachments/assets/636c5b3f-f5c8-4233-9268-7b6e8c0606e7

> 10 秒展示视频

- 更多截图见 `gallery/` 目录（含 Android 混合渲染）

## 快速开始

项目使用 CMake + Ninja / VS2022 构建，三方依赖由 vcpkg 管理。建议部署良好网络环境(可完整访问github)，并已安装 Git。

Windows（Visual Studio 2022）：
``` bat
rem windows前置安装: 
rem 确保安装3.31版本的cmake，不要安装4.x版本的cmake
rem 确保安装Visual Studio 2022并选择c++工作负载
rem 确保安装VulkanSDK 1.4.313.2
rem 由于vcpkg部分tar包解压有乱码，请确保打开windows语言设置中的[使用Unicode UTF-8提供全球语言支持]
vcpkg.bat windows
.\build.bat windows-dev
.\run.bat
```

Windows（MSYS2 MinGW）：
``` shell
# 无需前置安装
pacman -S --needed git mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain
./vcpkg.sh
./build.sh --preset default-mingw
./run.sh --preset default-mingw
```

Linux（示例：Ubuntu）：
``` shell
# 无需前置安装
sudo apt install build-essential cmake ninja-build curl zip unzip tar libxi-dev libxinerama-dev libxcursor-dev xorg-dev autoconf autoconf-archive automake libtool
./vcpkg.sh
./build.sh --preset default-linux
./run.sh --preset default-linux
```

macOS：
``` shell
# 无需前置安装
brew install molten-vk glslang ninja
./vcpkg.sh
./build.sh --preset default-macos-arm64
./run.sh --preset default-macos-arm64
```

Android（在 Windows）：
``` bat
rem 需 JDK 17+，安装 Android SDK 与 NDK r27（示例：27.0.12077973）
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
vcpkg.bat
build.bat --android
run.bat --preset android
```

## 技术要点（概览）

- 重要性采样（BRDF/Light），GGX VNDF
- 时域重投影（含多样本复用），高速双边滤波去噪，OpenImageDenoise
- Visibility Buffer（主命中），路径追踪/软追踪/探针 GI（后续命中），渲染器热切换/对照渲染
- 全流程 GPU‑Driven + Bindless 管线

## 参考与感谢

- RayTracingInVulkan — https://github.com/GPSnoopy/RayTracingInVulkan
- Vulkan Tutorial — https://vulkan-tutorial.com/
- Vulkan‑Samples — https://github.com/KhronosGroup/Vulkan-Samples

## 参与贡献

- 欢迎 Issue / PR
- 思考与记录见 `doc/Thoughts.md`
- 开发协作与命名规范：见 `AGENTS.md`

## 第三方依赖

[cpptrace](https://github.com/jeremy-rifkin/cpptrace), [cxxopts](https://github.com/jarro2783/cxxopts), [sdl3](https://github.com/libsdl-org/SDL), [glm](https://github.com/g-truc/glm), [imgui](https://github.com/ocornut/imgui), [stb](https://github.com/nothings/stb), [curl](https://github.com/curl/curl), [nlohmann-json](https://github.com/nlohmann/json), [tinygltf](https://github.com/syoyo/tinygltf), [draco](https://github.com/google/draco), [fmt](https://github.com/fmtlib/fmt), [meshoptimizer](https://github.com/zeux/meshoptimizer), [ktx](https://github.com/KhronosGroup/KTX-Software), [joltphysics](https://github.com/jrouwe/JoltPhysics), [xxhash](https://github.com/Cyan4973/xxHash), [spdlog](https://github.com/gabime/spdlog), [cpp-base64](https://github.com/ReneNyffenegger/cpp-base64), [catch2](https://github.com/catchorg/Catch2), [entt](https://github.com/skypjack/entt), [libwebp](https://github.com/webmproject/libwebp), [vulkan-loader](https://github.com/KhronosGroup/Vulkan-Loader), [libavif](https://github.com/AOMediaCodec/libavif)
