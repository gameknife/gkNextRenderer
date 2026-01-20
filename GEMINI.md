# Gemini Code Assistant Context

This document provides context for the Gemini code assistant to understand the `gkNextRenderer` project.

## Project Overview

`gkNextRenderer` is a cross-platform 3D game engine written in modern C++. It focuses on modern rendering techniques and game technology practices. The engine supports Windows, Linux, macOS, Android, and iOS.

The project is structured as a collection of libraries and executables. The main library is `gkNextEngine`. Executables include:
*   `gkNextRenderer`: The main renderer (Path Tracing / Hybrid).
*   `gkNextEditor`: ImGui-based scene editor.
*   `gkNextUnitTests`: Unit and Integration tests using Catch2.

## Technologies

*   **Language:** C++20
*   **Rendering:** Vulkan, Slang (Shaders)
*   **Build:** CMake, vcpkg
*   **Core Deps:** SDL3, ImGui, Jolt Physics, Catch2, glTF 2.0

## 🤖 Agent Guide (单一事实来源)

请根据任务类型查阅 `AGENT_GUIDE/` 目录下的具体指引：

| 任务类型 | 查阅文件 | 说明 |
| :--- | :--- | :--- |
| **核心架构 & 常用命令** | [Layer 1: Core Patterns](AGENT_GUIDE/core-patterns.md) | **必读**。包含架构原则、构建、**单元测试**核心流程。 |
| **详细开发规则** | [Layer 2: Contextual Rules](AGENT_GUIDE/contextual-rules.md) | 开发新功能、理解构建系统细节、测试规范。 |
| **代码规范细节** | [Coding Standards](AGENT_GUIDE/coding-standards.md) | 命名约定、代码风格、C++ 特性使用详情。 |
| **命令速查 & 排错** | [Layer 3: Quick Commands](AGENT_GUIDE/quick-commands.md) | 紧急修复、复杂构建命令、故障排查。 |

## Development Conventions

*   **Language Preference:** All interactions should be conducted in **Chinese (中文)**.
*   **Coding Style:** Enforced by `.clang-format`. See `AGENT_GUIDE/coding-standards.md` for details.
*   **Testing:** Tests are located in `src/Tests`. **Crucial:** Unit tests must be run with the working directory set to the binary's location.

## Quick Start

```bash
# Install Dependencies
./vcpkg.bat    # Windows
./vcpkg.sh     # macOS/Linux

# Build (Auto-detects platform)
./build.bat    # Windows
./build.sh     # macOS/Linux
```

For more detailed commands (Android, specific presets, running tests), refer to `AGENT_GUIDE/quick-commands.md`.
