<!-- The transparent SVG switches between black and white to match the active GitHub theme. -->
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <source media="(prefers-color-scheme: light)" srcset="./assets/brand/gknext_logo_vertical.svg" />
    <img src="./assets/brand/gknext_logo_vertical.svg" width="480" alt="gkNextEngine" />
  </picture>
</p>

<h3 align="center">A cross-platform C++20 & Vulkan engine for real-time path tracing, gameplay prototyping, and AI-native workflows.</h3>

<p align="center">
  <em>A personal R&D engine playground · No commercial compromises · High-end visual fidelity under strict performance constraints · Empowering AI-native content generation & automated validation.</em>
</p>

<p align="center">
  <a href="https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml"><img src="https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml/badge.svg" alt="Desktop CI" /></a>
  <a href="https://deepwiki.com/gameknife/gkNextEngine"><img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT" /></a>
</p>

<p align="center">
  <a href="https://gameknife.github.io/gkNextEngine/">Website</a> &nbsp;·&nbsp;
  <a href="README.en.md">English</a> &nbsp;·&nbsp;
  <a href="README.md">简体中文</a> &nbsp;·&nbsp;
  <a href="https://deepwiki.com/gameknife/gkNextEngine">DeepWiki</a> &nbsp;·&nbsp;
  <a href="AGENTS.md">AGENTS.md</a> &nbsp;·&nbsp;
  <a href="https://github.com/gameknife/gkNextEngine/discussions">Community</a> &nbsp;·&nbsp;
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

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/gknexteditor.webp" width="100%" alt="gkNextEditor" />
</p>

gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. The renderer is the core; around it sit the editor, C# scripting, Jolt Physics, structured content pipelines (SCAD / LDraw / glTF / Gaussian Splat), and a dozen-plus gameplay prototypes (MagicaLego, Brotato3D, KongLie3D, BrickPlayer, and others) — together forming the groundwork for AI-native content generation, scene understanding, gameplay iteration, and automated validation.

The scripting layer is **C#**: a game is declared by a single JSON manifest, created from a template, loaded and run in-process, and hot-reloaded from the launcher or the editor — no C++, no CMake target. All five platforms (Windows / Linux / macOS / Android / iOS) share one codebase and one `gnb` command to build and run.

<hr>

> [!NOTE]
> **Core Goals**
> - **Real-Time Visual Performance**: Deliver visually compelling results with real-time path tracing, hybrid rendering, and HDR lighting that reliably hold up inside a live runtime.
> - **Full-Stack Engine Capability**: Rendering, editor, scripting, physics, and content pipelines form one runnable, extensible engine that serves gameplay prototyping and AI-native workflows.

> [!TIP]
> **Key Focus Areas**
> - **Real-Time Graphics**: Practical path tracing, physical materials, HDR environment lighting, and high-density scene rendering
> - **Runtime Performance**: Studying a Vulkan rendering architecture strictly constrained by real-time performance budget
> - **Unified Engine Architecture**: Tying rendering, editor, scripting, physics, content pipelines, and prototypes into a coherent engine
> - **Engineering Reference**: Approachable codebase (< 50k LOC Core) designed for learning modern C++20 / Vulkan engine implementation

**Supported platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

<sub>The three desktop platforms are feature-aligned, mobile ships a touch input layer, and Steam Deck / Arch gets a dedicated PathTracingLite tier; devices that cannot meet the bindless budget or lack `bufferDeviceAddress` (an A12X iPad, for example) run the compatibility renderer.</sub>

<p align="center">✦</p>

## ✨ Project Highlights

