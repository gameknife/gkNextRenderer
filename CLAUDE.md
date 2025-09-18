# gkNextRenderer - Claude 开发指南

## 项目概述
gkNextRenderer 是一个基于 Vulkan 的跨平台渲染器，支持路径追踪、现代延迟渲染等多种渲染技术。

## 构建和测试

### 统一构建系统

项目现在使用统一的构建脚本，支持自动平台检测和手动指定平台：

#### 依赖安装
```bash
./vcpkg.sh              # 自动检测当前平台并安装依赖
./vcpkg.sh macos        # 指定macOS平台
./vcpkg.sh linux        # 指定Linux平台
./vcpkg.sh android      # 指定Android平台
./vcpkg.sh mingw        # 指定MinGW平台
```

#### 项目构建
```bash
./build.sh              # 自动检测当前平台并构建
./build.sh macos        # 指定macOS平台
./build.sh linux        # 指定Linux平台
./build.sh android      # 指定Android平台
./build.sh mingw        # 指定MinGW平台
```

#### 环境准备
```bash
./prepare.sh setup      # 自动检测平台并准备开发环境
./prepare.sh slangc     # 安装slangc编译器
```

### 快速测试
构建完成后，运行以下命令测试基本功能：
```bash
cd build/macos/bin && ./gkNextRenderer
```

**成功标志**: 在日志中看到 `uploaded scene [CornellBox.proc] to gpu` 即表示启动成功

## 项目结构

### 重要目录
- `src/Runtime/` - 核心运行时代码
- `src/Runtime/Platform/` - 平台特定代码
- `src/Vulkan/` - Vulkan 渲染相关代码
- `src/Assets/` - 资源管理代码
- `assets/` - 模型、纹理等资源文件

### 平台抽象架构
使用 `PlatformCommon.h` 统一管理平台相关代码：
- `PlatformCommon.h` - 根据平台宏自动包含对应的平台头文件
- `PlatformAndroid.h` - Android 平台特定实现
- `PlatformWindows.h` - Windows 平台特定实现
- `PlatformLinux.h` - Linux 平台特定实现

## 代码风格和约定

### 头文件包含
- **优先使用 `PlatformCommon.h`** 而不是直接包含特定平台头文件
- 所有平台都可以安全地包含 `PlatformCommon.h`，内部会自动处理平台差异

### 宏定义组织
- 将平台特定的宏定义移动到对应的平台头文件中
- 避免在源文件中直接定义平台相关的宏

### 命名约定
- 使用 `#if ANDROID` 而不是 `#ifdef ANDROID`
- 保持与现有代码风格一致

## 开发工作流

### 1. 修改前的准备
- 首先使用搜索工具了解现有代码结构
- 检查是否有现有的抽象或模式可以遵循

### 2. 实施修改
- 保持修改的原子性和可测试性
- 优先考虑代码的一致性和可维护性

### 3. 验证和测试
- 每次修改后运行 `./build.sh` 确保编译成功
- 运行应用程序验证基本功能正常

## 常见任务

### 平台相关代码重构
1. 将分散的平台特定宏定义集中到对应的平台头文件中
2. 修改源文件使用 `PlatformCommon.h` 而非条件包含
3. 编译测试确保无错误
4. 运行测试确保功能正常

### 新功能开发
1. 了解现有的架构模式
2. 遵循项目的命名和组织约定
3. 确保跨平台兼容性

## 项目特定的注意事项

### 构建系统
- 使用 CMake + Ninja
- 依赖 vcpkg 进行包管理
- 支持多平台构建（macOS, Linux, Windows, Android）
- **统一脚本架构**: 使用 `vcpkg.sh`、`build.sh`、`prepare.sh` 和 `package.bat` 统一管理所有平台的构建和打包

### 脚本统一
- **vcpkg.sh/vcpkg.bat**: 统一的依赖管理脚本，支持自动平台检测
- **build.sh/build.bat**: 统一的构建脚本，替代所有平台特定的构建脚本
- **prepare.sh**: 统一的环境准备脚本，整合了引导和工具安装
- **package.bat**: 统一的打包脚本，支持不同的打包模式
- **优化特性**: 自动清理debug文件，PDB优化，包体积减少90%

#### 使用方法
```bash
./package.bat local              # 打包gkNextRenderer（自动清理debug文件）
./package.bat local clean        # 清理构建目录并打包
./package.bat magicalego v1.0.0  # 打包MagicaLego游戏
```

#### 打包优化
- **自动清理**: 打包前自动删除PDB文件、debug版本exe/lib文件
- **PDB优化**: 使用/Z7编译标志生成最小调试信息（支持崩溃堆栈跟踪）
- **体积优化**: 启用链接时优化(/OPT:REF /OPT:ICF)
- **效果**: 包大小从2.2GB减少到216MB（减少90%）

### 渲染器特性
- 支持路径追踪、延迟渲染、体素追踪等多种渲染模式
- 使用 Vulkan 作为图形 API
- 支持实时光线追踪（如果硬件支持）

### 资源管理
- 使用全局纹理池管理纹理资源
- 支持多种 3D 模型格式
- 内置场景管理系统

## 沟通和反馈

### 有效的合作模式
- **明确目标**: 每次任务开始时清晰说明目标
- **渐进式改进**: 一次专注于一个具体目标
- **即时反馈**: 及时指出需要调整的地方
- **验证导向**: 每次修改后都要进行构建和功能测试

### 期望的工作方式
- 主动使用 TodoWrite 工具跟踪任务进度
- 在进行重大修改前先了解现有代码结构
- 保持代码的一致性和可维护性
- 遵循项目既有的设计模式和约定

## 记录的重要经验

### 2025-09-18 构建脚本统一重构
- **问题**: 项目中存在大量分散的平台特定构建脚本（vcpkg_xxx、build_xxx、package_xxx等）
- **解决方案**: 将所有构建脚本整合为统一的脚本架构
  - `vcpkg.sh/vcpkg.bat`: 统一依赖管理，支持自动平台检测
  - `build.sh/build.bat`: 统一构建脚本，替代所有平台特定构建脚本
  - `prepare.sh`: 统一环境准备，整合引导脚本
  - `package.bat`: 统一打包脚本，支持不同打包模式
- **关键学习**:
  - 统一脚本架构大幅简化维护工作
  - 自动平台检测提高用户体验
  - 保持向后兼容性很重要
- **验证方法**: 编译测试 + GitHub Actions CI 验证

### 2025-09-18 Android GLFW 定义重构
- **问题**: Android 特定的 GLFW 定义分散在多个 Runtime 文件中
- **解决方案**: 将所有定义集中到 `PlatformAndroid.h` 中
- **关键学习**: 使用 `PlatformCommon.h` 统一平台抽象，避免条件包含
- **验证方法**: 编译测试 + 运行测试（看到 `uploaded scene` 日志）

### 2025-09-18 打包系统优化
- **问题**: package.bat生成的包体积过大（2.2GB），包含大量冗余文件
- **解决方案**:
  - 在package.bat中添加自动清理步骤，删除PDB、debug版本文件
  - 修改CMakeLists.txt，使用/Z7编译标志优化PDB生成
  - 启用链接时优化(/OPT:REF /OPT:ICF)
- **关键学习**:
  - 使用/Z7替代/Zi可以将调试信息嵌入目标文件而非单独PDB
  - 自动清理流程显著减少包体积
  - 保持崩溃堆栈跟踪能力的同时大幅减小包大小
- **验证方法**: 打包测试 + 包大小对比（2.2GB→216MB，减少90%）

---

*最后更新: 2025-09-18*