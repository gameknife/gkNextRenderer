# AGENTS.md

This file is the single source of truth for AI coding assistants (Claude Code, Gemini CLI, Codex, GitHub Copilot, Cursor, etc.) working in this repository. `CLAUDE.md` imports it via `@AGENTS.md`; other tools read `AGENTS.md` directly.

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

**Build (vcpkg is auto-bootstrapped on first run):**
- Setup once: `./gnb setup` (Windows: `gnb.bat setup`)
- Desktop build: `./gnb build` (Windows: `gnb.bat build`)
- Specific target: `./gnb build gkNextEditor`
- Android: `./gnb android`
- Clean rebuild: `./gnb build --clean`
- Force vcpkg update: `./gnb setup --refresh`

**CMake presets:** `windows`, `linux`, `macos-arm64`, `ios`.

**Optional Features:**
- AVIF is manual: `cmake --preset windows -DENABLE_AVIF=ON -DVCPKG_MANIFEST_FEATURES=avif` then `./gnb build`
- DLSS/Streamline is always enabled on Windows and disabled elsewhere
- OIDN and MinGW support have been removed

**Build output:** `out/build/<platform>/bin/`

## Run Commands

- Default target: `./gnb run`
- Specific target: `./gnb run gkNextEditor`
- Editor shortcut: `./gnb editor`
- Android: `./gnb android`

Desktop binaries can now be launched from any working directory; no `cd out/build/<preset>/bin` is required.

**Runtime success indicator:** Log shows `uploaded scene [...] to gpu`

## Testing

Tests no longer require the current working directory to be `bin`; launch them via their executable path.

```bash
# Unit tests (Catch2)
./out/build/<preset>/bin/gkNextUnitTests

# Run specific test by name or tag
./out/build/<preset>/bin/gkNextUnitTests "RenderComponent Usage"
./out/build/<preset>/bin/gkNextUnitTests "[Unit][RenderComponent]"

# List available tests/tags
./out/build/<preset>/bin/gkNextUnitTests --list-tests
./out/build/<preset>/bin/gkNextUnitTests --list-tags

# Visual tests (renders scenes, generates screenshots + report)
./out/build/<preset>/bin/gkNextVisualTest
```

**Visual Test Config:** `assets/configs/visual_test.json` defines scenes, frame counts, output directory.

## Linting

**Static Analysis:**
- Config: `.clang-tidy` (naming + include cleaner)
- Generate compile database: `cmake --preset <platform> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Run clang-tidy: `python3 tools/clang-tools/run-clang-tidy.py -p out/build/<platform>`
- Run naming checks: `BUILD_DIR=out/build/<platform> tools/clang-tools/run-naming.sh`

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
- TypeScript hot reload support via bundled `tools/tsc/tsc[.exe]` (`tsc.exe` on Windows, `tsc` on macOS/Linux); no Node/npm/global `tsc` dependency is required at runtime
- ES module loading supports compiled TypeScript relative imports under `assets/scripts`
- Components reflected via `entt::meta` are auto-exposed to JavaScript
- Global namespace: `Global.GetEngine()`, `Global.GetScene()`, `Global.spdlog()`
- Scripted games should extend `assets/typescript/NextGameInstanceBase.ts` and call `RunGameInstance(new YourGameInstance())`
- Scene API: `Scene.FindNodeIdWithComponent()`, `Scene.GetNodeById()`, `SceneBuild.*` for rebuild-time procedural scene construction, `Scene.AddRenderNode()` for runtime nodes
- See `AGENT_GUIDE/QuickJSBindings.md`; `FlappyCpp` / `FlappyJs` replay parity is the binding regression demo

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

1. **Build:** For AI assistant verification, run the platform default through gnb:
   - macOS/Linux: `./gnb build --reconfigure`
   - Windows: `gnb.bat build --reconfigure`
   - If only one target needs verification, pass it as `./gnb build <target>`
2. **Run:** Verify application starts and logs `uploaded scene [...] to gpu`
3. **Test:** Run unit tests if touching core systems
4. **Visual:** For rendering changes, validate visually in gkNextRenderer or run gkNextVisualTest

**Assistant Note:** Large refactors must include a full `gnb build --reconfigure` and fix any compile errors before reporting completion.

## Key References

- **AGENT_GUIDE/** - Layered documentation:
  - `core-patterns.md` - Essential patterns and commands (Layer 1)
  - `contextual-rules.md` - Context-specific rules (Layer 2)
  - `coding-standards.md` - Detailed code review guidelines
  - `quick-commands.md` - Command reference (Layer 3)
  - `ReflectionSystem.md` - Reflection system documentation
  - `PrefabSceneWorkflow.md` - KayKit procedural scene prefab workflow and review rules
  - `MagicaLego.md` - MagicaLego subproject notes
- **README.en.md** - Project overview and quick start
- **.clang-tidy** - Naming conventions (source of truth)