- **Real-Time Path Tracing & Hybrid Rendering**: 1/2spp path tracing, denoising, and seamless multi-pipeline switching, with checkerboard shading halving the shading cost again.
- **High-Performance GPU Architecture**: Fully bindless resources, Visibility Buffer, and single-draw GPU-driven submission to minimize CPU overhead.
- **Radiance Caching & Sparse VRAM**: SHARC cache reuse and on-demand residency maximize rendering efficiency within fixed GPU budgets.
- **One Codebase, Five Platforms**: Desktop, mobile, and handheld all build and run through the same `gnb` commands; devices that fall short of the required capabilities run a compatibility renderer.
- **C# Scripting Layer**: The one scripting implementation is .NET — CoreCLR for hot reload during development, NativeAOT for release and mobile, with the same managed code unchanged.
- **Data-Driven C# Game Projects**: One `*.game.json` manifest is one game — scaffolded from a template, loaded and run in-process, rebuilt from a menu, without a line of C++.
- **Full Engine Stack & Gameplay Prototypes**: ECS, entt::meta reflection, an ImGui editor, Slang shader hot reload, and Jolt Physics.
- **AI-Native Infrastructure**: Automated agent validation plus structured content pipelines let AI generate, understand, and modify 3D assets and scripts directly.
- **Multi-Format Structured Assets**: glTF 2.0, LDraw (LEGO), OpenSCAD DSL, PlayCanvas Gaussian Splatting, and real-world city levels generated from SRTM + OpenStreetMap.

<p align="center">✦</p>

## ⚡ Performance & Rendering Efficiency

Performance is one of the project's core constraints. The engine leans on radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling to produce more frame for a fixed GPU budget while keeping memory in check.

### Performance Reference Data

<details>
<summary><b>Typical Scene Performance Benchmark Data (RTX 5070 Ti / 720p)</b> — <i>Click to expand ▾</i></summary>

**Baseline environment** (state the same fields when reproducing):
>
> | Item | Value |
> |---|---|
> | GPU | NVIDIA GeForce RTX 5070 Ti |
> | Driver | NVIDIA 596.49.0 |
> | Engine build | dev @ 2026-08-08 |
> | Orchestration | `assets/configs/motion_benchmark.example.json` |
> | Resolution | 1280x720 |
> | Sampling | 3 s warmup + 3 s measurement per scene |
> | Disabled | DLSS / FSR / GTAO / animation tick (pinned by the config cvars) |

| Scene | Pipeline | Frame time (ms) | FPS | VRAM | Draw AfterCull / View | Triangles AfterCull / View |
|------|----------|-----------------|-----|------|---------------------------|--------------------------|
| MaterialShowcase | PathTracing | 2.342 | 427 | 978 MiB | 15 / 15 | 13,862 / 13,862 |
| MaterialShowcase | SoftwareModernNoAmbient | 0.619 | 1,614 | 925 MiB | 15 / 15 | 13,542 / 13,542 |
| LightingShowcase | PathTracing | 2.836 | 353 | 978 MiB | 9 / 9 | 4,953 / 4,953 |
| LightingShowcase | SoftwareModernNoAmbient | 0.707 | 1,414 | 925 MiB | 5 / 5 | 2,881 / 2,881 |
| GIBootcamp | PathTracing | 4.950 | 202 | 925 MiB | 30 / 36 | 4,741 / 4,952 |
| GIBootcamp | SoftwareModernNoAmbient | 0.994 | 1,006 | 925 MiB | 33 / 40 | 5,175 / 5,388 |
| KilometerWorld | PathTracing | 1.651 | 606 | 925 MiB | 401 / 1,780 | 4,789 / 21,362 |
| KilometerWorld | SoftwareModernNoAmbient | 0.991 | 1,009 | 925 MiB | 405 / 1,798 | 4,851 / 21,580 |
| MassiveAsteroidBelt | PathTracing | 2.089 | 479 | 1,192 MiB | 34,265 / 67,786 | 2,733,342 / 5,422,850 |
| MassiveAsteroidBelt | SoftwareModernNoAmbient | 0.987 | 1,013 | 1,139 MiB | 32,977 / 65,197 | 2,620,345 / 5,215,602 |

