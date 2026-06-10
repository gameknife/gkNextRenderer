# src/Engine 核心层精简重构：分析与执行计划

> 状态：待执行 | 编写日期：2026-06-10 | 面向：执行重构的 AI agents
>
> 目标：`src/Engine` 核心层代码（不含注释和空行）控制在 **30,000 行以内**，并达到"精炼、易读、优雅"的代码质量要求。

---

## 1. 现状基线

统计口径说明：下文所有行数为**非空行（含注释）**，于 2026-06-10 用 ripgrep 对 `src/Engine/**/*.{cpp,hpp,h,c}` 统计。纯代码行（去注释）约为该数字的 88%~92%。**执行任何 Phase 前后，必须用 `./gnb loc` 重新量测并记录**，验收以 `gnb loc` 的非注释非空行口径为准。

- 文件总数：289
- 非空行总计：**57,230**（估算纯代码 ≈ 50k~52k）
- 目标：≤ 30,000（纯代码口径），即需削减 **约 40%**

### 1.1 模块体量分布

| 模块 | 非空行 | 占比 | 备注 |
|---|---:|---:|---|
| Runtime/Subsystems | 8,681 | 15.2% | 含 AI 子树 2,048 + AIService 1,529 + VoiceInput 530 |
| Assets/Loaders | 8,776 | 15.3% | 含 FScad* 4,579 + FLDraw* 2,515 |
| Vulkan | 6,161 | 10.8% | 后端 + RayTracing |
| Runtime/Editor | 4,722 | 8.3% | ImGui 后端 + 控制台 + 主题库 + Gizmo |
| Rendering | 4,240 | 7.4% | VulkanBaseRenderer 即占 2,780 |
| Assets/Core | 2,834 | 5.0% | Scene/Model/Node |
| NextGameplay | 2,224 | 3.9% | 已是独立库 target，目录仍在 Engine 下 |
| Runtime/Remote | 2,116 | 3.7% | WebRTC 远程串流 |
| Runtime/Scene | 2,069 | 3.6% | SceneList.cpp 即占 1,612 |
| Runtime/Engine.* | 2,045 | 3.6% | 引擎主循环 god class |
| Assets/GPU | 1,863 | 3.3% | Texture.cpp 即占 1,387 |
| Runtime/Utilities | 1,327 | 2.3% | 大半是 debug overlay |
| Runtime/Components | 1,319 | 2.3% | SkinnedMeshComponent 即占 1,125 |
| Assets/Acceleration | 1,270 | 2.2% | CPU BVH |
| Runtime/Reflection | 1,242 | 2.2% | entt::meta 反射 |
| Runtime/UI | 1,195 | 2.1% | RmlUiSystem 单文件 |
| Runtime/Config | 1,182 | 2.1% | CVarSystem 即占 896 |
| Runtime/Command | 1,049 | 1.8% | 编辑器 undo/redo |
| Engine/Utilities | ~800 | 1.4% | |
| Assets/Savers | 602 | 1.1% | |
| Runtime/Camera | 457 | 0.8% | |
| 其余（Common/Platform/Options/Data/杂项） | ~1,050 | 1.8% | |

### 1.2 单文件 Top 15（god class 候选）

| 文件 | 非空行 |
|---|---:|
| Rendering/VulkanBaseRenderer.cpp | 2,473 |
| Runtime/Subsystems/QuickJSEngine.cpp | 2,409 |
| Runtime/Editor/UserInterface.cpp | 2,354 |
| Assets/Loaders/FScadEvaluator.cpp | 1,776 |
| Runtime/Engine.cpp | 1,757 |
| Runtime/Scene/SceneList.cpp | 1,612 |
| Assets/Core/Scene.cpp | 1,456 |
| Runtime/Subsystems/AIService.cpp | 1,439 |
| Assets/GPU/Texture.cpp | 1,387 |
| Runtime/UI/RmlUiSystem.cpp | 1,158 |
| Assets/Acceleration/CPUAccelerationStructure.cpp | 1,136 |
| Runtime/Components/SkinnedMeshComponent.cpp | 988 |
| Assets/Loaders/FSceneLoader.cpp | 970 |
| Runtime/Editor/ProfessionalUI.cpp | 942 |
| Runtime/Subsystems/NextPhysics.cpp | 872 |

---

## 2. 问题诊断

### 2.1 核心层混入了大量"非核心"代码（≈ 24k 行，主要矛盾）

判断标准：**渲染器/ECS/资产管线/脚本运行时是核心；只服务于个别 Application、或属于可选工具链的，不是核心。**

1. **专用资产 loader**（~7.6k）
   - `Assets/Loaders/FScad*`（4,579 行）：OpenSCAD DSL 的 lexer/parser/evaluator/CSG/text，只服务 ScadStudio。
   - `Assets/Loaders/FLDraw*`（2,515 行）：LDraw 解析，只服务 MagicaLego/BrickPlayer。
   - `KayKitPieceLoader`（127 行）：KayKit prefab 专用，**按用户决定直接删除**（连同 SceneList 中 `CharacterPlayground` 的 KayKit 摆放代码；CharacterDemo 场景退化为平板地面）。
   - 注意：`FProcModel`（352 行）虽在 Loaders 目录，但它是通用程序化网格工厂（CreateBox/CreateSphere/CreateCornellBox/CreateAreaLight），被 Brotato3D/KongLie3D/Voyage3D/Flappy/StudioSim、FScadLoader、FLDrawLoader、QuickJSEngine（SceneBuild 绑定）广泛使用，**属于核心，保留不动**。
   - 反观 `FSceneLoader`（glTF, 970 行）和 `Texture` 是真正的核心 loader，应保留。

2. **AI/语音子系统**（~4.1k）：`Runtime/Subsystems/AIService.cpp`、`Runtime/Subsystems/AI/`（AgentLoop/AIChat/RepoTools/ToolRegistry/PathSandbox/LlamaPidFile）、`VoiceInputService`。这是编辑器工具属性的功能，不是渲染引擎核心。其中 **VoiceInputService（530 行）及其 whisper.cpp/ggml 依赖按用户决定直接删除**（引擎暂不做语音输入），其余 AI 部分外移成模块。

3. **远程串流**（2,116 行）：`Runtime/Remote/`（信令、H264 编码、视频管线、虚拟手柄）。仅在 `options_->RemoteMode` 时启用，典型的可选模块。

4. **编辑器/开发者 UI**（~6.0k）
   - `Runtime/Editor/ProfessionalUI`（1,057 行）：ImGui 主题与应用 chrome 控件库。
   - `Runtime/Editor/GizmoController`（446）、`NotificationCenter`（163）、`FontLoader`（120）、`ConsoleLogBuffer`（76）。
   - `Runtime/Editor/UserInterface.cpp`（2,550 行 cpp+hpp）混合了四种职责：ImGui Vulkan 渲染后端（核心，须保留）、控制台 UI、统计 overlay、platform viewport 管理。
   - `Runtime/Utilities/` 下的 `GraphicsDebugPanel.hpp`（352）、`PhysicsDebugOverlay`（350）、`ProfileDebugOverlay`（247）：调试面板。

5. **场景注册表**（1,638 行）：`Runtime/Scene/SceneList.cpp` 内含大量 procedural demo 场景构建代码 + 按扩展名分发 loader 的逻辑。demo 场景属于 Application/资产数据，分发逻辑可以反转为注册制。

6. **测试设施**（209 行）：`Runtime/Scene/GltfTestRunner` 应在 `src/Tests` 或工具层。

7. **NextGameplay**（2,224 行）：CMake 中已是独立库 target（`NextGameplay`），但源码目录在 `src/Engine/NextGameplay/`，把它计入了核心层行数。目录归位即可。

8. **RmlUi 中间件**（1,195 行）：`Runtime/UI/RmlUiSystem.cpp` 是 RmlUi 的集成胶水，单文件 1,158 行，属可选 UI 中间件。

### 2.2 God class / 巨型文件（次要矛盾，影响"易读优雅"）

- **VulkanBaseRenderer.cpp（2,473）**：内嵌约 500 行 DLSS/Streamline 集成（`sl::`/Streamline 调用散布在 62~260、1818~2002、2193 行附近），与基础渲染循环耦合。
- **QuickJSEngine.cpp（2,409）**：第 32~2099 行是一个巨型匿名 namespace，装着全部 JS 绑定（Scene/SceneBuild/Global/输入等）；类本体只有约 400 行。
- **Engine.cpp/hpp（2,045）**：`NextEngine` 持有一切——窗口、渲染器、场景加载管线、输入分发、截图、agent validation、任务队列、9 个子系统。hpp 已做内部 struct 分组，但 cpp 仍是大杂烩。
- **Texture.cpp（1,387）**：纹理管理器 + HDR cache 序列化（lzav 压缩、FNV hash）+ webp/ktx2 解码 + 升降级策略混在一起；且 `#include "Engine/Runtime/Engine.hpp"`——**下层（Assets）反向依赖上层（Runtime）**。
- **SyncAndTiming.hpp（479 行的 header）**、`TaskCoordinator.hpp`（340）、`GraphicsDebugPanel.hpp`（352）：实现塞在头文件里，拖慢编译且不易读。
- **Scene.cpp（1,456）**：已有 `Scene.Selection.cpp` 分部的先例，可继续按职责分部。

### 2.3 依赖方向问题

- `Assets/GPU/Texture.cpp` → `Runtime/Engine.hpp`（取 TaskCoordinator/设备），应改为构造注入。
- `Runtime/Scene/SceneList.cpp` 直接 include 所有专用 loader（FProcModel/FLDraw/FScad/KayKit），导致核心层被迫链接全部 loader。
- `Runtime/Engine.cpp` 无条件 include `Remote/RemoteServer.hpp`。

---

## 3. 目标架构

```
src/
├── Engine/                    # 核心层（目标 ≤30k 纯代码行）
│   ├── Common/                # CoreMinimal、平台抽象
│   ├── Vulkan/                # Vulkan 后端 + RayTracing
│   ├── Rendering/             # 渲染管线（DLSS 集成拆为独立文件，仍属此层）
│   ├── Assets/                # Scene/Model/Node/Texture/glTF loader/CPU BVH/Saver
│   └── Runtime/               # Engine 主循环、ECS 组件、反射、QuickJS、物理、音频、
│                              # Config/CVar、Command、Camera、ImGui 渲染后端、Platform
├── Gameplay/                  # ← 原 Engine/NextGameplay 目录平移（库名不变）
├── Modules/                   # 可选引擎模块，各自独立静态库，按需链接
│   ├── LDrawLoader/           # ← Assets/Loaders/FLDraw*
│   ├── ScadLoader/            # ← Assets/Loaders/FScad*
│   │                          #   （KayKitPieceLoader 直接删除；FProcModel 属通用核心，留在 Engine/Assets）
│   ├── NextAI/                # ← AIService + Subsystems/AI/（VoiceInputService 不外移，直接删除）
│   ├── NextRemote/            # ← Runtime/Remote
│   ├── NextRmlUi/             # ← Runtime/UI/RmlUiSystem
│   └── DevTools/              # ← 控制台 UI、统计 overlay、ProfessionalUI 主题库、
│                              #   Gizmo、NotificationCenter、各 Debug overlay/panel
├── Application/               # 不变；按需链接 Modules
└── Tests/                     # ← 收编 GltfTestRunner
```

核心层与模块层的边界机制（必须先落地，再搬代码）：

1. **Loader 注册制**：`Engine/Assets/Loaders/` 提供 `ISceneLoader`（按扩展名注册：`.ldr/.mpd`、`.scad`、`.proc` 等）。`SceneList`/`FSceneLoader` 只保留 glTF 路径 + 注册表分发，专用 loader 在各自模块的静态注册函数中挂入，由链接该模块的 Application 调用（显式 `Modules::LDraw::Register()`，避免静态初始化顺序问题）。
2. **Service 注册制**：`NextEngine::FRuntimeServices` 中 AI/Voice 改为类型擦除的服务槽（`RegisterService<T>` / `GetService<T>`），或保留现有 getter 但移到模块侧的 adapter 头。Remote 同理：引擎暴露 `IFrameStreamer` 注入点，`NextRemote` 模块实现它。
3. **UI 分层**：ImGui 的 Vulkan 渲染后端（pipeline、字体、draw data 提交、platform viewport 回调）是核心，留在 `Engine/Runtime/Editor/ImGuiBackend.*`；控制台、overlay、主题、Gizmo 是 DevTools 模块，通过现有的 `funcPreConfig/funcInit` 回调和 `auxDrawRequest_` 挂接。

---

## 4. 分阶段执行计划

每个 Phase 独立可交付、可验证。**执行顺序即依赖顺序**。每个 Phase 完成后：
- 构建：`./gnb build gkNextRenderer gkNextUnitTests`；涉及 CMake/文件移动时用 `./gnb build --reconfigure` 全量验证（本重构属于"大型 engine 重构"，每个 Phase 收尾时全量一次）。
- 运行：`./gnb run`，确认日志出现 `uploaded scene [...] to gpu`。
- 渲染验证：`gnb shot --scene assets/models/playground.glb`，肉眼比对截图。
- 单测：`./out/build/<preset>/bin/gkNextUnitTests`。
- 量测：`./gnb loc`，在本文档第 6 节登记行数变化。

### Phase 0 — 基线与脚手架（无行为变更）

1. 运行 `./gnb loc`，把 `src/Engine` 当前纯代码行数记入第 6 节基线表。
2. 创建 `src/Modules/` 目录与 CMake 骨架：每个模块一个 `add_library`，更新 `src/cmake/SourceFiles.cmake`（新增 `src_files_module_*` GLOB 组）。
3. 注意 `src/CMakeLists.txt` 有 **ANDROID / IOS / else 三个分支**，模块源组需同步加入三处（Android 是单 SHARED target，直接把模块源并入；iOS/桌面用独立静态库）。

产出：空模块骨架编译通过。风险：低。

### Phase 1 — 纯移动类（低风险，先拿 ~4.7k）

| 动作 | 行数减量(≈) |
|---|---:|
| `Engine/NextGameplay/` → `src/Gameplay/`，更新 SourceFiles.cmake 的 `src_files_nextgameplay` 路径与全部 include 路径（`Engine/NextGameplay/...` → `Gameplay/...`） | 2,224 |
| `Runtime/Scene/GltfTestRunner.*` → `src/Tests/`（已确认调用方仅 `src/DesktopMain.cpp` 一处，需同步调整其 include 与链接） | 209 |
| `Runtime/Utilities/{PhysicsDebugOverlay,ProfileDebugOverlay,GraphicsDebugPanel}` → `Modules/DevTools/`（挂接方式：它们目前由 Engine/UserInterface 直接调用，先改为 `auxDrawRequest_`/回调注册） | ~950 |
| **删除 KayKit（用户已确认，不外移）**：删 `Assets/Loaders/KayKitPieceLoader.{h,cpp}`；改写 `SceneList.cpp` 的 `CharacterPlayground()`（第 1105 行起）——去掉全部 `loader.LoadPiece(...)` 与 KayKit 节点摆放（~250 行），只保留相机/灯光/天空设置 + 一块 `FProcModel::CreateBox` 平板地面；`CharacterDemoGameInstance.cpp` 第 62~70 行的资产探测去掉 KayKit probe（`kKayKitProbe`），仅保留角色网格 probe，错误提示文案同步修改。验证：`gnb shot --target CharacterDemo`，确认角色站在平板上、NavGrid 正常构建。`FProcModel` 为通用核心工厂，保留 | ~380 |
| `Runtime/Config/AISettings.hpp` 随 AI 模块走（Phase 3 前可先留） | 9 |

注意：`Tests/Test_LDrawConfig.cpp` 等单测引用 loader 头文件，`UNIT_TEST_SOURCES` 链接对应模块库即可，**测试本身不动**。

### Phase 2 — Loader 注册制 + 专用 loader 模块化（~8.3k）

