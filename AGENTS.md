# AGENTS.md

This file provides guidance to AI coding assistants (GitHub Copilot, Cursor, etc.) when working with code in this repository.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, and GPU-driven rendering. Target codebase size is <50k LOC (currently ~15k).

**Key Technologies:**
- C++20/C11, Vulkan API, Slang shader language
- ECS architecture (entt library)
- QuickJS TypeScript scripting with hot reload
- Multi-platform: Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64

**Subprojects:**
- gkNextRenderer (main renderer)
- gkNextEditor (ImGui editor with node-based material editor)
- MagicaLego (voxel building game with AI assistant)
- gkNextBenchmark
- gkNextVisualTest (automated visual testing)
- Packager (asset packaging to `.pkg`)

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
- List presets: `cmake --list-presets=configure`

**Presets:**
- `minimal-*`: Fewest dependencies (KTX2 only)
- `default-*`: Standard features (KTX2 + Physics + Audio)
- `full-*`: All features including DLSS/OIDN

**Optional Features (via build flags or CMake args):**
- `--avif`: AVIF texture loading and screenshots
- `--dlss`: NVIDIA DLSS support (Windows only, downloads Streamline SDK)
- `--oidn`: Intel OpenImageDenoise support (not on macOS, auto-downloads runtime)
- Example: `./build.bat --preset default-windows -- -DENABLE_AVIF=ON`

**Build output:** `out/build/<preset>/bin/`

## Run Commands

- Windows: `./run.bat --preset <preset>`
- macOS/Linux: `./run.sh --preset <preset>`
- Specific target: `./run.sh --preset default-macos-arm64 --target gkNextEditor`
- Android: `./run.sh --preset android`

**Runtime success indicator:** Log shows `uploaded scene [...] to gpu`

## Testing

**CRITICAL: Tests must be run from the bin directory (CWD must be `bin`).**

```bash
# Unit tests (Catch2)
cd out/build/<preset>/bin && ./gkNextUnitTests

# Run specific test by name or tag
cd out/build/<preset>/bin && ./gkNextUnitTests "RenderComponent Usage"
cd out/build/<preset>/bin && ./gkNextUnitTests "[Unit][RenderComponent]"

# List available tests/tags
cd out/build/<preset>/bin && ./gkNextUnitTests --list-tests
cd out/build/<preset>/bin && ./gkNextUnitTests --list-tags

# Visual tests (renders scenes, generates screenshots + report)
cd out/build/<preset>/bin && ./gkNextVisualTest
```

**Visual Test Config:** `assets/configs/visual_test.json` defines scenes, frame counts, output directory.

## Linting

**Static Analysis:**
- Config: `.clang-tidy` (naming + include cleaner)
- Generate compile database: `./build.sh --preset <preset> -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Run clang-tidy: `python3 tools/clang-tools/run-clang-tidy.py -p out/build/<preset>`
- Run naming checks: `BUILD_DIR=out/build/<preset> tools/clang-tools/run-naming.sh`

## Code Style (Summary)

**Naming (enforced by .clang-tidy):**
- Types/functions: PascalCase (e.g., `class RenderContext`, `void RenderFrame()`)
- Variables/parameters: camelCase (e.g., `int frameCounter`)
- Private members: camelCase_ (trailing underscore, e.g., `VkDevice device_`)
- Global variables: PascalCase (e.g., `GOption`)
- Constants/constexpr: camelCase (e.g., `constexpr int maxFrames`)
- Macros: UPPER_CASE (e.g., `VK_CHECK_RESULT`)

**Formatting:**
- Indentation: 4 spaces, no tabs
- Braces: Allman style (opening brace on new line)
- First include: `Common/CoreMinimal.hpp` (includes std, fmt, spdlog, platform detection)
- Platform abstraction: Use `PlatformCommon.h`, not direct platform headers; use `#if ANDROID` not `#ifdef`

**Shaders:**
- Use Slang (`.vert.slang`, `.frag.slang`, `.rgen.slang`, `.comp.slang`)
- Uses ray query API, not ray pipeline
- Avoid hard-coded constants; use uniforms/push constants

**Vulkan:**
- Always check VkResult with `VK_CHECK_RESULT`
- RAII for resource cleanup (pair allocations with destructors)
- Prefer `std::unique_ptr`/`std::shared_ptr` over raw owning pointers