> To reproduce (no optional asset packs needed - every scene is a built-in proc demo):
>
> ```bash
> ./gnb run gkNextMotionBenchmark --benchmark-config assets/configs/motion_benchmark.example.json
> ```
>
> Results are written to `out/build/<preset>/bin/motion_benchmark_report.csv`.
> Note: the orchestration JSON's `scenes` list only accepts built-in proc demo scene names
> (for example `GIBootcamp.proc`); `.glb` paths are ignored and the run silently falls back
> to the built-in scene list.

</details>

### 🚀 Startup & Load Time

Cold start to first frame is a separately constrained path:

- **Persistent pipeline cache**: `VkPipelineCache` is written to disk and reused, so a second launch skips graphics / compute pipeline compilation
- **Parallel Streamline init**: `slInit` has to load and NGX-probe every Streamline plugin, so it runs on a worker thread overlapped with reflection registration, pak mounting, SDL init, and window creation, and logs how many milliseconds it actually blocked startup
- **Asset caching**: `FileHelper`, scene build, CPU acceleration structures, and the texture upload path share one cache, so the same data is read and parsed once
- **Startup splash**: staged progress appears as soon as the window exists

On the development machine, cold start to first frame lands at roughly **1.3 seconds** (with DLSS off).

### 🔍 Profiling

The engine has no separate runtime CPU / GPU timing aggregator. Named scopes bind directly to Tracy (CPU/GPU) and Superluminal (CPU), so the engine does not retain a duplicate CPU timing tree, GPU query banks, or ImGui timing history.

Development builds also enable the Tracy client by default (on-demand, so disconnected processes do not keep accumulating events). Run `gnb tracy fetch` to obtain the official GUI matching the vcpkg client, then `gnb tracy` to launch it; on Android use `gnb tracy --android` and connect to `127.0.0.1` through adb forwarding. See the [Tracy Profiling Guide](docs/guides/tracy-profiling.md). Release builds pass `--tracy=off` and do not ship the Tracy client.

### ⏱️ Superluminal Integration

