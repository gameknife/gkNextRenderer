# 核心模式 - Layer 1

## 🎯 架构原则

### 平台抽象
- **统一入口**: 使用 `PlatformCommon.h` 处理所有平台特定代码
- **禁止直接包含**: 绝不直接包含平台头文件（PlatformAndroid.h、PlatformWindows.h等）
- **条件编译**: 使用 `#if ANDROID` 而非 `#ifdef ANDROID`

### 统一头文件
- **首选包含**: 始终先包含 `CoreMinimal.hpp`
- **包含内容**: 标准库、fmt格式化、spdlog日志、平台检测、基础类型定义
- **避免分散**: 不要单独包含标准库头文件，CoreMinimal.hpp已提供

### 构建验证
- **每次必做**: 代码修改后必须运行构建脚本
- **运行时验证**: 构建成功后必须运行应用程序验证功能
- **成功标准**: 日志显示 `uploaded scene [CornellBox.proc] to gpu`

## ⚡ 关键命令

### 依赖安装
```bash
./vcpkg.bat    # Windows
./vcpkg.sh     # macOS/Linux
```

### 项目构建
```bash
./build.bat --preset windows-dev   # Windows
./build.sh --preset macos-arm64    # macOS
./build.sh --preset linux-release  # Linux
./build.bat --android              # Android
```

### 快速验证
```bash
# Windows
./run.bat --target gkNextRenderer --dry-run
./run.bat --preset windows-dev

# macOS/Linux
./run.sh --target gkNextRenderer --dry-run
./run.sh --preset [preset_name]

# Android
./run.bat --preset android
```

## ✅ 成功标准

### 构建成功标志
- 编译无错误和警告
- 生成可执行文件到正确目录
- 依赖库正确链接

### 运行时成功标志
- 应用程序正常启动
- 日志显示 `uploaded scene [CornellBox.proc] to gpu`
- 渲染窗口正常显示

### 输出目录
- **Windows**: `out/build/windows-dev/bin/`
- **macOS**: `out/build/macos-arm64/bin/`
- **Linux**: `out/build/linux-release/bin/`
- **Android**: `android/app/build/outputs/apk/`

## 🔄 基本工作流程

1. **代码修改** → 遵循架构原则
2. **构建验证** → 运行对应平台构建脚本
3. **功能测试** → 运行应用程序验证
4. **问题解决** → 查看构建日志或参考上下文规则

---
*这是Agent必须掌握的核心模式。掌握这些内容后，可以处理90%的日常任务。*