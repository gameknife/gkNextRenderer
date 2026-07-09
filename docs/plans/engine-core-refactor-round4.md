---
title: "src/Engine 核心层精炼 Round 4：重回 30k —— 模块外移 + 写法压缩"
category: plan
status: 草案
owner: engine
created: 2026-07-09
last_updated: 2026-07-09
---

# src/Engine 核心层精炼 Round 4：重回 30k —— 模块外移 + 写法压缩

> 状态：分析/待执行 | 编写日期：2026-07-09 | 前置：[engine-core-refactor.md](engine-core-refactor.md)（Round 1，已完成）、[engine-core-refactor-round2.md](engine-core-refactor-round2.md)（Round 2，部分执行）、[engine-core-refactor-round3.md](engine-core-refactor-round3.md)（Round 3，三项已落地）
>
> 本轮主题：**把核心层从 38.3k 压回 ≤30k**。两条腿走路：Track A 把"非所有 program 都需要"的功能外移成 module / 下沉到 app 层（大头，~4.6k 含 A6）；Track B 在核心层内做写法重构，让同样的功能占更少、更易读的行数（~2.1k+）。缺口最后一段由候选池（§5.2）补齐。
>
> 本文档面向执行 agent：每个改动项给出证据（引用计数 / 触点清单）、接缝设计、执行步骤、预估净减与验证方式。执行前请重新 grep 验证证据（代码在持续演进）。
>
> **决策记录（2026-07-09，用户拍板）**：A5（GaussianSplatPass 外移）本轮**搁置不做**；A6（FSceneSaver 外移）**执行**，落点为独立模块（编辑器与需要保存能力的 application 显式链接依赖）。两项均已按此收敛，本文不再是"方案对比"而是"既定方案"。

---

## 1. 现状快照（2026-07-09）

- 统计口径：**`gnb loc` 纯代码口径**（非空、非纯注释行），对 `src/Engine/**/*.{cpp,hpp,h,c,inl}`。本文所有"行"均指此口径，与 Round 2/3 的"非空行（含注释）"口径不同，对比时注意。
- 当前总量：**38,279** | 目标：**≤30,000** | 缺口：**−8,279**

### 1.1 按目录分布

| 目录 | 代码行 | 占比 | 备注 |
|---|---:|---:|---|
| Runtime/ | 11,868 | 31% | Engine 主体 + 子系统 + 组件 + 编辑器支撑 |
| Rendering/ | 10,175 | 27% | BaseRenderer + 各渲染路径 + Upscaler + Preview |
| Assets/ | 9,414 | 25% | Scene/Model + 加载/保存 + GPU 资源 + CPU 加速结构 |
| Vulkan/ | 5,723 | 15% | Vulkan 后端 |
| Utilities/ + Common/ + Options | 1,099 | 3% | — |

### 1.2 单文件 Top 15（代码行）

| 文件 | 行 | 本轮动作 |
|---|---:|---|
| Rendering/VulkanBaseRenderer.cpp | 2,028 | B2 压缩 |
| Rendering/Upscaler/StreamlineIntegration.cpp | 1,702 | **A1 外移** |
| Runtime/Engine.cpp | 1,121 | B10 顺手 |
| Assets/Loaders/FSceneLoader.cpp | 1,046 | B5 压缩 |
| Assets/Core/Scene.cpp | 997 | B3 压缩 |
| Runtime/Editor/UserInterface.cpp | 887 | B9 小改 |
| Runtime/Config/CVarSystem.cpp | 864 | B1 压缩 |
| Assets/Core/Scene.Build.cpp | 779 | B3 压缩 |
| Assets/Savers/FSceneSaver.cpp | 742 | **A6 外移至模块** |
| Assets/GPU/Texture.cpp | 708 | B4 压缩 |
| Runtime/Subsystems/NextPhysics.cpp | 671 | B7 压缩 |
| Rendering/Preview/AssetThumbnailRenderer.cpp | 633 | **A2 外移** |
| Runtime/Scene/SceneList.cpp | 579 | B6 压缩 |
| Assets/Acceleration/CPUAccelerationStructure.cpp | 576 | 保留（全局共用） |
| Rendering/GaussianSplat/GaussianSplatPass.cpp | 570 | 本轮保留（A5 搁置） |

