# 上下文规则 - Layer 2

## 💻 代码开发规则

### 详细代码规范
**请查阅 [Coding Standards](coding-standards.md)**。该文件包含了详细的命名约定（PascalCase/camelCase）、C++ 特性使用指南、性能优化建议和平台兼容性细节。所有代码修改必须严格遵守该规范。

### 核心开发原则
- **统一头文件**: 新文件优先包含 `CoreMinimal.hpp`。
- **平台抽象**: 必须通过 `PlatformCommon.h` 访问平台特性。
- **避免循环依赖**: 保持头文件轻量，注意包含顺序。

## 🛠️ 构建系统规则

### 统一脚本使用
- **Windows 脚本**: 核心逻辑使用 PowerShell (`.ps1`)。
- **优先使用**: `build.[bat|sh] --preset [name]`。
- **支持平台**: windows-dev, linux-release, macos-arm64, android。

### 构建输出管理
- **桌面端**: 输出到 `out/build/{preset}/bin/`。
- **Android**: 输出到 `android/app/build/outputs/apk/`。

## 🧪 测试和验证规则

### 1. 单元测试执行规范 (CRITICAL)
**重要规则**: 运行 `gkNextUnitTests` 时，**当前工作目录 (CWD)** 必须是可执行文件所在的目录（即 `bin` 目录）。

*   **原因**: 测试程序需要加载同级目录下的动态库（DLL/so）或相对路径的资源文件。如果从项目根目录运行，可能会导致加载失败。
*   **正确做法**: `cd out/build/<preset>/bin && ./gkNextUnitTests`
*   **错误做法**: `./out/build/<preset>/bin/gkNextUnitTests` (这样 CWD 是项目根目录)

### 2. 编译验证
- **每次修改**: 代码修改后必须验证编译成功。
- **多平台**: 跨平台修改需要在每个目标平台测试。

### 3. 功能验证
- **重要修改**: 运行 `gkNextRenderer` 进行视觉验证。
- **成功标志**: 看到日志 `uploaded scene [...] to gpu` 且渲染画面正常。

## 📂 项目结构理解

### 核心目录
- `src/Runtime/`: 核心运行时代码
- `src/Runtime/Platform/`: 平台特定代码 (通过 `PlatformCommon.h` 暴露)
- `src/Vulkan/`: Vulkan 渲染后端
- `src/Tests/`: 单元测试代码 (Catch2)

### 关键文件
- `src/Common/CoreMinimal.hpp`: 统一头文件，包含标准库和基础类型。
- `vcpkg.json`: 依赖定义。

---
*这些规则覆盖了日常开发中的详细场景。遇到底层代码风格问题，请查阅 `coding-standards.md`。*