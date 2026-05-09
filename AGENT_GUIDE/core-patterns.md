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

## ⚡ 核心命令

### 1. 依赖安装
`gnb build` 会在首次运行时自动引导 vcpkg，无需手动调用。强制更新：
```bash
gnb.bat setup --refresh   # Windows
./gnb.sh setup --refresh  # macOS/Linux
```

### 2. 项目构建
```bash
gnb.bat build      # Windows
./gnb.sh build     # macOS/Linux
./gnb.sh android   # Android
```

### 3. 单元测试
```bash
gnb.bat test
./gnb.sh test
```

### 4. 快速运行主程序
```bash
gnb.bat run                 # 列出可启动 application
gnb.bat run gkNextRenderer
./gnb.sh run gkNextRenderer
```

## ✅ 成功标准

### 构建成功标志
- 编译无错误和警告
- 生成可执行文件到正确目录 (`out/build/<preset>/bin/`)

### 运行时成功标志
- 应用程序正常启动
- 日志显示 `uploaded scene [CornellBox.proc] to gpu`

### 测试成功标志
- `gkNextUnitTests` 输出 `All tests passed`

---
*这是Agent必须掌握的核心模式。掌握这些内容后，可以处理90%的日常任务。*