### 1.3 与 Round 1~3 的关系

Round 3 三项均已落地（NextCharacterController 下沉 Gameplay、SkinnedMesh IK 删除、MultiViewportBackend 抽到 `src/Application/Editor/Common/`），当时核心层已接近 32k。**6 月中旬以来新增的功能把体量推回 38.3k**：RenderView 多视口（RenderView.hpp/cpp + Preview 服务 ~1.7k）、GaussianSplat 渲染（~0.7k）、GPU AuxDraw（~0.5k）、Upscaler 帧生成扩容（Upscaler 目录现 1,974）、CVarSystem 扩容（774→864）等。**这些新功能大多是"部分 program 才用"的典型模块候选**——这正是本轮 Track A 的空间来源。

Round 2 的遗留未执行项（include 卫生收尾、死代码扫描、pipeline 样板表驱动）并入本轮 B10。

---

## 2. 改动总览与预算

| # | 改动 | 类型 | 风险 | 净减(≈) | 依赖 |
|---|---|---|:--:|---:|---|
| A3 | 编辑命令下沉 Editor Common | 搬家+小接缝 | 低中 | −780 | — |
| A2 | Preview（缩略图/离屏视图）外移 | 搬家+接缝 | 中 | −1,000 | — |
| A4 | VulkanVideoCaps → NextRemote | 搬家+小接缝 | 低 | −240 | — |
| A1 | Streamline → 模块 NextStreamline | 搬家+接缝 | 中高 | −1,650 | — |
| B1~B10 | 核心层写法压缩（§4） | 重构 | 低中 | −2,100 | A1 后做 B2 |
| A6 | FSceneSaver → 模块 SceneExport | 搬家 | 低 | −700 | 已拍板执行 |
| ~~A5~~ | ~~GaussianSplatPass → SplatLoader~~ | — | — | ~~−570~~ | **本轮搁置** |
| C | 候选池（§5.2） | 混合 | 中 | −1,000~−1,100 | 按缺口取用 |

**里程碑推演**：38,279 → P1（A3+A2+A4）36,259 → P2（A1）34,609 → P3（B 系列，保守口径）32,509 → P4（A6）31,809 → 候选池全取 → **~30,800（B 保守）/ ~30,200（B 上限）**。
**缺口提示**：A5 搁置后，30k 目标的余量为负——需要 B 系列冲上限 **且** 候选池基本全取才能贴近 30k。若 P4 结束仍差数百行，优先向用户申请**重启 A5**（−570，接缝设计已在 §5.1 备档）而不是放松 B 项的质量要求或用压行作弊凑数。

外移不减少仓库总行数，只减 `src/Engine`——与目标口径一致（`gnb loc` 中 Engine 类目单列）。Android 注意：模块源码在 Android 上直接编入单一 SHARED target（`src/CMakeLists.txt:264` 附近注释），外移不改变 Android 行为；Streamline 本就 Windows-only。

---

## 3. Track A：模块外移（详细设计）

### A1. Streamline/DLSS 集成 → `src/Modules/NextStreamline`（−1,650，最大单项）

**证据**：`Rendering/Upscaler/` 共 1,944 行代码，其中 `StreamlineIntegration.cpp` 1,702 行是 sl.* API 的完整封装（DLSS-SR / Frame Generation / Reflex）。它是 Windows-only（`WITH_STREAMLINE` 编译门 + `sl.interposer.lib` 链接，`src/CMakeLists.txt:519`），Linux/macOS/Android/iOS 五个平台里四个用不到——"非所有 program 都需要"的最典型案例。

