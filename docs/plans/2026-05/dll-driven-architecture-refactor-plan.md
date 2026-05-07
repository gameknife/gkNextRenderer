# DLL 化开发架构改造计划（Launcher + Editor + Game Plugins）

> **作者：** 架构调研 Agent · 2026-05-07
> **范围：** `src/CMakeLists.txt`、`src/cmake/SourceFiles.cmake`、`src/DesktopMain.cpp`、`src/Common/PluginExport.hpp`、`src/Editor/*`、`src/Runtime/Plugin/*`、`src/Runtime/Engine.{hpp,cpp}` 以及 `src/Application/Flappy/*` 子目录（A2 阶段）；其余 `src/Application/*` 仅在 A6 阶段批量改造。
> **目的：** 把当前实验性的 `gkNextHost + gkNextEngineShared + FlappyCppPlugin` 模式固化为 **开发期默认形态**：两个瘦入口（`gkNextLauncher`、`gkNextEditor`）+ 一份带 editor 代码的 `gkNextEngine.dll` + 若干 `${Game}Plugin.dll`；编辑器支持 in-process 加载 game plugin 实现 "Play in Viewport"（PIE）。**A2 阶段范围限定 FlappyCpp + FlappyJs**，其余游戏维持 monolithic exe 共存；后续在 A6 批量推进。Packager 与 unit tests 走静态链接路径（`gkNextEngineStatic`），不依赖 engine.dll。**发布期** 保留全部 monolithic 编译路径不变。
> **执行约定：** 桌面三平台（Windows/Linux/macOS）启用，移动端（Android/iOS）继续走 monolithic 路径。每个阶段都给出独立可交付的 PR 切片与验收命令。

---

## TL;DR

| 阶段 | 目标 | 主产出 | 平台 | 风险 |
|---|---|---|---|---|
| **A0** | 接口/状态盘点 + ABI 边界文档化 | 设计文档 + AGENT_GUIDE 章节 | 全桌面 | 低 |
| **A1** | CMake 构建矩阵：`GK_PRIMARY_DEV_MODE` 开关 + 双库（SHARED + STATIC）共存 + 把第三方库统一锚定到 SHARED engine | `gkNextEngine`（SHARED）和 `gkNextEngineStatic`（STATIC）并存；`gkNextLauncher` 入口（保留 `gkNextHost` 过渡期 alias） | Win/Linux/macOS | 中（链接重组、OBJECT lib 拆分） |
| **A2** | **最小可行 plugin 集**：只把 `FlappyCpp` 与 `FlappyJs` 改造为 `${Game}Plugin`；其他 9 个游戏维持原 monolithic exe | `FlappyCppPlugin.dll` / `FlappyJsPlugin.dll`；`gkNextLauncher --game=FlappyCpp/FlappyJs` 全流程跑通 | Win/Linux/macOS | 低（FlappyCpp 已有 plugin 雏形，FlappyJs 复刻） |
| **A2.5** | Launcher 内置 Game Picker：无 `--game` 启动时枚举 `bin/*Plugin.{dll,so,dylib}` 并以 ImGui 列表让用户点击进入 | 双击 `gkNextLauncher.exe` 也能用 | Win/Linux/macOS | 中（引擎需支持 deferred game instance） |
| **A3** | 编辑器解耦：`EditorGameInstance` → `EditorShell`（编辑器子系统），改由引擎直接持有；`EditorInterface` 切到引擎 API | `gkNextEditor.exe` 在没有任何 game plugin 的情况下也能进入空场景编辑 | 全桌面 | 中（涉及 Editor 全套类的指针迁移） |
| **A4** | PIE 主循环：编辑器内可加载/卸载 game plugin，play/pause/stop 三状态机 | 编辑器顶部工具栏加 ▶/■ 按钮；scene 状态可保留/恢复；plugin 热重载在 Play 模式仍可用 | 全桌面 | 高（双 game-instance 槽位、reflection 资源生命周期） |
| **A5** | 发布路径回归：`monolithic` preset 验证所有 12 个游戏可执行版本仍可出包；CI 对比 dev/release 输出 | preset 矩阵 + run.bat/run.sh 兼容 | 全桌面 | 低 |
| **A6（未来）** | 剩余 9 个游戏批量 plugin 化（沿用 A2 模板） | 全部 `Application/*` 输出 plugin DLL（仅 dev 模式） | Win/Linux/macOS | 低-中（机械批改造） |

---

## 1. 现状盘点

### 1.1 已有 plugin 路径（FlappyCpp 模式）

- `src/CMakeLists.txt:100-109` —— `gkNextEngineShared` 是 `gkNextEngine` 同源 SHARED 库，定义 `ENGINE_API_SHARED` + `WINDOWS_EXPORT_ALL_SYMBOLS=ON`，仅在 `GK_ENABLE_HOT_RELOAD=ON` 时构建。
- `src/CMakeLists.txt:152-162` —— `gkNextHost` 是只含 `DesktopMain.cpp`（`GK_HOST_APP=1`）的可执行；`FlappyCppPlugin` 是 SHARED 库，链接 `gkNextEngineShared`，定义 `GK_BUILDING_GAME_PLUGIN=1`，源码是 `${src_files_flappycpp} + Application/PluginEntry.cpp`。
- `src/Application/PluginEntry.cpp` —— 三个导出符号：`gkCreateGameInstance / gkDestroyGameInstance / gkPluginAbiVersion`。
- `src/Common/PluginExport.hpp` —— `GK_ENGINE_ABI_VERSION = 20260507u`；`GetEngineHotReloadAbiVersion()` 在 `Engine.hpp:413-424` 把 `sizeof(NextGameInstanceBase)` / `sizeof(NextEngine)` / Debug-vs-Release 异或进版本号，已经能阻拦 ABI 漂移。
- `src/Runtime/Plugin/PluginLoader.{hpp,cpp}` —— 实现 shadow-copy 加载 + mtime 轮询 + reload；同时被 `Engine::CreateConfiguredGameInstance()`（`Engine.cpp:299-331`）和 `Engine::TickHotReload()`（`Engine.cpp:360-409`）调用。
- 入口分叉：`DesktopMain.cpp:49-57` 用 `#if !defined(GK_HOST_APP)` 区分静态工厂 vs 空工厂；host 路径完全依赖 plugin loader 注入 `gameInstance_`。

### 1.2 现存 monolithic 游戏列表（12 个）

| 应用 | 静态 source 集合 | 是否需要改造 |
|---|---|---|
| `gkNextRenderer` | `${src_files_gkrenderer}` | 是（标准 plugin 化） |
| `gkNextStillBenchmark` / `gkNextMotionBenchmark` | benchmark common + 各自 entry | 是（plugin 化），共享 benchmark common 代码可拆为 PUBLIC INTERFACE 库 |
| `gkNextVisualTest` | `${src_files_gkvisualtest}` | 是（plugin 化），但 baseline 工作流要保留 |
| `gkNextEditor` | `${src_files_editor}` | **特殊**：本身就是 host，需要进一步解耦（详见 A3/A4） |
| `MagicaLego` | `${src_files_magicalego}`，附带 `ffmpeg.exe` 拷贝逻辑（CMakeLists.txt:480-489） | 是（plugin 化，附带资源步骤随 plugin target 移动） |
| `KongLie3D` / `Brotato3D` | 各自 source | 是 |
| `FlappyCpp` / `FlappyJs` | 各自 source | FlappyCpp 已是双 target（exe + plugin），可留作 release 验证；FlappyJs 改造 |
| `BrickPlayer` / `CharacterDemo` | 各自 source；CharacterDemo 还链接 `NextGameplay` 静态库（CMakeLists.txt:516-517） | 是；`NextGameplay` 链接需要在 plugin 端复刻 |
| `Packager` | 仅 `PackagerMain.cpp`，**不是游戏**，不进入 plugin 化范围 | 否，保留为独立 exe |
| `gkNextUnitTests` | Catch2 测试 | 否，保留为独立 exe |

