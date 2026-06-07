# 上下文规则 - Layer 2

## 💻 代码开发规则

### 详细代码规范
**请查阅 [Coding Standards](coding-standards.md)**。该文件包含了详细的命名约定（PascalCase/camelCase）、C++ 特性使用指南、性能优化建议和平台兼容性细节。所有代码修改必须严格遵守该规范。

### 核心开发原则
- **统一头文件**: 新文件优先包含 `CoreMinimal.hpp`。
- **平台抽象**: 必须通过 `PlatformCommon.h` 访问平台特性。
- **避免循环依赖**: 保持头文件轻量，注意包含顺序。

## 🛠️ 构建系统规则

### 统一入口 gnb
- **唯一入口**: 一律通过 `gnb`（Windows `./gnb.bat`、macOS/Linux `./gnb.sh`）构建、运行、测试，不再有 `build.bat`/`build.sh`。
- **可用 CMake 预设**: `windows`、`linux`、`macos-arm64`、`ios`。

### 构建输出管理
- **桌面端**: 输出到 `out/build/<preset>/bin/`。
- **Android**: 输出到 `android/app/build/outputs/apk/`。

## 🧪 测试和验证规则

### 1. 单元测试执行
- 测试不再要求 CWD 为 `bin`，直接用可执行文件路径启动即可：
  `./out/build/<preset>/bin/gkNextUnitTests`
- 按名称/标签筛选：`gkNextUnitTests "RenderComponent Usage"` 或 `gkNextUnitTests "[Unit][RenderComponent]"`。

### 2. 编译验证
- **每次修改**: 代码修改后必须验证编译成功。
- **多平台**: 跨平台修改需要在每个目标平台测试。

### 3. 功能验证
- **重要修改**: 运行 `gkNextRenderer` 进行视觉验证。
- **成功标志**: 看到日志 `uploaded scene [...] to gpu` 且渲染画面正常。

## 📂 项目结构理解

### 核心目录
- `src/Engine/Runtime/`: 核心运行时代码（ECS、脚本、反射）
- `src/Engine/Common/`: 跨平台抽象（通过 `PlatformCommon.h` 暴露）
- `src/Engine/Vulkan/`: Vulkan 渲染后端
- `src/Tests/`: 单元测试代码 (Catch2)

### 关键文件
- `src/Engine/Common/CoreMinimal.hpp`: 统一头文件，包含标准库和基础类型。
- `vcpkg.json`: 依赖定义。

---
*这些规则覆盖了日常开发中的详细场景。遇到底层代码风格问题，请查阅 `coding-standards.md`。*