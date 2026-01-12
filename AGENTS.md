# AGENTS.md

This file provides guidance to Qoder (qoder.com) when working with code in this repository.

---

# 🤖 Agent 指引

## 📍 核心指引位置

**完整分层指引**: `AGENT_GUIDE/README.md`

### 按任务类型快速导航

| 任务类型 | 阅读文件 | 预估时间 |
|---------|---------|---------|
| **简单任务** | `AGENT_GUIDE/core-patterns.md` | 30秒 |
| **复杂开发** | `AGENT_GUIDE/core-patterns.md` → `AGENT_GUIDE/contextual-rules.md` | 2.5分钟 |
| **紧急修复** | `AGENT_GUIDE/quick-commands.md` | 15秒 |

## ⚡ 立即行动清单

1. **阅读核心模式**: `AGENT_GUIDE/core-patterns.md`
2. **使用中文交流**: 所有回复使用中文
3. **使用TodoWrite**: 开始任务前创建任务清单
4. **遵循架构**: 使用 `CoreMinimal.hpp` 和 `PlatformCommon.h`
5. **验证构建**: 每次修改后运行 `./build.[bat|sh] [platform]`

## 🎯 核心命令

```bash
# 构建（选择你的平台）
./build.sh macos            # macOS
./build.sh linux-release    # Linux
./build.bat windows-dev     # Windows
./build.bat --android       # Android

# 可选编译选项 (支持组合)
# --avif: 开启 AVIF 支持
# --dlss: 开启 DLSS 支持 (Windows Only)
# --oidn: 开启 OIDN 支持
./build.bat windows-dev --oidn --dlss
```

# 快速验证
```bash
./run.bat windows-dev
```

## ✅ 成功标准

看到日志: `uploaded scene [CornellBox.proc] to gpu`

## 🏗️ 项目结构（补充）

- `src/Runtime/` 核心运行时代码，包含平台抽象层 `Platform/`
- `src/Vulkan/` Vulkan 渲染核心
- `src/Rendering/` 渲染逻辑与管线
- `src/Assets/` 资源管理（Scene、Node、Model 场景图）
- `src/Application/` 应用启动入口
- `src/Common/` 通用代码，包含 `CoreMinimal.hpp` 统一头文件
- `src/Tests/` 单元测试（Catch2 框架）
- `assets/` 资源与着色器，`android/` Android 项目目录
- `AGENT_GUIDE/` Agent 专用分层指引目录

## 🧪 测试策略

### 测试框架
- **框架**: Catch2（目标：`gkNextUnitTests`）
- **位置**: `src/Tests/`
- **运行**: `./build.[bat|sh] [platform]` 后执行 `./build/[platform]/bin/gkNextUnitTests[.exe]`

### 测试约定
- 使用 Catch2 宏：`TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`
- 集成测试需要完整引擎栈（GPU + 编译的 Shaders）
- 优先独立测试组件（如 `NextPhysics`、`Node`），避免在无 GPU 环境中测试 `VulkanBaseRenderer`
- 运行测试前确保 `assets/shaders/*.spv` 已编译，工作目录为项目根目录

### 当前已知问题
- **Shader 依赖**: 引擎启动需要 `Process.UpScaleFSR.comp.slang.spv` 等着色器文件
- **路径问题**: `FileHelper` 使用相对路径，需从项目根运行

## 🔧 开发约定

### 代码风格
- 遵循 `.clang-format` 配置
- 使用现代 C++20 标准
- 命名约定：`CamelCase` 类/方法，`camelCase_` 或 `_camelCase` 成员变量
- 除非明确要求，否则不添加代码注释

### 引擎架构模式
- **引擎核心**: Singleton 模式 `NextEngine`
- **物理系统**: Jolt Physics（通过 `NextPhysics` 包装集成）
- **场景管理**: `Scene`、`Node`、`Model` 场景图
- **渲染后端**: 完整 Vulkan 管线

## 💡 关键历史经验

### 构建系统优化
- **统一脚本**: 优先使用 `build.bat [platform]` 而非分散的脚本
- **自动清理**: 必要时删除整个 `build/` 目录重新构建
- **包大小优化**: 生产构建可减少90%体积（2.2GB→216MB）

### 架构重构经验
- **平台抽象**: 使用 `PlatformCommon.h` 避免条件包含，提高可维护性
- **统一头文件**: `CoreMinimal.hpp` 集成所有通用功能，简化维护
- **编译期控制**: 使用 `#if ANDROID` 而非 `#ifdef ANDROID`

### 工作流程验证
- **多平台验证**: 各平台构建验证正常，统一脚本工作可靠
- **依赖管理**: vcpkg统一管理，支持缓存共享
- **功能验证**: 每次修改后必须构建+运行验证

---

**记住**: 始终从这里开始，然后按照任务复杂度选择阅读深度。这些经验来自实际项目重构，可直接应用到你的任务中。