### 1.3 编辑器特殊性（EditorGameInstance）

- `src/Editor/EditorMain.{cpp,h}` —— `EditorGameInstance: NextGameInstanceBase`，构造时直接修改 `WindowConfig`（标题/尺寸/`HideTitleBar=true`/`ForceSDR=true`/`KeepCPUMeshData=true`）。这部分要在 A3 中迁移到 **Editor 启动时的引擎初始化路径**，避免它和未来 PIE 加载的 game-plugin 抢同一个 `gameInstance_` 槽。
- `src/Editor/EditorInterface.{hpp,cpp}` —— `EditorInterface` 持有 `EditorGameInstance* editor_`，访问 `editor_->Actions() / editor_->GetGizmoController() / editor_->DrawGizmo() / editor_->GetEngine()`（EditorInterface.cpp:268,346,351）。这些指针都需要在 A3 中改为指向新的 `EditorShell` 或 `NextEngine` 直接 API。
- `src/Editor/AI/EditorAIService.cpp:9,26` 与 `src/Editor/AI/EditorScriptExecutor.cpp:16,888` 通过 `engine_.GetQuickJSEngine()` 访问 QuickJS，这条路径在 plugin 化后**继续可用**（QuickJS 实例属于 engine.dll，不在 plugin 侧）。
- `ImNodeFlow` 在 `src/CMakeLists.txt:195-196,503-504,527` 仅链接到 `gkNextEditor` —— 改造时随 editor 源码一起合入 shared engine（详见 §4.2）。

### 1.4 第三方库链接现状（关键约束）

`src/CMakeLists.txt:342-456` 把所有第三方库直接 `target_link_libraries(${target} PRIVATE ...)` 到 engine target，包括：

- 必装：SDL3、xxHash、meshoptimizer、fmt、CURL、glm、imgui、draco、WebP、Vulkan、cpptrace、spdlog（PUBLIC）。
- 条件装：Streamline / OIDN / Jolt / quickjs / whisper.cpp / avif / ozz / KTX2 / SuperluminalAPI。

这些第三方均为 STATIC 库或 import lib（vcpkg 提供的形式）。**关键含义：**

> 同一份静态第三方库在同一进程中只能被链接进 **一个** SHARED 模块。一旦同时存在 `engine.dll` 和 `editor.dll` 各自吞掉一份 SDL3/imgui，立即出现：
> - 全局符号重复或运行时分裂（imgui context、SDL event queue、Jolt physics world 各持一份）；
> - Windows 下静态 CRT 与 `__declspec(dllimport)` 冲突；
> - macOS 下双份 `_main` / `dyld` 警告。

→ **结论：编辑器代码必须最终被链接到 shared engine 同一份 DLL 内**（用户提到的 "把 editor 做成带 editor 代码的 engine.dll" 是正解），不再独立成 `editor.dll`。

---

## 2. 目标形态

### 2.1 开发期（`GK_PRIMARY_DEV_MODE=ON`，桌面三平台默认）

```
out/build/<preset>/bin/
├── gkNextEngine.dll              ← SHARED engine + editor 代码 + ImNodeFlow + 所有第三方静态库
├── gkNextEngineStatic.lib(.a)    ← STATIC 同源库，仅链时使用；供 Packager / unit tests 等无需 DLL 的工具
├── gkNextLauncher.exe            ← 瘦 host，链接 gkNextEngine.dll；不含 editor UI；无 --game 时弹 picker
├── gkNextEditor.exe              ← 瘦 host，链接 gkNextEngine.dll；启动后初始化 EditorShell 子系统
├── FlappyCppPlugin.dll           ← A2 阶段交付
├── FlappyJsPlugin.dll            ← A2 阶段交付
│
├── gkNextRenderer.exe            ← 维持 monolithic（链接 gkNextEngineStatic）
├── MagicaLego.exe                ← 维持 monolithic
├── KongLie3D.exe / Brotato3D.exe / FlappyCpp.exe / FlappyJs.exe / BrickPlayer.exe / CharacterDemo.exe
├── gkNextStillBenchmark.exe / gkNextMotionBenchmark.exe / gkNextVisualTest.exe
├── Packager.exe                  ← 静态链接 gkNextEngineStatic，不依赖 engine.dll
├── gkNextUnitTests.exe           ← 静态链接，保留独立 exe
└── _hot/                         ← PluginLoader shadow-copy 工作目录
```

> **A2 阶段** dev 模式输出：launcher + editor + 上述两个 plugin 共四个新形态产物，与 9 个 monolithic exe 长期共存；A6 之后可逐步淘汰部分 monolithic exe（保留 release 路径）。

> **双库策略：** `gkNextEngine`（SHARED）与 `gkNextEngineStatic`（STATIC）从同一份源码编译两次，靠 CMake `OBJECT` 中间库共享编译产物（详见 §3.3）。SHARED 版本被 launcher / editor / plugins 链接；STATIC 版本被 Packager / unit tests / monolithic exes 链接。同一进程不会同时存在两份 engine 实例。

启动方式：

```bash
# 直接跑某游戏（A2 后仅 FlappyCpp/FlappyJs 走这条路；A6 后扩展）
./bin/gkNextLauncher --game=FlappyCpp

# 双击 launcher，无参数 → 弹 game picker（A2.5）
./bin/gkNextLauncher

# 进编辑器（空场景）
./bin/gkNextEditor

# 进编辑器并 PIE 启动一个游戏（设计目标，A4 完成后可用）
./bin/gkNextEditor --game=Brotato3D --pie=play
```

### 2.2 发布期（`GK_PRIMARY_DEV_MODE=OFF`，所有平台均可用）

继续产出今天的 12 个 monolithic 可执行（`MagicaLego.exe` / `Brotato3D.exe` / …），逻辑等价于把 `${Game}Plugin` 与 `gkNextEngine`（STATIC）一起静态链接到一个 exe。这是 **Android/iOS 的唯一路径**（`GK_ENABLE_HOT_RELOAD=OFF`）；也是商店发布、Steam 包、benchmark 服务器等强可重复构建场景的首选。

### 2.3 入口与进程模型对照

| 入口 | 是否含 editor | 是否加载 game plugin | 备注 |
|---|---|---|---|
| `gkNextLauncher` | 否 | **是**：带 `--game=<Name>` 直接加载；不带参数 → ImGui game picker（A2.5） | 取代旧 `gkNextHost`；纯运行时 |
| `gkNextEditor` | 是（编辑器作为引擎子系统） | 可选；不带 plugin 也可进空场景；带 plugin 即进入 PIE-ready 状态 | 取代当前 `gkNextEditor.exe` 的 `EditorGameInstance` 模型 |
| `<Game>.exe`（dev/release 共存） | 否 | N/A，整体单体编译，链接 `gkNextEngineStatic` | A2 阶段不动；A6 才考虑下线部分 |
| `Packager` | 否 | N/A | 静态链接 `gkNextEngineStatic`，不依赖 `engine.dll`，保持单文件可分发 |
| `gkNextUnitTests` | 否 | N/A | 静态链接 `gkNextEngineStatic` + Catch2，保持独立 exe |

