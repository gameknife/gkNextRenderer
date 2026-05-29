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

gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. It is primarily focused on two goals:

- delivering visually compelling results with **real-time path tracing, hybrid rendering, and HDR lighting**
- building **runtime systems that are usable, extensible, and suitable for actual gameplay prototyping**, instead of stopping at isolated rendering demos

The project is renderer-first, but it keeps expanding around editor tooling, scripting, physics, asset import, and gameplay experiments. LDraw / BrickPlayer is one of the clearest examples of that direction: the engine can import LDraw models directly and bring those structured assets into a unified runtime, rendering, and interaction pipeline.

This project is especially relevant if you are interested in:

- seeing actual real-time results for path tracing, metal / glass / plastic materials, HDR environments, and dense scenes
- studying a Vulkan renderer that is closer to a real game runtime than an offline-style tech demo
- understanding how an engine ties together **rendering, editor tooling, scripting, physics, asset import, and gameplay prototyping**
- reading a codebase with controlled scope, clear engineering priorities, and a structure that is approachable for learning modern engine implementation

**Supported platforms:** Windows x86_64 · Linux x86_64 · macOS arm64 · Android arm64 · iOS arm64

---

## Project Highlights

- **Real-time path tracing and hybrid rendering**  
  The project keeps pushing on 1spp + temporal reuse, denoising, reprojection, and multi-pipeline switching so path tracing becomes part of a practical runtime workflow instead of a pure offline showcase.

- **Game-oriented GPU architecture**  
  With Visibility Buffer, fully bindless resources, GPU-driven rendering, and Multi-Draw Indirect, the design tries to spend CPU time on content and gameplay while keeping GPU budget focused on what actually improves the frame.

- **Engine systems that serve content and gameplay prototypes**  
  ECS, reflection, editor tooling, script hot reload, physics sync, runtime import, and stable rendering behavior all work together to support playable content rather than isolated subsystems.

- **Multi-format asset import and interoperability**  
  The engine supports full runtime glTF import with partial export, and can also import `.ldr` / `.mpd` directly so structured LDraw scenes become first-class runtime assets.

---

## Core Capabilities

### 1. High-quality rendering designed for runtime use

- **Real-time path tracing**: built around 1spp + temporal reuse with attention to image quality under actual runtime constraints
- **Hybrid rendering**: combines rasterization and ray tracing in a way that is practical for mobile targets and game-like workloads
- **Hot-swappable renderers**: the same scene and asset set can be switched across multiple pipelines for comparison and validation
- **HDR screenshots and high-quality media export**: useful for visual validation, presentation materials, and regression tracking

### 2. Full runtime and tooling support

- **ECS + reflection**: based on entt, with a reflection layer shared across runtime systems, editor workflows, and scripting bindings
- **ImGui editor**: `gkNextEditor` supports scene, material, and runtime-oriented editing workflows
- **QuickJS hot reload scripting**: makes gameplay logic, tools, and experiments much faster to iterate on
- **Jolt Physics**: provides a more realistic base for interaction prototypes, drag-and-drop workflows, and gameplay validation

### 3. Controlled codebase size for learning and extension

- **Target code size under 50k LOC**: the project is intentionally kept within a range that remains understandable and maintainable
- **Clarity over over-engineering**: favors explicit data flow, clear ownership, and mature third-party libraries over unnecessary abstraction
- **A good engine codebase to study**: from Vulkan rendering and resource management to scripting, editor integration, reflection, and testing

### 4. glTF and LDraw content pipelines

- **Full glTF import**: runtime support for scenes, materials, animation, and skeletal content
- **Partial glTF export**: selected runtime content can be written back into a glTF-oriented workflow
- **Direct LDraw runtime import**: `.ldr` / `.mpd` assets can be loaded directly into the engine runtime
- **Color and material mapping**: from `LDConfig.ldr` and LGEO realistic colors into engine-side PBR materials
- **Shadow / Connector abstraction**: not just raw mesh import, but a path toward converting brick connectivity semantics into data the building system can understand

---

## Technical Direction

### Rendering and GPU architecture

- **Visibility Buffer**
- **Fully Bindless + GPU-Driven**
- **Multi-Draw Indirect**
- **Hardware / Software Ray Tracing**
- **Temporal Reprojection / JBF / FSR / DLSS RR**

### Engine and tooling

- **Modern CMake Presets + vcpkg**
- **Cross-platform runtime: desktop / Android / iOS**
- **ImGui Editor + node-based material workflow**
- **QuickJS runtime scripting with bundled TypeScript hot reload via `tools/tsc/tsc[.exe]`, no Node/npm dependency**
- **Visual Test / Benchmark / Packager**

### AI Native

- **Built-in AI agent infrastructure with room for runtime LLM features**
- **Codex-native development for engine infrastructure and sample demos**
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

- Start with `./gnb doctor` (Windows: `gnb.bat doctor`) to see which host-side tools are still missing
- `./gnb setup` (Windows: `gnb.bat setup`) prepares vcpkg, project external toolchains, and optional pak assets; if you go straight to `./gnb build`, the first build will also bootstrap the core toolchain when needed
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
gnb.bat setup
gnb.bat build
gnb.bat run gkNextRenderer
```

Aside from host-side requirements such as Visual Studio, the rest of the project dependencies are usually prepared by `gnb`, including the pinned Vulkan SDK, Slang, and TypeScript toolchains.

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
- deployment notes from a real Steam Deck setup are available in [docs/steamdeck-deployment-notes.md](docs/steamdeck-deployment-notes.md)

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
gnb.bat setup --vcpkg-only
gnb.bat android
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
gnb.bat paks fetch
```

## Subprojects

| Project | Description |
|------|------|
| `gkNextRenderer` | Main renderer for path tracing / hybrid rendering / multi-pipeline comparison |
| `gkNextEditor` | ImGui editor for materials, scenes, and runtime-oriented tooling |
| `BrickPlayer` | Digital LEGO building prototype based on LDraw |
| `Brotato3D` | Top-down 3D survival shooter prototype, intro in [docs/projects/brotato-3d/introduction.md](docs/projects/brotato-3d/introduction.md) |
| `CharacterDemo` | Character control, AI behavior, navigation, and combat interaction experiments |
| `FlappyCpp` / `FlappyJs` | Flappy Bird dual implementation used to verify C++/QuickJS behavior parity, intro in [docs/projects/flappy-bird-parity/introduction.md](docs/projects/flappy-bird-parity/introduction.md) |
| `MagicaLego` | A lighter LEGO / voxel-style gameplay playground |
| `gkNextStillBenchmark` | Static-scene rendering benchmark |
| `gkNextMotionBenchmark` | Dynamic-scene rendering benchmark |
| `gkNextVisualTest` | Automated visual testing and screenshot reports |
| `Packager` | Packs assets into `.pkg` archives |

---

## References & Thanks

- [RayTracingInVulkan](https://github.com/GPSnoopy/RayTracingInVulkan)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)

---

## Contributing

Issues and PRs are welcome.

- See `AGENTS.md` for collaboration guidelines
- If you are interested in real-time path tracing, modern rendering architecture, LDraw, editor tooling, AI-native workflows, or gameplay prototyping, feel free to reach out

---

## Third-Party Dependencies

cpptrace · cxxopts · sdl3 · glm · imgui · stb · curl · nlohmann-json · tinygltf · draco · fmt · meshoptimizer · ktx · joltphysics · xxhash · spdlog · cpp-base64 · catch2 · entt · libwebp · vulkan-loader · libavif
