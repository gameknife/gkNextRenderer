# gkNextEngine

**面向实时路径追踪、游戏原型与高质量视觉表现的跨平台 3D 引擎**

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

![Play ground](docs/gallery/4_playground.avif)

---

gkNextEngine 是一个基于现代 C++20 与 Vulkan 的跨平台 3D 游戏引擎 / 渲染实验场，重点放在两件事上：

- 用 **实时路径追踪、Hybrid Rendering 与 HDR 光照表现** 做出真正有展示力的画面
- 用 **可运行、可扩展、可用于游戏原型验证的引擎能力** 支撑长期演进，而不是停留在单点 demo

项目以渲染器能力为核心，同时持续扩展编辑器、脚本、物理、内容导入与玩法原型。LDraw / BrickPlayer 是当前很有代表性的内容方向：引擎层可以直接导入 LDraw 模型，并把这类结构化资产纳入统一的 Runtime、渲染与交互系统。

如果你关注以下方向，这个项目会比较值得参考：

- 想看实时路径追踪、金属 / 玻璃 / 塑料材质、HDR 环境光和高密度场景的实际画面
- 想研究一套更贴近游戏运行时的 Vulkan 渲染架构，而不是只会离线出图的 demo
- 想看一个引擎如何把 **渲染、编辑器、脚本、物理、内容导入与玩法原型** 串成完整系统
- 想读一套规模可控、强调工程清晰度、适合学习现代 Vulkan 渲染与引擎实现的代码库

**支持平台：** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## 项目特性

- **实时路径追踪与 Hybrid Rendering**  
  围绕 1spp + temporal reuse、降噪、重投影和多管线切换持续推进，让路径追踪不只停留在离线效果演示，而是面向真实运行时表现。

- **游戏级性能取向的 GPU 架构**  
  通过 Visibility Buffer、全 Bindless、GPU-Driven、Multi-Draw Indirect 等设计，尽量把 CPU 开销留给内容与玩法，把 GPU 算力用在真正影响画面的地方。

- **引擎能力服务于内容与玩法原型**  
  包括 ECS、反射、编辑器、脚本热重载、物理同步、运行时导入和稳定的渲染行为。这些能力共同支撑更完整的可玩内容系统。

- **多格式内容导入与互操作**  
  引擎完整支持 glTF 运行时导入，并支持部分导出；同时也可以直接导入 `.ldr` / `.mpd`，将结构化的 LDraw 场景纳入统一的 Runtime、渲染与交互系统。

---

## 核心能力

### 1. 面向运行时的高质量渲染

- **实时路径追踪**：围绕 1spp + temporal reuse 持续推进，关注真实运行时条件下的画面质量与可用性能
- **Hybrid Rendering**：在移动平台与游戏级工况下，把传统光栅与光追做合理混合
- **多套渲染器热切换**：同一套资产与场景，可直接切换不同管线做对比和验证
- **HDR 截图与高质量素材导出**：便于做视觉验证、展示与回归对比

### 2. 完整的运行时与工具链能力

- **ECS + Reflection**：基于 entt 的组件系统，加上反射层，服务于运行时、编辑器和脚本绑定
- **ImGui 编辑器**：`gkNextEditor` 面向材质、场景和运行时内容的编辑工作流
- **QuickJS 脚本热重载**：运行时使用仓库内置 `tools/tsc/tsc[.exe]` 编译 TypeScript（Windows 为 `tsc.exe`，macOS/Linux 为 `tsc`），无需 Node/npm 或全局 `tsc`
- **Jolt Physics**：为交互原型、拖拽玩法和游戏化验证提供更真实的物理基础

### 3. 代码规模可控，适合学习和扩展

- **目标代码规模 < 50k LOC**：当前代码量仍然保持在便于理解和持续演进的区间
- **优先清晰实现而非过度设计**：尽量用明确的数据流、职责边界和成熟三方库解决问题
- **适合阅读现代引擎实现**：从 Vulkan 渲染、资源管理到脚本、编辑器、反射与测试链路，都能看到较完整的工程组织方式

### 4. glTF 与 LDraw 的内容导入能力

- **glTF 完整导入**：面向运行时支持 glTF 场景、材质、动画、骨骼蒙皮等完整内容导入
- **glTF 部分导出**：支持将部分运行时内容回写到 glTF 工作流
- **LDraw 直接导入 Runtime**：`.ldr` / `.mpd` 可直接进入 Runtime
- **颜色与材质映射**：从 `LDConfig.ldr`、LGEO realistic color 到引擎 PBR 材质的完整映射
- **Shadow / Connector 抽象**：不是只导入网格，而是开始把零件连接语义转换成搭建系统可理解的数据