---

## 3. 构建矩阵设计

### 3.1 新增 CMake 选项

```cmake
# CMakeLists.txt
option(GK_PRIMARY_DEV_MODE "Build shared engine + game plugins as the primary dev layout" ON)
if (ANDROID OR IOS OR MINGW)
    set(GK_PRIMARY_DEV_MODE OFF CACHE BOOL "" FORCE)
endif()

# 兼容旧开关：DEV mode 隐含 HOT_RELOAD 能力
if (GK_PRIMARY_DEV_MODE)
    set(GK_ENABLE_HOT_RELOAD ON CACHE BOOL "" FORCE)
endif()
```

### 3.2 target 拓扑（dev mode）

| target | 类型 | 链接 | 备注 |
|---|---|---|---|
| `gkNextEngineCore` | OBJECT | 全部 3rd-party headers（仅编译） | 中间产物：`${src_files_engine,assets,utilities,vulkan,rendering,thirdparty}` 编译一次，复用到 SHARED 与 STATIC |
| `gkNextEngineEditorCore` | OBJECT | 同上 + ImNodeFlow headers | 仅 dev 模式：editor 源 + ImNodeFlow，吞进 SHARED 版本 |
| `gkNextEngine` | SHARED | `$<TARGET_OBJECTS:gkNextEngineCore>` + `$<TARGET_OBJECTS:gkNextEngineEditorCore>` + 全部 3rd-party | 给 launcher / editor / plugin 用；定义 `ENGINE_API_SHARED` + `ENGINE_EXPORTS` |
| `gkNextEngineStatic` | STATIC | `$<TARGET_OBJECTS:gkNextEngineCore>` + 全部 3rd-party（INTERFACE 传递） | 给 Packager / unit tests / monolithic exe 用；不含 editor 源 |
| `NextGameplay` | STATIC | `gkNextEngineCore` headers | 跨 plugin/exe 复用的 gameplay 公共代码（保持 STATIC） |
| `gkNextLauncher` | EXECUTABLE | `gkNextEngine`、`SDL3` | 旧 `gkNextHost` 改名；保留 alias 过渡期；定义 `GK_LAUNCHER_APP=1` |
| `gkNextEditor` | EXECUTABLE | `gkNextEngine`、`SDL3` | 仅入口 `DesktopMain.cpp`；定义 `GK_EDITOR_APP=1`；editor 源不再链接到此 target |
| `FlappyCppPlugin` / `FlappyJsPlugin` | SHARED | `gkNextEngine`、可选 `NextGameplay`、`PluginEntry.cpp` | A2 阶段唯二 plugin；`FlappyCppPlugin` 已在主分支存在，仅做收口 |
| 其余 9 个 `<Game>` | EXECUTABLE | `gkNextEngineStatic`、可选 `NextGameplay` | A2 不动；与 dev 模式的 launcher/editor 共存于同一 bin 目录 |
| `Packager` | EXECUTABLE | `gkNextEngineStatic` | 静态链接，无 DLL 依赖；保持单文件分发能力 |
| `gkNextUnitTests` | EXECUTABLE | `gkNextEngineStatic`、`NextGameplay`、`Catch2` | 静态链接，CI 友好 |

> **关于 OBJECT lib：** CMake 3.12+ 对 `OBJECT` 库的 `target_link_libraries` 已支持 transitive `INTERFACE` 传递，但 vcpkg 提供的部分 `IMPORTED` target（如 `KTX::ktx`）在 OBJECT 模式下传递可能不稳。若验证 OBJECT 方案有坑，**降级方案** 是同一份源码列表分别 `add_library(... SHARED ...)` 和 `add_library(... STATIC ...)`，付出"编译两次"的代价（dev 完整重编预计多 +30s，可接受）。两种方案都在 A1.3 验证。

### 3.3 release/monolithic 模式

- 仅构建 `gkNextEngineStatic`；不构建 `gkNextEngine` SHARED、不构建任何 `${Game}Plugin`。
- 保留今天所有 `add_executable(<Game> ${src_files_<game>} DesktopMain.cpp)`，链接 `gkNextEngineStatic`。
- 用同一份 `src/Application/<Game>/` 源码，由 CMake `if(GK_PRIMARY_DEV_MODE)` 分支决定是否额外产出 plugin DLL。
- `PluginEntry.cpp` 仅在 dev 模式被加进 plugin target；exe 路径不引用它。
- Android/iOS 由顶层 CMake 强制 `GK_PRIMARY_DEV_MODE=OFF`，等价于 release 拓扑。

### 3.4 third-party 链接收口

把现有 `target_link_libraries(${target} PRIVATE ...)` 中针对 engine 的部分集中到一个 helper：

```cmake
function(gk_link_engine_thirdparty engine_target visibility)
    # visibility = PRIVATE（SHARED 引擎吞下符号）或 INTERFACE（STATIC 引擎需把链接传递给 exe）
    target_link_libraries(${engine_target} ${visibility}
        SDL3::SDL3 xxHash::xxhash meshoptimizer::meshoptimizer
        fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp
        ${Vulkan_LIBRARIES} cpptrace::cpptrace ${extra_libs}
        ${CMAKE_DL_LIBS}
    )
    target_link_libraries(${engine_target} PUBLIC spdlog::spdlog)
    # 条件依赖一并迁入：Streamline / OIDN / Jolt / quickjs / whisper / avif / ozz / KTX2 / Superluminal
endfunction()

gk_link_engine_thirdparty(gkNextEngine PRIVATE)
gk_link_engine_thirdparty(gkNextEngineStatic INTERFACE)
```

plugin/launcher/editor 不再各自 `target_link_libraries(... ${Vulkan_LIBRARIES})`，改为只 `target_link_libraries(... PRIVATE gkNextEngine)`，由 engine 的 `INTERFACE` 链接传递必要符号（`spdlog::spdlog` 已经是 PUBLIC，符合预期）。

> **必须验证：** Windows + MSVC 下，`WINDOWS_EXPORT_ALL_SYMBOLS=ON` 是否能正确导出 `Vulkan::*`、`Assets::*`、`NextEngine::*`、reflection 注册符号；不能就改用显式 `ENGINE_API` 标注（`Common/CoreMinimal.hpp:35-50` 已有宏体）。

---

## 4. 源码层迁移

### 4.1 Plugin 接口契约统一化

| 项 | 当前位置 | 改造动作 |
|---|---|---|
| 自由函数 `CreateGameInstance(...)` | 各游戏的 `<Game>GameInstance.cpp` 顶层 | **保持不变**：dev plugin 路径下被 `PluginEntry.cpp` 调用；release/monolithic 路径下继续被 `DesktopMain.cpp` 的静态工厂调用——同一份源码两条用途。 |
| `extern "C"` 导出 | `Application/PluginEntry.cpp` | 通过 CMake `target_sources(${Game}Plugin PRIVATE Application/PluginEntry.cpp)` 注入到 plugin target。**不**为每个游戏复制一份。 |
| ABI 版本号 | `Common/PluginExport.hpp:GK_ENGINE_ABI_VERSION` | 每次涉及 plugin-engine 边界的头改动都 +1；建议把版本号改为 `git describe`-style 或自动从 build.version 派生 |
| Window 配置回调 | `NextGameInstanceBase::ConfigureWindow` 静态保护方法 | 不变；plugin 在构造时调用 |

### 4.2 Editor 源码归并

