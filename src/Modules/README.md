# src/Modules — 可选引擎模块

每个子目录是一个独立静态库（`add_library(<Name> STATIC ...)`），由需要它的
Application 显式链接；核心层 `src/Engine` 不得反向依赖本目录。

构建机制见 `src/cmake/SourceFiles.cmake`（`GK_MODULE_NAMES` / `src_files_module_*`）
与 `src/CMakeLists.txt`（`GK_MODULE_TARGETS`）：模块目录为空时自动跳过，
Android 平台模块源直接并入单一 SHARED target。

规划中的模块（见 `docs/EngineCoreRefactor.md`）：

| 模块 | 来源 |
|---|---|
| LDrawLoader | `Engine/Assets/Loaders/FLDraw*` |
| ScadLoader | `Engine/Assets/Loaders/FScad*` |
| NextAI | `Engine/Runtime/Subsystems/AI*` |
| NextRemote | `Engine/Runtime/Remote/` |
| NextRmlUi | `Engine/Runtime/UI/RmlUiSystem` |
| DevTools | 控制台 UI、调试 overlay、ProfessionalUI、Gizmo 等 |