**已有接缝**：`Rendering/Upscaler/IUpscaler.hpp`（36 行）已经是纯虚接口，`UpscalerTypes.hpp`（213 行）是纯类型定义。调用侧（VulkanBaseRenderer 的 `BuildUpscalerFrameInputs` / `CaptureFrameGenerationHudless` 等）已经面向接口。**问题只在于实现体和工厂函数 `CreateStreamlineUpscaler()` 还在核心层。**

**残余硬耦合（执行时逐一处理）**：直接 include `StreamlineIntegration.hpp` 的核心层文件有 6 个——`Vulkan/Instance.cpp`、`Vulkan/Device.cpp`、`Vulkan/SwapChain.cpp`、`Vulkan/WindowSurface.cpp`、`Runtime/Engine.cpp`、`Rendering/VulkanBaseRenderer.cpp`。原因是 Streamline 要做 Vulkan interposer（拦截 instance/device 创建、代理交换链函数）。

**接缝设计**：
1. 核心层保留 `IUpscaler.hpp` + `UpscalerTypes.hpp`，新增一个小注册头 `Upscaler/UpscalerRegistry.hpp`（~30 行）：`void RegisterUpscalerFactory(std::function<std::unique_ptr<IUpscaler>()>)` + `std::unique_ptr<IUpscaler> CreateRegisteredUpscaler()`。
2. Vulkan 层新增 `IVulkanInterposer` 窄接口（~60 行）：`GetInstanceProcAddr 覆盖 / GetDeviceProcAddr 覆盖 / 额外 instance·device 扩展与 feature 请求 / 交换链函数代理`——把 Instance/Device/SwapChain/WindowSurface 里现有的 `#if WITH_STREAMLINE` 分支改写为对该接口的调用（每处 3~6 行）。
3. `StreamlineIntegration.{cpp,hpp}` 整体移入 `src/Modules/NextStreamline/`，实现 `IUpscaler` + `IVulkanInterposer`，模块内完成 sl.* 初始化顺序。
4. 注册点：`DesktopMain.cpp` 按 `WITH_STREAMLINE` 注册（与 DevTools provider 同模式，`src/CMakeLists.txt:553-558`）；CMake 把 `STREAMLINE_LIB_DIR` 链接从 per-target 移到模块 target 上。
5. `Options.cpp`/`EngineCVars.cpp` 里的 DLSS 选项保留（是 UI 面用户语义，行数少），值通过 `UpscalerTypes` 传递。

**核算**：移出 1,767 行（cpp+hpp），核心层新增接缝 ~120 行，净 −1,650。
**验证**：Windows 上 DLSS on/off + 帧生成 on/off 各跑一次 `gnb shot --target gkNextRenderer`；Linux 构建确认零 Streamline 符号；`gnb build gkNextRenderer gkNextUnitTests`。
**风险**：interposer 的函数指针接管顺序是唯一难点，改动期间用 `docs/plans/dlss-superres-no-aa-fix.md` 里的回归场景核对。

### A2. Rendering/Preview → 编辑器公共层 + 新模块（−1,000）

**证据**：`Rendering/Preview/` 共 1,223 行。`AssetThumbnailRenderer`（763 行 cpp+hpp）唯一的外部用户是编辑器 `MaterialEditorPanel.cpp`；`OffscreenRenderViewController`（~390 行）用户是编辑器 `CameraViewPanel.cpp` 和 `Modules/NextRemote/RemoteServer.cpp`。二者都不被任何游戏 target 使用。

