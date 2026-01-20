# 快速命令 - Layer 3

## 🎯 按任务类型操作流程

### 新功能开发
`分析` -> `TodoWrite` -> `代码修改` -> `构建` -> `测试`

### Bug修复
`搜索代码` -> `复现` -> `修复` -> `验证`

## ⚡ 常用命令速查

### 1. 构建 (Build)
```bash
# Windows
./build.bat windows-dev
./build.bat windows-dev --clean  # 清理重编

# macOS / Linux
./build.sh macos-arm64
./build.sh linux-release

# Android
./build.bat --android
```

### 2. 单元测试 (Test)
**必须进入 bin 目录运行！**

```bash
# Windows
cd out/build/windows-dev/bin && ./gkNextUnitTests.exe

# macOS
cd out/build/macos-arm64/bin && ./gkNextUnitTests

# Linux
cd out/build/linux-release/bin && ./gkNextUnitTests
```

### 3. 运行主程序 (Run)
```bash
# Windows (开发模式)
./run.bat windows-dev

# 指定 Target
./run.bat windows-dev --target gkNextEditor.exe

# macOS/Linux
./run.sh macos-arm64
```

### 4. 依赖管理 (Vcpkg)
```bash
# 安装/更新依赖
./vcpkg.bat    # Windows
./vcpkg.sh     # Unix
```

## 🔍 故障排查 (Troubleshooting)

### 编译错误
*   **头文件缺失**: 检查是否包含了 `CoreMinimal.hpp`。
*   **平台错误**: 检查 `PlatformCommon.h` 的宏定义。

### 运行时错误
*   **找不到 DLL/so**: 确保运行了 `vcpkg` 并且是在 `bin` 目录下运行测试。
*   **Shader 缺失**: 确保 `assets/shaders` 下有编译好的 `.spv` 文件。

### 常用搜索
```bash
# 搜索类定义
grep -r "class NextEngine" src/

# 搜索 TODO
grep -r "TODO" src/
```