1. 在 `Engine/Assets/Loaders/` 新增 `ISceneLoader.hpp` + `LoaderRegistry.{hpp,cpp}`（目标 <150 行）：`RegisterLoader(extensions, loadFn)`、`TryLoad(path, ctx)`。
2. 重写 `SceneList.cpp` 的扩展名分发（第 44~80 行的 `ESceneCategory` 逻辑）走注册表；glTF 仍为内建。
3. `FLDraw*` → `Modules/LDrawLoader/`（2,515 行）；`FScad*` → `Modules/ScadLoader/`（4,579 行）。命名空间保持不变，只动目录/库归属，降低 diff 噪声。
4. `SceneList.cpp` 中的 procedural demo 场景构建代码（大头，~1.2k）拆到 `Application/Render/gkNextRenderer`（或共享的 `Application/Common`），核心只保留场景列表接口与注册分发（目标 SceneList ≤ 300 行）。
5. 调用方修正：MagicaLego/BrickPlayer 显式 `Register` LDraw loader；ScadStudio 注册 Scad loader；`gnb shot --target ScadStudio --scene assets/scad/beer_cup.scad` 验证。

风险：中。已确认 Engine 内部对 FScad/FLDraw 的引用只有两处：`SceneList.cpp` 的分发逻辑（本 Phase 消除）和 `Assets/Core/Model.hpp` 第 192~194 行的 `friend class FProcModel/FSceneLoader/FLDrawLoader` 声明——需把 friend 访问改为公开的 Model 构建 API（或最小 `FModelBuilder`），消除核心头对模块类名的引用。另外 ScadStudio 应用层（ScadOutline/ScadAIService 等）直接使用 FScad 类型，链接 ScadLoader 模块即可。

### Phase 3 — 语音输入删除 + AI / Remote 服务模块化（~6.2k）

0. **删除语音输入（~530 行 + 依赖链，用户已确认，不外移）**：
   - 删除 `Runtime/Subsystems/VoiceInputService.{hpp,cpp}`。
   - `Runtime/Engine.hpp/cpp`：移除 `FRuntimeServices::voiceInputService`、`GetVoiceInputService()` 两个 getter 及构造/析构处理；`Runtime/RuntimeFwd.hpp` 移除前置声明。
   - `AIService.{hpp,cpp}`：移除 `FVoiceInputConfig`、`TryGetVoiceInputConfig()`（AIService.cpp 第 1145 行附近）及 `hasVoiceInputConfig_`/`voiceInputConfig_` 成员与 `ai_config.json` 解析分支。
   - 调用方清理（移除麦克风/语音 UI 入口）：`Application/Editor/gkNextEditor/Panels/AIPanel.cpp`、`Application/Game/MagicaLego/MagicaLegoUserInterface.cpp`。
   - 构建系统移除 whisper/ggml：根 `CMakeLists.txt` 第 37~41 行的 `WITH_WHISPERCPP` option；`src/CMakeLists.txt` 第 266~278 行的 `GK_WHISPER_LINK_LIBS`/`GK_WHISPER_COMPILE_DEFINITIONS` 块及第 433~434、519~520 行的链接/定义引用；`vcpkg.json` 移除 `whisper-cpp` 依赖（ggml 是 whisper-cpp port 的传递依赖，随之消失）。全仓 grep `WITH_WHISPERCPP|whisper|ggml|VoiceInput` 确认零残留。
   - 验证：`./gnb build --reconfigure` 全量（vcpkg manifest 变更必须 reconfigure）。
1. `Runtime/Subsystems/AI/` + `AIService.*` → `Modules/NextAI/`（~3,586 行）。
   - `NextEngine` 的 `GetAIService()` 改为泛型服务槽，或在模块里提供 `NextAI::Get(engine)` adapter；同步修正调用方（已确认调用面：gkNextEditor 的 `AIPanel`/`EditorAIService`、ScadStudio 的 `ScadAIService` 等、MagicaLego 的 `MagicaLegoAIService`/`MagicaLegoUserInterface`、StudioSim，以及 Engine 内 UserInterface 控制台）。
   - 单测 `Test_AIChatProtocol/Test_LlamaPidFile/Test_AgentLoop/Test_RepoTools` 链接 NextAI 模块。