**接缝设计**：
1. `RenderViewServices`（114 行）留在核心层作为调度接缝，但**改薄**：现在它的 header 直接 include 两个实现头，改为持有 `std::vector<std::unique_ptr<IRenderViewProvider>>`，接口方法即现有的 `BeforeNextFrame / ScheduleViews / OnMainSceneChanged / OnHdrShUpdated / OnSwapChainResourcesInvalidated / HasWork`（`RenderViewServices.hpp:33-38` 已枚举完整）。
2. 清理 `VulkanBaseRenderer.hpp:215-217` 的 `friend class AssetThumbnailRenderer / OffscreenRenderViewController`：梳理它们实际访问的私有成员，改为窄的公有 API（renderer 已有 `ScheduleRenderView` / `CreateRenderTargetBank` 等公有能力，预计只需补 1~2 个 accessor）。
3. `AssetThumbnailRenderer` → `src/Application/Editor/Common/`（Round 3 MultiViewportBackend 先例，GLOB 自动收录）。
4. `OffscreenRenderViewController` → 新模块 `src/Modules/RenderViews/`（NextRemote 与编辑器都链接它；模块间 static lib 依赖可行）。若不想加新模块，备选落点是 DevTools（所有 program 都链，Engine LOC 同样减少，但游戏包多编译 ~400 行，权衡后仍可接受）。
5. 注册点：编辑器 `EditorMain` / NextRemote 启动时向 `RenderViewServices` 注册 provider。

**核算**：移出 ~1,110 行，接缝改造净增 ~60 行（provider 接口 + 注册），净 −1,000。
**验证**：编辑器打开材质编辑器（缩略图出图）+ CameraViewPanel；`gnb validate` 编辑器冒烟脚本；NextRemote 远程画面回归。

### A3. 编辑命令下沉 Editor Common（−780）

**证据**：`Runtime/Command/` 13 个文件 1,038 行。外部用户全部是编辑器 panel / EditorAIService / DevTools GizmoController。核心层内部的耦合只有一处：`NextEngine` 持有 `commandHistory_` 成员并暴露 `GetCommandHistory()`（`Engine.hpp:152,402`），且 `Engine.Input.cpp` / `Engine.SceneLoad.cpp` 直接构造 `DeleteNodesCommand` / `DuplicateNodesCommand`（Del / Ctrl+D 热键）。

**接缝设计**：
1. **留核心**：`ICommand.hpp` + `CommandHistory.{hpp,cpp}`（~250 行）——undo/redo 栈是通用基建，`NextEngine::GetCommandHistory()` 不动，DevTools/编辑器继续可用。
2. **下沉**：六个具体命令（DeleteNodes / DuplicateNodes / RenameNode / TransformNodes / PropertyCommand / SelectionCommandUtils，~790 行）→ `src/Application/Editor/Common/Command/`。
3. `Engine.Input.cpp` 的 Del / Ctrl+D 处理移出：引擎输入层只保留"把未消费按键转发给 GameInstance / DebugUiProvider"的通路，删除/复制节点热键改由编辑器侧 input handler 实现（编辑器已有完整快捷键层）。
4. **行为变化确认点**：非编辑器 target（gkNextRenderer viewer、各游戏）里 Del/Ctrl+D 将不再删除/复制节点。执行前 grep 确认没有游戏依赖此行为；若 viewer 需要保留，让 gkNextRenderer 链接 Editor Common（它已经是"带 UI 的查看器"定位）。

**核算**：移出 ~790 行，热键搬迁近似零和，净 −780。
**验证**：编辑器 undo/redo/删除/复制/改名/移动全链路 + CommandHistoryPanel；`gnb build` 全 target 一次（改了 Engine.hpp）。

### A4. Vulkan/VulkanVideoCaps → NextRemote（−240）

**证据**：`VulkanVideoCaps.{cpp,hpp}` 282 行，消费者只有 `Modules/NextRemote/`（视频编码推流）。核心层触点：`Device.cpp`（创建 video queue / 开扩展）与 `VulkanBaseRenderer`（caps 查询转发）。

**接缝设计**：Device 增加通用的"额外 queue/扩展请求"回调（若 A1 的 `IVulkanInterposer` 已含扩展请求，直接复用同一机制），NextRemote 注册请求并自持 caps 查询代码。
**核算**：移出 ~282 行，接缝 +40，净 −240。
**验证**：NextRemote 推流回归；无 NextRemote 的 target 确认不再启用 video 扩展。

### A6. FSceneSaver → 新模块 `src/Modules/SceneExport`（−700，已拍板）

> Round 2 §5 曾否决"FSceneSaver 外移"。2026-07-09 用户重新拍板：**执行**，落点为独立模块，由编辑器和需要保存能力的 application 显式链接依赖。理由：缺口从 2.2k 变成 8.3k，且证据更干净了。

**证据（2026-07-09 复核）**：
- `Assets/Savers/FSceneSaver.cpp`（742 行）是 glTF/GLB 导出器（tinygltf 序列化）。
- 核心层入口只有 `Scene::Save → SaveAsGLB/SaveAsGLTF` 三个转发方法（`Scene.cpp:1146-1166`，共 ~25 行）。
- 全仓真实调用方**只有 gkNextEditor** 两处：`EditorMain.cpp:301`、`Overlays/TitleBarOverlay.cpp:55`（均走 `Scene::Save`）。游戏无一使用（MagicaLego 的存档是自有 `FMagicaLegoSave` 格式，与此无关）；QuickJS 绑定、Gameplay、其余模块均未暴露 scene 保存。
- `Scene.Build.cpp:9` / `Scene.Update.cpp:9` 的 `#include FSceneSaver.h` 是**死 include**，顺手删除。

**接缝设计**：
1. 新建 `src/Modules/SceneExport/`，`FSceneSaver.{h,cpp}` 整体移入（namespace 可改 `SceneExport`，或保留 `Assets` 以减小 diff——建议前者，模块代码不应再冒充资产层）。`src/cmake/SourceFiles.cmake:67` 的 `GK_MODULE_NAMES` 追加 `SceneExport`，GLOB 自动建库。
2. **删除** `Scene::Save / SaveAsGLB / SaveAsGLTF` 三个方法及 `Scene.cpp:7` 的 include——保存不再是 Scene 的成员能力。模块侧提供自由函数 `SceneExport::SaveScene(const Assets::Scene&, const std::string& path)`（内部按扩展名分发 GLB/GLTF，即现 `Scene::Save` 的逻辑搬过去）。
3. 消费方改造：编辑器两处调用点改为 `SceneExport::SaveScene(...)`；CMake `target_link_libraries(gkNextEditor PRIVATE SceneExport)`。未来哪个 application/module 需要导出，同样显式链接（不做运行期注册表——消费方是编译期确定的，直接依赖最简单；若日后出现"核心层代码需要触发保存"的需求再升级为 SaverRegistry，与 LoaderRegistry 对称）。
4. 访问面检查：FSceneSaver 序列化只读遍历 Scene/Node/Model/Material 的**公有** getter（执行时确认无 friend / 私有访问；若有，同 A2 的原则梳理成窄公有 API）。
5. Android：模块源码自动编入单一 SHARED target，行为不变；不需要的话可在 Android 分支把 SceneExport 从 GLOB 列表剔除（可选优化，非本轮必做）。

**核算**：移出 742 行 + 删除 Scene 侧转发与死 include ~30 行，模块侧新增分发函数 ~20 行（在模块内，不计入 Engine），净 **−700**（Engine 口径 −770，取整保守记 −700）。
**验证**：编辑器"保存 / 另存为 / 最近场景"全链路；保存出的 .glb 重新载入 diff（已有 `scene-gltf-glb-save-rewrite-plan.md` 的回归口径可复用）；`gnb build gkNextEditor gkNextUnitTests`。
**风险**：低——纯搬家 + 调用点改写，无行为变化。唯一注意点是 `Scene::Save` 从公有 API 消失属于 ABI 破坏面，需全量构建一次确认无漏网调用方。

---

## 4. Track B：核心层写法压缩（−2,100 ~ −2,700）

原则：**先测量后动手**（每项动手前用 `gnb loc` / grep 复核当前行数）；等价变换优先，不引入新抽象层除非它同时提高可读性；每项独立提交。

