# CMake 结构设计

为了现代化和清理 `CMakeLists.txt`，我们将把逻辑迁移到 `cmake/` 目录下的模块化结构中。

## 目录布局

```
cmake/
├── StandardSettings.cmake   # C++ 标准 (20), 标准合规标志
├── CompilerWarnings.cmake   # 不同编译器的警告级别 (Wall, /W4)
├── PlatformSetup.cmake      # 平台特定定义 (WIN32, UNIX, IOS definitions)
├── Dependencies.cmake       # 所有 find_package() 调用
└── Utilities.cmake          # 辅助宏 (例如源文件分组)
```

## 模块职责

### `StandardSettings.cmake`
- 设置 `CMAKE_CXX_STANDARD 20`
- 设置 `CMAKE_CXX_STANDARD_REQUIRED ON`
- 设置输出目录 (`CMAKE_RUNTIME_OUTPUT_DIRECTORY` 等)

### `CompilerWarnings.cmake`
- 检测 MSVC, Clang, GCC。
- 应用标准警告标志。
- 在可能的情况下替换全局 `add_compile_options`，或定义一个函数 `set_project_warnings(target)` 以逐个目标应用。

### `PlatformSetup.cmake`
- 处理 `if (WIN32)`, `if (IOS)`, `if (ANDROID)` 逻辑。
- 设置特定定义，如 `-DUNICODE`, `-D_CRT_SECURE_NO_WARNINGS`。
- 处理 iOS Bundle 代码签名属性。

### `Dependencies.cmake`
- 集中管理 `find_package(SDL3)`, `find_package(Vulkan)` 等。
- 处理 `slangc` 检测逻辑。

## 重构策略
1.  创建 `cmake/` 目录。
2.  迭代地将逻辑从 `CMakeLists.txt` 移动到这些模块。
3.  使用 `list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")` 和 `include(ModuleName)` 将它们包含在 `CMakeLists.txt` 中。
