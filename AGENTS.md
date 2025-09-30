# Repository Guidelines / 仓库指南

## 项目结构与模块 / Project Structure & Modules
- `src/`：引擎与入口程序所在；`Application/` 提供渲染器、编辑器、基准测试和 MagicaLego 启动器；`Rendering/`、`Vulkan/`、`Runtime/` 负责渲染核心；`Assets/` 维护运行时资源加载与着色器桥接。<br>`src/` hosts engine code and entry points; `Application/` launches renderer, editor, benchmarks, and MagicaLego; `Rendering/`, `Vulkan/`, and `Runtime/` deliver the graphics core; `Assets/` bundles loaders and shader glue.
- `assets/`：素材与着色器源文件，`Assets` CMake 目标会在构建时同步到 `build/*/assets`。<br>`assets/` stores art, shader sources, and scripts staged by the `Assets` target into `build/*/assets`.
- `tools/`：工具链与脚本（`typescript/`、`node/`）；分发元数据位于 `package/`；长文档放在 `doc/`。<br>`tools/` holds auxiliary toolchains (`typescript/`, `node/`); packaging metadata lives in `package/`; long-form notes reside under `doc/`.

## 构建与测试命令 / Build, Test, and Dev Commands
- `./prepare.sh setup` 自动检测 macOS/Linux 并安装依赖；`./prepare.sh slangc` 下载 Slang 着色器编译器。<br>`./prepare.sh setup` auto-detects macOS/Linux dependencies; `./prepare.sh slangc` fetches the Slang compiler.
- `./vcpkg.sh <platform>`（Windows 使用 `vcpkg.bat`）安装三方库，依赖变化时请重跑。<br>`./vcpkg.sh <platform>` (`vcpkg.bat` on Windows) bootstraps third-party libraries; rerun when dependencies shift.
- vcpkg 默认克隆到仓库根目录的 `.vcpkg`；设置 `VCPKG_ROOT` 可复用共享缓存或自定义路径。<br>vcpkg clones into `.vcpkg` by default; set `VCPKG_ROOT` to reuse a shared cache or pick a custom location.
- `./build.sh <platform>`（或 `build.bat`）调用 CMake+Ninja，产物位于 `build/<platform>/bin/`；macOS 默认配置为 `RelWithDebInfo`，如需 Debug 请手动覆写 `CMAKE_BUILD_TYPE`。<br>`./build.sh <platform>` (`build.bat`) drives CMake+Ninja; macOS runs `RelWithDebInfo` by default—override `CMAKE_BUILD_TYPE` if you need Debug.
- 增量编译：`cmake --build build/<platform> --target gkNextRenderer`（替换目标名称即可）。<br>Incremental builds: `cmake --build build/<platform> --target gkNextRenderer` (swap target as needed).
- 运行可执行文件（渲染器、编辑器、基准程序）后再提交，确认性能与渲染输出。<br>Run renderer/editor/benchmark binaries before submitting to validate performance and visuals.
- 准备测试或切分支前，使用 `git status` 确认工作区干净；若需临时保存改动，使用 `git stash push -u` 并在回到任务后恢复。<br>Before testing or switching branches, run `git status` to ensure a clean tree; stash work-in-progress via `git stash push -u` when you need temporary isolation.
- iOS 构建会自动下载 MoltenVK；可提前执行 `tools/fetch_moltenvk.sh ios` 复用缓存。<br>iOS builds auto-fetch MoltenVK; run `tools/fetch_moltenvk.sh ios` beforehand to reuse the cache.
- Android 依赖清单按平台裁剪：`./vcpkg.sh android` 会跳过 SDL3 并使用内置 `imgui` 定制；Gradle 工程仍需在 `android/app/libs` 放置官方 SDL3 `.aar`。<br>The Android manifest skips SDL3 (use the `.aar` in `android/app/libs`) and installs an imgui build without the SDL3 binding.

## 命名与清理流程 / Naming & Cleanup Workflow
- 推荐使用 `./build.sh <platform>` 生成非 Unity 的 `compile_commands.json`（可通过 `-DGK_ENABLE_UNITY_BUILD=OFF` 禁用 Unity Build）以配合静态分析。<br>Generate a non-unity `compile_commands.json` via `./build.sh <platform>` (`-DGK_ENABLE_UNITY_BUILD=OFF`) for static tooling.
- 项目根目录维护 `.clang-tidy`（默认函数/方法 PascalCase，变量 camelCase，私有成员尾随 `_`，宏大写；第三方目录自动排除），并在 `tools/clang-tools/` 提供 `run-naming.sh`、`run-clang-tidy.py` 等脚本。<br>A repo-level `.clang-tidy` enforces PascalCase for functions/methods, camelCase for variables, trailing `_` on private members, uppercase macros, excluding ThirdParty; helper scripts live under `tools/clang-tools/`.
- 批量命名或头文件梳理时，使用 `python3 tools/clang-tools/run-clang-tidy.py -p build/<platform> ... -fix -j <N>`；需确保 `PATH` 含 `/opt/homebrew/opt/llvm/bin` 获取 `clang-tidy` 与 `clang-apply-replacements`。<br>For bulk renames/include cleanup, run `python3 tools/clang-tools/run-clang-tidy.py -p build/<platform> ... -fix -j <N>` with `/opt/homebrew/opt/llvm/bin` on `PATH`.
- 命名自动化后仍需手动 check API 符号（如 `Sample()`/`sample()`），并保留人工例外：函数/方法 PascalCase、全局变量首字母大写(`GOption`)、局部/参数 camelCase、常量 camelCase。<br>After automation, spot-check API identifiers (e.g., `Sample()` vs `sample()`); enforced conventions: PascalCase for functions/methods, leading-cap globals (`GOption`), camelCase locals/params, camelCase constants.
- 拼写修正建议辅以 `codespell` 或正则排查（例如 “available”、“Thumbnail” 等常见错误）。<br>For spell fixes, consider `codespell` or targeted regex sweeps (e.g., “available”, “Thumbnail”).

## 代码风格与命名 / Coding Style & Naming
- C++ 使用 C++20/C11，开启警告即错误；避免无用符号，优先使用标准库。<br>C++ targets C++20/C11 with warnings-as-errors; avoid unused symbols and prefer STL solutions.
- 缩进四空格，函数同行大括号；类型使用 `PascalCase`，函数与局部变量使用 `camelCase`，私有成员使用结尾下划线（例：`supportRayTracing_`）。<br>Indent four spaces, braces on function lines; types in `PascalCase`, functions/locals in `camelCase`, private members with trailing underscore.
- 着色器与脚本命名需对应运行时标识（如 `ERT_*`）；保持 Stage 文件后缀一致。<br>Align shader/script names with runtime enums (e.g., `ERT_*`) and stage-specific suffixes.
- TypeScript 脚本遵循 `src/Typescript/tsconfig.json`，使用 ES Module，测试桩集中在 `test.ts`。<br>TypeScript utilities honor `src/Typescript/tsconfig.json`, use ES modules, and keep stubs in `test.ts`.

## 测试规范 / Testing Guidelines
- 当前无自动化测试；需在关键平台构建并手动验证 `gkNextRenderer`、`gkNextEditor` 与基准程序。<br>No dedicated automated suite; build relevant targets per platform and validate manually.
- 修改脚本后需重建资产，确保 `assets/scripts/*.js` 同步；可通过 `npx ts-node src/Typescript/test.ts` 快速检查。<br>Resync assets after script edits to update `assets/scripts/*.js`; use `npx ts-node src/Typescript/test.ts` for quick checks.
- 性能/画质改动请记录截图或 FPS 数据。<br>Capture screenshots or FPS metrics for performance/visual changes.

## 提交与合并请求 / Commit & PR Guidelines
- Git 历史偏好短促命题式提交（如 `fix platform judgement`、`more android competibale`）；保持单一关注点。<br>History favors concise, imperative commits (e.g., `fix platform judgement`, `more android competibale`); keep each commit scoped.
- 提交 PR 时说明改动内容、构建平台（如 `macOS build.sh`、`windows build.bat`）、并附关键截图或性能对比。<br>PR descriptions should outline changes, list verified build platforms, and attach visuals or perf comparisons.
- 引入可选依赖（AVIF、Superluminal 等）记得在描述中标注，方便复现。<br>Call out optional dependencies (AVIF, Superluminal, etc.) in PR notes for reproducibility.
