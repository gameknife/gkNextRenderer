# CMake 结构

`CMakeLists.txt` 的逻辑按职责拆到 `cmake/`（项目级配置）与 `src/cmake/`（源文件/目标）下的模块里。根 `CMakeLists.txt` 只负责按顺序 `include` 这些模块并 `add_subdirectory(assets)` / `add_subdirectory(src)`。

## 目录布局

```
cmake/
├── SetupPlatform.cmake       # 输出目录(bin/lib)、Debug 后缀、各平台架构(iOS/Android/桌面)
├── ProjectOptions.cmake      # gk_project_options INTERFACE 目标：C++20、Windows/MSVC 编译定义与警告
├── SetupExternalLibs.cmake   # 校验由 `gnb setup` 准备好的外部工具链路径（CMake 只验证，不下载）
├── SetupDependencies.cmake   # find_package(SDL3/Vulkan/...) 与 Vulkan SDK 解析
└── vcpkg-overlays/           # vcpkg overlay portfile（joltphysics、ktx）

src/cmake/
└── SourceFiles.cmake         # 用 GLOB_RECURSE 集中定义各模块/子项目的源文件分组
```

目标（可执行文件 / 库）定义在 `src/CMakeLists.txt`。

## 模块职责

- **SetupPlatform.cmake**：设置 `CMAKE_RUNTIME/LIBRARY/ARCHIVE_OUTPUT_DIRECTORY`（产物落在 `bin/` 与 `lib/`）、`CMAKE_DEBUG_POSTFIX`、以及 iOS / Android / 桌面的架构与签名属性。
- **ProjectOptions.cmake**：定义 `gk_project_options` INTERFACE 库，集中 C++ 标准、Windows 宏（`UNICODE`/`NOMINMAX`/`WIN32_LEAN_AND_MEAN` 等）与 MSVC 警告级别，供各 target 链接。
- **SetupExternalLibs.cmake**：外部下载由 `gnb setup` 完成，本模块只用 `gk_require_path` 之类的 helper 校验 Slang / Vulkan SDK 等路径是否就位，缺失即 `FATAL_ERROR`。
- **SetupDependencies.cmake**：集中 `find_package(SDL3 ...)`、`find_package(Vulkan ...)` 及项目托管 Vulkan SDK 的解析逻辑。
- **src/cmake/SourceFiles.cmake**：用 `GLOB_RECURSE` 把 `Engine/`、各 `Application/` 子项目、测试等源文件收敛成 `src_files_*` 变量。**新增/删除源文件后需 `gnb build --reconfigure`**，CMake 才会重新跑 glob。
