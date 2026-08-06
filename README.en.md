# gkNextEngine

**A cross-platform 3D engine for real-time path tracing, gameplay prototyping, and high-end visual quality**

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Desktop CI](https://github.com/gameknife/gkNextEngine/actions/workflows/desktop.yml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/confroom.webp" width="49%" alt="Conference Room" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/luxball.webp" width="49%" alt="Luxball" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/playground.webp" width="49%" alt="Playground" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp" width="49%" alt="Still" />
</p>


---

gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. Its goals have stayed the same:

- deliver visually compelling results with **real-time path tracing, hybrid rendering, and HDR lighting** that actually hold up inside a runtime, not just offline renders
- build **runtime systems that are usable, extensible, and suitable for gameplay prototyping and AI-native workflows**, instead of stopping at isolated rendering demos

The project is renderer-first, but it keeps expanding around editor tooling, scripting, physics, asset import, and multiple gameplay prototypes. Current prototypes such as MagicaLego, Brotato3D, KongLie3D, BrickPlayer, CharacterDemo, Flappy, and others, together with structured content pipelines such as SCAD, LDraw, and Gaussian Splat, are not isolated features; they are groundwork for AI-native content generation, scene understanding, gameplay iteration, and automated validation.

This project is especially relevant if you are interested in:

- seeing actual real-time results for path tracing, metal / glass / plastic materials, HDR environments, and dense scenes
- studying a Vulkan renderer that is genuinely **constrained by runtime performance**, rather than an offline-style tech demo
- understanding how an engine ties together **rendering, editor tooling, scripting, physics, asset import, and gameplay prototyping**
- reading a codebase with controlled scope, clear engineering priorities, and a structure that is approachable for learning modern engine implementation

**Supported platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## Project Highlights

- **Real-time path tracing and hybrid rendering**
  The project keeps pushing on 1/2spp + temporal reuse, denoising, reprojection, and multi-pipeline switching so path tracing becomes part of a practical runtime workflow instead of a pure offline showcase.

- **A performance-constrained rendering pipeline**
  Radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling all aim to produce more frame for a fixed GPU budget while keeping memory in check — rather than throwing unlimited resources at a single still.

- **Game-oriented GPU architecture**
  With Visibility Buffer, fully bindless resources, and single-draw GPU-driven submission, the design tries to spend CPU time on content and gameplay while keeping GPU budget focused on what actually improves the frame.

- **Engine systems that serve content and gameplay prototypes**
  ECS, reflection, editor tooling, script hot reload, physics sync, runtime import, and stable rendering behavior all work together to support playable content rather than isolated subsystems.

- **Infrastructure for AI-native workflows**
  Multiple game prototypes validate gameplay loops, input, physics, scripting, and rendering under real runtime conditions. SCAD, LDraw, and Gaussian Splat import give AI systems structured 3D content that can be generated, modified, understood, and validated instead of only producing static assets.

- **Multi-format asset import and interoperability**
  Full runtime glTF import with partial export, plus direct import of LDraw `.ldr` / `.mpd`, OpenSCAD `.scad` DSL, and PlayCanvas `.sog` Gaussian-splat assets, bringing structured scenes into a unified runtime, rendering, and interaction pipeline.

---

## Performance & Rendering Efficiency

Performance is one of the project's core constraints. The engine leans on radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling to produce more frame for a fixed GPU budget while keeping memory in check. Below is a set of **runtime performance references** for typical scenes, backed by a built-in per-pass profiler and a Superluminal integration for deeper analysis.

### Performance Reference Data

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

### Built-in Profiler

The engine ships a CPU / GPU per-pass timing system: every render pass is annotated with a named scope, `VulkanGpuTimer` collects per-pass GPU-side timings, and an ImGui overlay (`ProfileDebugOverlay`) shows per-pass frame time and statistics live at runtime. You can locate rendering hotspots and compare the cost of different pipelines and settings without any external tooling.

### Superluminal Integration

On Windows, if the [Superluminal](https://superluminal.eu/) Performance API is installed (probed by default at `C:/Program Files/Superluminal/Performance/API`), the build automatically enables `WITH_SUPERLUMINAL` and forwards the engine's named CPU and GPU events to the Superluminal timeline (GPU events emitted from a dedicated replay thread), enabling fine-grained sampling profiles and cross-frame analysis. When it is not installed, the integration is skipped and the build is unaffected.

---

## Core Capabilities

### 1. High-quality rendering designed for runtime use

- **Real-time path tracing and hybrid rendering**: built around 1/2spp + temporal reuse, denoising, reprojection, and multi-pipeline switching so path tracing remains usable under real runtime constraints
- **Modern GPU raster pipeline**: Visibility Buffer, fully bindless resources, single-draw GPU-driven submission, Soft Mesh Shader, and GPU CSM shadows support dense scenes and game-like workloads
- **Hot-swappable renderers**: the same scene and asset set can switch across PathTracing, SoftwareTracing, SoftwareModern / NoAmbient, and related pipelines for image-quality, performance, and platform comparisons
- **GI, denoising, and upscaling**: SHARC world radiance cache, ReSTIR DI, as well as FSR / DLSS / DLSS RR / Native TAAU / SGSR2
- **Gaussian Splat co-rendering**: PlayCanvas SOG v2 Gaussian Splatting runs through a hardware-billboard path and can coexist with mesh scenes

### 2. Runtime, editor, and validation tooling

- **ECS + reflection**: based on entt, with a reflection layer shared across runtime systems, editor property panels, undo / redo, and QuickJS bindings
- **ImGui editor and material workflow**: `gkNextEditor` supports scene, material, and runtime-oriented editing, with data-driven settings, cvar panels, and a node-based material workflow
- **QuickJS + TypeScript hot reload**: TypeScript is compiled at runtime via the bundled `tools/tsc/tsc[.exe]`, with no Node/npm or global `tsc` dependency; see [docs/guides/typescript-integration.md](docs/guides/typescript-integration.md)
- **Jolt Physics and interactive runtime support**: provides a practical physics base for dragging, collision, character movement, playable prototypes, and automated scene validation
- **Agent validation tools**: `gnb shot` captures hidden-window validation screenshots, while `gnb validate` supports input-driven scripts, assertions, and JSON reports for rendering, UI, and gameplay-state regression checks
- **Profiler / benchmark / TUI**: built-in CPU / GPU pass profiling, `gkNextMotionBenchmark` CSV performance reports, `gkNextVisualTest` visual regression, and `gnb tui` terminal rendering previews
- **Remote Play mode**: `gnb remote` / `--remote` can run any desktop target as a WebRTC host, stream the frame to a zero-install browser client, and route keyboard, mouse, and virtual-gamepad input back into the runtime; video uses Vulkan Video H.264 hardware encoding
- **gnb dashboard and local LLM**: `gnb dashboard` provides local TODO, Build, Run, Test, Git, Chat, and LOC workflows; `gnb llm` integrates llama.cpp / Gemma as a local OpenAI-compatible service for tooling and runtime AI features

### 3. AI-native workflows and gameplay prototypes

- **Multiple prototypes validate real requirements**: MagicaLego, BrickPlayer, Brotato3D, KongLie3D, CharacterDemo, Flappy, AirportSim, StudioSim, NextRA, and the Voyage3D source prototype exercise building, action, physics, scripting, UI, combat, simulation, and AI interaction scenarios
- **Structured content for AI generation**: SCAD, LDraw, Gaussian Splat, and glTF pipelines give AI systems parseable, editable, and verifiable 3D content instead of only uncontrolled static assets
- **AI-assisted gameplay iteration**: local LLM support, QuickJS scripting, reflected components, agent validation, and the dashboard form a loop for "generate content -> run validation -> iterate"
- **Script parity and deterministic validation**: Flappy C++ / JS parity, input scripts, hidden-window screenshots, and benchmark reports help constrain behavioral regressions after AI-assisted changes

### 4. glTF, LDraw, OpenSCAD, and Gaussian-splat content pipelines

- **Full glTF import / partial export**: runtime support for scenes, materials, animation, and skeletal content, with selected runtime content exportable back into a glTF-oriented workflow
- **Direct LDraw runtime import**: `.ldr` / `.mpd` load directly into the runtime, with full color/material mapping from `LDConfig.ldr` and LGEO realistic colors into engine PBR materials, plus brick connectivity semantics converted into data the building system can understand
- **OpenSCAD DSL and ScadStudio**: parse / evaluate `.scad` directly, with geometry through Manifold CSG and text through FreeType; `ScadStudio` builds on this for modeling, scene generation, and character-rig experiments
- **ScadRig rigid-body characters**: SCAD files can describe rigid-body bone hierarchies and animation clips, currently used in the AirportSim / StudioSim direction for character visualization and role-based coloring experiments
- **Gaussian Splat assets**: load PlayCanvas `.sog` directly (packed ZIP or `meta.json` + `.webp`) and co-render with mesh, material, camera, and runtime scene data

### 5. Controlled codebase size for learning and extension

- **First-party Engine core targeted under 50k LOC**: the core stays intentionally understandable and maintainable
- **Clarity over over-engineering**: favors explicit data flow, clear ownership, and mature third-party libraries, without turning experimental systems into heavy frameworks too early
- **A good engine codebase to study**: from Vulkan rendering, resource management, scripting, editor integration, reflection, and content import to testing, benchmarking, and agent validation

---

## Visual Preview

![BrickPlayer Gameplay](docs/gallery/6_debug_draw.avif)

<details>
<summary><b>Sample Screenshots</b></summary>

| Scene | Screenshot |
|------|------|
| still | ![still](docs/gallery/1_still.avif) |
| livingroom | ![livingroom](docs/gallery/2_living_room.avif) |
| ldrawlego | ![ldrawlego](docs/gallery/3_lego_ldraw.avif) |
| luxball | ![luxball](docs/gallery/5_luxball.avif) |
| brickplayer | ![brickplayer](docs/gallery/7_brick_player.avif) |

</details>

---

## Quick Start

For developers in mainland China, please ensure your network stability first. Recommended tools:

[Link with referral code](https://nxonearth.com/signupbyemail.aspx?MemberCode=93e1edc92a95412dbc7ff38c8288951920240913095147)
[Link without referral code](https://nxonearth.com/signupbyemail.aspx)

The project uses CMake + Ninja, with dependencies managed through vcpkg. Beyond the host-side basics you must already have installed (compiler / IDE, CMake, platform SDKs, and similar tools), project-specific dependencies, external toolchains, and optional assets are now prepared by `gnb` whenever possible. You will need a network environment that can access GitHub during dependency setup.

### General Notes

- Start with `./gnb.sh doctor` (Windows: `gnb.bat doctor`) to see which host-side tools are still missing
- `./gnb.sh setup` (Windows: `gnb.bat setup`) prepares vcpkg, project external toolchains, and optional pak assets; if you go straight to `./gnb.sh build`, the first build will also bootstrap the core toolchain when needed
- Desktop binaries are now built and launched through `gnb`, so you usually no longer need to `cd` into `out/build/<platform>/bin`
- CMake presets are now: `windows`, `linux`, `macos-arm64`, `ios`

### Platform Builds

<details>
<summary><b>Windows (Visual Studio 2022)</b></summary>

**Prerequisites:**

- CMake 3.26+
- Visual Studio 2022 with C++ workload
- Vulkan SDK 1.4.341.1 (downloaded into the repository by default; if `VULKAN_SDK` is set, that SDK is used first)
- Enable "Use Unicode UTF-8 for worldwide language support"

```bat
./gnb.bat setup
./gnb.bat build        # Builds core targets by default (gkNextRenderer + gkNextUnitTests)
./gnb.bat build --all  # Full build of all 15+ subprojects
./gnb.bat run gkNextRenderer
```

Aside from host-side requirements such as Visual Studio, the rest of the project dependencies are usually prepared by `gnb`, including the pinned Vulkan SDK, Slang, and TypeScript toolchains. `gnb` defaults to the **Ninja** generator on Windows (with automatic MSVC & SDK environment resolution). NVIDIA Streamline (DLSS) is enabled by default on Windows.

</details>

<details>
<summary><b>Linux (Ubuntu)</b></summary>

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

- On apt / pacman hosts, `gnb setup` and the first Linux `gnb build` automatically install the required desktop build packages before vcpkg bootstrap
- If automatic installation is unavailable, install them manually: `sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev`
- Non apt/pacman distributions still stop with an explicit missing desktop dependency hint

</details>

<details>
<summary><b>Steam Deck / Arch Linux</b></summary>

```shell
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

Notes:

- if no usable `VULKAN_SDK` is available, `gnb setup` automatically downloads the pinned LunarG Vulkan SDK into `external/VulkanSDK/`
- if `slangc` is not installed yet, `gnb setup` automatically fetches the project-managed Slang toolchain into `external/`
- on pacman hosts, `gnb setup` and the first Linux `gnb build` automatically install the required system packages before vcpkg bootstrap; if that is unavailable, run `sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libx11 libxft libxext libxi libxinerama libxcursor libxrandr wayland-protocols libxkbcommon` manually
- if a GitHub archive download fails during vcpkg setup, rerun the same build command once before doing deeper troubleshooting
- deployment notes from a real Steam Deck setup are available in [docs/notes/steamdeck-deployment-notes.md](docs/notes/steamdeck-deployment-notes.md)

</details>

<details>
<summary><b>macOS</b></summary>

**Prerequisites:**

- Xcode / Command Line Tools
- CMake 3.26+
- Ninja (if your local CMake distribution does not already provide it)

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

`gnb setup` automatically downloads the Vulkan SDK, Slang, and TypeScript toolchains used by the project, so those project-level dependencies no longer need to be installed separately. If `VULKAN_SDK` is explicitly set, that SDK takes precedence.

</details>

### Run Examples

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

## Subprojects

<p align="center">
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/airportsim.webp" width="32%" alt="AirportSim" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp" width="32%" alt="BrickPlayer" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp" width="32%" alt="Brotato3D" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/citysim.webp" width="32%" alt="CitySim" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/flappyjs.webp" width="32%" alt="FlappyJs" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp" width="32%" alt="MagicaLego" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp" width="32%" alt="NextDayZ" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nexttotalwar.webp" width="32%" alt="NextTotalWar" />
  <img src="https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/procscad.webp" width="32%" alt="ProcScad" />
</p>

| Project | Description |
|------|------|
| `gkNextRenderer` | Main renderer for path tracing / hybrid rendering / multi-pipeline comparison |
| `gkNextStillBenchmark` | Static-scene rendering benchmark |
| `gkNextMotionBenchmark` | Dynamic-camera / multi-scene rendering benchmark with CSV profile reports |
| `gkNextVisualTest` | Automated visual testing and screenshot reports |
| `RmlUiDemo` | RmlUi runtime UI integration and interaction demo |
| `gkNextEditor` | ImGui editor for materials, scenes, and runtime-oriented tooling |
| `ScadStudio` | OpenSCAD (`.scad`) DSL modeling / character-rig experiment editor |
| `BrickPlayer` | Digital LEGO building prototype based on LDraw |
| `MagicaLego` | A lighter LEGO / voxel-style gameplay playground |
| `Brotato3D` | Top-down 3D survival shooter prototype, intro in [docs/projects/brotato-3d/introduction.md](docs/projects/brotato-3d/introduction.md) |
| `KongLie3D` | Board deployment / synergy / round-combat prototype |
| `NextRA` | Deterministic RTS / lockstep / replay prototype |
| `CharacterDemo` | Character control, AI behavior, navigation, and combat interaction experiments |
| `AirportSim` | Airport ecosystem simulation for SCAD POIs, queues, pathfinding, LLM decisions, and ScadRig characters |
| `StudioSim` | Studio-management simulation for local LLM events, employee goals, SCAD offices, and ScadRig presentation |
| `FlappyCpp` / `FlappyJs` | Flappy Bird dual implementation used to verify C++/QuickJS behavior parity, intro in [docs/projects/flappy-bird-parity/introduction.md](docs/projects/flappy-bird-parity/introduction.md) |
| `gkNextUnitTests` | Catch2 unit tests |
| `Packager` | Packs assets into `.pkg` archives |

> Desktop targets can usually be launched through `gnb remote --target <Target>` as Remote Play hosts. The mode currently targets Windows / Linux desktop systems with Vulkan Video H.264 support. `src/Application/Game/Voyage3D` remains in the tree as a sailing trade / port / naval-combat source prototype, but it is not currently exposed as a standalone CMake target.

---

## References & Thanks

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

---

## Contributing

Issues and PRs are welcome.

- See `AGENTS.md` for collaboration guidelines
- If you are interested in real-time path tracing, modern rendering architecture, rendering performance optimization, LDraw, editor tooling, AI-native workflows, or gameplay prototyping, feel free to reach out

---

## Third-Party Dependencies

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif

---

## License

gkNextEngine is released under the [MIT License](LICENSE). Source code for third-party libraries is detailed in the third-party notices in [LICENSE](LICENSE).
