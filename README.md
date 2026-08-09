<!-- The transparent SVG switches between black and white to match the active GitHub theme. -->
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <source media="(prefers-color-scheme: light)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <img src="./assets/brand/gknext_logo_vertical.svg" width="480" alt="gkNextEngine" />
  </picture>
</p>

<h3 align="center">面向实时路径追踪、游戏原型与 AI Native 工作流的跨平台 C++20 / Vulkan 引擎</h3>

<p align="center">
  <em>个人 R&D 引擎实验场 · 不设商业妥协 · 追求极致画质与性能约束 · 赋能 AI Native 内容生成与自动化验证</em>
</p>

<p align="center">
  <a href="https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml"><img src="https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml/badge.svg" alt="Desktop CI" /></a>
  <a href="https://deepwiki.com/gameknife/gkNextEngine"><img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT" /></a>
</p>

<p align="center">
  <a href="README.en.md">English</a> &nbsp;·&nbsp;
  <a href="README.md">简体中文</a> &nbsp;·&nbsp;
  <a href="https://deepwiki.com/gameknife/gkNextEngine">DeepWiki</a> &nbsp;·&nbsp;
  <a href="AGENTS.md">AGENTS.md</a> &nbsp;·&nbsp;
  <a href="https://github.com/gameknife/gkNextEngine/issues">Issues</a>
</p>

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/confroom.webp" width="49%" alt="Conference Room" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/luxball.webp" width="49%" alt="Luxball" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/playground.webp" width="49%" alt="Playground" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp" width="49%" alt="Still" />
</p>

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp" width="24%" alt="MagicaLego" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp" width="24%" alt="Brotato3D" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp" width="24%" alt="BrickPlayer" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp" width="24%" alt="NextDayZ" />
</p>

gkNextEngine 是一个基于现代 C++20 与 Vulkan 的跨平台 3D 游戏引擎 / 渲染实验场。项目以渲染器能力为核心，同时持续扩展编辑器、脚本、物理、内容导入与多游戏原型（如 MagicaLego、Brotato3D、KongLie3D、BrickPlayer 等），以及 SCAD、LDraw、Gaussian Splat 等结构化内容管线，为 AI Native 的内容生成、场景理解、玩法迭代和自动化验证打下基础。

> [!NOTE]
> **引擎核心目标**
> - **实时视觉表现**：用实时路径追踪、Hybrid Rendering 与 TrueHDR 做出真正有展示力、且能稳定运行的画面。
> - **全栈架构演进**：用可运行、可扩展、可用于玩法原型验证与 AI Native 工作流的引擎能力支撑长期演进。

> [!TIP]
> **适用关注方向**
> - **实时渲染效果**：观察实时路径追踪、金属 / 玻璃 / 塑料材质、HDR 环境光和高密度场景的实际表现
> - **运行时性能架构**：研究真正以运行时性能为约束的 Vulkan 渲染管线，而非离线 Demo
> - **完整引擎系统**：理解渲染、编辑器、脚本、物理、内容导入与玩法原型如何紧密协作
> - **工程学习参考**：阅读规模可控（Core < 50k LOC）、强调代码清晰度的现代 Vulkan 引擎源码

**支持平台：** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

<p align="center">✦</p>

## ✨ 项目特性

- **实时路径追踪与 Hybrid 渲染**：面向真实运行时的 1/2spp 路径追踪、降噪与多管线无缝切换。
- **高性能 GPU 架构**：全 Bindless、Visibility Buffer 与 GPU-Driven 单 Draw 提交，最小化 CPU 开销。
- **辐射缓存与稀疏显存**：借助 SHARC 缓存复用与按需驻留，在固定 GPU 预算下最大化渲染效率。
- **全栈引擎与玩法原型**：整合 ECS、反射、ImGui 编辑器、QuickJS/TS 热重载与 Jolt 物理，支撑丰富玩法验证。
- **AI Native 基础设施**：配合自动化 Agent 验证与结构化内容管线，让 AI 可直接生成、理解并修改 3D 资产与脚本。
- **多格式结构化资产导入**：原生支持 glTF 2.0、LDraw（乐高）、OpenSCAD DSL 与 PlayCanvas 高斯溅射（Gaussian Splatting）。

<p align="center">✦</p>

## ⚡ 性能与渲染效率

性能是当前的核心约束之一。引擎围绕世界辐射缓存复用、稀疏显存布局、GPU-Driven 海量提交、按需驻留与多档上采等手段，在固定 GPU 预算下尽量多出画面、少占显存。下面给出一组**典型场景的运行时性能参考**，并配套内置的逐 pass profiler 与 Superluminal 集成做剖析。

### 性能参考数据

<details>
<summary><b>典型场景性能参考数据（RTX 5070 Ti / 720p Benchmark）</b> — <i>点击展开 ▾</i></summary>

**基线环境**（复现时请以同样口径标注）：
>
> | 项 | 值 |
> |---|---|
> | GPU | NVIDIA GeForce RTX 5070 Ti |
> | 驱动 | NVIDIA 596.49.0 |
> | 引擎版本 | dev @ 2026-08-08 |
> | 编排配置 | `assets/configs/motion_benchmark.example.json` |
> | 分辨率 | 1280x720 |
> | 采样 | 每场景 3 秒预热 + 3 秒统计 |
> | 关闭项 | DLSS / FSR / GTAO / 动画 tick（由配置中的 cvars 固定） |

| 场景 | 渲染管线 | 帧时间 (ms) | GPU 时间 (ms) | FPS | 显存 | Draw AfterCull / View | 三角形 AfterCull / View |
|------|----------|------------|---------------|-----|------|------------------------|-------------------|
| MaterialShowcase | PathTracing | 2.342 | 1.846 | 427 | 978 MiB | 15 / 15 | 13,862 / 13,862 |
| MaterialShowcase | SoftwareModernNoAmbient | 0.619 | 0.249 | 1,614 | 925 MiB | 15 / 15 | 13,542 / 13,542 |
| LightingShowcase | PathTracing | 2.836 | 2.408 | 353 | 978 MiB | 9 / 9 | 4,953 / 4,953 |
| LightingShowcase | SoftwareModernNoAmbient | 0.707 | 0.275 | 1,414 | 925 MiB | 5 / 5 | 2,881 / 2,881 |
| GIBootcamp | PathTracing | 4.950 | 4.455 | 202 | 925 MiB | 30 / 36 | 4,741 / 4,952 |
| GIBootcamp | SoftwareModernNoAmbient | 0.994 | 0.547 | 1,006 | 925 MiB | 33 / 40 | 5,175 / 5,388 |
| KilometerWorld | PathTracing | 1.651 | 1.255 | 606 | 925 MiB | 401 / 1,780 | 4,789 / 21,362 |
| KilometerWorld | SoftwareModernNoAmbient | 0.991 | 0.496 | 1,009 | 925 MiB | 405 / 1,798 | 4,851 / 21,580 |
| MassiveAsteroidBelt | PathTracing | 2.089 | 1.598 | 479 | 1,192 MiB | 34,265 / 67,786 | 2,733,342 / 5,422,850 |
| MassiveAsteroidBelt | SoftwareModernNoAmbient | 0.987 | 0.595 | 1,013 | 1,139 MiB | 32,977 / 65,197 | 2,620,345 / 5,215,602 |

> 复现方式（**不需要**可选资源包，全部为内置 proc demo 场景）：
>
> ```bash
> ./gnb run gkNextMotionBenchmark --benchmark-config assets/configs/motion_benchmark.example.json
> ```
>
> 结果写入 `out/build/<preset>/bin/motion_benchmark_report.csv`。
> 注意：编排 JSON 的 `scenes` 只接受内置 proc demo 场景名（如 `GIBootcamp.proc`），
> 传入 `.glb` 路径会被忽略并静默回落到内置场景列表。