把 `src/Editor/*` 全量加入 `src_files_engine`（dev mode 下），并把 `ImNodeFlow` 链接到 engine：

```cmake
file(GLOB_RECURSE src_files_engine ...)
if (GK_PRIMARY_DEV_MODE)
    list(APPEND src_files_engine ${src_files_editor})
endif()

if (GK_PRIMARY_DEV_MODE)
    add_subdirectory(ThirdParty/ImNodeFlow)
    target_link_libraries(gkNextEngine PRIVATE ImNodeFlow)
endif()
```

> 在 release/monolithic 模式下保留旧路径：editor 是独立 exe，源不进 engine。

### 4.3 EditorGameInstance → EditorShell

**核心拆分：** 把 `EditorGameInstance` 拆成两半：

1. **`EditorShell`**（新类，住在 `src/Editor/Core/EditorShell.{hpp,cpp}`）：
   - 不继承 `NextGameInstanceBase`；
   - 持有 `EditorInterface`、`ModelViewController`、`GizmoController`、`EditorActionDispatcher`；
   - 由引擎在启动 editor 入口时通过 `NextEngine::EnableEditorShell()` 创建并接管 `OnRenderUI / OnPreConfigUI / OnInitUI / OnKey / OnMouseButton / OnCursorPosition / OnScroll`；
   - 这些回调要么直接由 `NextEngine` 在主循环中调用，要么通过新增的 `IEngineSubsystem` 抽象注册（推荐后者，保持引擎主循环对 editor 不感知具体类型）。

2. **`PIEGameInstance` 槽位**（引擎侧）：
   - `NextEngine` 当前只有一个 `gameInstance_` 字段；扩展为：
     - `editorShell_`（仅 editor 入口下非空）
     - `gameInstance_`（保持原语义；launcher 入口下从 plugin 加载；editor 入口下默认空，PIE 启动时填入）
   - 主循环遍历两者：先 tick `editorShell_`（如有），再 tick `gameInstance_`（如有）。
   - PIE 期间，`editorShell_` 和 `gameInstance_` 同时存在；编辑器输入事件先经 `editorShell_`，再视情况转发给 `gameInstance_`（如鼠标在 viewport panel 内）。

**`EditorInterface` 接口收紧：**

- 把对 `EditorGameInstance::GetEngine()` 的调用全部替换为 `NextEngine::GetInstance()` 或构造时注入的 `NextEngine&`；
- `EditorInterface(EditorShell* shell)` 替代 `EditorInterface(EditorGameInstance* editor)`；
- `editor_->Actions()` / `editor_->GetGizmoController()` / `editor_->DrawGizmo()` 全部走 `shell_->...`，签名不变。

### 4.4 Launcher Game Picker（A2.5）

**问题背景：** 双击 `gkNextLauncher.exe` 而不带任何参数时，期待用户体验类似一个简单 launcher 界面：列出当前 `bin/` 目录里所有可启动的 plugin，点击其一即进入。

**约束：** 引擎在 `NextEngine::ctor` 中调用 `CreateConfiguredGameInstance()`（`Engine.cpp:281`）就已经需要 game instance。当前 `--hot-reload` + 无 `--game` 的组合会回落到 `NextGameInstanceVoid`，但没有 picker UI。

**设计：复用引擎已有 SDL+ImGui 栈，引入 "deferred game instance" 状态。**

1. **NextEngine 扩展：**
    - 新增 `NextEngine::SetDeferredGameInstance(std::string pluginName)`：被 launcher 主循环驱动，调用时通过 PluginLoader 销毁当前 `gameInstance_`（应为 `NextGameInstanceVoid`）并加载指定 plugin。
    - `NextEngine::DiscoverAvailablePlugins() -> std::vector<FPluginEntry>`：扫描 `CandidatePluginDirectories()`（已存在，`PluginLoader.cpp:30-43`），匹配 `*Plugin{debugPostfix}.{dll,so,dylib}`，跳过 `_hot/` 子目录，返回去重后的 `{displayName, sourcePath}` 列表。
    - 该机制同时被 PIE 用到：editor 工具栏的 plugin 选择下拉框复用同一函数。

2. **Launcher UI（仅 launcher 入口注册）：**
    - 新增 `src/Runtime/Plugin/PluginPickerOverlay.{hpp,cpp}`（住在引擎库内、仅 launcher 入口启用）。
    - 实现一个 `IEngineSubsystem` 风格的 OnRenderUI 钩子：检测 `gameInstance_` 是 `NextGameInstanceVoid` 且无 `--game` → 全屏 ImGui modal 列出 `DiscoverAvailablePlugins()`，每条一个按钮 + 缩略图（缩略图可选，先用文字）。
    - 用户点击 → `engine.SetDeferredGameInstance(name)` → 主循环下一帧切换；overlay 自销毁。

3. **`gkNextLauncher` 入口注入：**
    ```cpp
    // DesktopMain.cpp（GK_LAUNCHER_APP 分支）
    NextEngine::SetGameInstanceFactory({});
    if (GOption->GameName.empty())
    {
        // 引擎构造时是 Void，启动后由 picker 注入
        NextEngine::EnableLauncherPicker();
    }
    ```

4. **回退路径：** Picker 启动 5s 后未发现任何 plugin → 切换为错误页（"No game plugins found in bin/. Build a `${Game}Plugin` target or pass `--game=<Name>`"），按 Esc 退出。

**为什么放在 A2.5（A2 之后、A3 之前）：** picker 一旦实现，A3 的 EditorShell + A4 的 PIE plugin 选择器可以直接复用同一份 `DiscoverAvailablePlugins()` 与 `SetDeferredGameInstance()`。A2 的 FlappyCpp/FlappyJs 不依赖 picker（`--game=` 显式传入即可），因此 A2 与 A2.5 可独立 PR。

**验收：**

```bash
# 双击启动（无参）→ picker 显示 FlappyCpp / FlappyJs 两个条目
./out/build/dev-windows/bin/gkNextLauncher.exe

# 显式指定 → 直接进入，picker 不出现
./out/build/dev-windows/bin/gkNextLauncher.exe --game=FlappyCpp
```

### 4.5 入口（DesktopMain）二分

`DesktopMain.cpp` 不再用 `#if !defined(GK_HOST_APP)` 分两条路。改为根据 target 决定：

| target | 入口模式 |
|---|---|
| `gkNextLauncher` | 不调 `SetGameInstanceFactory`，期待 `--game` + plugin loader |
| `gkNextEditor` | 不调 `SetGameInstanceFactory`，但调 `NextEngine::EnableEditorShell()`；可选地按 `--game` 触发自动 PIE |
| `<Game>.exe`（release） | 调 `SetGameInstanceFactory(CreateGameInstance)`，monolithic 路径 |

实现：保留单一 `DesktopMain.cpp` 文件，新增三个互斥宏：

```cpp
#if defined(GK_LAUNCHER_APP)
    NextEngine::SetGameInstanceFactory({});  // 必须有 plugin
#elif defined(GK_EDITOR_APP)
    NextEngine::SetGameInstanceFactory({});
    NextEngine::EnableEditorShell();         // 引擎在 ctor/Start 内拉起 EditorShell
#else
    NextEngine::SetGameInstanceFactory(
        [](Vulkan::WindowConfig& cfg, Options& opt, NextEngine* eng){
            return CreateGameInstance(cfg, opt, eng);
        });
#endif
```

各 target 的 CMake 定义：`-DGK_LAUNCHER_APP=1`、`-DGK_EDITOR_APP=1`，monolithic 不定义。

