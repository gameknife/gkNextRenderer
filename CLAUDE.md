# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with this repository.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, and GPU-driven rendering.

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

**Optional flags:** `--avif`, `--dlss`, `--oidn`

## Run Commands

- Windows: `./run.bat --preset <preset>`
- macOS/Linux: `./run.sh --preset <preset>`
- Specific target: `./run.sh --preset default-macos-arm64 --target gkNextEditor`

## Testing

**CRITICAL: Tests must be run from the bin directory.**

```bash
# Unit tests (Catch2)
cd out/build/<preset>/bin && ./gkNextUnitTests

# Run specific test
cd out/build/<preset>/bin && ./gkNextUnitTests "[Unit][RenderComponent]"

# Visual tests
cd out/build/<preset>/bin && ./gkNextVisualTest
```

## Code Style (Summary)

- **First include:** `Common/CoreMinimal.hpp`
- **Platform abstraction:** Use `PlatformCommon.h`, not direct platform headers
- **Naming:**
  - Types/functions: PascalCase
  - Variables/parameters: camelCase
  - Private members: camelCase_ (trailing underscore)
  - Macros: UPPER_CASE
- **Braces:** Allman style (opening brace on new line)
- **Indentation:** 4 spaces, no tabs
- **Shaders:** Slang (`.vert.slang`, `.frag.slang`, `.rgen.slang`)

## Architecture Overview

- `src/Runtime/` - Core engine runtime
- `src/Runtime/Platform/` - Platform-specific code
- `src/Vulkan/` - Vulkan backend
- `src/Tests/` - Catch2 unit tests
- `assets/shaders/` - Slang shaders
- `assets/configs/` - Runtime configuration

## Key References

- **AGENTS.md** - Complete coding standards, build commands, and agent guidelines
- **AGENT_GUIDE/** - Layered documentation (core-patterns, contextual-rules, coding-standards, quick-commands)
- **README.en.md** - Project overview and quick start
