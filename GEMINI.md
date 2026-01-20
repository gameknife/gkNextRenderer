# Gemini 代码助手上下文 (Gemini Code Assistant Context)

本文档为 Gemini 代码助手提供 `gkNextRenderer` 项目的核心上下文。

## 项目概览

`gkNextRenderer` 是一个使用现代 C++ 编写的跨平台 3D 游戏引擎，专注于现代渲染技术和游戏技术实践。引擎支持 Windows, Linux, macOS, Android 和 iOS。

项目结构由一系列库和可执行文件组成。核心库为 `gkNextEngine`。主要可执行文件包括：
*   `gkNextRenderer`: 主渲染器，支持路径追踪 (Path Tracing) 和混合渲染。
*   `gkNextEditor`: 基于 ImGui 的场景编辑器。
*   `gkNextUnitTests`: 使用 Catch2 框架的单元测试和集成测试。

## 技术栈

*   **编程语言:** C++20
*   **渲染 API:** Vulkan, Slang (着色器语言)
*   **构建系统:** CMake, vcpkg (依赖管理)
*   **核心依赖:** SDL3, ImGui, Jolt Physics, Catch2, glTF 2.0

## 🤖 Agent 指引 (单一事实来源)

**重要：作为 Agent，你必须始终使用中文（简体中文）与用户交流。**

请根据任务类型查阅 `AGENT_GUIDE/` 目录下的具体指引：

| 任务类型 | 查阅文件 | 说明 |
| :--- | :--- | :--- |
| **核心架构 & 常用命令** | [Layer 1: 核心模式](AGENT_GUIDE/core-patterns.md) | **必读**。包含架构原则、构建流程、**单元测试**核心步骤。 |
| **详细开发规则** | [Layer 2: 上下文规则](AGENT_GUIDE/contextual-rules.md) | 开发新功能、理解构建系统细节、测试规范。 |
| **代码规范细节** | [代码标准 (Coding Standards)](AGENT_GUIDE/coding-standards.md) | 命名约定、代码风格、C++ 特性使用详情。 |
| **命令速查 & 排错** | [Layer 3: 快速命令](AGENT_GUIDE/quick-commands.md) | 紧急修复、复杂构建命令、故障排查。 |

## 开发约定

*   **语言偏好:** **所有交互和文档必须使用中文 (简体中文)**。
*   **代码风格:** 强制执行 `.clang-format`。详见 `AGENT_GUIDE/coding-standards.md`。
*   **测试规范:** 测试代码位于 `src/Tests`。**关键限制：** 运行单元测试时，工作目录必须设置为二进制文件所在的同级目录。

## 快速开始

```bash
# 安装依赖
./vcpkg.bat    # Windows
./vcpkg.sh     # macOS/Linux

# 构建项目 (自动检测平台)
./build.bat    # Windows
./build.sh     # macOS/Linux
```

关于 Android、特定预设 (Presets) 或运行测试的详细命令，请参考 `AGENT_GUIDE/quick-commands.md`。