---

## 5. 引擎侧改动（PIE 主循环）

### 5.1 `NextEngine` 新增 API

```cpp
// Engine.hpp 新增
class NextEngine final
{
public:
    // ... 现有 API ...

    // Editor 子系统接入（仅 editor host 调用）
    void EnableEditorShell();
    bool HasEditorShell() const { return editorShell_ != nullptr; }
    EditorShell* GetEditorShell() { return editorShell_.get(); }

    // PIE 控制（editor 入口）
    bool BeginPlayInEditor(std::string_view gamePluginName);  // 加载 plugin → 创建 game instance → OnInit
    void EndPlayInEditor();                                   // OnDestroy → 卸载 plugin → 还原状态
    enum class EPlayMode { Edit, Play, Paused };
    EPlayMode GetPlayMode() const { return playMode_; }
    void SetPlayMode(EPlayMode mode);                          // Pause/Resume

    // PIE 状态保留：进入 Play 前快照 scene；退出时恢复
    // 复用 FHotReloadState 或新引入 FPIESnapshot
private:
    std::unique_ptr<EditorShell> editorShell_;
    EPlayMode playMode_{EPlayMode::Edit};
    FHotReloadState pieSnapshot_;  // 暂存 scene/camera/cvar 等
};
```

### 5.2 主循环顺序（伪代码）

```cpp
bool NextEngine::Tick(bool forcingDelta)
{
    TickHotReload();  // shader + plugin 自动 reload（包含 PIE 期间的 game-plugin reload）

    // 编辑器优先 tick UI（dock layout / panel / outliner / properties）
    if (editorShell_) editorShell_->OnTick(deltaSeconds_);

    // play 状态下转发到 game instance
    if (gameInstance_ && playMode_ == EPlayMode::Play)
        gameInstance_->OnTick(deltaSeconds_);

    // 渲染：editor shell 决定 viewport 大小、覆盖相机；game instance 通过 OverrideRenderCamera 在 Play 模式下抢回相机
    // ...
}
```

### 5.3 PluginLoader 在 editor 中的复用

- `PluginLoader` 已完成 shadow-copy + reload 全部逻辑，无需重写；
- editor 调用 `BeginPlayInEditor` 时 new 一个 `PluginLoader`，`Load(<Game>Plugin)` → `Create(...)` → 把返回值塞进 `gameInstance_`；
- `EndPlayInEditor` → `DestroyGameInstance(true)` + `pluginLoader_.reset()`；
- PIE 期间的 plugin 热重载：复用现有 `TickHotReload` 逻辑，加一行检查 "PIE 模式 + Play 状态才允许 reload"。

### 5.4 状态边界关键点

**保留在引擎（即跨 PIE 周期保留）：**

- Vulkan 设备 / SwapChain / 各 Renderer
- Scene 数据（除非 PIE 主动 reload scene）
- CVar 系统
- Audio / Physics / QuickJS / Animation 子系统
- Reflection 注册表（components 已在 engine 静态注册）
- Editor UI 状态（Dock 布局、recent scene 列表）

**Plugin 生命周期内（PIE 一次结束即销毁）：**

- 游戏侧 `NextGameInstanceBase` 派生类的所有成员
- 游戏注册的临时 timer/ticked task（注意：`AddTickedTask` 当前是 `std::function`，闭包持有 plugin DLL 内函数指针；PIE 结束前必须清空，否则 stop 后引擎主循环还在调用已卸载 DLL 的函数 → crash）

**新增引擎 API（必需）：**

```cpp
class NextEngine
{
public:
    // 给 plugin 注册的 task 自动绑定生命周期
    void AddPluginScopedTickedTask(TickedTask task);  // PIE 结束/plugin 卸载时自动清理
    void AddPluginScopedTimerTask(double delay, DelayedTask task);
};
```

---

## 6. 阶段化路线图

### A0 · 接口审计与契约固化（前置，纯文档）

| 任务 | 输出 | 验收 |
|---|---|---|
| A0.1 列出 plugin 边界上每个 `extern "C"` 函数与对应 C++ 类型的 layout 假设；标记哪些类型在跨 reload/PIE 时必须保持二进制布局稳定 | `AGENT_GUIDE/PluginABI.md`（新建） | 文档 PR；列出至少 10 个稳定类型（`Vulkan::WindowConfig` / `Options` / `NextEngine` / `Assets::Scene` / `Assets::Camera` / `FHotReloadState` / `NextGameInstanceBase` 虚表 / `NextCVar::FCVarSystem` 等） |
| A0.2 列出现有 12 个游戏的 `CreateGameInstance` 实现差异（哪些做了 `ConfigureWindow`、哪些直接改 `Options` 字段、哪些链接 `NextGameplay`），形成"统一改造清单" | 表格 | 文档 PR |
| A0.3 列出 EditorInterface / Panels / Nodes / Overlays 中所有对 `EditorGameInstance::*` 的引用，给出"重构动作"列 | 表格 | 文档 PR |
| A0.4 评估 release/monolithic 与 dev/plugin 模式下的二进制差异（启动时间、链接产物大小、调试体验），给出推荐缺省 | 性能评估文档 | 文档 PR |

### A1 · CMake 重构（基础设施）

**输入：** `CMakeLists.txt`、`src/CMakeLists.txt`、`src/cmake/SourceFiles.cmake`、`CMakePresets.json`。

| 任务 | 关键修改 |
|---|---|
| A1.1 新增 `option(GK_PRIMARY_DEV_MODE)`，桌面默认 ON，移动端强制 OFF；移除 `gkNextEngineShared` 与 `GK_ENABLE_HOT_RELOAD` 的强绑定（两者解耦） | `CMakeLists.txt` |
| A1.2 把 engine 第三方依赖收敛到 `gk_link_engine_thirdparty()` helper；plugin/exe 不再各自 link | `src/CMakeLists.txt` 重构 |
| A1.3 引入 `gkNextEngineCore`（OBJECT）+ 双库（`gkNextEngine` SHARED + `gkNextEngineStatic` STATIC）；dev 模式下 SHARED 吞下 editor 源 + ImNodeFlow；release 模式只产 STATIC | `src/CMakeLists.txt:91-109` |
| A1.4 把 `Packager` / `gkNextUnitTests` 改为链接 `gkNextEngineStatic`（dev 模式下也走 STATIC，避免 engine.dll 运行时依赖）；其余 9 个 monolithic 游戏 exe 同样链接 STATIC | `src/CMakeLists.txt:175-181, 386-394` |
| A1.5 重命名 target：`gkNextHost` → `gkNextLauncher`（用 CMake alias 保过渡期） | `src/CMakeLists.txt:152-156` |
| A1.6 新增 `gkNextEditor` 在 dev 模式下的"瘦入口"分支：仅 `DesktopMain.cpp` + `-DGK_EDITOR_APP=1`，**不**链接 `${src_files_editor}`；release/monolithic 模式下保留旧形态（独立 exe + 独立链接 editor 源） | `src/CMakeLists.txt:131-134` |
| A1.7 `CMakePresets.json` 增 `dev-windows` / `dev-linux` / `dev-macos-arm64`（隐含 `GK_PRIMARY_DEV_MODE=ON`），并把 `default-*` 的语义改为别名指向 dev preset；`release-*` / `full-*` 显式 `GK_PRIMARY_DEV_MODE=OFF` | `CMakePresets.json` |

**验收：**