---

## 技术方向

### 渲染与 GPU 架构

- **Visibility Buffer**
- **全 Bindless + GPU-Driven**
- **Multi-Draw Indirect**
- **Hardware / Software Ray Tracing**
- **Temporal Reprojection / JBF / OIDN / DLSS RR**

### 引擎与工具链

- **现代 CMake Presets + vcpkg**
- **跨平台运行时：桌面 / Android / iOS**
- **ImGui Editor + Node-based Material Workflow**
- **QuickJS Runtime Scripting**
- **Visual Test / Benchmark / Packager**

### AI Native

- **内置 AI Agent 基础设施，可扩展运行时 LLM 能力**
- **使用 Codex 进行引擎基础设施与示例 Demo 的原生开发**
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

项目使用 CMake + Ninja，依赖由 vcpkg 管理。构建依赖下载阶段需要可访问 GitHub 的网络环境。

### 通用说明

- 桌面平台现在通过 `gnb` 统一构建和运行，通常不再需要先 `cd` 到 `out/build/<platform>/bin`
- 首次使用执行 `./gnb setup`（Windows: `gnb.bat setup`），之后日常使用 `./gnb build` / `./gnb run`
- 可用 CMake 预设收敛为：`windows`、`linux`、`macos-arm64`、`ios`

### 平台构建

<details>
<summary><b>Windows (Visual Studio 2022)</b></summary>

**前置条件：**

- CMake 3.26+
- Visual Studio 2022（C++ 工作负载）
- Vulkan SDK 1.4.313.2
- 启用“使用 Unicode UTF-8 提供全球语言支持”

```bat
gnb.bat setup
gnb.bat build
gnb.bat run gkNextRenderer
```

</details>

<details>
<summary><b>Linux (Ubuntu)</b></summary>

```shell
sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev autoconf autoconf-archive automake libtool python3.12-venv
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

`gnb setup` / Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前按上面的列表准备系统包；非 apt/pacman 发行版仍会给出缺失桌面依赖提示。

</details>

<details>
<summary><b>Steam Deck / Arch Linux</b></summary>

```shell
sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libxrandr wayland-protocols libxkbcommon
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

说明：

- 如果机器上还没有 `slangc`，`gnb setup` 会自动下载项目约定的 Slang 工具链到 `external/`
- `gnb setup` / Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前按上面的列表准备系统包
- 如果 vcpkg 阶段遇到 GitHub 归档下载失败，优先直接重试同一条构建命令
- 一次真实 Steam Deck 部署的复盘见 [docs/steamdeck-deployment-notes.md](docs/steamdeck-deployment-notes.md)

</details>

<details>
<summary><b>macOS</b></summary>

```shell
brew install molten-vk glslang ninja
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

</details>

<details>
<summary><b>Android (Windows 构建)</b></summary>

**前置条件：** JDK 17+、Android SDK、NDK r27

```bat
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
gnb.bat setup --vcpkg-only
gnb.bat android
```

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
gnb.bat paks fetch
```

## 子项目

| 项目 | 说明 |
|------|------|
| `gkNextRenderer` | 主渲染器，路径追踪 / Hybrid Rendering / 多管线对比 |
| `gkNextEditor` | ImGui 编辑器，服务于材质、场景与运行时工具链 |
| `BrickPlayer` | 基于 LDraw 的数字乐高搭建原型 |
| `CharacterDemo` | 角色控制、AI 行为、导航与战斗交互实验 |
| `MagicaLego` | 更轻量的乐高 / voxel 风格玩法实验场 |
| `gkNextStillBenchmark` | 静态场景渲染基准测试 |
| `gkNextMotionBenchmark` | 动态场景渲染基准测试 |
| `gkNextVisualTest` | 自动化视觉测试与截图报告 |
| `Packager` | 资产打包为 `.pkg` |

---

## 参考与感谢

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

---

## 参与贡献

欢迎 Issue / PR。

- 开发协作说明见 `AGENTS.md`
- 如果你对实时路径追踪、现代渲染架构、LDraw、编辑器工具链、AI Native 工作流或玩法原型验证感兴趣，欢迎交流

---

## 第三方依赖

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif
