---
title: "Engine 层精简重构计划"
category: plan
status: 草案
owner: engine
created: 2026-06-08
last_updated: 2026-06-08
---

# Engine 层精简重构计划

> 状态：草案 / 待评审
> 范围：`src/Engine/**`（不含 `src/Application/**`、`ThirdParty/`、`external/`）
> 力度：**激进重构 + 明确 LOC 目标**
> 日期：2026-06-08

---

## 1. 背景与目标

`AGENTS.md` 写明的目标是「first-party 引擎代码 < 50k LOC」。当前实测：

| 范围 | LOC | 占比 |
|------|-----|------|
| **Engine 层合计** | **63,183** | 100% |
| Runtime/ | 29,466 | 47% |
| Assets/ | 17,834 | 28% |
| Vulkan/ | 7,316 | 12% |
| Rendering/ | 4,897 | 8% |
| NextGameplay/ | 2,549 | 4% |
| Utilities/ | 817 | 1% |

也就是说，**仅 Engine 层就已超出 50k 目标约 13k LOC**。本计划用第一性原理把这 13k 找回来，同时让架构更易读——核心手段不是「删功能」，而是把**领域专用 / 开发期工具**从引擎运行时核心中剥离，并拆解两个 god-class。

### 衡量标准（重构后应同时满足）

1. Engine 层 `gnb loc` < 50k。
2. 单文件不超过 ~800 LOC，单函数不超过 ~80 LOC（现有最大函数 208 行）。
3. 渲染器的 enum / class / namespace / 目录 / 文件名一一对应、可读。
4. `gnb build gkNextRenderer gkNextUnitTests` 通过；`gkNextVisualTest` 无回归。

---

## 2. 第一性原理：引擎核心该装什么？

一个引擎 runtime 核心只应包含「所有 program 都会用到的能力」：

- **设备与帧**：Vulkan 后端、swapchain、同步、资源分配。
- **场景与资产**：Scene/Node/Model/Material、GPU 上传、加速结构、通用 glTF/纹理加载。
- **渲染管线**：PathTracing / 光栅 / 软件 GI / 阴影。
- **运行时系统**：ECS、反射、脚本、命令历史、物理、音频、输入、相机。

不属于核心、应当下沉到 Application/插件层或可选模块的，是「**只有个别 program 用到的领域专用解析器**」与「**开发期工具**」。下面的诊断据此展开。

---

## 3. 现状问题诊断（含证据）

### 3.1 渲染器命名彻底错位（**最高可读性收益，最低风险**）

`Rendering/` 里 enum、类名、namespace、目录、文件名**互相不对应**，甚至语义反转：

| enum (`ERendererType`) | 实际类名 | namespace | 目录 / 文件 |
|---|---|---|---|
| `ERT_PathTracing` | `PathTracingRenderer` | `Vulkan::RayTracing` | `PathTracing/` ✅ |
| `ERT_ModernDeferred` | `SoftwareTracingRenderer` | `Vulkan::ModernDeferred` | `SoftwareTracing/` ⚠️ 名实反 |
| `ERT_LegacyDeferred` | `SoftwareModernRenderer` | `Vulkan::LegacyDeferred` | `SoftwareModern/` ⚠️ 名实反 |
| `ERT_LegacyDeferredNoAmbient` | `Renderer`（裸名） | `Vulkan::NoAmbientDeferred` | `SoftwareModern/SwModernNoAmbient*` |
| `ERT_VoxelTracing` | `VoxelTracingRenderer` | `Vulkan::VoxelTracing` | 藏在 `SoftwareModern/SoftwareModernRenderer.cpp` ⚠️ |

「ModernDeferred 这个 enum 指向一个叫 SoftwareTracing 的类」这种映射对任何阅读者都是认知陷阱。`VoxelTracingRenderer` 还寄生在 `SoftwareModernRenderer.cpp` 里，与文件名无关。

### 3.2 `VulkanBaseRenderer` 是 god-class（2,807 LOC）

单类持有 11 个资源 struct：`DeviceCaps / DeviceContext / FrameResources / RayTracingResources / AmbientCubePipelines / SkinnedMeshResources / BindlessStorageImages / OverlayPipelines / LogicRendererRegistry / ScreenshotResources / Delegates`。同一个类既管设备/swapchain 生命周期，又管帧编排，又亲自实现 BLAS/TLAS 构建、ambient cube 烘焙、skinning dispatch、阴影、wireframe overlay、截图。`LogicRendererBase` 这层策略抽象是对的，但 base 类把本该属于各 pass 的实现全吸到了自己身上。

### 3.3 `NextEngine` 是第二个 god-object（1,972 LOC）

`Engine.hpp` 的 `NextEngine` 直接拥有 renderer、scene、9 个服务（`FRuntimeServices`）、两套 UI、command history、输入、截图、agent validation、hot reload、task 队列。`Engine.cpp` 里：

- `GetUniformBufferObject` 单函数 **208 行**。
- `OnKey` **160 行**。
- `Tick` ~190 行。

这些巨型函数把帧逻辑、相机抖动（Halton/jitter 工具函数也散落其中）、输入分发混在一起。

### 3.4 功能重复：三套 UI / 两套配置 / 两处反射注册

- **三套 UI**：`Editor/UserInterface.cpp`（2,672，198 处 `ImGui::`）+ `Editor/ProfessionalUI.cpp`（1,040，283 处 `ImGui::`）两套 ImGui，外加 `UI/RmlUiSystem.cpp`（1,279）第三套 HTML/CSS UI——**RmlUi 仅 1 个 app 使用**。
- **两套配置**：`Config/CVarSystem.cpp`（872）+ `EngineCVars.cpp`（198）与 `UserSettings.hpp` + `ShowFlags.hpp` 是两条并行的配置/开关机制。
- **两处反射注册**：`Runtime/Reflection/ReflectionRegistry` 与 `NextGameplay/Reflection/GameplayReflectionRegistry` 各注册一遍，可统一为单一入口 + 分模块登记回调。

### 3.5 领域专用 / 开发期模块占据引擎核心（**LOC 主战场**）

| 模块 | LOC | 实际使用方 | 性质 |
|---|---|---|---|
| `Assets/Loaders/FScad*`（OpenSCAD DSL：词法/语法/求值/CSG/文字/曲面细分） | **5,001** | 仅 `ScadStudio` 一个 editor | 领域专用 |
| `Assets/Loaders/FLDraw*`（LDraw 乐高零件） | **2,917** | 仅 `BrickPlayer` / `MagicaLego` | 领域专用 |
| `Runtime/Subsystems/AI/`（AgentLoop / RepoTools / PathSandbox / AIChat） + `AIService.cpp` | **3,847** | 5 个 app | 含**改仓库文件的开发期 agent 工具**（`RepoTools` 738） |
| `Runtime/Subsystems/VoiceInput*` | **628** | 2 个 app | 可选输入 |
| `Runtime/UI/RmlUiSystem` | **1,327** | 1 个 app | 第三套 UI |

这 5 块合计约 **13.7k LOC**，恰好覆盖超标的部分。它们大多不是「全 program 共用的引擎能力」，而是「某个游戏 / 编辑器的领域逻辑」或「开发期工具」。

---

## 4. 重构方案（分阶段，标注 LOC 影响与风险）

> 原则：**先低风险高可读收益，再做结构性外移，最后拆 god-class**。每阶段独立可交付、可单独验证。

### Phase 0 — 渲染器命名统一（纯重命名，风险极低）

把 enum / 类 / namespace / 目录 / 文件名拉齐为一套自洽命名（建议以「技术手段」为准：`PathTracing` / `ModernDeferred` / `LegacyDeferred` / `LegacyDeferredNoAmbient` / `VoxelTracing`，类名同名 + `Renderer` 后缀）：

- 把寄生的 `VoxelTracingRenderer` 从 `SoftwareModernRenderer.cpp` 拆到独立 `VoxelTracing/VoxelTracingRenderer.{hpp,cpp}`。
- `SwModernNoAmbient` 的裸 `Renderer` 类改为带语义的 `LegacyDeferredNoAmbientRenderer`。
- 目录名与 namespace 对齐。

LOC 影响：±0（纯改名）。收益：渲染层一眼可读。验证：`gnb build gkNextRenderer` + `gnb shot --scene assets/models/playground.glb`。

### Phase 1 — 领域专用 / 边缘模块外移（**LOC 主减项，约 −13k**）

四个边缘模块全部处理（已确认范围）：

1. **SCAD 加载器外移**（−5.0k）：`Assets/Loaders/FScad*` → 下沉到 `Application/Editor/ScadStudio/`（或独立静态库 `ScadLib`，仅 ScadStudio 链接）。引擎只保留通用 `IModelSource` 接口，ScadStudio 注册自己的 source。
2. **LDraw 加载器外移**（−2.9k）：`Assets/Loaders/FLDraw*` → `Application/Game/BrickPlayer/`（或共享给 MagicaLego 的 `LDrawLib`）。同样通过 source 接口注入。
3. **AI 子系统瘦身 + 工具外移**（约 −3.2k，保留 ~0.6k 薄接口）：
   - 引擎核心只保留 `FAIService` 的**薄 provider 接口**（连 llama-server 的 chat 调用）。
   - `RepoTools`（738，改仓库文件的开发期工具）、`AgentLoop`、`PathSandbox`、`AIChat` 全部移到工具/插件层；游戏内的 `*AIService`（EditorAIService/ScadAIService/MagicaLegoAIService）改为实现引擎暴露的工具注册接口。
