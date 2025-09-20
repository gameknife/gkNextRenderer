# gkNextRenderer - Claude 开发指南

## 🚀 快速开始

### 首次构建
```bash
# 1. 安装依赖
./vcpkg.bat windows    # Windows
./vcpkg.sh macos       # macOS
./vcpkg.sh linux       # Linux
./vcpkg.sh android     # Android

# 2. 构建项目
./build.bat windows    # Windows
./build.sh macos       # macOS
./build.sh linux       # Linux
./build.bat android    # Android

# 3. 测试运行
cd build/windows/bin && ./gkNextRenderer.exe  # Windows
cd build/macos/bin && ./gkNextRenderer        # macOS
```

**成功标志**: 日志中显示 `uploaded scene [CornellBox.proc] to gpu`

### 日常工作流程
1. **了解需求** - 使用搜索工具分析现有代码结构
2. **制定计划** - 使用 TodoWrite 工具创建任务清单
3. **实施修改** - 遵循项目代码风格和架构模式
4. **验证测试** - 运行构建脚本并测试功能
5. **更新文档** - 记录重要的学习经验

## 📁 项目结构

### 核心目录
- `src/Runtime/` - 核心运行时代码
- `src/Runtime/Platform/` - 平台特定代码
- `src/Vulkan/` - Vulkan 渲染相关代码
- `src/Assets/` - 资源管理代码
- `assets/` - 模型、纹理等资源文件
- `android/` - Android 项目目录
- `build/` - 构建输出目录

### 平台抽象架构
```
PlatformCommon.h (统一入口)
├── PlatformAndroid.h   # Android 平台实现
├── PlatformWindows.h   # Windows 平台实现
└── PlatformLinux.h     # Linux 平台实现
```

**重要**: 始终使用 `PlatformCommon.h` 而非直接包含平台头文件

## 🛠️ 构建系统

### 统一脚本架构
项目使用统一的构建脚本，支持自动平台检测：

| 脚本 | 功能 | 支持平台 |
|------|------|----------|
| `vcpkg.sh/bat` | 依赖管理 | windows, macos, linux, android, mingw |
| `build.sh/bat` | 项目构建 | windows, macos, linux, android, mingw |
| `prepare.sh` | 环境准备 | setup, slangc |
| `package.bat` | 打包发布 | local, magicalego |

### 构建输出位置
- **Windows**: `build/windows/bin/`
- **macOS**: `build/macos/bin/`
- **Linux**: `build/linux/bin/`
- **Android**: `android/app/build/outputs/apk/`

### 打包优化特性
- **自动清理**: 删除 PDB 文件和 debug 版本文件
- **PDB 优化**: 使用 `/Z7` 编译标志减少调试信息体积
- **链接优化**: 启用 `/OPT:REF /OPT:ICF`
- **效果**: 包大小从 2.2GB 减少到 216MB（减少90%）

## 💻 开发规范

### 代码风格
- **头文件包含**: 优先使用 `PlatformCommon.h`
- **宏定义**: 使用 `#if ANDROID` 而非 `#ifdef ANDROID`
- **命名**: 保持与现有代码风格一致
- **注释**: 如无特殊要求，避免添加注释

### 平台兼容性
- 避免在源文件中直接定义平台相关宏
- 将平台特定代码移动到对应的平台头文件中
- 使用 `PlatformCommon.h` 统一处理平台差异

### 验证要求
- 每次修改后必须运行构建脚本确保编译成功
- 重要修改需要运行应用程序验证功能正常
- 跨平台修改需要在多个平台上测试

## 🔧 常见任务模式

### 平台代码重构
1. 搜索分散的平台特定代码
2. 集中到对应的平台头文件中
3. 修改源文件使用 `PlatformCommon.h`
4. 编译测试 + 功能验证

### 新功能开发
1. 分析现有架构模式和设计
2. 遵循项目命名和组织约定
3. 确保跨平台兼容性
4. 使用 TodoWrite 跟踪进度

### 构建问题排查
1. 检查依赖是否正确安装（`./vcpkg.bat [platform]`）
2. 清理构建目录重新构建
3. 查看编译日志定位具体错误
4. 参考历史经验记录

## 📋 重要经验记录

### 2025-09-18: 构建脚本统一重构
**问题**: 大量分散的平台特定脚本导致维护困难
**解决**: 整合为统一脚本架构，支持自动平台检测
**学习**:
- 统一架构大幅简化维护
- 自动检测提升用户体验
- 向后兼容性很重要

### 2025-09-18: Android GLFW 定义重构
**问题**: Android GLFW 定义分散在多个文件中
**解决**: 集中到 `PlatformAndroid.h`，使用 `PlatformCommon.h` 统一抽象
**学习**: 平台抽象层避免条件包含，提高代码可维护性

### 2025-09-18: 打包系统优化
**问题**: 包体积过大（2.2GB），包含大量冗余文件
**解决**:
- 添加自动清理流程删除 PDB 和 debug 文件
- 使用 `/Z7` 编译标志优化调试信息
- 启用链接时优化
**效果**: 包大小减少90%（2.2GB→216MB）

### 2025-09-19: 多平台构建验证
**Windows**:
- `./build.bat windows` → `build/windows/bin/`
- 成功检测 RTX 5070 Ti，运行正常
**Android**:
- `./build.bat android` → `android/gradlew.bat build`
- 生成 APK 在 `android/app/build/outputs/apk/`
**学习**: 统一脚本工作正常，优先使用 `build.bat [platform]`

## 🎯 工作期望

### 合作模式
- **明确目标**: 任务开始时清晰说明预期结果
- **渐进改进**: 一次专注于一个具体目标
- **即时反馈**: 及时指出需要调整的方向
- **验证导向**: 每次修改后都要构建测试

### Claude 行为指南
- ✅ 主动使用 TodoWrite 工具跟踪任务进度
- ✅ 修改前先了解现有代码结构
- ✅ 保持代码一致性和可维护性
- ✅ 遵循项目既有的设计模式
- ✅ 记录重要经验到文档
- ❌ 避免创建不必要的文档文件
- ❌ 除非明确要求，否则不添加注释

---

*最后更新: 2025-09-19*