On Windows, if the [Superluminal](https://superluminal.eu/) Performance API is installed (probed by default at `C:/Program Files/Superluminal/Performance/API`), the build automatically enables `WITH_SUPERLUMINAL` and forwards the engine's named CPU events and debug markers to the Superluminal timeline, enabling fine-grained sampling profiles and cross-frame analysis. When it is not installed, the integration is skipped and the build is unaffected.

### 🧪 RenderDoc Integration

On Windows, if `C:/Program Files/RenderDoc/renderdoc_app.h` exists, the build automatically enables `WITH_RENDERDOC` and dynamically loads the RenderDoc application API from the same installation. Append `--renderdoc` to any desktop target to capture the first frame after the scene is ready and open it in the RenderDoc UI; when RenderDoc is not installed, the integration is skipped and the build is unaffected.

<p align="center">✦</p>

## 🛠️ Core Capabilities

### 1️⃣ Rendering

- **Real-time path tracing**: 1/2spp sampling with temporal reuse, reprojection, and denoising — aimed at frames that hold up at runtime, not offline stills
- **Modern GPU raster pipeline**: Visibility Buffer, fully bindless resources, single-draw GPU-driven submission, Soft Mesh Shader, and GPU CSM shadows
- **Unified surface pipeline**: surface build and scheduling are the default path, so the renderers share one set of dense surface render targets
- **Checkerboard shading**: only half the pixels are shaded each frame and a resolve pass reconstructs the rest; available on both the Tracing and NoAmbient paths (`r.checkerboardRendering`)
- **Hot-swappable renderers**: PathTracing, PathTracingLite, SoftwareTracing, SoftwareModern / NoAmbient, and VoxelTracing share one scene and asset set, so image quality, performance, and platform fit compare directly
- **Compatibility renderer**: a device that cannot meet the bindless descriptor budget or lacks `bufferDeviceAddress` switches to the `Compatibility` renderer, which declares no screen-space chain; scenes, UI, and input all work. This is a device verdict, not a quality tier
- **Global illumination and upscaling**: SHARC world radiance cache, ReSTIR DI, plus DLSS / DLSS-RR / FSR / SGSR2 / Native TAAU
- **Gaussian Splat co-rendering**: PlayCanvas SOG v2 splats run through a hardware-billboard path and share the frame with mesh scenes

### 2️⃣ Runtime and Editor

- **ECS + reflection**: an entt component system and entt::meta reflection layer — one registration serves the runtime, editor property panels, undo / redo, and the C# bindings alike
- **Visual editor**: scene editing, a node-based material graph, cvar tuning, and data-driven settings in a single ImGui workflow
- **Play in editor**: F5 runs a C# game inside the editor, F8 ejects back to the editor to inspect and edit the running scene, and Stop restores the scene that was open before Play
- **Shader hot reload**: incremental Slang compilation plus pipeline rebuild, so shader edits apply live
- **Physics and character runtime**: Jolt Physics backs collision, grab-and-drag, vehicles, and character movement
- **Mobile touch**: Android and iOS share one input layer — a virtual stick on the left half of the screen moves, dragging the right half turns the camera

### 3️⃣ C# Scripting and Game Projects

The only implementation of `Runtime::IScriptRuntime` is `Modules/NextDotNet`; there is no second scripting language in the engine.

- **Two backends, one set of managed code**: CoreCLR for hot reload and debugging during development, NativeAOT for release and mobile where size and startup matter. Switching is one CMake option and not a single line of C# changes; `gnb dotnet ci` is the enforcement point for the two-backend ABI
- **The binding surface is declared once**: an engine function is one line in `EngineApi.def.h`, a component property reuses the `entt::meta` reflection, and `gnb csharpgen` emits the C# wrappers
- **One game = one manifest**: `assets/configs/games/<id>.game.json` declares the window, assembly, required modules, initial scene, and hot-reload policy; the per-game native shell is 15 lines, and the eight engine hooks share one forwarding implementation
- **Scaffold from a template**: the launcher's New Project card or the editor's File > New Game Project, with five templates (blank, 2D arcade, top-down survivor, first-person explorer, third-person shooter) covering the usual starting points. Adding a template means dropping a directory into `assets/templates/games/` — no code changes
- **In-process load and unload**: `gkNextLauncher` uses a collectible `AssemblyLoadContext` to select, load, and unload any managed game in a single process, and can rebuild the C# from its menu. Unloading runs a full world reset (scene, physics, audio, cvars, show flags, window title), and repeated failures to collect are treated as a leak that demands a restart
- **Parity as the regression**: `FlappyCpp` and `FlappyCSharp` are line-for-line counterparts compared frame by frame through deterministic replay, so binding regressions surface immediately

Start with [CSharpGameDevelopment](docs/AGENT_GUIDE/CSharpGameDevelopment.md); the architecture is in [.NET Scripting Runtime](docs/designs/dotnet-scripting-design.md) and [Managed Game Launcher](docs/designs/managed-game-launcher-design.md).

### 4️⃣ Content Pipelines

- **glTF 2.0**: full import of scenes, materials, animation, and skeletal meshes, plus partial export of runtime content
- **LDraw**: `.ldr` / `.mpd` load straight into the runtime, with the official color table and LGEO realistic materials mapped onto engine PBR, and brick connectivity preserved as data building gameplay can use
- **OpenSCAD DSL**: a built-in parser and evaluator turn procedural scripts into renderable meshes — geometry via Manifold CSG, text via FreeType
- **ScadRig**: SCAD describes rigid-body bone hierarchies and animation clips that drive characters in the simulation prototypes
- **Gaussian Splat**: load PlayCanvas `.sog` directly and place it in the same runtime scene as meshes, materials, and cameras
- **Real-world geography**: `gnb geo` turns SRTM elevation and OpenStreetMap vectors into renderable, walkable `.scad` city levels — changing location means changing a latitude and longitude, and 1 km parts tile up into 3–5 km blocks

### 5️⃣ Tooling and Automated Validation

- **One CLI**: `gnb` covers dependency provisioning, builds, runs, tests, packaging, and the project website, with the same interface across desktop and mobile (`gnb android build/run` and `gnb ios build/run` deploy straight to a device)
- **Profiling**: Tracy 0.14.1 CPU/GPU zones (`gnb tracy fetch` pulls the matching GUI; Android connects over adb forwarding), with optional Superluminal CPU timeline and RenderDoc frame capture
- **Automated regression**: headless screenshots, input-script assertions, visual regression, and benchmark CSV reports — all CI-ready
- **Remote Play**: any desktop target can act as a WebRTC host, streaming to a zero-install browser client that routes keyboard, mouse, and virtual-gamepad input back; video uses Vulkan Video hardware encoding
- **Local workbench**: a graphical dashboard for todos, builds, runs, tests, and Git, plus a bundled llama.cpp inference service shared by the toolchain and the runtime

### 6️⃣ AI-Native Workflow

- **A parseable content foundation**: the SCAD, LDraw, glTF, and splat pipelines give AI readable, editable, verifiable 3D content rather than opaque static assets
- **A programmable runtime**: reflected components expose engine state to both the editor and the C# bindings
- **A machine-checkable loop**: screenshots, assertion scripts, replay parity, and benchmark reports close the "generate → run → validate → iterate" cycle
- **Local inference**: a bundled llama.cpp / Gemma OpenAI-compatible service serves both content generation and in-game AI decisions

> The first-party Engine core is deliberately held under 50k LOC, favoring explicit data flow and clear ownership over prematurely abstracting experiments into heavy frameworks.

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

> **Network prerequisite**: the build downloads dependencies, external toolchains and optional asset
> packs from GitHub and the vcpkg upstream. Make sure those hosts are reachable, or configure a vcpkg
> mirror, before running `gnb setup`.

The project uses CMake + Ninja, with dependencies managed through vcpkg. Beyond the host-side basics you must already have installed (compiler / IDE, CMake, platform SDKs), project-specific dependencies, external toolchains, and optional assets are prepared by `gnb`. You will need a network environment that can access GitHub during dependency setup.

### General Notes

- First run `./gnb.sh doctor` (Windows: `gnb.bat doctor`) to check host tool readiness
- `./gnb.sh setup` (Windows: `gnb.bat setup`) prepares vcpkg, external toolchains, and optional pak assets; running `./gnb.sh build` automatically fetches core toolchains on first build if missing
- Desktop platforms build and run through `gnb` directly; no `cd out/build/<platform>/bin` is needed
- Available CMake presets: `windows`, `linux`, `macos-arm64`, `ios`

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

Beyond host tools such as Visual Studio, remaining project dependencies are handled by `gnb`; the default workflow pulls project-versioned Vulkan SDK and Slang directly into the workspace. On Windows, `gnb` uses **Ninja** as the generator (automatically discovering MSVC/SDK environments), with NVIDIA Streamline (DLSS) enabled by default.

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

`gnb setup` automatically downloads the project-specified Vulkan SDK and Slang, removing the need to manually prepare these dependencies. If `VULKAN_SDK` is explicitly set, that environment SDK takes precedence.

</details>

### Running Examples

```shell
# Main renderer
./gnb.sh run gkNextRenderer

# Editor
./gnb.sh run gkNextEditor

# C# game launcher (load / unload / rebuild any managed game in-process)
./gnb.sh run gkNextLauncher

# TUI terminal mode (headless, frame streamed to the terminal)
./gnb.sh tui --scene assets/models/playground.glb

# Remote Play (browser-based WebRTC host)
./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720
```

### Mobile Platforms

```shell
./gnb.sh android build      # build the release APK
./gnb.sh android run        # install and launch (starts an AVD when no device is online)
./gnb.sh ios build
./gnb.sh ios run            # install and run on a paired physical device
```

### Creating a Game in C#

No C++, and no new CMake target:

1. Run `gnb run gkNextLauncher` and click the **New Project** card at the end of the grid (or **File > New Game Project...** in the editor)
2. Enter a project name, pick one of the five templates (blank, 2D arcade, top-down survivor, first-person explorer, third-person shooter), and tick Publish
3. Two things are generated and immediately playable: `assets/csharp/<ProjectName>/` and `assets/configs/games/<id>.game.json`
4. Run `gnb dotnet sln` so the new project joins `assets/csharp/GkNextManaged.sln`; after editing C#, hit **Rebuild C#** in the launcher or the editor — with hot reload on, the running game picks up the new assembly directly

See [Developing gkNextEngine Applications in C#](docs/AGENT_GUIDE/CSharpGameDevelopment.md).

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
      <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/flappyjs.webp" width="100%" style="display: block; width: 100%;" alt="FlappyCpp" />
      <div style="padding: 10px 8px 12px 8px;">
        <strong>🐤 FlappyCpp / FlappyCSharp</strong><br>
        <sub>Line-for-line C++ and C# implementations compared frame by frame for binding parity</sub>
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
- **`ScadLibrary`**: The unified authoring tool for SCAD assets — kit browsing, object-based scene composition, terrain rules, and character clip editing, with live agent-assisted iteration.
- **`gkNextLauncher`**: C# game launcher that selects, loads, and unloads any managed game in one process, scaffolds new projects from templates, and rebuilds C# from its menu (CoreCLR only).
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
- **`FlappyCpp` / `FlappyCSharp`**: Line-for-line C++ and C# Flappy Bird implementations serving as the deterministic replay parity baseline for the binding surface.
- **`Brotato3DCSharp`**: The C# implementation of Brotato3D, exercising the managed binding surface at full gameplay scale.
- **`NextWorldTravel`**: A real-world location browser with Walk / Aerial / Focus views over city tiles generated by `gnb geo`.
- **`TruckerDemo` / `CitySolSim` / `NextDayz` / `NextTotalWar`**: Vehicle driving, city traffic, survival tactics, and army simulation prototypes.

#### Benchmarks & Developer Utilities
- **`gkNextStillBenchmark`**: Static-scene frame-rate and image-quality benchmark.
- **`gkNextMotionBenchmark`**: Dynamic-camera / multi-scene rendering benchmark generating CSV profile reports.
- **`gkNextVisualTest`**: Automated visual regression testing generating scene comparison reports.
- **`gkNextUnitTests`**: Catch2 unit test suite.
- **`Packager`**: Asset packaging tool bundling scenes and textures into `.pkg` archives.

> Desktop targets can be launched via `./gnb remote --target <Target>` as Remote Play hosts (zero-install WebRTC browser play). `src/Application/Game/Voyage3D` is a sailing / port / naval-combat source prototype.

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
- Website and docs: <https://gameknife.github.io/gkNextEngine/> (source in `website/`, `gnb website` for local hot reload)
- Community: [GitHub Discussions](https://github.com/gameknife/gkNextEngine/discussions)
- If you are interested in real-time path tracing, modern rendering architecture, rendering performance optimization, the C# scripting layer, LDraw, editor tooling, AI-native workflows, or gameplay prototyping, feel free to reach out

<p align="center">✦</p>

## 📦 Third-Party Dependencies

tracy · cpptrace · cxxopts · sdl3 · vulkan-headers · vulkan-loader · vulkan-memory-allocator · glm · imgui · rmlui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · manifold · earcut-hpp · freetype · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · libdatachannel · cpp-httplib · libavif (optional)

<p align="center">✦</p>

## 📜 License

gkNextEngine is released under the [MIT License](LICENSE). Source code for third-party libraries is detailed in the third-party notices in [LICENSE](LICENSE).