```bash
cmake --preset dev-windows
cmake --build out/build/dev-windows --target gkNextEngine gkNextEngineStatic gkNextLauncher gkNextEditor FlappyCppPlugin Packager
# 启动 launcher（继承现有 FlappyCppPlugin，A2 范围内）
./out/build/dev-windows/bin/gkNextLauncher.exe --game=FlappyCpp
# 启动 editor（A3 之前会以 EditorGameInstance 形态启动；A1 仅验证链接通）
./out/build/dev-windows/bin/gkNextEditor.exe
# Packager 不依赖 engine.dll：把 engine.dll 临时改名后 Packager 仍可运行
mv ./out/build/dev-windows/bin/gkNextEngine.dll{,.bak}
./out/build/dev-windows/bin/Packager.exe --help && mv ./out/build/dev-windows/bin/gkNextEngine.dll{.bak,}
# 同时 release preset 仍能 build 出 monolithic exes
cmake --preset full-windows && cmake --build out/build/full-windows --target MagicaLego
```

### A2 · 最小可行 plugin 集（FlappyCpp + FlappyJs）

**目标：** 不动其余 9 个游戏，仅保证两条 Flappy 路径在 `gkNextLauncher --game=...` 下可跑。FlappyCpp 已经有 plugin target，A2 主要工作是 (1) 确保它在 A1 重构后的 CMake 拓扑下仍能正确链接，(2) 复刻同样模式给 FlappyJs。

**输入：** A1 的 CMake 基础设施。

| 任务 | 修改 |
|---|---|
| A2.1 把 `FlappyCppPlugin` 从"仅 `GK_ENABLE_HOT_RELOAD=ON` 时编译"改为"`GK_PRIMARY_DEV_MODE=ON` 时编译"；链接由 `gkNextEngineShared` 改为新的 `gkNextEngine`（SHARED） | `src/CMakeLists.txt:152-162, 372-377` |
| A2.2 新增 `FlappyJsPlugin` SHARED target，源 = `${src_files_flappyjs} + Application/PluginEntry.cpp`，链接 `gkNextEngine` | `src/CMakeLists.txt`（紧邻 FlappyCppPlugin） |
| A2.3 dev 模式下保留 `FlappyCpp` / `FlappyJs` 的 monolithic exe（不删除），方便对比 plugin 与 exe 的行为差异；release 模式不变 | `src/CMakeLists.txt:148-166` |
| A2.4 验证 `PluginLoader::ResolvePluginPath` 已覆盖 dev preset 的 bin 输出（已覆盖 `executableDir` 与 `current_path`），无需改 | 仅验证 |
| A2.5 验证 plugin 热重载在两条 Flappy 上仍工作（已有 e2e 流程） | 文档 + 手测 |

**验收：**

```bash
# Flappy 两版本均能由 launcher 启动并打印 "uploaded scene [...] to gpu"
./out/build/dev-windows/bin/gkNextLauncher.exe --game=FlappyCpp --fastexit
./out/build/dev-windows/bin/gkNextLauncher.exe --game=FlappyJs --fastexit

# Plugin 热重载冒烟（修改 FlappyCpp 内一个常量，rebuild plugin，进程不退出）
# 详见 docs/plans/2026-05/cpp-shader-hot-reload-plan.md 的 H2 验证步骤

# 单元测试 + 9 个 monolithic 游戏 + Packager 全部回归通过
./out/build/dev-windows/bin/gkNextUnitTests
./out/build/dev-windows/bin/MagicaLego.exe --fastexit
./out/build/dev-windows/bin/Packager.exe --help
# release 路径回归
cmake --build out/build/full-windows --target MagicaLego && ./out/build/full-windows/bin/MagicaLego.exe --fastexit
```

> **范围明确：** 本阶段**不**改造 `gkNextRenderer` / `MagicaLego` / `KongLie3D` / `Brotato3D` / `BrickPlayer` / `CharacterDemo` / `gkNextStillBenchmark` / `gkNextMotionBenchmark` / `gkNextVisualTest`。它们继续作为 monolithic exe 与 launcher/editor 共存于 dev bin 目录。批量改造在 A6 阶段执行（路线图末尾）。

### A2.5 · Launcher Game Picker

**输入：** A2 完成后的 launcher（无 `--game` 时回落 `NextGameInstanceVoid`）。

| 任务 | 修改 |
|---|---|
| A2.5.1 引擎新增 `DiscoverAvailablePlugins()` 与 `SetDeferredGameInstance(name)`；前者扫描 `bin/*Plugin{debugPostfix}.{dll,so,dylib}`，后者驱动 PluginLoader 替换 `gameInstance_` | `src/Runtime/Engine.{hpp,cpp}` + `src/Runtime/Plugin/PluginLoader.cpp` |
| A2.5.2 引擎新增 `EnableLauncherPicker()`：注册一个仅在 `gameInstance_` 是 Void 且无 `--game` 时显示的 ImGui modal | `src/Runtime/Plugin/PluginPickerOverlay.{hpp,cpp}`（新建） |
| A2.5.3 Launcher 入口检测无 `--game` 时调 `EnableLauncherPicker()`；用户点击后 picker 自销毁 | `src/DesktopMain.cpp`（GK_LAUNCHER_APP 分支） |
| A2.5.4 Picker UI：列表 + 启动按钮 + "No plugins found" 错误页（5s 超时） | `src/Runtime/Plugin/PluginPickerOverlay.cpp` |
| A2.5.5 退出策略：picker 显示中按 Esc → 引擎 `RequestClose()` | 同上 |

**验收：**

```bash
# 1. 双击 launcher（无参数）→ picker 弹出，列出 FlappyCpp / FlappyJs
./out/build/dev-windows/bin/gkNextLauncher.exe
# 2. 用户点击 FlappyCpp → 切到 Flappy 主菜单
# 3. 显式指定 game 时 picker 不出现
./out/build/dev-windows/bin/gkNextLauncher.exe --game=FlappyCpp
# 4. bin 目录无任何 plugin 时（临时移走两个 plugin） → picker 显示错误页
```

### A3 · 编辑器解耦（EditorShell）

**输入：** A2 完成后的稳定 dev 构建。

| 任务 | 修改 |
|---|---|
| A3.1 新建 `src/Editor/Core/EditorShell.{hpp,cpp}`：迁移 `EditorGameInstance` 的所有非 `NextGameInstanceBase` 成员；删除继承关系；改为持有 `NextEngine&` 引用 | `src/Editor/Core/*` |
| A3.2 `EditorInterface` 改为 `EditorInterface(EditorShell&)`；所有 `editor_->...` 调用对应迁移 | `src/Editor/EditorInterface.{hpp,cpp}` |
| A3.3 `NextEngine::EnableEditorShell()` 实现：在 `Start()` 之前创建 `editorShell_`，`Tick()` 中 `if (editorShell_) editorShell_->OnTick(...)`，事件分发同步 | `src/Runtime/Engine.{hpp,cpp}` |
| A3.4 把 `EditorGameInstance::ApplyDefaultCVars`、`ConfigureWindow` 内的窗口配置代码迁到 `EditorShell::Initialize()`，保持 SDR/title-bar 行为一致 | `src/Editor/Core/EditorShell.cpp` |
| A3.5 删除 `EditorMain.cpp` 中的 `CreateGameInstance` 自由函数；删除 `EditorGameInstance` 类与 `EditorMain.h`（或保留为 thin compatibility shim 一段时间） | `src/Editor/EditorMain.{cpp,h}` |
| A3.6 dev mode 下，editor 入口可以 `--game=<plugin>` 触发自动 PIE 启动（A4 实装；本阶段先把命令行参数透传） | `src/Options.{hpp,cpp}` |

