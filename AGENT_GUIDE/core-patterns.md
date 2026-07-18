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
- **按影响面验证**: C++/shader 修改按 `AGENTS.md` 选择受影响 target；不要默认全量构建
- **文档与工具**: 纯文档无需 C++ build，gnb/工具修改运行自身测试
- **运行时验证**: 只在改动影响运行时行为时启动相关应用或使用 `gnb shot` / `gnb validate`

## ⚡ 核心命令

### 1. 依赖安装
`gnb build` 会在首次运行时自动引导 vcpkg，无需手动调用。强制更新：
```bash
./gnb.bat setup --refresh   # Windows
./gnb.sh setup --refresh  # macOS/Linux
```

### 2. 项目构建
```bash
./gnb.bat build gkNextRenderer gkNextUnitTests  # Windows Engine 改动
./gnb.sh build gkNextRenderer gkNextUnitTests   # macOS/Linux Engine 改动
./gnb.sh android   # Android
```

### 3. 单元测试
```bash
./gnb.bat test
./gnb.sh test
```

### 4. 快速运行主程序
```bash
./gnb.bat run                 # 列出可启动 application
./gnb.bat run gkNextRenderer
./gnb.sh run gkNextRenderer
```

## ✅ 成功标准

### 构建成功标志
- 受影响目标编译无错误
- 生成可执行文件到正确目录 (`out/build/<preset>/bin/`)

### 运行时成功标志
- 应用程序正常启动
- 日志显示 `uploaded scene [...] to gpu`

### 测试成功标志
- `gkNextUnitTests` 输出 `All tests passed`

---
*这是Agent必须掌握的核心模式。掌握这些内容后，可以处理90%的日常任务。*
