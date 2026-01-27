# gkNextRenderer Agent Guide

This document is for agentic coding assistants working in this repo.
It consolidates build/test commands and the local coding standards.

Sources used to derive this guide:
- `AGENT_GUIDE/core-patterns.md`
- `AGENT_GUIDE/contextual-rules.md`
- `AGENT_GUIDE/coding-standards.md`
- `AGENT_GUIDE/quick-commands.md`
- `README.md`, `README.en.md`
- `.clang-tidy`
- `build.sh`, `build.ps1`, `run.sh`

No Cursor rules or Copilot rules were found in:
- `.cursor/rules/`
- `.cursorrules`
- `.github/copilot-instructions.md`

--------------------------------------------------------------------------------
Build, Run, Lint, Test
--------------------------------------------------------------------------------

List available CMake presets:
- `cmake --list-presets=configure`

Dependencies (vcpkg):
- Windows: `./vcpkg.bat`
- macOS/Linux: `./vcpkg.sh`

Build (native):
- Windows: `./build.bat --preset default-windows`
- macOS: `./build.sh --preset default-macos-arm64`
- Linux: `./build.sh --preset default-linux`
- Clean rebuild: add `--clean`

Build (Android):
- Windows: `./build.bat --android`
- macOS/Linux: `./build.sh --android`

Optional build flags (via CMake args):
- Example: `./build.sh --preset default-linux -- -DENABLE_AVIF=ON`
- Windows example: `./build.bat --preset default-windows -- -DENABLE_AVIF=ON`

Run (native):
- `./run.sh --preset <preset>`
- Windows: `./run.bat --preset <preset>`
- Example target: `./run.sh --preset default-macos-arm64 --target gkNextEditor`

Run (Android):
- `./run.sh --preset android`

Tests (Catch2):
IMPORTANT: run tests from the bin directory (CWD must be `bin`).
- Windows: `cd out/build/default-windows/bin && ./gkNextUnitTests.exe`
- macOS: `cd out/build/default-macos-arm64/bin && ./gkNextUnitTests`
- Linux: `cd out/build/default-linux/bin && ./gkNextUnitTests`

Run a single test or tag (Catch2 filter):
- `cd out/build/<preset>/bin && ./gkNextUnitTests "RenderComponent Usage"`
- `cd out/build/<preset>/bin && ./gkNextUnitTests "[Unit][RenderComponent]"`
- List tests: `./gkNextUnitTests --list-tests`
- List tags: `./gkNextUnitTests --list-tags`

Visual Tests (gkNextVisualTest):
Automated visual testing for rendering validation. Loads scenes from a JSON config,
renders each for a configurable number of frames, captures screenshots, and generates
a Markdown report. Suitable for CI integration.

- Config file: `assets/configs/visual_test.json`
- Run (from bin directory):
  - Windows: `cd out/build/default-windows/bin && ./gkNextVisualTest.exe`
  - macOS: `cd out/build/default-macos-arm64/bin && ./gkNextVisualTest`
  - Linux: `cd out/build/default-linux/bin && ./gkNextVisualTest`
- Output: `screenshots/visual_test/` (screenshots + `visual_test_report.md`)

Config format (visual_test.json):
```json
{
    "version": 1,
    "outputDir": "screenshots/visual_test",
    "defaultFramesToWait": 120,
    "scenes": [
        { "path": "assets/models/playground.glb", "frames": 120 },
        { "path": "assets/models/livingroom.glb", "frames": 150 }
    ]
}
```

Lint / static analysis:
- clang-tidy config: `.clang-tidy` (naming + include cleaner)
- Create compile database (if not already):
  - `./build.sh --preset <preset> -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Run clang-tidy (all files):
  - `python3 tools/clang-tools/run-clang-tidy.py -p out/build/<preset>`
- Run naming checks helper:
  - `BUILD_DIR=out/build/<preset> tools/clang-tools/run-naming.sh`

Build output locations:
- Desktop: `out/build/<preset>/bin/`
- Android: `android/app/build/outputs/apk/`

--------------------------------------------------------------------------------
Code Style and Architecture Rules
--------------------------------------------------------------------------------

Language and formatting:
- C++20 and C11; prefer modern C++ (RAII, smart pointers, range-based for).
- Indentation: 4 spaces, no tabs.
- Braces: Allman style (opening brace on the next line).
- Line length: keep under 120 columns where practical.

Naming (enforced by `.clang-tidy`):
- Types (class/struct/enum/typedef/namespace): PascalCase.
- Functions and methods: PascalCase.
- Variables/parameters/static locals: camelCase.
- Private members: camelCase with trailing underscore (e.g., `device_`).
- Global variables: PascalCase (e.g., `GOption`).
- Constants/constexpr: camelCase (e.g., `maxFrames`).
- Macros: UPPER_CASE (leading underscore macros allowed, e.g., `_USE_MATH_DEFINES`).

Includes and headers:
- New files should include `Common/CoreMinimal.hpp` first.
- Avoid including platform headers directly; use `PlatformCommon.h`.
- Avoid redundant standard headers if `CoreMinimal.hpp` already provides them.
- Keep includes minimal and avoid cyclic dependencies.

Platform rules:
- Use `PlatformCommon.h` for platform abstractions.
- Use `#if ANDROID` instead of `#ifdef ANDROID`.
- Guard platform-specific code with the correct platform macro.
- Use cross-platform paths (`std::filesystem` or engine wrappers).

Error handling and logging:
- Always check Vulkan `VkResult` (use `VK_CHECK_RESULT`).
- Handle failure paths: log with context (file, function, reason) and clean up.
- Avoid silent failures in init/resource loading paths.

Resources and memory:
- Follow RAII; pair allocations with deterministic cleanup.
- Vulkan objects must be destroyed in destructors or explicit cleanup routines.
- Prefer `std::unique_ptr`/`std::shared_ptr` over raw owning pointers.

Shaders:
- Use Slang for shaders.
- Use consistent extensions: `.vert.slang`, `.frag.slang`, `.rgen.slang`, etc.
- Avoid hard-coded constants; use uniforms/push constants.

Testing and verification expectations:
- After code changes, ensure the project builds for the target preset.
- Important rendering changes should be visually validated in `gkNextRenderer`.
- Runtime success indicator: log contains `uploaded scene [...] to gpu`.

Repository hygiene:
- Do not modify third-party code in `ThirdParty/` or `external/`.
- Do not commit build artifacts (`out/`, object files).
- Do not hard-code absolute paths or add secrets/keys.

--------------------------------------------------------------------------------
Project Layout (quick map)
--------------------------------------------------------------------------------

- `src/Runtime/` core runtime code
- `src/Runtime/Platform/` platform-specific code (via `PlatformCommon.h`)
- `src/Vulkan/` Vulkan backend
- `src/Tests/` Catch2 tests
- `assets/` shaders, scenes, runtime data

--------------------------------------------------------------------------------
Notes for Agents
--------------------------------------------------------------------------------

- Follow the naming rules strictly; `.clang-tidy` is the source of truth.
- Prefer scripted workflows (`build.sh`, `build.ps1`, `run.sh`) over manual CMake.
- If you add a new dependency, update `vcpkg.json` accordingly.
- If unsure about a preset name, use `cmake --list-presets=configure`.
- When creating commits, add an AI co-author line matching the model you used (e.g., `Co-authored-by: gpt-5.2-codex <gpt-5.2-codex@openai.com>`).
