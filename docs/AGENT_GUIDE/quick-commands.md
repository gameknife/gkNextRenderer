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
./gnb.bat build        # 默认构建核心目标 (gkNextRenderer + gkNextUnitTests)
./gnb.bat build --all  # 全量构建所有 15+ 子项目
./gnb.bat build --clean  # 清理重编

# macOS / Linux
./gnb.sh build

# Android
./gnb.sh android debug    # 构建、安装、启动
./gnb.sh android release  # 只构建 APK
```

**⚠️ 按范围选目标，默认无需全量构建：**
```bash
# 改 Engine 层 (src/Engine/**、shaders、公共 runtime)：默认 ./gnb.bat build 即可（即编渲染器 + 单测）
./gnb.bat build

# 改某个具体 program (src/Application/** 单一子项目)：只编它自己
./gnb.bat build MagicaLego

# 改 gnb/tools/纯文档：无需 C++ 构建
```
仅**大型重构 / 广泛 header / ABI 改动 / 用户明确要求**时，才执行全量 `./gnb.bat build --all --reconfigure`。增量构建无需 `--reconfigure`（除非改了 CMake/preset 或新增文件未被 glob 收录）。

### 2. 单元测试 (Test)
```bash
./gnb.sh test
./gnb.bat test
```

### 3. 运行主程序 (Run)
```bash
./gnb.sh run                 # 列出可启动 application
./gnb.sh run gkNextRenderer

# 指定 Target
./gnb.sh run gkNextEditor
./gnb.bat run gkNextEditor
```

### 4. 依赖管理 (Vcpkg)
首次 `gnb build` 会自动引导 vcpkg。仅在需要强制更新 vcpkg 版本时手动调用：
```bash
./gnb.bat setup --refresh   # Windows
./gnb.sh setup --refresh  # Unix
```

## 🔍 故障排查 (Troubleshooting)

### 编译错误
*   **头文件缺失**: 检查是否包含了 `CoreMinimal.hpp`。
*   **平台错误**: 检查 `PlatformCommon.h` 的宏定义。

### 运行时错误
*   **找不到 DLL/so**: 先运行 `gnb setup` 和 `gnb build`。
*   **Shader 缺失**: 检查构建日志，并确认运行时 build assets 的 `assets/shaders` 下有对应 `.slang.spv` 文件；source tree 只保存 `.slang` 源码。

### 常用搜索
```bash
# 搜索类定义
rg "class NextEngine" src/

# 搜索 TODO
rg "TODO" src/
```