4. **VoiceInput 外移**（−0.6k）：`VoiceInputService` → 可选模块，仅按需链接的 2 个 app 引入。
5. **RmlUi 评估**（−1.3k 或合并）：仅 1 个 app 使用。优先**外移到该 app**；若该 UI 已不活跃则直接移除，统一收敛到 ImGui 路径。

Phase 1 小计：Engine 核心约 **−13k LOC → ~50k**，已达标。风险：中（涉及 CMake glob、跨层接口）。验证：全量 `gnb build --reconfigure`（此阶段确实需要全量，确认所有 program 仍编译）。

### Phase 2 — 拆解 `VulkanBaseRenderer` god-class（可读性，LOC 基本持平）

把 base 类瘦成「设备 + 帧编排 + LogicRenderer 注册表」，其余按 pass 拆为独立单元，base 通过组合持有：

- `AccelerationStructureManager`（BLAS/TLAS 构建与生命周期，吃掉 `RayTracingResources` 的实现）。
- `AmbientCubeBaker`（ambient 烘焙 + distance field cascade）。
- `SkinningPass`（skinning dispatch + buffer）。
- `OverlayPass`（wireframe / visibility / visual debugger）。
- 截图、shadow 已有独立类，保持。

目标：`VulkanBaseRenderer.cpp` 从 2,807 → < 800 LOC，其余分散到 4~5 个 ~300 LOC 的 pass 文件。验证：`gkNextVisualTest` baseline 回归。

### Phase 3 — 配置 / 反射统一（去重）

- **配置**：以 `CVarSystem` 为单一事实源，`UserSettings` / `ShowFlags` 改为 CVar 之上的 typed view（或反过来），消除两套并行存储。
- **反射**：统一为单一 `ReflectionRegistry`，`NextGameplay` 通过注册回调登记自己的组件，去掉第二个 registry 入口。

LOC 影响：−0.5~1k（去重）。风险：低~中。

### Phase 4 — 巨型函数拆分与收尾（可读性）

- `NextEngine::GetUniformBufferObject`（208）→ 拆为相机/抖动/阴影 cascade/光照 几个 helper；把散落的 `GenerateJitter / HaltonSequence / *JitterProjectionMatrix` 收进 `Runtime/Camera/` 的 `JitterUtil`。
- `OnKey`（160）→ 表驱动的 shortcut 映射。
- `Tick`（~190）→ 按「输入 / 任务队列 / 渲染提交 / 统计」拆子步骤。
- `NextEngine` 的 9 服务可按需懒构造，`FRuntimeServices` 维持聚合但缩小 `Engine.cpp` 体积。

LOC 影响：±0，可读性显著提升。

---

## 5. LOC 目标表

| 阶段 | 主要动作 | Engine 层预计 LOC |
|---|---|---|
| 现状 | — | 63,183 |
| Phase 1 后 | SCAD/LDraw/AI/Voice/RmlUi 外移 | **≈ 49,500** ✅ < 50k |
| Phase 3 后 | 配置 / 反射去重 | ≈ 48,500 |
| Phase 2 & 4 | god-class / 巨型函数拆解（LOC 持平，文件数↑、单文件↓） | ≈ 48,500，**无 >800 LOC 文件，无 >80 LOC 函数** |

> 注：Phase 1 是达成 <50k 的关键；Phase 2/4 不显著降 LOC 但兑现「优雅易读」。

---

## 6. 风险与验证策略

- **每阶段独立 PR、独立验证**，不一次性大爆破。
- 外移模块用「引擎暴露接口 + Application 侧注册」而非物理删除，保证现有 program 行为不变。
- 渲染相关改动一律走 `gnb shot --scene <X>` 肉眼验证 + `gkNextVisualTest` baseline 回归。
- 接口面改动后跑 `gkNextUnitTests`。
- Phase 1 / Phase 2 这类广面改动用全量 `gnb build --reconfigure`，确认全部 program 编译。

## 7. 明确不做（Out of Scope）

- 不动 `ThirdParty/` 与 `external/`。
- 不改渲染算法/视觉效果（仅搬运与拆分，像素级不变）。
- 不删除任何 program 仍在用的功能——边缘模块是**外移/下沉**，不是丢弃。
- 不引入新的第三方依赖。

---

## 8. 建议执行顺序（一句话）

**Phase 0（改名，立刻见效）→ Phase 1（外移，达成 LOC 目标）→ Phase 3（去重）→ Phase 2（拆 renderer god-class）→ Phase 4（拆巨型函数收尾）。**
