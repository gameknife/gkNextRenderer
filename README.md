# gkNextEngine

**面向实时路径追踪、游戏原型与高质量视觉表现的跨平台 3D 引擎**

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows_self.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux_self.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos_self.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android_self.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

![Play ground](docs/gallery/4_playground.avif)

---

gkNextEngine 是一个基于现代 C++20 与 Vulkan 的跨平台 3D 游戏引擎 / 渲染实验场，核心目标始终是两件事：

- 用 **实时路径追踪、Hybrid Rendering 与 HDR 光照** 做出真正有展示力、且能稳定跑在运行时里的画面
- 用 **可运行、可扩展、可用于玩法原型验证的引擎能力** 支撑长期演进，而不是停留在单点 demo

项目以渲染器能力为核心，同时持续扩展编辑器、脚本、物理、内容导入与玩法原型。**近期的重心明显落在渲染效率与画质上**：世界辐射缓存（SHARC）、AmbientCube 显存与命中驱动残留、间接光 RGB9E5 编码、GPU-Driven 剔除的原子竞争治理、降噪与重投影稳定性等一系列改进，让同样的画面用更少的显存和 GPU 时间跑出来，细节见下文 [性能与渲染效率](#性能与渲染效率) 一节。

如果你关注以下方向，这个项目会比较值得参考：

- 想看实时路径追踪、金属 / 玻璃 / 塑料材质、HDR 环境光和高密度场景的实际画面
- 想研究一套**真正以运行时性能为约束**的 Vulkan 渲染架构，而不是只会离线出图的 demo
- 想看一个引擎如何把 **渲染、编辑器、脚本、物理、内容导入与玩法原型** 串成完整系统
- 想读一套规模可控、强调工程清晰度、适合学习现代 Vulkan 渲染与引擎实现的代码库

**支持平台：** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## 项目特性

- **实时路径追踪与 Hybrid Rendering**
  围绕 1spp + temporal reuse、降噪、重投影和多管线切换持续推进，让路径追踪不只停留在离线效果演示，而是面向真实运行时表现。

- **以性能为约束的渲染管线**
  世界辐射缓存复用、稀疏显存布局、GPU-Driven 海量提交、按需驻留与多档上采，目标是在固定 GPU 预算下尽量多出画面、少占显存，而不是为单帧画质无限堆资源。

- **游戏级取向的 GPU 架构**
  通过 Visibility Buffer、全 Bindless、GPU-Driven 单 draw 提交等设计，尽量把 CPU 开销留给内容与玩法，把 GPU 算力用在真正影响画面的地方。

- **引擎能力服务于内容与玩法原型**
  包括 ECS、反射、编辑器、脚本热重载、物理同步、运行时导入和稳定的渲染行为。这些能力共同支撑更完整的可玩内容系统。

- **多格式内容导入与互操作**
  完整支持 glTF 运行时导入与部分导出；同时可直接导入 `.ldr` / `.mpd`、OpenSCAD `.scad` DSL 与 PlayCanvas `.sog` 高斯溅射资产，把结构化场景纳入统一的 Runtime、渲染与交互系统。

---

## 性能与渲染效率

性能是当前的核心约束之一。引擎围绕世界辐射缓存复用、稀疏显存布局、GPU-Driven 海量提交、按需驻留与多档上采等手段，在固定 GPU 预算下尽量多出画面、少占显存。下面给出一组**典型场景的运行时性能参考**，并配套内置的逐 pass profiler 与 Superluminal 集成做剖析。

### 性能参考数据

> ⚠️ 下表为占位模板，数值待在统一硬件 / 驱动下实测后人工填入，目前仅列出场景与管线组合。

| 场景 | 分辨率 | 渲染管线 | GPU | 帧时间 (ms) | FPS | 显存 |
|------|--------|----------|-----|------------|-----|------|
| playground | 1920×1080 | PathTracing (4spp + temporal) | _待测_ | _待测_ | _待测_ | _待测_ |
| living room | 1920×1080 | PathTracing (4spp + temporal) | _待测_ | _待测_ | _待测_ | _待测_ |
| lego (LDraw) | 1920×1080 | SoftwareModernNoAmbient | _待测_ | _待测_ | _待测_ | _待测_ |
| luxball | 1920×1080 | SoftwareModernNoAmbient | _待测_ | _待测_ | _待测_ | _待测_ |
| brickplayer | 1920×1080 | SoftwareModernNoAmbient | _待测_ | _待测_ | _待测_ | _待测_ |

> 以上数据可用 `gkNextMotionBenchmark` 在统一硬件 / 驱动下复现；可选模型先执行 `./gnb paks fetch`。启动时只传一个编排 JSON：
>
> ```bash
> ./gnb run gkNextMotionBenchmark --benchmark-config assets/configs/motion_benchmark.example.json
> ```
>
> CSV 会输出 `frame_time_ms`、`gpu_time_ms`、`fps`、`vram_mib`、GPU / driver、实际 renderer、分辨率，以及 GPU cull 后的 `draw_calls_actual / draw_calls_total`、`tris_actual / tris_total`，可直接填入上表。

### 内置 Profiler

引擎内置一套 CPU / GPU 逐 pass 计时系统：每个渲染 pass 由命名 scope 标注，`VulkanGpuTimer` 采集各 pass 的 GPU 端耗时，运行时以 ImGui 叠加层（`ProfileDebugOverlay`）实时显示逐 pass 帧时间与统计。无需外部工具即可定位渲染热点、对比不同管线与设置的开销。

### Superluminal 集成

Windows 上若安装了 [Superluminal](https://superluminal.eu/) Performance API（默认探测 `C:/Program Files/Superluminal/Performance/API`），构建会自动启用 `WITH_SUPERLUMINAL`，把引擎的 CPU 与 GPU 命名事件投递到 Superluminal 时间线（GPU 事件经独立回放线程标注），便于做细粒度采样剖析与跨帧分析。未安装时自动跳过，不影响构建。

---

## 核心能力

### 1. 面向运行时的高质量渲染

- **实时路径追踪**：围绕 1spp + temporal reuse 持续推进，关注真实运行时条件下的画面质量与可用性能
- **世界辐射缓存 GI**：SHARC 世界空间辐射缓存作为间接光复用层，跨帧跨探针复用，默认开启
- **Hybrid Rendering**：在移动平台与游戏级工况下，把传统光栅与光追做合理混合
- **多套渲染器热切换**：同一套资产与场景，可直接切换路径追踪 / 软追踪 / Modern 等管线做对比和验证
- **HDR 截图与高质量素材导出**：便于做视觉验证、展示与回归对比

### 2. 完整的运行时与工具链能力

- **ECS + Reflection**：基于 entt 的组件系统，加上反射层，服务于运行时、编辑器和脚本绑定
- **ImGui 编辑器**：`gkNextEditor` 面向材质、场景和运行时内容的编辑工作流，支持渐进式渲染迭代与数据驱动的设置 / cvar 面板
- **QuickJS 脚本热重载**：运行时使用仓库内置 `tools/tsc/tsc[.exe]` 编译 TypeScript（Windows 为 `tsc.exe`，macOS/Linux 为 `tsc`），无需 Node/npm 或全局 `tsc`；整合链路见 [docs/guides/typescript-integration.md](docs/guides/typescript-integration.md)
- **Jolt Physics**：为交互原型、拖拽玩法和游戏化验证提供更真实的物理基础
- **TUI 终端渲染模式**：`gnb tui` 可把最终画面以 truecolor 半块字符刷到终端，便于无窗口环境下快速预览，见 [docs/guides/tui-mode.md](docs/guides/tui-mode.md)

### 3. 代码规模可控，适合学习和扩展

- **第一方引擎代码目标 < 50k LOC**：引擎核心刻意保持在便于理解和持续演进的区间（连同全部示例 game + 测试约 85k LOC，可用 `gnb loc` 查看分类统计）
- **优先清晰实现而非过度设计**：尽量用明确的数据流、职责边界和成熟三方库解决问题
- **适合阅读现代引擎实现**：从 Vulkan 渲染、资源管理到脚本、编辑器、反射与测试链路，都能看到较完整的工程组织方式

### 4. glTF、LDraw、OpenSCAD 与高斯溅射的内容导入能力

- **glTF 完整导入**：面向运行时支持 glTF 场景、材质、动画、骨骼蒙皮等完整内容导入
- **glTF 部分导出**：支持将部分运行时内容回写到 glTF 工作流
- **LDraw 直接导入 Runtime**：`.ldr` / `.mpd` 可直接进入 Runtime，从 `LDConfig.ldr`、LGEO realistic color 到引擎 PBR 材质的完整颜色与材质映射，并把零件连接语义转换成搭建系统可理解的数据
- **OpenSCAD DSL 导入**：直接解析 / 求值 `.scad`，几何走 Manifold CSG、文本走 FreeType，把程序化建模脚本变成可渲染网格；`ScadStudio` 即基于此做建模与角色绑定实验
- **高斯溅射资产**：直接加载 PlayCanvas `.sog`（打包 ZIP 或 `meta.json` + `.webp`），与 mesh 场景共渲染

---

## 技术方向

### 渲染与 GPU 架构

- **Visibility Buffer**
- **全 Bindless + GPU-Driven**
- **Single-Draw GPU-Driven Submit（Soft Mesh Shader）**
- **Hardware / Software Ray Tracing（ray query）**
- **SHARC 世界辐射缓存 / 间接光 RGB9E5**
- **AmbientCube GI：稀疏显存 + 命中驱动残留**
- **降噪：ReLAX 风格方差引导 / à-trous / JBF**
- **Temporal Reprojection / Sky Occlusion（GTAO）**
- **Upscaler：FSR / Windows 上的 NVIDIA Streamline DLSS SR / RR / Frame Generation**
- **Gaussian Splatting（SOG v2 + 硬件 billboard）**

### 引擎与工具链

- **现代 CMake Presets + vcpkg**
- **跨平台运行时：桌面 / Android / iOS**
- **ImGui Editor + Node-based Material Workflow**
- **QuickJS Runtime Scripting**
- **TUI 终端渲染 / Visual Test / Benchmark / Packager**
- **`gnb` 项目 CLI（构建 / 运行 / 截图验证 / dashboard / 本地 LLM）**

### AI Native

- **内置 AI Agent 基础设施，可扩展运行时 LLM 能力**
- **使用 Codex / agentic coding 进行引擎基础设施与示例 Demo 的原生开发**
- **放弃 low-code 叙事，转向更直接的 agentic coding 工作流**

---

## 视觉预览

![BrickPlayer Gameplay](docs/gallery/6_debug_draw.avif)

<details>
<summary><b>示例截图</b></summary>

| 场景 | 截图 |
|------|------|
| still | ![still](docs/gallery/1_still.avif) |
| livingroom | ![livingroom](docs/gallery/2_living_room.avif) |
| ldrawlego | ![ldrawlego](docs/gallery/3_lego_ldraw.avif) |
| luxball | ![luxball](docs/gallery/5_luxball.avif) |
| brickplayer | ![brickplayer](docs/gallery/7_brick_player.avif) |

</details>

---

## 快速开始

项目使用 CMake + Ninja，依赖由 vcpkg 管理。除了宿主机本身必须具备的基础工具（编译器 / IDE、CMake、平台 SDK 等），项目级依赖、外部工具链和可选资源包现在都尽量交给 `gnb` 准备。构建依赖下载阶段需要可访问 GitHub 的网络环境。

### 通用说明

- 推荐先执行 `./gnb doctor`（Windows: `./gnb.bat doctor`）检查宿主机缺失的基础工具
- `./gnb setup`（Windows: `./gnb.bat setup`）会准备 vcpkg、项目外部工具链与可选资源包；如果直接执行 `./gnb build`，首次缺少 toolchain 时也会自动补齐核心依赖
- 桌面平台现在通过 `gnb` 统一构建和运行，通常不再需要先 `cd` 到 `out/build/<platform>/bin`
- 可用 CMake 预设收敛为：`windows`、`linux`、`macos-arm64`、`ios`

### 平台构建

<details>
<summary><b>Windows (Visual Studio 2022)</b></summary>

**前置条件：**

- CMake 3.26+
- Visual Studio 2022（C++ 工作负载）
- Vulkan SDK 1.4.341.1（默认由 `gnb` 自动下载到仓库内；若设置 `VULKAN_SDK` 则优先使用环境里的 SDK）
- 启用“使用 Unicode UTF-8 提供全球语言支持”

```bat
./gnb.bat setup
./gnb.bat build
./gnb.bat run gkNextRenderer
```

除 Visual Studio 这类宿主工具外，其余项目依赖通常都由 `gnb` 自动准备；默认会拉取项目约定版本的 Vulkan SDK、Slang 与 TypeScript 工具链到仓库内。Windows 默认启用 NVIDIA Streamline（DLSS）。

</details>

<details>
<summary><b>Linux (Ubuntu)</b></summary>

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

- 在 apt / pacman 环境下，`gnb setup` 与 Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前自动安装桌面构建所需系统包
- 如果自动安装不可用，再手动补齐：`sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev autoconf autoconf-archive automake libtool libsystemd-dev`
- 非 apt/pacman 发行版仍会给出缺失桌面依赖提示

</details>

<details>
<summary><b>Steam Deck / Arch Linux</b></summary>

```shell
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

说明：

- 如果机器上没有可用的 `VULKAN_SDK`，`gnb setup` 会自动下载项目约定版本的 LunarG Vulkan SDK 到 `external/VulkanSDK/`
- 如果机器上还没有 `slangc`，`gnb setup` 会自动下载项目约定的 Slang 工具链到 `external/`
- 在 pacman 环境下，`gnb setup` / Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前自动安装系统包；如果自动安装不可用，可手动执行 `sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libxrandr wayland-protocols libxkbcommon systemd-libs`
- 如果 vcpkg 阶段遇到 GitHub 归档下载失败，优先直接重试同一条构建命令
- 一次真实 Steam Deck 部署的复盘见 [docs/notes/steamdeck-deployment-notes.md](docs/notes/steamdeck-deployment-notes.md)

</details>

<details>
<summary><b>macOS</b></summary>

**前置条件：**

- Xcode / Command Line Tools
- CMake 3.26+
- Ninja（如果本机的 CMake 发行版未自带）

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

`gnb setup` 会自动下载项目使用的 Vulkan SDK、Slang 与 TypeScript 工具链，无需再单独准备这些项目级依赖。若显式设置 `VULKAN_SDK`，则优先使用该环境变量指向的 SDK。

</details>

<details>
<summary><b>Android (Windows 构建)</b></summary>

**前置条件：** JDK 17+、Android SDK、NDK r27

```bat
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
./gnb.bat setup --vcpkg-only
./gnb.bat android
```

Android 主机侧仍需要提供 JDK / SDK / NDK；项目内的 vcpkg 依赖与外部工具链则继续由 `gnb` 处理。

</details>

### 运行示例

```shell
# 主渲染器
./gnb.sh run gkNextRenderer

# Editor
./gnb.sh run gkNextEditor

# BrickPlayer（数字乐高 / LDraw 搭建原型）
./gnb.sh run BrickPlayer

# CharacterDemo（角色控制 / AI / 导航实验）
./gnb.sh run CharacterDemo

# TUI 终端模式（无窗口，画面刷到终端）
./gnb.sh tui --scene assets/models/playground.glb
```

### 可选资源包（optional assets）

部分较大的二进制资源不随仓库提交，需要按需拉取：

| 选择器 | 内容 | 落盘位置 | 缺失影响 |
|------|------|------|------|
| `ldraw` | `ldraw.pak` | `assets/paks/` | BrickPlayer 缺 LDraw 零件库 |
| `optional` | `optional.pak` | `assets/paks/` | 主渲染器 / Editor / CharacterDemo / MagicaLego 缺场景资源 |
| `sfx` | 6 个 mp3/wav | `assets/sfx/` | MagicaLego / BrickPlayer 静音 |
| `ffmpeg` | `ffmpeg.exe` | `src/ThirdParty/ffmpeg/bin/` | Windows 下 MagicaLego 视频录制不可用 |

```bash
# Linux / macOS / Git Bash：默认拉取全部可选资源
./gnb.sh paks fetch

# 或只拉指定资源
./gnb.sh paks fetch optional ldraw
./gnb.sh paks fetch ffmpeg sfx

# Windows
./gnb.bat paks fetch
```

## 子项目

| 项目 | 说明 |
|------|------|
| `gkNextRenderer` | 主渲染器，路径追踪 / Hybrid Rendering / 多管线对比 |
| `gkNextEditor` | ImGui 编辑器，服务于材质、场景与运行时工具链 |
| `ScadStudio` | OpenSCAD（`.scad`）DSL 建模 / 角色绑定实验编辑器 |
| `BrickPlayer` | 基于 LDraw 的数字乐高搭建原型 |
| `MagicaLego` | 更轻量的乐高 / voxel 风格玩法实验场 |
| `Brotato3D` | 俯视角 3D 生存射击原型，介绍见 [docs/projects/brotato-3d/introduction.md](docs/projects/brotato-3d/introduction.md) |
| `CharacterDemo` | 角色控制、AI 行为、导航与战斗交互实验 |
| `FlappyCpp` / `FlappyJs` | Flappy Bird 双实现回归样例，用于验证 C++ 与 QuickJS/TypeScript 行为一致性，介绍见 [docs/projects/flappy-bird-parity/introduction.md](docs/projects/flappy-bird-parity/introduction.md) |
| `gkNextBenchmark` | 静态 / 动态场景渲染基准测试 |
| `gkNextVisualTest` | 自动化视觉测试与截图报告 |
| `Packager` | 资产打包为 `.pkg` |

> 仓库中还有若干处于早期阶段的玩法 / 模拟原型（如 AirportSim、StudioSim、Voyage3D、KongLie3D 等），主要用于驱动引擎能力演进，接口尚不稳定。

---

## 参考与感谢

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

---

## 参与贡献

欢迎 Issue / PR。

- 开发协作说明见 `AGENTS.md`
- 如果你对实时路径追踪、现代渲染架构、渲染性能优化、LDraw、编辑器工具链、AI Native 工作流或玩法原型验证感兴趣，欢迎交流

---

## 第三方依赖

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif
