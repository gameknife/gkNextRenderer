# gkNextEngine

**A cross-platform 3D engine for real-time path tracing, gameplay prototyping, and high-end visual quality**

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Desktop CI](https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

<!-- The transparent SVG switches between black and white to match the active GitHub theme. -->
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <source media="(prefers-color-scheme: light)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <img src="./assets/brand/gknext_logo_vertical.svg" width="480" alt="gkNextEngine" />
  </picture>
</p>

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/confroom.webp" width="49%" alt="Conference Room" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/luxball.webp" width="49%" alt="Luxball" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/playground.webp" width="49%" alt="Playground" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp" width="49%" alt="Still" />
</p>


gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. The project is renderer-first, while continuously expanding around editor tooling, scripting, physics, asset import, and multiple gameplay prototypes (such as MagicaLego, Brotato3D, KongLie3D, BrickPlayer), alongside SCAD, LDraw, and Gaussian Splat structured pipelines — forming the groundwork for AI-native content generation, scene understanding, gameplay iteration, and automated validation.

> [!NOTE]
> **Core Goals**
> - **Real-Time Visual Performance**: Deliver visually compelling results with real-time path tracing, hybrid rendering, and HDR lighting that reliably hold up inside a live runtime.
> - **Full-Stack Engine Evolution**: Build extensible runtime systems suited for gameplay prototyping and AI-native workflows to drive long-term engine evolution.

> [!TIP]
> **Key Focus Areas**
> - **Real-Time Graphics**: Practical path tracing, physical materials, HDR environment lighting, and high-density scene rendering
> - **Runtime Performance**: Studying a Vulkan rendering architecture strictly constrained by real-time performance budget
> - **Unified Engine Architecture**: Tying rendering, editor, scripting, physics, content pipelines, and prototypes into a coherent engine
> - **Engineering Reference**: Approachable codebase (< 50k LOC Core) designed for learning modern C++20 / Vulkan engine implementation

**Supported platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

<p align="center">✦</p>

## ✨ Project Highlights

- **Real-Time Path Tracing & Hybrid Rendering**: Practical 1/2spp path tracing, denoising, and seamless multi-pipeline switching built for real runtime constraints.
- **High-Performance GPU Architecture**: Fully bindless resources, Visibility Buffer, and single-draw GPU-driven submission to minimize CPU overhead.
- **Radiance Caching & Sparse VRAM**: Leaning on SHARC cache reuse and on-demand residency to maximize rendering efficiency within fixed GPU budgets.
- **Full Engine Stack & Gameplay Prototypes**: Integrated ECS, reflection, ImGui editor, QuickJS/TS hot reload, and Jolt Physics to support interactive prototyping.
- **Multi-Format Structured Asset Pipelines**: Direct runtime import for glTF 2.0, LDraw (LEGO), OpenSCAD DSL, and PlayCanvas Gaussian Splatting.

<p align="center">✦</p>

## ⚡ Performance & Rendering Efficiency

Performance is one of the project's core constraints. The engine leans on radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling to produce more frame for a fixed GPU budget while keeping memory in check. Below is a set of **runtime performance references** for typical scenes, backed by a built-in per-pass profiler and a Superluminal integration for deeper analysis.

### Performance Reference Data

<details>
<summary><b>Typical Scene Performance Benchmark Data (RTX 5070 Ti / 720p)</b> — <i>Click to expand ▾</i></summary>

> The figures below come from `out/build/windows/bin/motion_benchmark_report.csv`, measured on an NVIDIA GeForce RTX 5070 Ti with NVIDIA 610.47.0 at 1280x720. Each scene was sampled for roughly 3 seconds; DLSS, FSR, and denoising were disabled.

| Scene | Resolution | Pipeline | Frame time (ms) | GPU time (ms) | FPS | VRAM | Draw AfterCull / View | Triangles AfterCull / View |
|------|------------|----------|-----------------|---------------|-----|------|---------------------------|--------------------------|
| pbr | 1280x720 | PathTracing | 1.714 | 1.300 | 583 | 884 MiB | 10 / 10 | 8,754 / 8,754 |
| pbr | 1280x720 | SoftwareModernNoAmbient | 0.547 | 0.157 | 1,827 | 857 MiB | 10 / 10 | 8,753 / 8,753 |
| playground | 1280x720 | PathTracing | 2.432 | 1.924 | 411 | 857 MiB | 82 / 84 | 10,384 / 10,465 |
| playground | 1280x720 | SoftwareModernNoAmbient | 0.586 | 0.213 | 1,708 | 859 MiB | 83 / 85 | 10,479 / 10,561 |
| livingroom | 1280x720 | PathTracing | 1.408 | 0.948 | 710 | 893 MiB | 10 / 146 | 57,843 / 560,308 |
| livingroom | 1280x720 | SoftwareModernNoAmbient | 0.614 | 0.217 | 1,628 | 893 MiB | 10 / 141 | 56,086 / 537,628 |
| castle | 1280x720 | PathTracing | 3.695 | 3.224 | 271 | 859 MiB | 1,448 / 2,313 | 96,640 / 155,867 |
| castle | 1280x720 | SoftwareModernNoAmbient | 0.779 | 0.368 | 1,284 | 925 MiB | 1,426 / 2,276 | 94,235 / 152,691 |
| complex | 1280x720 | PathTracing | 3.041 | 2.446 | 329 | 925 MiB | 3,373 / 19,715 | 40,683 / 237,561 |
| complex | 1280x720 | SoftwareModernNoAmbient | 0.702 | 0.261 | 1,424 | 952 MiB | 3,219 / 18,662 | 37,963 / 224,852 |

> The figures above can be reproduced using `gkNextMotionBenchmark` under unified hardware / drivers; fetch optional models first with `./gnb paks fetch`. Pass a single orchestration JSON at launch:
>
> ```bash
> ./gnb run gkNextMotionBenchmark --benchmark-config assets/configs/motion_benchmark.example.json
> ```

</details>

### 🔍 Built-in Profiler

The engine ships a CPU / GPU per-pass timing system: every render pass is annotated with a named scope, `VulkanGpuTimer` collects per-pass GPU-side timings, and an ImGui overlay (`ProfileDebugOverlay`) shows per-pass frame time and statistics live at runtime. You can locate rendering hotspots and compare the cost of different pipelines and settings without any external tooling.

### ⏱️ Superluminal Integration

On Windows, if the [Superluminal](https://superluminal.eu/) Performance API is installed (probed by default at `C:/Program Files/Superluminal/Performance/API`), the build automatically enables `WITH_SUPERLUMINAL` and forwards the engine's named CPU and GPU events to the Superluminal timeline (GPU events emitted from a dedicated replay thread), enabling fine-grained sampling profiles and cross-frame analysis. When it is not installed, the integration is skipped and the build is unaffected.

<p align="center">✦</p>

## 🛠️ Core Capabilities

### 1️⃣ High-quality rendering designed for runtime use

- **Real-time path tracing and hybrid rendering**: built around 1/2spp + temporal reuse, denoising, reprojection, and multi-pipeline switching so path tracing remains usable under real runtime constraints
- **Modern GPU raster pipeline**: Visibility Buffer, fully bindless resources, single-draw GPU-driven submission, Soft Mesh Shader, and GPU CSM shadows support dense scenes and game-like workloads
- **Hot-swappable renderers**: the same scene and asset set can switch across PathTracing, SoftwareTracing, SoftwareModern / NoAmbient, and related pipelines for image-quality, performance, and platform comparisons
- **GI, denoising, and upscaling**: SHARC world radiance cache, ReSTIR DI, as well as FSR / DLSS / DLSS RR / Native TAAU / SGSR2
- **Gaussian Splat co-rendering**: PlayCanvas SOG v2 Gaussian Splatting runs through a hardware-billboard path and can coexist with mesh scenes

### 2️⃣ Runtime, editor, and validation tooling

- **ECS + reflection**: based on entt, with a reflection layer shared across runtime systems, editor property panels, undo / redo, and QuickJS bindings
- **ImGui editor and material workflow**: `gkNextEditor` supports scene, material, and runtime-oriented editing, with data-driven settings, cvar panels, and a node-based material workflow
- **QuickJS + TypeScript hot reload**: TypeScript is compiled at runtime via the bundled `tools/tsc/tsc[.exe]`, with no Node/npm or global `tsc` dependency; see [docs/guides/typescript-integration.md](docs/guides/typescript-integration.md)
- **Jolt Physics and interactive runtime support**: provides a practical physics base for dragging, collision, character movement, playable prototypes, and automated scene validation
- **Agent validation tools**: `gnb shot` captures hidden-window validation screenshots, while `gnb validate` supports input-driven scripts, assertions, and JSON reports for rendering, UI, and gameplay-state regression checks
- **Profiler / benchmark / TUI**: built-in CPU / GPU pass profiling, `gkNextMotionBenchmark` CSV performance reports, `gkNextVisualTest` visual regression, and `gnb tui` terminal rendering previews
- **Remote Play mode**: `gnb remote` / `--remote` can run any desktop target as a WebRTC host, stream the frame to a zero-install browser client, and route keyboard, mouse, and virtual-gamepad input back into the runtime; video uses Vulkan Video H.264 hardware encoding
- **gnb dashboard and local LLM**: `gnb dashboard` provides local TODO, Build, Run, Test, Git, Chat, and LOC workflows; `gnb llm` integrates llama.cpp / Gemma as a local OpenAI-compatible service for tooling and runtime AI features

### 3️⃣ AI-native workflows and gameplay prototypes

- **Multiple prototypes validate real requirements**: MagicaLego, BrickPlayer, Brotato3D, KongLie3D, CharacterDemo, Flappy, AirportSim, StudioSim, NextRA, and the Voyage3D source prototype exercise building, action, physics, scripting, UI, combat, simulation, and AI interaction scenarios
- **Structured content for AI generation**: SCAD, LDraw, Gaussian Splat, and glTF pipelines give AI systems parseable, editable, and verifiable 3D content instead of only uncontrolled static assets
- **AI-assisted gameplay iteration**: local LLM support, QuickJS scripting, reflected components, agent validation, and the dashboard form a loop for "generate content -> run validation -> iterate"
- **Script parity and deterministic validation**: Flappy C++ / JS parity, input scripts, hidden-window screenshots, and benchmark reports help constrain behavioral regressions after AI-assisted changes

### 4️⃣ glTF, LDraw, OpenSCAD, and Gaussian-splat content pipelines

- **Full glTF import / partial export**: runtime support for scenes, materials, animation, and skeletal content, with selected runtime content exportable back into a glTF-oriented workflow
- **Direct LDraw runtime import**: `.ldr` / `.mpd` load directly into the runtime, with full color/material mapping from `LDConfig.ldr` and LGEO realistic colors into engine PBR materials, plus brick connectivity semantics converted into data the building system can understand
- **OpenSCAD DSL and ScadStudio**: parse / evaluate `.scad` directly, with geometry through Manifold CSG and text through FreeType; `ScadStudio` builds on this for modeling, scene generation, and character-rig experiments
- **ScadRig rigid-body characters**: SCAD files can describe rigid-body bone hierarchies and animation clips, currently used in the AirportSim / StudioSim direction for character visualization and role-based coloring experiments
- **Gaussian Splat assets**: load PlayCanvas `.sog` directly (packed ZIP or `meta.json` + `.webp`) and co-render with mesh, material, camera, and runtime scene data

### 5️⃣ Controlled codebase size for learning and extension

- **First-party Engine core targeted under 50k LOC**: the core stays intentionally understandable and maintainable
- **Clarity over over-engineering**: favors explicit data flow, clear ownership, and mature third-party libraries, without turning experimental systems into heavy frameworks too early
- **A good engine codebase to study**: from Vulkan rendering, resource management, scripting, editor integration, reflection, and content import to testing, benchmarking, and agent validation

<p align="center">✦</p>

## 🖼️ HDR Screenshots

<p align="center">
  <img src="docs/gallery/1_still.avif" width="49%" alt="Still Scene" />
  <img src="docs/gallery/2_living_room.avif" width="49%" alt="Living Room" />
  <img src="docs/gallery/3_lego_ldraw.avif" width="49%" alt="LDraw Lego" />
  <img src="docs/gallery/4_playground.avif" width="49%" alt="Playground Scene" />
  <img src="docs/gallery/5_luxball.avif" width="49%" alt="Luxball" />
  <img src="docs/gallery/6_debug_draw.avif" width="49%" alt="Debug Draw" />
</p>

<p align="center">✦</p>

## 🚀 Quick Start

For developers in mainland China, please ensure your network stability first. Recommended tools:

[Link with referral code](https://nxonearth.com/signupbyemail.aspx?MemberCode=93e1edc92a95412dbc7ff38c8288951920240913095147)
[Link without referral code](https://nxonearth.com/signupbyemail.aspx)

The project uses CMake + Ninja, with dependencies managed through vcpkg. Beyond the host-side basics you must already have installed (compiler / IDE, CMake, platform SDKs, and similar tools), project-specific dependencies, external toolchains, and optional assets are now prepared by `gnb` whenever possible. You will need a network environment that can access GitHub during dependency setup.

### General Notes

- First run `./gnb.sh doctor` (Windows: `gnb.bat doctor`) to check host tool readiness
- `./gnb.sh setup` (Windows: `gnb.bat setup`) prepares vcpkg, external toolchains, and optional pak assets; running `./gnb.sh build` automatically fetches core toolchains on first build if missing
- Desktop platforms build and run through `gnb` directly; no `cd out/build/<platform>/bin` is needed
- Consolidated CMake presets: `windows`, `linux`, `macos-arm64`, `ios`

### Platform Builds

<details>
<summary><b>Windows (Visual Studio 2022)</b> — <i>Click to show steps ▾</i></summary>

**Prerequisites:**

- CMake 3.26+
- Visual Studio 2022 (C++ workload)
- Vulkan SDK 1.4.341.1 (downloaded into repository by `gnb` by default; setting `VULKAN_SDK` uses the environment SDK instead)
- System language setting: enable "Use Unicode UTF-8 for worldwide language support"

```bat
./gnb.bat setup
./gnb.bat build        # Default core build (gkNextRenderer + gkNextUnitTests)
./gnb.bat build --all  # Full build (all 15+ subprojects)
./gnb.bat run gkNextRenderer
```

Beyond host tools such as Visual Studio, remaining project dependencies are handled by `gnb`; the default workflow pulls project-versioned Vulkan SDK, Slang, and TypeScript toolchains directly into the workspace. On Windows, `gnb` uses **Ninja** as the generator (automatically discovering MSVC/SDK environments), with NVIDIA Streamline (DLSS) enabled by default.

</details>

<details>
<summary><b>Linux (Ubuntu)</b> — <i>Click to show steps ▾</i></summary>

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

- Under apt / pacman environments, `gnb setup` and the first `gnb build` automatically install host system packages before bootstrapping vcpkg
- If auto-installation is unavailable, install packages manually: `sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev`
- Non-apt/pacman distros will report missing desktop dependencies if needed

</details>

<details>
<summary><b>Steam Deck / Arch Linux</b> — <i>Click to show steps ▾</i></summary>

```shell
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

Notes:

- If `VULKAN_SDK` is missing, `gnb setup` automatically downloads the project-specified LunarG Vulkan SDK into `external/VulkanSDK/`
- If `slangc` is missing, `gnb setup` automatically downloads the Slang toolchain into `external/`
- Under pacman environments, `gnb setup` / first `gnb build` auto-installs system packages before bootstrapping vcpkg; otherwise install manually via `sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libx11 libxft libxext libxi libxinerama libxcursor libxrandr wayland-protocols libxkbcommon`
- If vcpkg hits a GitHub archive download failure, retry the same command
- A real Steam Deck deployment postmortem is available in [docs/notes/steamdeck-deployment-notes.md](docs/notes/steamdeck-deployment-notes.md)

</details>

<details>
<summary><b>macOS</b> — <i>Click to show steps ▾</i></summary>

**Prerequisites:**

- Xcode / Command Line Tools
- CMake 3.26+
- Ninja (if CMake distribution does not include it)

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

`gnb setup` automatically downloads the project-specified Vulkan SDK, Slang, and TypeScript toolchains, removing the need to manually prepare these dependencies. If `VULKAN_SDK` is explicitly set, that environment SDK takes precedence.

</details>

### Running Examples

```shell
# Main renderer
./gnb.sh run gkNextRenderer

# Editor
./gnb.sh run gkNextEditor

# TUI terminal mode (headless, frame streamed to the terminal)
./gnb.sh tui --scene assets/models/playground.glb

# Remote Play (browser-based WebRTC host)
./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720
```

<p align="center">✦</p>

## 🧩 Subprojects

<table>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/airportsim.webp" width="100%" style="display: block; width: 100%;" alt="AirportSim" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>✈️ AirportSim</strong><br>
        <sub>Airport ecosystem simulation validating SCAD POIs, queues, A* pathfinding, and LLM decisions</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp" width="100%" style="display: block; width: 100%;" alt="MagicaLego" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧱 MagicaLego</strong><br>
        <sub>LEGO / voxel-style scene building and physics gameplay playground</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp" width="100%" style="display: block; width: 100%;" alt="Brotato3D" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🥔 Brotato3D</strong><br>
        <sub>Top-down 3D survival shooter validating wave spawns, object pooling, and Jolt Physics</sub>
      </div>
    </td>
  </tr>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp" width="100%" style="display: block; width: 100%;" alt="BrickPlayer" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧩 BrickPlayer</strong><br>
        <sub>Digital LEGO building prototype based on LDraw library and brick interaction</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/flappyjs.webp" width="100%" style="display: block; width: 100%;" alt="FlappyJs" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🐤 FlappyCpp / FlappyJs</strong><br>
        <sub>Dual C++ & QuickJS/TS implementations validating engine replay parity</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/procscad.webp" width="100%" style="display: block; width: 100%;" alt="ScadStudio" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>📐 ScadStudio</strong><br>
        <sub>OpenSCAD DSL modeling, evaluation, scene generation, and ScadRig character binding</sub>
      </div>
    </td>
  </tr>
  <tr>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/citysim.webp" width="100%" style="display: block; width: 100%;" alt="StudioSim" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🏢 StudioSim / CitySolSim</strong><br>
        <sub>Studio management and city simulation validating local LLM events and ScadRig characters</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp" width="100%" style="display: block; width: 100%;" alt="NextDayZ" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🧟 NextDayZ / CharacterDemo</strong><br>
        <sub>Character control, NavGrid A* pathfinding, AI behavior tree, and survival combat</sub>
      </div>
    </td>
    <td width="33%" align="center" valign="top" style="padding: 0;">
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nexttotalwar.webp" width="100%" style="display: block; width: 100%;" alt="NextTotalWar" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>⚔️ NextTotalWar / NextRA</strong><br>
        <sub>Massive army tactical simulation and lockstep deterministic RTS validation</sub>
      </div>
    </td>
  </tr>
</table>

<details>
<summary><b>Complete List of 15+ Subprojects & Categories</b> — <i>Click to expand ▾</i></summary>

#### Render & Editor Tooling
- **`gkNextRenderer`**: Main renderer supporting real-time path tracing, hybrid rendering, denoising, and multi-pipeline comparison.
- **`gkNextEditor`**: ImGui editor for scenes, material node workflow, and runtime cvar tuning.
- **`ScadStudio`**: OpenSCAD (`.scad`) procedural DSL modeling, evaluation, scene generation, and ScadRig character binding.
- **`RmlUiDemo`**: RmlUi runtime HTML/CSS UI engine integration and interactive demo.

#### Gameplay & Simulation Prototypes
- **`AirportSim`**: Airport ecosystem simulation for SCAD POIs, queues, A* pathfinding, LLM decisions, and ScadRig characters.
- **`StudioSim`**: Studio-management simulation for local LLM events, employee goals, SCAD offices, and ScadRig character roles.
- **`MagicaLego`**: Voxel / LEGO-style gameplay prototype and physics building playground.
- **`BrickPlayer`**: Digital LEGO brick interaction and assembly prototype based on LDraw standard.
- **`Brotato3D`**: Top-down 3D survival shooter prototype validating wave spawns, monster AI, object pooling, and Jolt Physics.
- **`KongLie3D`**: Auto-chess / synergy / round-based combat simulation prototype.
- **`NextRA`**: Deterministic RTS simulation prototype validating lockstep synchronization and replay.
- **`CharacterDemo`**: Character actor mounting, NavGrid A* navigation, AI behavior tree, and combat interaction.
- **`FlappyCpp` / `FlappyJs`**: Dual C++ and QuickJS/TS Flappy Bird implementations for verifying engine replay parity.
- **`TruckerDemo` / `CitySolSim` / `NextDayz` / `NextTotalWar`**: Vehicle driving, city traffic, survival tactics, and army simulation prototypes.

#### Benchmarks & Developer Utilities
- **`gkNextStillBenchmark`**: Static-scene frame-rate and image-quality benchmark.
- **`gkNextMotionBenchmark`**: Dynamic-camera / multi-scene rendering benchmark generating CSV profile reports.
- **`gkNextVisualTest`**: Automated visual regression testing generating scene comparison reports.
- **`gkNextUnitTests`**: Catch2 unit test suite.
- **`Packager`**: Asset packaging tool bundling scenes and textures into `.pkg` archives.

> Desktop targets can be launched via `./gnb remote --target <Target>` as Remote Play hosts (zero-install WebRTC browser play). `src/Application/Game/Voyage3D` remains in tree as a sailing / port / naval-combat source prototype.

</details>

<p align="center">✦</p>

## 📚 References & Thanks

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

<p align="center">✦</p>

## 🤝 Contributing

Issues and PRs are welcome.

- See `AGENTS.md` for collaboration guidelines
- If you are interested in real-time path tracing, modern rendering architecture, rendering performance optimization, LDraw, editor tooling, AI-native workflows, or gameplay prototyping, feel free to reach out

<p align="center">✦</p>

## 📦 Third-Party Dependencies

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif

<p align="center">✦</p>

## 📜 License

gkNextEngine is released under the [MIT License](LICENSE). Source code for third-party libraries is detailed in the third-party notices in [LICENSE](LICENSE).