**验收：**

```bash
./out/build/dev-windows/bin/gkNextEditor.exe                       # 进入空场景，所有面板可用，PIE 按钮显示 "No Game Loaded"
./out/build/dev-windows/bin/gkNextEditor.exe --load-scene=assets/models/playground.glb
```

**回归：** `gkNextLauncher --game=...`、`MagicaLego.exe`（release）、`gkNextUnitTests` 全部通过。

### A4 · PIE 主循环

**输入：** A3 完成的 EditorShell。

| 任务 | 修改 |
|---|---|
| A4.1 `NextEngine::BeginPlayInEditor / EndPlayInEditor / SetPlayMode` 实装；`gameInstance_` 槽位语义扩展（与 `editorShell_` 共存） | `src/Runtime/Engine.{hpp,cpp}` |
| A4.2 PIE Snapshot：进入 Play 前用 `Assets::Scene::Save()` 序列化到内存（或拷贝 `Scene` 到影子结构）；Stop 时恢复 | `src/Runtime/Engine.cpp` 新增 `pieSnapshot_` |
| A4.3 输入路由：viewport 内点击 → `gameInstance_` 接管；editor panel 内 → `editorShell_` 接管；`Esc` 在 Play 模式下退出 PIE | `src/Runtime/Engine.cpp::OnKey/OnMouseButton` 加 dispatcher |
| A4.4 `AddPluginScopedTickedTask`：plugin 卸载时自动从 `tickedTasks_` 移除被 plugin DLL 持有的 lambda（用 source-tag 标记） | `src/Runtime/Engine.cpp` |
| A4.5 `Editor/Panels/HotReloadPanel.cpp`（已存在）扩展为 PIE 工具栏：▶ Play、■ Stop、❚❚ Pause、🔄 Reload Plugin | `src/Editor/Panels/HotReloadPanel.cpp` |
| A4.6 PIE 期间的 plugin 热重载：保留 game instance 业务状态（用 `SaveHotReloadState/LoadHotReloadState`），与 launcher 模式行为一致 | `src/Runtime/Engine.cpp::TickHotReload` 已支持，仅验证 |

**验收：**

```bash
# 编辑器内 PIE 全流程
./out/build/dev-windows/bin/gkNextEditor.exe --game=Brotato3D
# 1. 进编辑器 → 看到 "Game Loaded: Brotato3D" 状态条
# 2. 点 ▶ → scene 切到 Brotato3D 初始场景，相机/输入交给 game instance
# 3. 点 ❚❚ → 暂停（gameInstance_->OnTick 不再被调用，editor 仍可拖拽 outliner）
# 4. 点 ■ → 还原到 Edit 模式，scene 还原到 Play 之前的 snapshot
# 5. 修改 Brotato3DPlugin.cpp 中某常量 → 重新 build → 等待 plugin 热重载触发 → 不退出 Play 模式
```

### A5 · 发布路径回归 + 文档收尾

| 任务 | 输出 |
|---|---|
| A5.1 CI 矩阵：dev preset + release preset 各跑一次完整 build；smoke test 涵盖 launcher 启动 FlappyCpp/FlappyJs（A2 范围）+ 9 个 monolithic exe `--fastexit` + Packager + UnitTests | `.github/workflows/*.yml` 或当前 conductor/ 配置 |
| A5.2 更新 `AGENTS.md` / `CLAUDE.md`：新构建命令、新入口、PIE 流程；`AGENT_GUIDE/Architecture.md` 增 DLL 拓扑章节；`AGENT_GUIDE/PluginABI.md`（A0 产出）补完 | 文档 |
| A5.3 `run.bat` / `run.sh` 增 `--game=<Name>` 与 `--editor` 快捷参数；老 `--target=<game>` 在 dev 模式下若 game 已 plugin 化则转发到 launcher，否则继续启动 monolithic exe | 启动脚本 |
| A5.4 alias 移除：`gkNextHost` 别名进入 deprecation；release-after-N 个 PR 后删除 | `src/CMakeLists.txt` |

### A6（未来）· 剩余游戏批量 plugin 化

A2 只覆盖 FlappyCpp/FlappyJs；其余 9 个游戏在 A3-A5 验证编辑器 + PIE + Picker 工作流稳定后再批量推进。模板已由 A2 验证完毕，A6 是机械批改造。

| 游戏 | 改造特殊点 |
|---|---|
| `gkNextRenderer` | streamline/oidn 二进制拷贝逻辑（CMakeLists.txt:491-501）需要随 plugin target 移动到 engine target，已是 engine 责任 |
| `MagicaLego` | `ffmpeg.exe` 拷贝（CMakeLists.txt:480-489）随 plugin POST_BUILD 移动 |
| `KongLie3D` / `Brotato3D` / `BrickPlayer` | 标准改造，无特殊依赖 |
| `CharacterDemo` | 链接 `NextGameplay`；验证 entt/reflection 跨 DLL 工作正常（component 注册必须在 engine 或 NextGameplay 中） |
| `gkNextStillBenchmark` / `gkNextMotionBenchmark` | 共享 `${src_files_benchmarkcommon}`，可拆为 INTERFACE 库供两 plugin target 引用 |
| `gkNextVisualTest` | baseline 工作流依赖 cwd 与产物路径，plugin 启动后自行 `chdir` 或 `--cwd=` |

A6 阶段每个游戏一个独立 PR，可批量并行；release/monolithic 路径继续保留所有 exe（避免发布管线断链）。

---

