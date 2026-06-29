# gkNextEngine

**A cross-platform 3D engine for real-time path tracing, gameplay prototyping, and high-end visual quality**

[English](README.en.md) | [简体中文](README.md)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/gameknife/gkNextEngine)
![Windows CI](https://github.com/gameknife/gkNextEngine/actions/workflows/windows_self.yml/badge.svg)
![Linux CI](https://github.com/gameknife/gkNextEngine/actions/workflows/linux_self.yml/badge.svg)
![macOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/macos_self.yml/badge.svg)
![Android CI](https://github.com/gameknife/gkNextEngine/actions/workflows/android_self.yml/badge.svg)
![iOS CI](https://github.com/gameknife/gkNextEngine/actions/workflows/ios.yml/badge.svg)

![Kitchen Scene](docs/gallery/4_playground.avif)

---

gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. Its goals have stayed the same:

- deliver visually compelling results with **real-time path tracing, hybrid rendering, and HDR lighting** that actually hold up inside a runtime, not just offline renders
- build **runtime systems that are usable, extensible, and suitable for actual gameplay prototyping**, instead of stopping at isolated rendering demos

The project is renderer-first, but it keeps expanding around editor tooling, scripting, physics, asset import, and gameplay experiments. **Recent work has clearly centered on rendering efficiency and image quality**: a world-space radiance cache (SHARC), AmbientCube memory reduction with hit-driven residency, RGB9E5 indirect-light encoding, atomic-contention treatment in GPU-driven culling, denoising, and reprojection stability — all aimed at producing the same frame with less VRAM and less GPU time. See [Performance & Rendering Efficiency](#performance--rendering-efficiency) below.

This project is especially relevant if you are interested in:

- seeing actual real-time results for path tracing, metal / glass / plastic materials, HDR environments, and dense scenes
- studying a Vulkan renderer that is genuinely **constrained by runtime performance**, rather than an offline-style tech demo
- understanding how an engine ties together **rendering, editor tooling, scripting, physics, asset import, and gameplay prototyping**
- reading a codebase with controlled scope, clear engineering priorities, and a structure that is approachable for learning modern engine implementation

**Supported platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## Project Highlights

- **Real-time path tracing and hybrid rendering**
  The project keeps pushing on 1spp + temporal reuse, denoising, reprojection, and multi-pipeline switching so path tracing becomes part of a practical runtime workflow instead of a pure offline showcase.

- **A performance-constrained rendering pipeline**
  Radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling all aim to produce more frame for a fixed GPU budget while keeping memory in check — rather than throwing unlimited resources at a single still.

- **Game-oriented GPU architecture**
  With Visibility Buffer, fully bindless resources, and single-draw GPU-driven submission, the design tries to spend CPU time on content and gameplay while keeping GPU budget focused on what actually improves the frame.

- **Engine systems that serve content and gameplay prototypes**
  ECS, reflection, editor tooling, script hot reload, physics sync, runtime import, and stable rendering behavior all work together to support playable content rather than isolated subsystems.

- **Multi-format asset import and interoperability**
  Full runtime glTF import with partial export, plus direct import of `.ldr` / `.mpd`, OpenSCAD `.scad` DSL, and PlayCanvas `.sog` Gaussian-splat assets, bringing structured scenes into a unified runtime, rendering, and interaction pipeline.

---

## Performance & Rendering Efficiency

Performance is one of the project's core constraints. The engine leans on radiance-cache reuse, sparse VRAM layouts, GPU-driven mass submission, on-demand residency, and multi-tier upscaling to produce more frame for a fixed GPU budget while keeping memory in check. Below is a set of **runtime performance references** for typical scenes, backed by a built-in per-pass profiler and a Superluminal integration for deeper analysis.

### Performance Reference Data

> ⚠️ The table below is a placeholder template. The numbers are to be filled in manually after measuring on uniform hardware / drivers — for now it only lists the scene + pipeline combinations.

| Scene | Resolution | Pipeline | GPU | Frame time (ms) | FPS | VRAM |
|------|------------|----------|-----|-----------------|-----|------|
| playground | 1920×1080 | PathTracing (1spp + temporal) | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| living room | 1920×1080 | PathTracing (1spp + temporal) | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| lego (LDraw) | 1920×1080 | SoftwareModern | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| luxball | 1920×1080 | SoftwareTracing | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| brickplayer | 1920×1080 | Hybrid | _TBD_ | _TBD_ | _TBD_ | _TBD_ |

> These figures can be reproduced on uniform hardware via `gkNextBenchmark` (static / dynamic scene benchmarks) and `gkNextVisualTest`.

### Built-in Profiler

The engine ships a CPU / GPU per-pass timing system: every render pass is annotated with a named scope, `VulkanGpuTimer` collects per-pass GPU-side timings, and an ImGui overlay (`ProfileDebugOverlay`) shows per-pass frame time and statistics live at runtime. You can locate rendering hotspots and compare the cost of different pipelines and settings without any external tooling.

### Superluminal Integration

On Windows, if the [Superluminal](https://superluminal.eu/) Performance API is installed (probed by default at `C:/Program Files/Superluminal/Performance/API`), the build automatically enables `WITH_SUPERLUMINAL` and forwards the engine's named CPU and GPU events to the Superluminal timeline (GPU events emitted from a dedicated replay thread), enabling fine-grained sampling profiles and cross-frame analysis. When it is not installed, the integration is skipped and the build is unaffected.

---

## Core Capabilities

### 1. High-quality rendering designed for runtime use

- **Real-time path tracing**: built around 1spp + temporal reuse with attention to image quality under actual runtime constraints
- **World-space radiance-cache GI**: SHARC world-space radiance cache as the indirect-light reuse layer, reused across frames and probes, on by default
- **Hybrid rendering**: combines rasterization and ray tracing in a way that is practical for mobile targets and game-like workloads
- **Hot-swappable renderers**: the same scene and asset set can be switched across path-tracing / software-tracing / Modern pipelines for comparison and validation
- **HDR screenshots and high-quality media export**: useful for visual validation, presentation materials, and regression tracking

### 2. Full runtime and tooling support

- **ECS + reflection**: based on entt, with a reflection layer shared across runtime systems, editor workflows, and scripting bindings
- **ImGui editor**: `gkNextEditor` supports scene, material, and runtime-oriented editing workflows, with progressive render iteration and data-driven settings / cvar panels
- **QuickJS hot reload scripting**: TypeScript compiled at runtime via the bundled `tools/tsc/tsc[.exe]` (`tsc.exe` on Windows, `tsc` on macOS/Linux), with no Node/npm or global `tsc` dependency; see [docs/guides/typescript-integration.md](docs/guides/typescript-integration.md)
- **Jolt Physics**: provides a more realistic base for interaction prototypes, drag-and-drop workflows, and gameplay validation
- **TUI terminal rendering**: `gnb tui` streams the final frame to the terminal as truecolor half-block characters for quick previews in headless environments; see [docs/guides/tui-mode.md](docs/guides/tui-mode.md)

### 3. Controlled codebase size for learning and extension

- **First-party engine code targeted under 50k LOC**: the engine core is intentionally kept understandable and maintainable (~85k LOC including all sample games + tests; browse the breakdown with `gnb loc`)
- **Clarity over over-engineering**: favors explicit data flow, clear ownership, and mature third-party libraries over unnecessary abstraction
- **A good engine codebase to study**: from Vulkan rendering and resource management to scripting, editor integration, reflection, and testing

### 4. glTF, LDraw, OpenSCAD, and Gaussian-splat content pipelines

- **Full glTF import**: runtime support for scenes, materials, animation, and skeletal content
- **Partial glTF export**: selected runtime content can be written back into a glTF-oriented workflow
- **Direct LDraw runtime import**: `.ldr` / `.mpd` load directly into the runtime, with full color/material mapping from `LDConfig.ldr` and LGEO realistic colors into engine PBR materials, and brick connectivity semantics converted into data the building system can understand
- **OpenSCAD DSL import**: parse / evaluate `.scad` directly — geometry through Manifold CSG, text through FreeType — turning procedural modeling scripts into renderable meshes; `ScadStudio` is built on this for modeling and character-rig experiments
- **Gaussian-splat assets**: load PlayCanvas `.sog` directly (packed ZIP or `meta.json` + `.webp`) and co-render with mesh scenes

---

## Technical Direction

### Rendering and GPU architecture

- **Visibility Buffer**
- **Fully Bindless + GPU-Driven**
- **Single-Draw GPU-Driven Submit (Soft Mesh Shader)**
- **Hardware / Software Ray Tracing (ray query)**
- **SHARC world radiance cache / RGB9E5 indirect light**
- **AmbientCube GI: sparse VRAM + hit-driven residency**
- **Denoising: ReLAX-style variance-guided / à-trous / JBF**
- **Temporal Reprojection / Sky Occlusion (GTAO)**
- **Upscaler: FSR / NVIDIA Streamline DLSS SR / RR / Frame Generation on Windows**
- **Gaussian Splatting (SOG v2 + hardware billboards)**

### Engine and tooling

- **Modern CMake Presets + vcpkg**
- **Cross-platform runtime: desktop / Android / iOS**
- **ImGui Editor + node-based material workflow**
- **QuickJS runtime scripting with bundled TypeScript hot reload via `tools/tsc/tsc[.exe]`, no Node/npm dependency**
- **TUI terminal rendering / Visual Test / Benchmark / Packager**
- **`gnb` project CLI (build / run / screenshot validation / dashboard / local LLM)**

### AI Native

- **Built-in AI agent infrastructure with room for runtime LLM features**
- **Codex / agentic-coding-native development for engine infrastructure and sample demos**
- **Moving away from low-code narratives toward a more direct agentic coding workflow**

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

The project uses CMake + Ninja, with dependencies managed through vcpkg. Beyond the host-side basics you must already have installed (compiler / IDE, CMake, platform SDKs, and similar tools), project-specific dependencies, external toolchains, and optional assets are now prepared by `gnb` whenever possible. You will need a network environment that can access GitHub during dependency setup.

### General Notes

- Start with `./gnb doctor` (Windows: `./gnb.bat doctor`) to see which host-side tools are still missing
- `./gnb setup` (Windows: `./gnb.bat setup`) prepares vcpkg, project external toolchains, and optional pak assets; if you go straight to `./gnb build`, the first build will also bootstrap the core toolchain when needed
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
./gnb.bat build
./gnb.bat run gkNextRenderer
```

Aside from host-side requirements such as Visual Studio, the rest of the project dependencies are usually prepared by `gnb`, including the pinned Vulkan SDK, Slang, and TypeScript toolchains. NVIDIA Streamline (DLSS) is enabled by default on Windows.

</details>

<details>
<summary><b>Linux (Ubuntu)</b></summary>

```shell
./gnb.sh setup
./gnb.sh build
./gnb.sh run gkNextRenderer
```

- On apt / pacman hosts, `gnb setup` and the first Linux `gnb build` automatically install the required desktop build packages before vcpkg bootstrap
- If automatic installation is unavailable, install them manually: `sudo apt install build-essential cmake ninja-build curl zip unzip tar pkg-config libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev wayland-protocols libxkbcommon-dev xorg-dev autoconf autoconf-archive automake libtool libsystemd-dev`
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
- on pacman hosts, `gnb setup` and the first Linux `gnb build` automatically install the required system packages before vcpkg bootstrap; if that is unavailable, run `sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libxrandr wayland-protocols libxkbcommon systemd-libs` manually
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

<details>
<summary><b>Android (Build on Windows)</b></summary>

**Prerequisites:** JDK 17+, Android SDK, NDK r27

```bat
set ANDROID_HOME=C:\Android\Sdk
set ANDROID_NDK_HOME=C:\Android\Sdk\ndk\27.0.12077973
./gnb.bat setup --vcpkg-only
./gnb.bat android
```

Android still depends on the host machine to provide JDK / SDK / NDK, while project-local vcpkg dependencies and external toolchains continue to be prepared by `gnb`.

</details>

### Run Examples

```shell
# Main renderer
./gnb.sh run gkNextRenderer

# Editor
./gnb.sh run gkNextEditor

# BrickPlayer (digital LEGO / LDraw building prototype)
./gnb.sh run BrickPlayer

# CharacterDemo (character control / AI / navigation experiment)
./gnb.sh run CharacterDemo

# TUI terminal mode (headless, frame streamed to the terminal)
./gnb.sh tui --scene assets/models/playground.glb
```

### Optional Assets

Some larger binary assets are not committed to the repo. Fetch them as needed:

| Selector | Contents | Lands at | If missing |
|------|------|------|------|
| `ldraw` | `ldraw.pak` | `assets/paks/` | BrickPlayer loses its LDraw parts library |
| `optional` | `optional.pak` | `assets/paks/` | Main renderer / Editor / CharacterDemo / MagicaLego lose their scene assets |
| `sfx` | six mp3/wav files | `assets/sfx/` | MagicaLego / BrickPlayer go silent |
| `ffmpeg` | `ffmpeg.exe` | `src/ThirdParty/ffmpeg/bin/` | Windows MagicaLego video capture disabled |

```bash
# Linux / macOS / Git Bash: fetch every optional asset by default
./gnb.sh paks fetch

# Or fetch specific groups
./gnb.sh paks fetch optional ldraw
./gnb.sh paks fetch ffmpeg sfx

# Windows
./gnb.bat paks fetch
```

## Subprojects

| Project | Description |
|------|------|
| `gkNextRenderer` | Main renderer for path tracing / hybrid rendering / multi-pipeline comparison |
| `gkNextEditor` | ImGui editor for materials, scenes, and runtime-oriented tooling |
| `ScadStudio` | OpenSCAD (`.scad`) DSL modeling / character-rig experiment editor |
| `BrickPlayer` | Digital LEGO building prototype based on LDraw |
| `MagicaLego` | A lighter LEGO / voxel-style gameplay playground |
| `Brotato3D` | Top-down 3D survival shooter prototype, intro in [docs/projects/brotato-3d/introduction.md](docs/projects/brotato-3d/introduction.md) |
| `CharacterDemo` | Character control, AI behavior, navigation, and combat interaction experiments |
| `FlappyCpp` / `FlappyJs` | Flappy Bird dual implementation used to verify C++/QuickJS behavior parity, intro in [docs/projects/flappy-bird-parity/introduction.md](docs/projects/flappy-bird-parity/introduction.md) |
| `gkNextBenchmark` | Static / dynamic scene rendering benchmarks |
| `gkNextVisualTest` | Automated visual testing and screenshot reports |
| `Packager` | Packs assets into `.pkg` archives |

> The repo also hosts several early-stage gameplay / simulation prototypes (AirportSim, StudioSim, Voyage3D, KongLie3D, and others) that mainly drive engine evolution; their interfaces are not yet stable.

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
