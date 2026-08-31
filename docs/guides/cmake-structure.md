---
title: "CMake 结构与目标维护"
category: guide
status: 现行
owner: build
created: 2026-06-26
last_updated: 2026-08-31
---

# CMake 结构与目标维护

当前构建已经从单一巨型 `src/CMakeLists.txt` 拆为分层、按 owner 维护的 CMake 文件。新增 target 时应放在对应目录，不要把应用和模块重新集中回根文件。

## 文件职责

- 根 `CMakeLists.txt`：preset 入口、平台选项、依赖发现、shader/assets 等全局步骤。
- `tools/android/CMakeLists.txt`：Android host driver；校验 SDK/NDK/JDK，生成临时 Gradle 工程，并编排 APK、安装、启动和 logcat target。
- `CMakePresets.json` 的 `ios-device`：选择 `iphoneos`、arm64 和 iOS vcpkg triplet；具体 application 由 `GK_MOBILE_APP` 选择。
- `src/CMakeLists.txt`：公共 helper、Engine/Modules/Gameplay/Application/Tests 子目录和标准 runtime module 集合。
- `src/cmake/SourceFiles.cmake`：Engine、Gameplay 和可选 module 的共享源码集合；Android 单体布局也使用它。
- `src/cmake/TargetHelpers.cmake`：编译选项、include、unity/链接策略，以及 `gk_configure_module`、`gk_add_application`、`gk_target_runtime_modules`。
- `src/cmake/RuntimeFeatures.cmake`：runtime feature 选择与相关配置。
- `src/Engine/`、`src/Modules/`、`src/Gameplay/`、`src/Application/**`、`src/Tests/` 下的 `CMakeLists.txt`：各层或各 target 自己拥有的源文件、链接依赖和平台条件。

源码收集主要使用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`。新增已被现有 glob 覆盖的 `.cpp/.hpp/.h` 通常会触发 CMake 自动重扫；只有修改 CMake/preset、创建未被 glob 覆盖的文件或生成器未重扫时才需要显式 `--reconfigure`。

## 新增 module

1. 在 `src/Modules/<Name>/` 放源码与 `CMakeLists.txt`，创建同名 static library。
2. 在 `src/cmake/SourceFiles.cmake` 的 `GK_MODULE_NAMES` 加名字，使共享/Android 路径可见。
3. 调用 `gk_configure_module(<Name>)`，只链接 module 自己真实需要的依赖。
4. 由应用在 `gk_add_application(... MODULES ...)` 中显式选择；只有普遍桌面 runtime 都需要时才考虑加入 `GK_STANDARD_RUNTIME_MODULES`。

Engine 核心不得依赖 `Modules/`。跨 module 能力优先通过 Engine interface/registration 注入，而不是反向 include。

## 新增应用

在对应 `src/Application/<Role>/<Name>/CMakeLists.txt` 调用统一入口：

```cmake
gk_add_application(MyTarget
    SOURCES ${MY_TARGET_SOURCES}
    MODULES NextUI MyOptionalModule
    LINK NextGameplay)
```

`MODULES` 用于运行时 module，`LINK` 用于 `NextGameplay` 这类引擎库；自带 `main()` 的工具传 `NO_DEFAULT_MAIN`。不要手写 `add_executable` + `gk_configure_application`，因为桌面、Android 和 iOS 的 application 形态不同。随后确认父目录 `CMakeLists.txt` 有 `add_subdirectory`。

若 application 支持移动端，还要登记到 [`MobileApplications.json`](../../src/Application/MobileApplications.json)。Android/iOS 每次只配置登记表中选中的一个 application，因此其叶子目录必须自洽，不能依赖父目录临时设置的变量。

## 验证口径

```bash
# Engine/API 改动
./gnb.sh build gkNextRenderer gkNextUnitTests

# 单个应用
./gnb.sh build NextRA

# CMake 结构或新增 target
./gnb.sh build <target> --reconfigure
```

纯文档、gnb Go 代码或 tools 修改不需要 C++ 构建。全量 `./gnb.sh build --reconfigure` 只用于大型 ABI/广泛 header 重构、影响范围确实不清楚或用户明确要求的情况。

Android 不使用根目录下的长期 Gradle 工程。`gnb android build <variant> [--app <target>]` 配置对应 driver，native、asset、Gradle 和 APK 产物都留在该 application 的构建目录。

iOS 同样只有根 CMake build graph。macOS Vulkan SDK 同时提供 headers、host `slangc` 与
`MoltenVK.xcframework`；CMake 固定选择 `ios-arm64` device slice。项目不提供 iOS Simulator
构建或运行路径；`gnb ios build [--app <target>] --team-id <TEAM_ID>` 是 device preset 的薄封装。