## 7. 风险登记册

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| Windows 下 `WINDOWS_EXPORT_ALL_SYMBOLS=ON` 不能完全覆盖 inline 模板/STL 实例化 → plugin link 时 unresolved external | 中 | 高 | 准备好 `ENGINE_API` 标注落地（已有宏体）；A1.1 验证一个含 entt/std::function 的接口可以跨 DLL 调用 |
| OBJECT 库 `gkNextEngineCore` 对部分 vcpkg `IMPORTED` target（KTX、Streamline 等）的 INTERFACE 传递在某些 CMake 版本下不稳 | 中 | 中 | A1.3 验证；不通过则降级为"同源代码两次 add_library"（编译时间多 +30s），方案在 §3.2 已写明 |
| Packager 误链接 `gkNextEngine`（SHARED）导致它被引入 engine.dll 的运行时依赖，破坏单文件分发 | 低 | 中 | A1.4 显式指定 `target_link_libraries(Packager PRIVATE gkNextEngineStatic)`；CI 加 `dumpbin /dependents Packager.exe` 检查无 `gkNextEngine.dll` 行 |
| 同进程内同时存在 `gkNextEngine.dll`（被 launcher/editor/plugin 用）+ `gkNextEngineStatic` 内嵌的 engine 实例（被 monolithic exe 用）时双初始化 | 低 | 高 | 物理上不可能：launcher.exe 进程内只有 SHARED；MagicaLego.exe 进程内只有 STATIC；不会跨进程共享 |
| 第三方静态库（imgui、Jolt）在 plugin 中被意外二次链接 → 双 context | 中 | 高 | A1.2 把 third-party link 收口到 helper；plugin target 严禁 `target_link_libraries(... imgui)`；CI 加链接图 grep 检查 |
| Launcher Picker 阶段 `gameInstance_` 是 Void，引擎主循环内某些子系统假设 `gameInstance_` 非 Void → 崩溃或行为异常 | 中 | 中 | A2.5.1 验证：`OnTick / OnPreConfigUI / OnRenderUI` 钩子链上所有 `gameInstance_->...` 调用都能容忍 Void；现有代码已经支持（启动期工厂为空时即用 Void） |
| Editor PIE 期间 plugin reload 持有的 lambda → unloaded code → crash | 高 | 高 | A4.4 强制 plugin-scoped task；reflection callback 同样需要 plugin-scoped；引擎对 `gameInstance_` reset 时遍历清理 |
| Reflection 注册表（entt::meta）在 plugin reload 时残留旧类型 → editor PropertyPanel 读到悬空 meta | 中 | 中 | plugin 不允许新增 reflected component（component 必须在 engine 或 NextGameplay 中注册）；写明白这条契约到 ABI 文档 |
| `MagicaLego` 的 `ffmpeg.exe` 等运行时资源跟随 plugin 移动 → release 模式漏拷 | 低 | 中 | A2.3 把 POST_BUILD 拷贝条件化到 plugin/exe target |
| visualtest baseline 工作流依赖 cwd 与产物路径 | 中 | 低 | A2.4 plugin 启动后自行 `chdir` 或显式 `--cwd=`；验证 baseline 比对脚本不变 |
| Linux/macOS 上 `dlopen` 的 RTLD_LOCAL + reflection 全局注册可能导致 plugin 内部对 engine 单例的 weak 链接错位 | 低 | 高 | macOS 验证：`dlopen(..., RTLD_NOW \| RTLD_LOCAL)` 现已使用，配合 `-fvisibility=default` 在 engine.dylib 上仍可见；保留一条 fallback 切换到 `RTLD_GLOBAL` 的 CVar |
| Android/iOS 误启用 dev mode | 低 | 中 | CMakeLists.txt 顶层强制 `if (ANDROID OR IOS) set(GK_PRIMARY_DEV_MODE OFF FORCE)` |
| 第三方依赖在某些可选 feature（DLSS、OIDN）下产生符号 → plugin 隐式期待 | 低 | 中 | engine 已经 PUBLIC 导出 `WITH_*` 宏；plugin 编译时统一从 engine PUBLIC 继承宏，不重复定义 |

---

## 8. 待确认问题

1. **PIE 是否需要双 viewport？** 当前设计是同一窗口、同一 swapchain，play/pause/stop 切换由 EditorShell 决定 viewport rect。如果想要 "Standalone Game" 子窗口，需要额外的 SDL window + secondary swapchain，建议列入 A4 之后的扩展。
2. **`Voyage3D` 当前不在 `AllTargets` 列表：** 该游戏目前不参与构建；A6 启动前需确认是否仍在维护、是否纳入批量 plugin 化。
3. **release/monolithic 模式下是否还需要 `gkNextLauncher`？** 我倾向不要：release 入口就是各 `<Game>.exe`，launcher 仅 dev 模式存在。如果发布场景也想要 launcher（例如带启动器选盘游戏），再单独评估。
4. **`gkNextHost` alias 的下线节奏：** 建议保留 2 个迭代周期；具体下线 PR 由 A5 末尾触发。
5. **Picker UI 的视觉档次：** A2.5 默认实现是纯文字按钮列表。是否要增加缩略图（每个 plugin 旁放 `assets/thumbnails/<Game>.png`）？这会引入 plugin manifest 概念（每个 plugin 旁配一个 `<Game>.json` 描述 displayName/description/thumbnail），是更大改动；先做 MVP 文字版，A4 之后再评估。

> **已确认（用户反馈 2026-05-07）：**
> - A2 范围限定 FlappyCpp + FlappyJs，其余 9 个游戏延后到 A6。
> - Packager 不进入 plugin 化；静态链接 `gkNextEngineStatic`。
> - Launcher 无 `--game` 时弹 ImGui game picker（A2.5），不退出、不静默回退。

---

## 9. 与既有计划的关系

- 与 [`cpp-shader-hot-reload-plan.md`](cpp-shader-hot-reload-plan.md) 的 H2 阶段完全衔接：H2 已经把 plugin loader/ABI/shadow copy 跑通，本计划在其之上把 plugin 模式从 "可选热重载机制" 升级为 "默认开发形态"。
- 与 [`engine-cleanup-and-unification.md`](engine-cleanup-and-unification.md) 的方向一致：本计划进一步通过 DLL 边界强制约束 engine 与 game 的耦合面，是 cleanup 工作的延续。
- 不影响 [`engine-uplift-from-brotato-konglie.md`](engine-uplift-from-brotato-konglie.md)：所有 uplift 工作仍在 engine 静态/共享库内部进行，plugin 化对其透明。

---

## 10. 估时与切片建议

| 阶段 | 估时（工程师 · 工作日） | 可独立交付 PR 数 |
|---|---|---|
| A0 | 2 | 1（文档） |
| A1 | 5 | 3（CMake 双库 / preset / alias） |
| A2 | 2 | 2（FlappyCppPlugin 收口 + FlappyJsPlugin 新增） |
| A2.5 | 2 | 2（DiscoverAvailablePlugins + 引擎 deferred game / Picker UI） |
| A3 | 6 | 2-3（EditorShell 拆分 / EditorInterface 切换 / 入口分叉） |
| A4 | 10 | 4-5（PIE API / Snapshot / 输入路由 / Plugin-scoped task / UI） |
| A5 | 3 | 2（CI / 文档） |
| **合计 A0-A5** | **30** | **~17 个 PR** |
| A6（未来） | 8 | 9（每个游戏一个 PR） |

> 阶段间允许并行：A2 与 A2.5 在 A1 落地后立即可启动且互不依赖；A3 可与 A2.5 并行。A6 在 A5 后任意时间分批推进。

---

## 附录 A · 当前关键文件索引（供后续 agent 快速定位）

| 内容 | 文件 | 行号 |
|---|---|---|
| 引擎入口分叉 | `src/DesktopMain.cpp` | 49-57 |
| 引擎工厂注入 | `src/Runtime/Engine.cpp` | 222-228 |
| Plugin 创建逻辑 | `src/Runtime/Engine.cpp` | 299-331 |
| Plugin 热重载 tick | `src/Runtime/Engine.cpp` | 360-409 |
| Plugin loader 实现 | `src/Runtime/Plugin/PluginLoader.cpp` | 完整文件 |
| Plugin ABI 版本号 | `src/Common/PluginExport.hpp` | 11 |
| ABI 异或哈希 | `src/Runtime/Engine.hpp` | 413-424 |
| `gkNextEngineShared` 定义 | `src/CMakeLists.txt` | 100-109 |
| `gkNextHost` 与 `FlappyCppPlugin` 定义 | `src/CMakeLists.txt` | 152-162 |
| Editor target 与 ImNodeFlow 链接 | `src/CMakeLists.txt` | 131-134, 195-196, 503-504 |
| EditorGameInstance | `src/Editor/EditorMain.{cpp,h}` | 完整文件 |
| EditorInterface 对 game-instance 的依赖点 | `src/Editor/EditorInterface.cpp` | 268, 274, 346, 351 |
| Plugin 入口模板 | `src/Application/PluginEntry.cpp` | 完整文件 |
| 第三方库链接收口（待重构） | `src/CMakeLists.txt` | 342-456 |
| `ENGINE_API` 宏 | `src/Common/CoreMinimal.hpp` | 35-56 |
| Plugin 入口宏：`GK_HOST_APP` | `src/CMakeLists.txt` | 156；`src/DesktopMain.cpp` | 49 |