| # | 对象 | 现状行数 | 手法 | 净减(≈) |
|---|---|---:|---|---:|
| B1 | Runtime/Config/CVarSystem.{cpp,hpp} | 1,044 | Register/Get/Set 五类型族已有 `RegisterTyped<T>` 内核，把外层 5 组包装、typed getter/setter、console 解析里的 per-type switch 归并为模板 + `std::variant` 访问器；JSON 持久化与 console 命令表数据驱动 | −350 |
| B2 | Rendering/VulkanBaseRenderer 家族（主 cpp 2,028 + 三分部 + hpp 415） | 3,268 | A1 完成后先删 15 处 Streamline 分支残留；`CreateStorageImage`/`CreateRenderTargetBank` 双重载合并（默认参数）；`SetPhysicalDeviceImpl` 的 feature-chain 改表驱动（Round 2 已列，未执行）；`DrawFrame`/`Render` 中 per-view 调度与 present 逻辑提纯到已有的 RenderView 协作对象 | −500 |
| B3 | Assets/Core/Scene 家族（Scene.cpp 997 + Build 779 + Update 341 + Selection + hpp） | ~2,700 | 三个 cpp 间的节点遍历/标记重建模式重复提炼；材质/灯光/splat 的"收集→上传"三段式统一成一个模板化 pipeline；`Scene.hpp` 上的胖 API 审计（只被一处调用的公有方法内联回调用点） | −300 |
| B4 | Assets/GPU/Texture.cpp 708 + HdrTextureCache 458 + TextureImage | 1,376 | ktx/stb/hdr 三条上传路径的 staging+mip 生成样板统一；HdrTextureCache 的 SH 投影与 prefilter 计算若与 shader 侧重复，删 CPU 侧冗余路径 | −220 |
| B5 | Assets/Loaders/FSceneLoader.cpp | 1,046 | mikktspace C 回调适配器（文件头 ~90 行）移到独立小 TU 或 Utilities；glTF extras 解析改 key→setter 表驱动 | −130 |
| B6 | Runtime/Scene/SceneList.cpp | 579 | 扫描/过滤/注册逻辑用 `std::filesystem` + ranges 收紧；demo 场景描述若仍有硬编码残留，转数据表 | −100 |
| B7 | Runtime/Subsystems/NextPhysics.cpp | 671 | Jolt 初始化 boilerplate 与 body 工厂的重复 settings 构造归并 | −100 |
| B8 | Vulkan/DebugUtilities.{cpp,hpp} | 457 | VkResult/VkFormat 等 →string 映射表改 X-macro；Round 2 已确认的死类 `BufferMemoryBarrier` 一并删除 | −120 |
| B9 | Runtime/Editor/UserInterface.cpp | 887 | 统计 overlay 绘制（`DrawIndicator` 等）移交 DevTools 的 DebugUiProvider；字体加载与 FontLoader.cpp 去重 | −80 |
| B10 | 全域 include 卫生 + 死代码第二轮 | — | Round 2 §3 未执行余项：CoreMinimal 冗余 std 头（66 处）、spdlog 收编、路径风格统一；clang-tidy include cleaner 跑一轮；零引用头/类扫描 | −200 |

**合计 −2,100（保守）~ −2,700（上限）。**

Track B 的验证统一为：`gnb build gkNextRenderer gkNextUnitTests` + 受影响渲染路径各一张 `gnb shot`（PathTracing / SoftwareModern / SoftwareTracing 三选相关者）+ 单元测试全绿。B3/B4 涉及资产管线，额外跑 `gkNextVisualTest` 与 baseline 对比。

---

## 5. 已决策项记录与候选池

### 5.1 A5：GaussianSplatPass → SplatLoader 模块（**本轮搁置**，分析备档）

**2026-07-09 拍板：本轮不做。** 以下分析保留，供缺口不足或未来 Round 重启时使用：

`Rendering/GaussianSplat/`（637 行）只服务于 splat 场景，数据加载已在 `Modules/SplatLoader`。外移需要 VulkanBaseRenderer 提供"外部渲染 pass"注册接缝（`RegisterExternalPass(stage, callback)` 风格；执行者先调研 GaussianSplatPass 当前的挂接点再定接口形状）。**Splat 的资产层数据结构（`Assets/Core/GaussianSplat.hpp`、Scene.Build/Update 的 splat 分支、组件）留在核心**——数据是资产层通用语义，只把渲染 pass 移走。预估净 −570。这个接缝一旦建立，候选池里的 AuxDraw 可以复用。
验证（若重启）：`gnb shot` splat 场景 + 非 splat 场景各一张（确认零开销路径）。

