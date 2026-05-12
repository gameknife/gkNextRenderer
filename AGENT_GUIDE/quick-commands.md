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
gnb.bat build
gnb.bat build --clean  # 清理重编

# macOS / Linux
./gnb.sh build

# Android
./gnb.sh android
```

### 2. 单元测试 (Test)
```bash
./gnb.sh test
gnb.bat test
```

### 3. 运行主程序 (Run)
```bash
./gnb.sh run                 # 列出可启动 application
./gnb.sh run gkNextRenderer

# 指定 Target
./gnb.sh run gkNextEditor
gnb.bat run gkNextEditor
```

### 4. 依赖管理 (Vcpkg)
首次 `gnb build` 会自动引导 vcpkg。仅在需要强制更新 vcpkg 版本时手动调用：
```bash
gnb.bat setup --refresh   # Windows
./gnb.sh setup --refresh  # Unix
```

## 🔍 故障排查 (Troubleshooting)

### 编译错误
*   **头文件缺失**: 检查是否包含了 `CoreMinimal.hpp`。
*   **平台错误**: 检查 `PlatformCommon.h` 的宏定义。

### 运行时错误
*   **找不到 DLL/so**: 先运行 `gnb setup` 和 `gnb build`。
*   **Shader 缺失**: 确保 `assets/shaders` 下有编译好的 `.spv` 文件。

### 常用搜索
```bash
# 搜索类定义
grep -r "class NextEngine" src/

# 搜索 TODO
grep -r "TODO" src/
```
