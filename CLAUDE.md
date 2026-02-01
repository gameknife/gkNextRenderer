# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, and GPU-driven rendering. Target codebase size is <50k LOC (currently ~15k).

**Subprojects:** gkNextRenderer (main renderer), gkNextEditor (ImGui editor), MagicaLego (voxel prototype), gkNextBenchmark, Packager

## Build Commands

**Dependencies (vcpkg):**
- Windows: `./vcpkg.bat`
- macOS/Linux: `./vcpkg.sh`

**Build:**
- Windows: `./build.bat --preset default-windows`
- macOS: `./build.sh --preset default-macos-arm64`
- Linux: `./build.sh --preset default-linux`
- Android: `./build.bat --android` (Windows) or `./build.sh --android`
- Clean rebuild: add `--clean`

**Presets:** `minimal-*` (fewest deps), `default-*` (standard), `full-*` (all features incl. DLSS/OIDN)

**List presets:** `cmake --list-presets=configure`

## Run Commands

- Windows: `./run.bat --preset <preset>`
- macOS/Linux: `./run.sh --preset <preset>`
- Specific target: `./run.sh --preset default-macos-arm64 --target gkNextEditor`

**Runtime success indicator:** Log shows `uploaded scene [...] to gpu`

## Testing

**CRITICAL: Tests must be run from the bin directory.**

```bash
# Unit tests (Catch2)
cd out/build/<preset>/bin && ./gkNextUnitTests

# Run specific test by name or tag
cd out/build/<preset>/bin && ./gkNextUnitTests "RenderComponent Usage"
cd out/build/<preset>/bin && ./gkNextUnitTests "[Unit][RenderComponent]"

# List available tests/tags
cd out/build/<preset>/bin && ./gkNextUnitTests --list-tests

# Visual tests (renders scenes, generates screenshots + report)
cd out/build/<preset>/bin && ./gkNextVisualTest
```

## Code Style (Summary)

- **First include:** `Common/CoreMinimal.hpp` (includes std, fmt, spdlog, platform detection)
- **Platform abstraction:** Use `PlatformCommon.h`, not direct platform headers; use `#if ANDROID` not `#ifdef`
- **Naming (enforced by .clang-tidy):**
  - Types/functions: PascalCase
  - Variables/parameters: camelCase
  - Private members: camelCase_ (trailing underscore)
  - Global variables: PascalCase (e.g., `GOption`)
  - Macros: UPPER_CASE
- **Braces:** Allman style (opening brace on new line)
- **Indentation:** 4 spaces, no tabs
- **Shaders:** Slang (`.vert.slang`, `.frag.slang`, `.rgen.slang`); uses ray query, not ray pipeline
- **Vulkan:** Always check VkResult with `VK_CHECK_RESULT`; RAII for resource cleanup

## Architecture Overview

```
src/
├── Runtime/           # Core engine runtime
│   ├── Platform/      # Platform abstraction (via PlatformCommon.h)
│   ├── Components/    # ECS components (entt)
│   ├── Reflection/    # Property reflection (entt::meta)
│   └── Command/       # Command history system
├── Vulkan/            # Vulkan backend
│   └── RayTracing/    # Hardware ray tracing
├── Rendering/         # Render pipelines (PathTracing, SoftwareTracing, SoftwareModern)
├── Editor/            # ImGui editor
├── Tests/             # Catch2 unit tests
└── Application/       # App entry points (gkNextRenderer, Packager, etc.)

assets/
├── shaders/           # Slang shaders
├── configs/           # Runtime config (visual_test.json)
└── models/            # glTF scenes
```

## Key References

- **AGENTS.md** - Complete coding standards, build commands, and agent guidelines
- **AGENT_GUIDE/** - Layered documentation (core-patterns, contextual-rules, coding-standards, quick-commands)
- **README.en.md** - Project overview and quick start