## Architecture Overview

```
src/
├── Runtime/           # Core engine runtime
│   ├── Platform/      # Platform abstraction (via PlatformCommon.h)
│   ├── Components/    # ECS components (entt)
│   ├── Reflection/    # Property reflection (entt::meta) for editor + JS bindings
│   └── Command/       # Command history system (undo/redo)
├── Vulkan/            # Vulkan backend
│   └── RayTracing/    # Hardware ray tracing
├── Rendering/         # Render pipelines
│   ├── PathTracing/   # Full path tracing
│   ├── SoftwareTracing/  # Software ray tracing
│   ├── SoftwareModern/   # Modern rasterization + software GI
│   └── PipelineCommon/   # Shared pipeline utilities
├── Editor/            # ImGui editor
│   ├── Panels/        # Property panel (auto-generated from reflection)
│   ├── Nodes/         # Node-based material editor
│   └── Commands/      # Editor command system (undo/redo)
├── Assets/            # Asset loading (glTF, textures, etc.)
├── Tests/             # Catch2 unit tests
├── Application/       # App entry points
│   ├── gkNextRenderer/
│   ├── gkNextEditor/
│   ├── MagicaLego/
│   ├── gkNextBenchmark/
│   ├── gkNextVisualTest/
│   └── Packager/
└── ThirdParty/        # Third-party code (DO NOT MODIFY)

assets/
├── shaders/           # Slang shaders (.slang)
├── configs/           # Runtime config (visual_test.json, ai_config.json)
├── models/            # glTF scenes
└── typescript/        # TypeScript definitions for QuickJS scripting
```

## Key Architectural Patterns

**Reflection System (entt::meta):**
- Provides auto-generated editor UI via PropertyPanel
- Exposes component properties to QuickJS JavaScript bindings
- Supports undo/redo for property modifications
- See `AGENT_GUIDE/ReflectionSystem.md` for detailed documentation
- Register components using `REFLECT_COMPONENT` macro in component's .cpp file
- TypeScript definitions in `assets/typescript/Engine.d.ts` mirror reflected properties

**QuickJS Scripting:**
- Hot reload support (modify `.js` files at runtime)
- Components reflected via `entt::meta` are auto-exposed to JavaScript
- Global namespace: `Global.GetEngine()`, `Global.spdlog()`
- Scene API: `Scene.FindNodeIdWithComponent()`, `Scene.GetNodeById()`

**Component System:**
- ECS via entt library
- All components inherit from `Assets::Component`
- Must implement `GetMetaType()` for reflection support
- Common components: RenderComponent, PhysicsComponent, SkinnedMeshComponent

**Resource Management:**
- Vulkan objects use RAII (destroyed in destructors)
- Always pair allocations with deterministic cleanup
- No silent failures in init/resource loading paths

## Repository Hygiene

- DO NOT modify third-party code in `ThirdParty/` or `external/`
- DO NOT commit build artifacts (`out/`, object files)
- DO NOT hard-code absolute paths or add secrets/keys
- When adding dependencies, update `vcpkg.json`

## Verification After Changes

1. **Build:** Run build script for target preset
2. **Run:** Verify application starts and logs `uploaded scene [...] to gpu`
3. **Test:** Run unit tests if touching core systems
4. **Visual:** For rendering changes, validate visually in gkNextRenderer or run gkNextVisualTest

**Assistant Note:** Large refactors must include a full build and fix any compile errors before reporting completion.

## Key References

- **CLAUDE.md** - Claude Code specific guidance
- **GEMINI.md** - Gemini Code Assistant specific guidance
- **AGENT_GUIDE/** - Layered documentation:
  - `core-patterns.md` - Essential patterns and commands (Layer 1)
  - `contextual-rules.md` - Context-specific rules (Layer 2)
  - `coding-standards.md` - Detailed code review guidelines
  - `quick-commands.md` - Command reference (Layer 3)
  - `ReflectionSystem.md` - Reflection system documentation
  - `MagicaLego.md` - MagicaLego subproject notes
- **README.en.md** - Project overview and quick start
- **.clang-tidy** - Naming conventions (source of truth)
