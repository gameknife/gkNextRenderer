# Gemini 代码助手上下文 (Gemini Code Assistant Context)

本文档为 Gemini 代码助手提供 `gkNextRenderer` 项目的核心上下文。

## 项目概览

`gkNextRenderer` 是一个使用现代 C++ 编写的跨平台 3D 游戏引擎，专注于现代渲染技术和游戏技术实践。引擎支持 Windows, Linux, macOS, Android 和 iOS。

项目结构由一系列库和可执行文件组成。核心库为 `gkNextEngine`。主要可执行文件包括：
*   `gkNextRenderer`: 主渲染器，支持路径追踪 (Path Tracing) 和混合渲染。
*   `gkNextEditor`: 基于 ImGui 的场景编辑器。
*   `MagicaLego`: 体素搭建小游戏，支持 AI 辅助建造。
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

关于 Android、特定预设 (Presets) 或运行测试的详细命令，请参考 `AGENT_GUIDE/quick-commands.md`。

## MagicaLego 子项目

MagicaLego 是一个体素搭建小游戏，集成了 Gemini AI 辅助建造功能。

### 核心功能
- 基于网格的乐高方块搭建（放置/挖掘/选择）
- mlscript 脚本系统（变量、循环、相对坐标）
- AI 助手（自然语言描述 → 自动生成脚本）
- 时间轴回放、截图、录屏

### 关键文件
| 文件 | 说明 |
|-----|------|
| `MagicaLegoGameInstance.cpp` | 核心游戏逻辑 |
| `MagicaLegoUserInterface.cpp` | UI 渲染（使用区域注释组织） |
| `MagicaLegoCommands.cpp` | 命令系统（place, dig, move, turn 等） |
| `MagicaLegoAIService.cpp` | Gemini API 集成 |
| `MagicaLegoConstants.hpp` | 集中常量定义（Grid, UI, Anim, AI） |
| `MagicaLegoUIHelpers.hpp` | 可复用 UI 辅助函数 |

### 代码组织最佳实践
1. **常量集中管理**: 所有魔法数字放入 `Constants.hpp`
2. **辅助函数提取**: 可复用代码放入 `UIHelpers.hpp`
3. **区域注释**: 大文件使用 `// ============ Section ============` 分隔
4. **异步 AI**: 使用回调避免阻塞 UI 线程

详细文档参见 `AGENT_GUIDE/MagicaLego.md`。