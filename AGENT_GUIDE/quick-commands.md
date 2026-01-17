# 快速命令 - Layer 3

## 🎯 按任务类型操作流程

### 新功能开发
```
分析现有模式 → 遵循命名约定 → 使用TodoWrite跟踪 → 构建+测试
```

**快速检查清单**:
- [ ] 是否使用 `CoreMinimal.hpp` 和 `PlatformCommon.h`
- [ ] 是否遵循现有命名约定
- [ ] 是否使用 TodoWrite 跟踪任务
- [ ] 是否构建验证成功

### Bug修复
```
搜索代码库 → 识别根因 → 最小修改 → 验证修复
```

**常用搜索命令**:
```bash
# 搜索函数定义
grep -r "function_name" src/

# 搜索类定义
find src/ -name "*.hpp" -o -name "*.cpp" | xargs grep "class ClassName"

# 搜索错误消息
grep -r "error_message" src/
```

### 平台相关问题
```
检查PlatformCommon.h → 使用平台抽象 → 目标平台测试
```

**平台特定检查**:
- Android: 检查 `PlatformAndroid.h` 中的定义
- Windows: 检查 `PlatformWindows.h` 中的定义
- Linux: 检查 `PlatformLinux.h` 中的定义

### 构建失败处理
```
清理重建 → 检查依赖 → 查看日志 → 重试
```

## 🚨 应急命令集合

### 完全清理重建
```bash
# 删除构建目录
rm -rf build/

# 清理Android构建
cd android && ./gradlew clean

# 重新安装依赖并构建
./vcpkg.sh [platform] && ./build.sh [platform]
```

### 依赖问题解决
```bash
# 重新安装依赖
./vcpkg.sh [platform]

# 强制重新安装
./vcpkg.sh [platform] --force

# 检查vcpkg状态
./vcpkg.sh [platform] --list
```

### 快速验证命令
```bash
# 验证编译
./build.sh [platform]

# 验证带可选特性的编译
./build.bat windows-dev --oidn --dlss --avif

# 验证运行（Windows）
./run.bat windows-dev
```

### 日志查看
```bash
# 查看构建日志
./build.sh [platform] 2>&1 | tee build.log

# 查看运行时日志
cd build/[platform]/bin && ./gkNextRenderer 2>&1 | tee runtime.log

# 查看详细构建日志
./build.sh [platform] --verbose
```

## 🔍 常见错误快速解决

### 编译错误
**找不到头文件**:
```bash
# 检查CoreMinimal.hpp包含
grep -r "#include.*CoreMinimal.hpp" src/
```

**链接错误**:
```bash
# 清理重建
rm -rf build/ && ./build.sh [platform]
```

**平台相关错误**:
```bash
# 检查PlatformCommon.h使用
grep -r "PlatformCommon.h" src/
```

### 运行时错误
**DLL/so文件缺失**:
```bash
# 重新构建依赖
./vcpkg.sh [platform] --force
./build.sh [platform]
```

**资源文件缺失**:
```bash
# 检查assets目录
ls -la assets/
```

### Android特定问题
**构建失败**:
```bash
cd android
./gradlew clean
./gradlew build
```

**APK安装失败**:
```bash
./gradlew uninstallDebug
./gradlew installDebug
```

## ⚡ 快速诊断脚本

### 环境检查
```bash
# 检查构建工具
which cmake
which ninja
which git

# 检查vcpkg
ls -la vcpkg_installed/

# 检查构建输出
ls -la build/
```

### 文件完整性检查
```bash
# 检查关键文件
ls -la src/Common/CoreMinimal.hpp
ls -la src/Runtime/Platform/PlatformCommon.h
ls -la vcpkg.json
ls -la CMakeLists.txt
```

## 📱 平台特定快捷方式

### Windows
```batch
# 快速构建
./build.bat windows-dev

# 快速运行
.\run.bat
```

### macOS/Linux
```bash
# 快速构建
./build.sh macos  # 或 linux

# 快速运行
cd build/macos/bin && ./gkNextRenderer  # 或 build/linux/bin
```

### Android
```bash
# 快速构建
cd android && ./gradlew.bat build

# 快速安装
./gradlew.bat installDebug
```

---
*这些命令旨在解决90%的常见问题。如遇复杂问题，请参考上下文规则或核心模式。*