### 5.2 候选池（按缺口取用，合计 −1,000 ~ −1,100）

> A5 搁置后候选池从"可选"变为"基本必取"（见 §2 缺口提示）。AuxDraw 一项原计划复用 A5 的外部 pass 接缝，现改为自建。

| 候选 | 净减(≈) | 前置 | 说明 |
|---|---:|---|---|
| AuxDraw → DevTools | −400 | 自建接缝 | GPU 调试画线是开发期工具。外部 pass 注册接缝（~60 行）本轮由此项自建，A5 未来重启时可直接复用；若有游戏在 shipping 路径用它画 gizmo 则否决——执行前 grep `NextEngineHelper` 的调用方 |
| ModelViewController → NextGameplay | −450 | 验证 | 若 `NextEngine` 不直接持有而只有 app 层构造（需验证），照 NextCharacterController 先例纯搬家 |
| ScreenShot.cpp 瘦身 | −80 | — | 格式转换分支收紧；注意保住 agent-validation 截图路径 |
| 死代码第三轮（工具扫描） | −100 | B10 后 | clang 静态分析 + 符号引用计数 |

---

## 6. 执行顺序与里程碑

| 阶段 | 内容 | 预计终值 | 验证闸门 |
|---|---|---:|---|
| P0 | 基线：记录 `gnb loc` 输出存档；每个 PR 描述附改动前后 Engine 行数 | 38,279 | — |
| P1 | A3 → A2 → A4（纯搬家优先，风险递增） | ~36,250 | 编辑器全功能冒烟 + NextRemote 回归 |
| P2 | A1 Streamline 外移 | ~34,600 | Windows DLSS 矩阵 + 四平台构建 |
| P3 | B1~B10（每项独立提交，B2 在 A1 之后） | ~32,500 | 单测 + visual test baseline |
| P4 | A6 + 候选池按缺口取用（AuxDraw → ModelViewController → 其余） | **~30,200 ~ 30,800** | 编辑器保存链路回归 + 最终 `gnb loc` 确认 |
| P5 | 若仍未达 30k：向用户申请重启 A5（−570）或立项 Round 5 | **≤30,000** | — |

通用约束（引自 AGENTS.md）：Engine 层改动默认只构建 `gnb build gkNextRenderer gkNextUnitTests` 验证；涉及 ABI/广泛 header 的阶段收尾时做一次全量 `./gnb build --reconfigure`。渲染改动用 `gnb shot` 肉眼验证，交互路径用 `gnb validate` 脚本断言。

## 7. 风险清单

| 风险 | 等级 | 缓解 |
|---|:--:|---|
| A1 interposer 函数指针接管顺序错误 → DLSS 静默失效或崩溃 | 高 | 分两步提交：先建接缝（Streamline 仍在核心层、走新接口），验证无回归后再物理搬移文件 |
| A2 friend 拆除时暴露的私有状态比预期多 | 中 | 先做只读梳理列出 friend 实际触碰的成员清单，再决定 accessor 形状；不接受"把私有成员改公开"式偷懒 |
| A3 热键行为变化影响非编辑器 target | 中 | 执行前 grep + 与用户确认 viewer 是否保留编辑热键 |
| B3/B4 等价变换意外改变资产管线行为 | 中 | 每项提交后跑 `gkNextVisualTest` 与 baseline diff |
| 外移后模块间出现循环依赖 | 低 | 模块只允许依赖 gkNextEngine 与更底层模块；新增依赖边先画在 PR 描述里 |
| 行数目标与可读性冲突（为凑数删注释/压行） | — | **明令禁止**：口径本就剔除注释；禁止单行化多语句、删空行等作弊手法，review 时对照 diff 检查 |