</details>

### Profiler

引擎内置一套 CPU / GPU 逐 pass 计时系统：每个渲染 pass 由命名 scope 标注，`VulkanGpuTimer` 采集各 pass 的 GPU 端耗时，运行时以 ImGui 叠加层（`ProfileDebugOverlay`）实时显示逐 pass 帧时间与统计。无需外部工具即可定位渲染热点、对比不同管线与设置的开销。

### Superluminal 集成

Windows 上若安装了 [Superluminal](https://superluminal.eu/) Performance API（默认探测 `C:/Program Files/Superluminal/Performance/API`），构建会自动启用 `WITH_SUPERLUMINAL`，把引擎的 CPU 与 GPU 命名事件投递到 Superluminal 时间线（GPU 事件经独立回放线程标注），便于做细粒度采样剖析与跨帧分析。未安装时自动跳过，不影响构建。

<p align="center">✦</p>

## 🛠️ 核心能力

### 1️⃣ 渲染

- **实时路径追踪**：1/2 spp 采样配合时域复用、重投影与降噪，目标是可稳定运行的实时画面，而非离线出图
- **现代 GPU 光栅管线**：Visibility Buffer、全 Bindless、GPU-Driven 单 Draw 提交、Soft Mesh Shader 与 GPU CSM 阴影
- **多渲染器热切换**：PathTracing、SoftwareTracing、SoftwareModern / NoAmbient 共享同一套场景与资产，可直接对比画质、性能与平台适配
- **全局光照与上采**：SHARC 世界辐射缓存、ReSTIR DI，以及 DLSS / DLSS-RR / FSR / SGSR2 / Native TAAU
- **高斯溅射共渲染**：PlayCanvas SOG v2 splat 走硬件 billboard 路径，与 mesh 场景在同一帧共存

### 2️⃣ 运行时与编辑器

- **ECS + 反射**：entt 组件系统与 entt::meta 反射层，一次注册即同时服务运行时、编辑器属性面板、撤销 / 重做与脚本绑定
- **可视化编辑器**：场景编辑、节点式材质图、cvar 调优与数据驱动设置集成在同一套 ImGui 工作流
- **TypeScript 脚本热重载**：QuickJS 运行时搭配仓库自带的 TypeScript 工具链，脚本与着色器改动即时生效，无外部 Node 依赖
- **物理与角色运行时**：Jolt Physics 支撑碰撞、抓取拖拽、载具与角色移动

### 3️⃣ 内容管线

- **glTF 2.0**：场景、材质、动画与骨骼蒙皮完整导入，并支持将部分运行时内容回写
- **LDraw**：`.ldr` / `.mpd` 直接进入运行时，官方色表与 LGEO 真实材质映射为引擎 PBR，零件连接语义转换为搭建玩法可用的数据
- **OpenSCAD DSL**：内置解析与求值器，几何走 Manifold CSG、文字走 FreeType，把程序化脚本变成可渲染网格
- **ScadRig**：以 SCAD 描述刚体骨骼层级与动画片段，驱动模拟类原型中的角色表现
- **Gaussian Splat**：直接加载 PlayCanvas `.sog`，与 mesh、材质、相机共处同一运行时场景

### 4️⃣ 工具链与自动化验证

- **统一 CLI**：`gnb` 单一入口覆盖依赖准备、构建、运行、测试与资产打包，桌面与移动平台口径一致
- **性能剖析**：逐 pass 的 CPU / GPU 计时叠加层，并可选接入 Superluminal 时间线做细粒度采样
- **自动化回归**：无窗口截图、输入脚本驱动的断言验证、视觉回归与 benchmark CSV 报告，均可直接接入 CI
- **Remote Play**：任意桌面 target 可作为 WebRTC host，浏览器零安装接入画面并回传键鼠与虚拟手柄输入，视频走 Vulkan Video 硬件编码
- **本地工作台**：图形化 dashboard 汇总待办、构建、运行、测试与 Git，内置 llama.cpp 本地推理服务同时供工具链与运行时使用

### 5️⃣ AI Native 工作流

- **可解析的内容基座**：SCAD、LDraw、glTF 与 Splat 管线让 AI 面对的是可读取、可修改、可校验的结构化 3D 内容，而非不可控的静态素材
- **可编程的运行时**：反射组件与 TypeScript 绑定把引擎状态直接开放给脚本和模型
- **可自动判定的闭环**：截图、断言脚本、replay parity 与 benchmark 报告构成“生成 → 运行 → 验证 → 迭代”的机器可读回路
- **本地推理**：集成 llama.cpp / Gemma 的本地 OpenAI 兼容服务，供内容生成与游戏内 AI 决策共用

> 第一方 Engine core 刻意维持在 50k LOC 以内，优先明确的数据流与职责边界，而不是过早把实验性功能抽象成重型框架。

<p align="center">✦</p>

## 🖼️ HDR 截图

<p align="center">
  <img src="docs/gallery/1_still.avif" width="49%" alt="Still Scene" />
  <img src="docs/gallery/2_living_room.avif" width="49%" alt="Living Room" />
  <img src="docs/gallery/3_lego_ldraw.avif" width="49%" alt="LDraw Lego" />
  <img src="docs/gallery/4_playground.avif" width="49%" alt="Playground Scene" />
  <img src="docs/gallery/5_luxball.avif" width="49%" alt="Luxball" />
  <img src="docs/gallery/6_debug_draw.avif" width="49%" alt="Debug Draw" />
</p>

<p align="center">✦</p>

## 🚀 快速开始

> **网络前置条件**：构建过程需要稳定访问 GitHub 与 vcpkg 上游（下载依赖库、外部工具链与可选资源包）。
> 若所在网络访问不稳定，请自行准备可靠的网络环境或 vcpkg 镜像后再执行 `gnb setup`。

项目使用 CMake + Ninja，依赖由 vcpkg 管理。除了宿主机本身必须具备的基础工具（编译器 / IDE、CMake、平台 SDK 等），项目级依赖、外部工具链和可选资源包现在都尽量交给 `gnb` 准备。构建依赖下载阶段需要可访问 GitHub 的网络环境。

### 通用说明

- 推荐先执行 `./gnb.sh doctor`（Windows: `gnb.bat doctor`）检查宿主机缺失的基础工具
- `./gnb.sh setup`（Windows: `gnb.bat setup`）会准备 vcpkg、项目外部工具链与可选资源包；如果直接执行 `./gnb.sh build`，首次缺少 toolchain 时也会自动补齐核心依赖
- 桌面平台现在通过 `gnb` 统一构建和运行，通常不再需要先 `cd` 到 `out/build/<platform>/bin`
- 可用 CMake 预设收敛为：`windows`、`linux`、`macos-arm64`、`ios`

### 平台构建

<details>
<summary><b>Windows (Visual Studio 2022)</b> — <i>点击展开步骤 ▾</i></summary>

**前置条件：**

- CMake 3.26+
- Visual Studio 2022（C++ 工作负载）
- Vulkan SDK 1.4.341.1（默认由 `gnb` 自动下载到仓库内；若设置 `VULKAN_SDK` 则优先使用环境里的 SDK）
- 启用“使用 Unicode UTF-8 提供全球语言支持”

```bat
./gnb.bat setup
./gnb.bat build        # 默认构建核心目标 (gkNextRenderer + gkNextUnitTests)
./gnb.bat build --all  # 构建全量 15+ 子项目
./gnb.bat run gkNextRenderer
```

除 Visual Studio 这类宿主工具外，其余项目依赖通常都由 `gnb` 自动准备；默认会拉取项目约定版本的 Vulkan SDK、Slang 与 TypeScript 工具链到仓库内。`gnb` 在 Windows 上默认使用 **Ninja** 极速构建生成器（自动管理 MSVC 与 SDK 环境路径），Windows 默认启用 NVIDIA Streamline（DLSS）。

</details>

<details>
<summary><b>Linux (Ubuntu)</b> — <i>点击展开步骤 ▾</i></summary>

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

- 在 apt / pacman 环境下，`gnb setup` 与 Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前自动安装桌面构建所需系统包
- 如果自动安装不可用，再手动补齐：`sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev`
- 非 apt/pacman 发行版仍会给出缺失桌面依赖提示

</details>

<details>
<summary><b>Steam Deck / Arch Linux</b> — <i>点击展开步骤 ▾</i></summary>

```shell
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

说明：

- 如果机器上没有可用的 `VULKAN_SDK`，`gnb setup` 会自动下载项目约定版本的 LunarG Vulkan SDK 到 `external/VulkanSDK/`
- 如果机器上还没有 `slangc`，`gnb setup` 会自动下载项目约定的 Slang 工具链到 `external/`
- 在 pacman 环境下，`gnb setup` / Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前自动安装系统包；如果自动安装不可用，可手动执行 `sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libx11 libxft libxext libxi libxinerama libxcursor libxrandr wayland-protocols libxkbcommon`
- 如果 vcpkg 阶段遇到 GitHub 归档下载失败，优先直接重试同一条构建命令
- 一次真实 Steam Deck 部署的复盘见 [docs/notes/steamdeck-deployment-notes.md](docs/notes/steamdeck-deployment-notes.md)

</details>

<details>
<summary><b>macOS</b> — <i>点击展开步骤 ▾</i></summary>

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

### 运行示例

```shell
# 主渲染器
./gnb.sh run gkNextRenderer

# Editor
./gnb.sh run gkNextEditor

# TUI 终端模式（无窗口，画面刷到终端）
./gnb.sh tui --scene assets/models/playground.glb

# Remote Play（浏览器 WebRTC 远程游玩 host）
./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720
```

<p align="center">✦</p>

## 🧩 子项目

<table>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/airportsim.webp" width="100%" style="display: block; width: 100%;" alt="AirportSim" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>✈️ AirportSim</strong><br>
        <sub>机场生态模拟，验证 SCAD POI、角色队列、A* 寻路与 LLM 智能决策</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp" width="100%" style="display: block; width: 100%;" alt="MagicaLego" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧱 MagicaLego</strong><br>
        <sub>乐高 / Voxel 风格体素场景搭建与物理玩法实验场</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp" width="100%" style="display: block; width: 100%;" alt="Brotato3D" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🥔 Brotato3D</strong><br>
        <sub>俯视角 3D 生存射击原型，验证技能波次、对象池与 Jolt 物理</sub>
      </div>
    </td>
  </tr>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp" width="100%" style="display: block; width: 100%;" alt="BrickPlayer" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧩 BrickPlayer</strong><br>
        <sub>基于 LDraw 标准的数字乐高积木交互与搭建原型</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/flappyjs.webp" width="100%" style="display: block; width: 100%;" alt="FlappyJs" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🐤 FlappyCpp / FlappyJs</strong><br>
        <sub>C++ 与 QuickJS/TS 双实现，验证引擎确定性 replay parity</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/procscad.webp" width="100%" style="display: block; width: 100%;" alt="ScadStudio" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>📐 ScadStudio</strong><br>
        <sub>OpenSCAD DSL 建模求值、场景生成与 ScadRig 刚体绑定</sub>
      </div>
    </td>
  </tr>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/citysim.webp" width="100%" style="display: block; width: 100%;" alt="StudioSim" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🏢 StudioSim / CitySolSim</strong><br>
        <sub>工作室经营与城市模拟，验证本地 LLM 事件与 ScadRig 职业表现</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp" width="100%" style="display: block; width: 100%;" alt="NextDayZ" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧟 NextDayZ / CharacterDemo</strong><br>
        <sub>角色控制、NavGrid A* 寻路、AI 行为树与生存战斗原型</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nexttotalwar.webp" width="100%" style="display: block; width: 100%;" alt="NextTotalWar" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>⚔️ NextTotalWar / NextRA</strong><br>
        <sub>大规模军团战术模拟与 Lockstep 确定性 RTS 验证</sub>
      </div>
    </td>
  </tr>
</table>

<details>
<summary><b>全量 15+ 子项目与工程分类详解清单</b> — <i>点击展开清单 ▾</i></summary>

#### 渲染与编辑器 (Render & Editor)
- **`gkNextRenderer`**：主渲染器，支持实时路径追踪 / Hybrid Rendering / 降噪与多管线对比。
- **`gkNextEditor`**：ImGui 综合编辑器，面向场景、材质节点工作流（material node editor）与运行时 cvar 调优。
- **`ScadStudio`**：OpenSCAD（`.scad`）程序化 DSL 建模求值、场景生成与 ScadRig 角色绑定实验场。
- **`RmlUiDemo`**：RmlUi 运行时 HTML/CSS UI 引擎集成与交互验证 demo。

#### 玩法与生态模拟 (Game & Simulation)
- **`AirportSim`**：机场生态模拟，验证 SCAD POI、角色队列、A* 寻路、LLM 决策与 ScadRig 角色。
- **`StudioSim`**：工作室经营模拟，验证本地 LLM 事件、员工目标、SCAD 办公室与 ScadRig 职业配色。
- **`MagicaLego`**：体素 / 乐高风格玩法原型与场景物理搭建。
- **`BrickPlayer`**：基于 LDraw 标准的数字乐高交互与搭建原型。
- **`Brotato3D`**：俯视角 3D 生存射击原型，验证技能波次、怪物 AI、对象池与 Jolt 物理。
- **`KongLie3D`**：自走棋 / 羁绊 / 战斗回合模拟原型。
- **`NextRA`**：确定性 RTS 模拟原型，验证 Lockstep 帧同步与 Replay 回放。
- **`CharacterDemo`**：角色 Actor 挂载、NavGrid A* 导航、AI 行为树与战斗交互实验。
- **`FlappyCpp` / `FlappyJs`**：Flappy Bird C++ / QuickJS TS 双实现，验证引擎重播与脚本行为一致性（Parity）。
- **`TruckerDemo` / `CitySolSim` / `NextDayz` / `NextTotalWar`**：车辆驾驶、城市交通、生存战术与军团模拟原型。

#### 基准测试与自动化工具 (Benchmark & Tools)
- **`gkNextStillBenchmark`**：静态场景帧率与画质基准测试。
- **`gkNextMotionBenchmark`**：动态镜头 / 多场景渲染性能基准，自动输出 CSV profile 报告。
- **`gkNextVisualTest`**：自动化视觉回归测试，渲染场景并生成对比截图报告。
- **`gkNextUnitTests`**：Catch2 单元测试套件。
- **`Packager`**：资产打包工具，将场景与纹理打包为 `.pkg` 归档文件。

> 所有桌面 Target 均可配合 `./gnb remote --target <Target>` 进入 Remote Play host 模式（WebRTC 浏览器零安装游玩）。`src/Application/Game/Voyage3D` 仍保留为航海/港口/海战源码原型。

</details>

<p align="center">✦</p>

## 📚 参考与感谢

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

<p align="center">✦</p>

## 🤝 参与贡献

欢迎 Issue / PR。

- 开发协作说明见 `AGENTS.md`
- 如果你对实时路径追踪、现代渲染架构、渲染性能优化、LDraw、编辑器工具链、AI Native 工作流或玩法原型验证感兴趣，欢迎交流

<p align="center">✦</p>

## 📦 第三方依赖

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif

<p align="center">✦</p>

## 📜 许可协议

gkNextEngine 以 [MIT 协议](LICENSE) 开源。第三方库的源代码详见 [LICENSE](LICENSE) 中的第三方声明。