2. `Runtime/Remote/` → `Modules/NextRemote/`（2,116 行）。
   - `Engine.cpp` 中 RemoteMode 启动段（~554-600 行）抽为 `IFrameStreamer` 注入：引擎只保留注入点与帧回调（<60 行），模块实现 RemoteServer 并在支持 remote 的 Application 中装配。
   - `Options.hpp` 的 Remote* 选项可留在核心（仅数据）。

风险：中高（动 `NextEngine` 公共 API，触发面广）。完成后必须 `./gnb build --reconfigure` 全量。

### Phase 4 — 编辑器 UI 拆分（~4.3k）

1. `UserInterface.{hpp,cpp}`（2,550）一拆为二：
   - **保留核心** `ImGuiBackend.*`：Vulkan pipeline / 字体纹理 / RenderDrawData / platform viewport 回调 / bindless ImTextureID 编解码 / 事件转发（~1,400 行）。
   - **移出 DevTools**：控制台（Draw/Submit/History/Match ~600 行）、统计 overlay 与 sparkline/TimingHistory（~300 行）、`SetStyle`。控制台依赖的 `ConsoleLogBuffer` 同行。
2. `ProfessionalUI.*`（1,057）、`GizmoController.*`（446）、`NotificationCenter.*`（163）、`FontLoader.*`（120）→ `Modules/DevTools/`。FontLoader 若被 ImGuiBackend 初始化字体所需，则字体加载留核心、字体配置 UI 走模块（执行时判断）。
3. `Runtime/UI/RmlUiSystem` → `Modules/NextRmlUi/`（1,195）。已确认调用方：RmlUiDemo、StudioSim（仍需全仓 grep `GetRmlUi` 复核）。

风险：高（ImGui 后端是所有 Application 的公共路径）。逐 target `gnb shot` 验证 UI 正常渲染：gkNextRenderer、gkNextEditor、MagicaLego 至少各一张。

### Phase 5 — God class 内部重构（不再移出，做"精炼易读"，净减 ~2.5k）

| 对象 | 动作 |
|---|---|
| VulkanBaseRenderer.cpp | DLSS/Streamline 全部代码抽到 `Rendering/Upscaler/StreamlineIntegration.{hpp,cpp}`，以 `#if WITH_STREAMLINE` 收口；主文件按 SwapChain 生命周期 / 帧循环 / 资源管理分组，目标 ≤1,600 行 |
| QuickJSEngine.cpp | 匿名 namespace 的绑定按域拆分部文件：`QuickJSBindings.Scene.cpp`、`.Engine.cpp`、`.Input.cpp` 等（类不变、声明在内部头）；主文件目标 ≤600 行 |
| Engine.cpp | 场景加载管线（LaunchLoadSceneTask/LoadScene/SceneLoadContext）抽 `Runtime/SceneLoadPipeline.*`；输入处理（OnKey/OnCursor/OnMouse/Gamepad）抽 `Runtime/EngineInput.*`（均为 NextEngine 的分部实现或友元协作类）；目标 ≤1,000 行 |
| Texture.cpp | HDR cache（lzav/FNV/HdrCacheHeader）抽 `Assets/GPU/HdrTextureCache.*`；**去掉对 Runtime/Engine.hpp 的反向依赖**（TaskCoordinator 由调用方注入）；目标 ≤900 行 |
| Scene.cpp | 沿用 `Scene.Selection.cpp` 先例，按 Build/Update/Lighting 分部，单文件 ≤800 行 |
| 大 header | SyncAndTiming.hpp(479)、TaskCoordinator.hpp(340) 等：实现下沉 .cpp，header 只留接口 |

约束：本 Phase 禁止行为变更，纯机械拆分 + 死代码清理；每改一个对象立即构建 + `gnb shot`。

### Phase 6 — 收尾与验收

1. 清理：跨文件重复的小工具函数（如 `ToLowerCopy` 之类散落实现）归并到 `Engine/Utilities/`；删除确认无引用的死代码。
2. `./gnb build --reconfigure` 全量编译所有 target；全部单测；`gkNextVisualTest` 跑 baseline 回归。
3. `./gnb loc` 终测：`src/Engine`（纯代码）≤ 30,000。未达标时优先回到 Phase 5 继续下沉/去重，**不得为凑数删注释**。
4. 更新 `AGENTS.md` 架构图（新增 `src/Modules`、`src/Gameplay`）与 `AGENT_GUIDE/` 相关文档；其中 `AGENT_GUIDE/PrefabSceneWorkflow.md`（KayKit prefab 工作流）随 KayKit 删除一并移除或改写，`CharacterDemo.md` 同步更新场景描述。

---

## 5. 行数预算（验收用）

| 项 | 移出/削减(非空行≈) |
|---|---:|
| Phase 1（NextGameplay、GltfTestRunner、overlays、KayKit 删除） | 3,760 |
| Phase 2（FScad、FLDraw、SceneList 场景代码） | 8,300 |
| Phase 3（语音输入直接删除 530、NextAI、NextRemote） | 6,200 |
| Phase 4（DevTools UI、RmlUi） | 4,300 |
| Phase 5（god class 拆分净减 + 去重） | 2,500 |
| **合计削减** | **≈25,100** |

57,230 − 25,100 ≈ **32,100 非空行（含注释）** ≈ **28k~29k 纯代码行** ✅ 达标且留余量。

---

## 6. 量测登记（执行 agent 填写）

| 时间 | Phase | gnb loc (src/Engine 纯代码) | 备注 |
|---|---|---:|---|
| 2026-06-10 | Phase 0 基线 | 55,279 | 290 文件；含 NextGameplay 2,217（Phase 1 移出）。`gnb loc` 非注释非空行口径 |
| 2026-06-10 | Phase 1 完成 | 51,465 | −3,814（NextGameplay→src/Gameplay 2,217；GltfTestRunner→Tests 203；overlays→Modules/DevTools ~950；KayKit 删除 ~440）。全 target 构建过、118 单测过、playground/CharacterDemo `gnb shot` 与 HEAD 基线一致 |
| 2026-06-10 | Phase 2 完成 | 43,804 | −7,661（FLDraw→Modules/LDrawLoader 2,507；FScad→Modules/ScadLoader 4,412；SceneList 1,385→247 行，demo 场景→Application/Common/DemoScenes (977)，CharacterPlayground→CharacterDemo）。核心新增 LoaderRegistry（~200）；Model 去 FLDrawLoader friend。全 target 构建过、118 单测过、playground 截图与基线一致、beer_cup.scad / CharacterPlayground.proc 经注册表加载成功 |

---

## 7. 执行约束与注意事项（必读）

1. **遵守 AGENTS.md**：命名规则（.clang-tidy 为准）、Allman 大括号、4 空格缩进、首个 include 必须 `Common/CoreMinimal.hpp`、平台代码走 `PlatformCommon.h`。
2. **不修改 `ThirdParty/`、`external/`**；新依赖进 `vcpkg.json`（本计划不应引入新依赖）。
3. **CMake 三分支同步**：`src/CMakeLists.txt` 的 ANDROID（单 SHARED）/ IOS / 桌面分支都要更新；`SourceFiles.cmake` 的 GLOB 路径随目录移动更新；新增文件/移动文件后构建需 `--reconfigure`。
4. **先 grep 再动手**：每次移动前用全仓 grep 确认调用面（重点：`GetAIService`、`GetRmlUi`、`FScad`、`FLDraw`、`SceneList`、`UserInterface`，以及删除项 `VoiceInput|whisper|ggml|WITH_WHISPERCPP`、`KayKit`），把调用方修正纳入同一提交。
5. **命名空间与文件名尽量不动**，只动目录与库归属，控制 diff 噪声；确需改名时单独提交。
6. **每个 Phase 单独提交**（或一个 Phase 内按对象分小提交），提交信息注明 Phase 编号与行数变化。
7. **行为零变更原则**：本重构全程不改变运行行为；任何"顺手优化"单独开任务。
8. 渲染验证统一用 `gnb shot`（不弹窗、自动退出）；涉及多 target 的 Phase 至少验证 gkNextRenderer、gkNextEditor、MagicaLego、ScadStudio 四个。
