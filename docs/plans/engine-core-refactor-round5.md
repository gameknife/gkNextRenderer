---
title: "src/Engine 核心层精炼 Round 5：守住 30k —— 装配边界、Splat 闭环与调试瘦身"
category: plan
status: 草案（已按用户决策收敛）
owner: engine
created: 2026-07-10
last_updated: 2026-07-10
---

# src/Engine 核心层精炼 Round 5：守住 30k —— 装配边界、Splat 闭环与调试瘦身

> 状态：分析/待执行 | 编写日期：2026-07-10 | 前置：[engine-core-refactor-round4.md](engine-core-refactor-round4.md)
>
> 本文基于 2026-07-10 当前工作树重新取证。当前 `src/Engine` 为 **30,324** 行。
>
> **用户决策（2026-07-10）**：本轮实施范围只包括 **A0、A4、A5、B**。原 A2/A3/A6/A7 虽然从纯模块边界角度可以外移，但属于 NextEngine 的合理核心能力、体量也有限，本轮明确保留。原 A1 不再采用“移除 core ImGui runtime”的方案：NextEngine 必须继续内建 ImGui context、输入和 Vulkan 渲染能力；默认字体、主题、loading/统计面板等表现层未来可另行收敛到 `NextUI` module，但不纳入本轮必做范围。
>
> **本轮验收目标：`src/Engine` ≤ 29,700 行；预期约 29.4k ~ 29.65k。** 目标是建立可持续边界并重新留出余量，不再为了追求 27k 把合理核心能力模块化。

---

## 1. 计划结论

Round 4 之后，核心层的大块非核心能力已经基本完成外移。当前再继续拆 logic renderer、CommandHistory、Localization 或 CVarConsole，虽然依赖方向上说得通，但会把 NextEngine 的常用基础能力切得过碎，并带来超过 LOC 收益的装配成本。

Round 5 收敛为四件事：

1. **A0：显式 target capability 装配**——解决“模块虽然搬出 Engine，却仍自动链接到几乎所有程序”的问题。
2. **A4：Splat 完整闭环**——pass 已在模块，但数据、组件、proxy、loader 签名和 CVar 仍反向污染核心；这是当前最干净、最值得完成的非核心边界。
3. **A5：AuxDraw CPU 队列归入 DevTools**——GPU pass 已外移，CPU debug primitive queue 应同步归位，核心只保留小型 debug draw 接口。
4. **B：有证据的小范围内部精炼**——继续保留核心能力，只删除重复实现、死代码和过期耦合。

UI 的修订原则是：

- **ImGui rendering capability 是 Engine core**；
- **默认 UI presentation policy 可以是 `NextUI` module**；
- 本轮只记录这个边界，不把 UserInterface/Vulkan backend 整体搬出核心，也不从 `gkNextEngine` 移除 ImGui 依赖。

---

## 2. 当前基线与 Round 4 落地审计

### 2.1 LOC 基线（2026-07-10）

统计命令：

```powershell
.\gnb.bat loc
```

统计口径是 `gnb loc`：物理行减空行和纯注释行。

| 目录 | 文件数 | 代码行 | 占 Engine |
|---|---:|---:|---:|
| Runtime/ | 82 | 9,608 | 31.7% |
| Assets/ | 40 | 7,659 | 25.3% |
| Rendering/ | 37 | 6,348 | 20.9% |
| Vulkan/ | 46 | 5,608 | 18.5% |
| Utilities/ + Common/ + top-level | 14 | 1,101 | 3.6% |
| **Engine 合计** | **219** | **30,324** | **100%** |

当前主要文件：

| 文件 | 代码行 | 本轮判断 |
|---|---:|---|
| Rendering/VulkanBaseRenderer.cpp | 1,959 | 保留核心；B 中仅做重复构造/descriptor 查询收紧 |
| Runtime/Engine.cpp | 1,113 | 保留核心生命周期与 UI frame composition |
| Assets/Core/Scene.cpp | 973 | 保留；A4 删除 Splat 特判 |
| Runtime/Editor/UserInterface.cpp | 887 | **保留 ImGui/Vulkan backend**；默认表现层仅作为未来 NextUI 候选 |
| Runtime/Config/CVarSystem.cpp | 810 | 保留完整核心能力；B 中允许等价去重 |
| Assets/Core/Scene.Build.cpp | 772 | A4 移出约 300 行 Splat proxy 构建 |
| Assets/GPU/Texture.cpp | 708 | 本轮不动 |
| Assets/Acceleration/CPUAccelerationStructure.cpp | 576 | 保留 CPU query/voxel bake；A4 只去掉 Splat 特判 |
| Runtime/Scene/SceneList.cpp | 573 | 保留 loader/reference/catalog；A4 只去掉 Splat payload 参数 |

### 2.2 Round 4 已落地内容

Round 4 文档仍标为草案，但当前代码已经完成其主要外移项：

| 项目 | 当前状态 | 当前落点 |
|---|---|---|
| Streamline | 已完成 | `Modules/NextStreamline` |
| Preview 实现 | 已完成 | Editor Common + `Modules/RenderViews` |
| 具体编辑命令 | 已完成外移 | `Modules/DevTools/Command`；通用 history 留核心 |
| VulkanVideoCaps | 已完成 | `Modules/NextRemote` |
| GaussianSplatPass | 已完成 | `Modules/SplatLoader` |
| FSceneSaver | 已完成 | `Modules/SceneExport` |
| ModelViewController | 已完成 | `Gameplay/Camera` |
| Jolt/miniaudio/glTF loader | 已完成 | `NextPhysics` / `NextAudio` / `GltfLoader` |
| AuxDraw | 完成一半 | GPU pass 在 DevTools，CPU queue 仍在 Engine |

本轮不重复这些搬移，也不把“还能再搬”自动视为“应该再搬”。

---

## 3. 本轮边界定义

### 3.1 允许保留在 core 的能力

以下能力明确视为 NextEngine core：

- 窗口、输入、Vulkan device/swapchain、帧同步和资源生命周期；
- Scene/Node/Model/Material、reflection、scene load/reference、程序化场景基础设施；
- PathTracing、SoftwareTracing、SoftwareModern、VoxelTracing、NoAmbient 等内建渲染模式及其 registry；
- ImGui context、SDL event backend、Vulkan render pass/pipeline、font atlas/upload、bindless ImTextureID、多 viewport draw-data 渲染；
- CommandHistory/ICommand；
- typed CVar registry、持久化、console command；
- Localization、SceneCatalog、Screenshot、FrameProfiler；
- CPU scene query、voxel/ambient acceleration 和 GPU ray-query 基础设施。

这些内容即使存在模块化可能，也不再作为本轮 LOC 候选。

### 3.2 应归入 module 的能力

满足以下特征时才外移：

- 明确属于单一资产格式或开发工具；
- core-only 和大多数游戏无需理解其具体数据语义；
- 已有稳定接缝，外移不会让核心增加同等规模的抽象层；
- 不会把常用基础能力拆成必须到处显式装配的小碎片。

本轮符合条件的只有 Splat 专用语义和 AuxDraw 具体实现。

### 3.3 依赖方向

```text
Application ───────> Modules ───────> Engine
      │                 │               ▲
      └────────> Gameplay ──────────────┘
```

- `Engine` 不得 include `Modules/**` 或 `Application/**`。
- module 可以依赖 Engine；module 间依赖必须是 DAG。
- application 决定装哪些 module；不再以“除 core-only 外全部自动链接”为默认策略。
- module 注册必须显式、幂等，不依赖 static initialization 顺序。

---

## 4. 改动范围与预算

### 4.1 必做范围

| # | 改动 | 风险 | 预计 Engine 净减 |
|---|---|:--:|---:|
| A0 | target capability 显式装配 | 中 | 0（架构前置） |
| A4 | Splat 数据/组件/proxy/CVar 完整回收进 SplatLoader | 中高 | −480 ~ −620 |
| A5 | AuxDraw CPU queue → DevTools，核心只留 `IDebugDraw` | 低中 | −110 ~ −130 |
| B | 有证据的重复实现、死代码和 include 收尾 | 低 | −80 ~ −150 |
| **合计** |  |  | **−670 ~ −900** |

预算推演：**30,324 → 29,654 ~ 29,424**。

### 4.2 不计入本轮预算的 UI 方向

未来把默认 UI presentation 移到 `Modules/NextUI`，预计还能移出约 150~300 行，但它不是本轮达到 29.7k 的依赖，也不授权执行 agent 顺手实施。详见 §6。

---

## 5. A0：显式 target capability 装配

### 5.1 现状问题

`src/CMakeLists.txt` 当前在通用 target 循环里自动给绝大多数 executable 链接 `DevTools`、`LiveCoding`、`NextAudio`、`NextPhysics`、`AgentDriver`、`NextStreamline`、`GltfLoader`；文件末尾又把 `NextRmlUi`、`NextRemote`、`NextTui` 链到几乎所有 executable。

`GK_CORE_ONLY_APPLICATION_TARGETS` 通过排除规则保护 `gkNextMinimalRenderer`。这种做法能降低 Engine LOC，却没有真正表达每个 program 使用哪些能力；后续新增 module 很容易再次变成“隐形核心”。

### 5.2 目标设计

新增 CMake helper，例如：

```cmake
gk_target_runtime_modules(gkNextRenderer
    MODULES DevTools LiveCoding NextAudio NextPhysics AgentDriver
            NextStreamline GltfLoader LDrawLoader ScadLoader SplatLoader)
```

helper 负责：

- `target_link_libraries`；
- 给共享 `DesktopMain.cpp` 增加按模块命名的 compile definition；
- 校验 module 名在 `GK_MODULE_NAMES` 中；
- 记录 target capability，供 configure-time 断言和 dashboard/诊断输出使用。

共享 entry point 的 include/install 按 compile definition 包围。安装时序分三类：

1. **pre-engine**：Streamline interposer；
2. **post-engine / pre-start**：Audio、Physics、AgentDriver、Gltf/Splat loader、DevTools service；
3. **on-demand**：Remote/TUI/RmlUi 等由 option 或具体 application 启用的服务。

### 5.3 执行步骤

1. 增加 `gk_target_runtime_modules()`，先保持所有 program 运行行为不变。
2. 把通用 target loop 和文件尾部的全 target 自动链接规则迁入显式 target 列表。
3. module 自身依赖落在 module target 上，例如 `NextRemote -> RenderViews`，而不是重复写到所有 application。
4. `gkNextMinimalRenderer` 不传任何 module，并增加 configure-time 断言。
5. `NextRmlUi`、NextRemote、NextTui 只链接真实消费者，不因共享 DesktopMain 而全量链接。
6. 为 A4 预留 Splat `Install(NextEngine&)`：必须发生在 Engine CVar 初始化后、场景扫描和 renderer Start 前。

### 5.4 core-only 验收

`gkNextMinimalRenderer` 是 module 装配测试，但 **ImGui 属于 gkNextEngine core，允许继续存在**。验收条件是：

1. CMake link graph 不含 `GK_MODULE_NAMES` 中的可选 target；
2. 能加载 `Minimal.proc`，日志出现 `uploaded scene [...] to gpu`；
3. `gnb shot --target gkNextMinimalRenderer --frames 30` 成功；
4. 未安装 DevTools/Splat/Audio/Physics 时有明确 no-op 或 capability unavailable 行为；
5. 默认 gkNextRenderer 的功能、CLI 和模块安装顺序不变。

### 5.5 验证闸门

```powershell
.\gnb.bat build gkNextMinimalRenderer gkNextRenderer gkNextUnitTests --reconfigure
.\gnb.bat shot --target gkNextMinimalRenderer --frames 30
.\gnb.bat shot --target gkNextRenderer --scene assets/models/playground.glb
```

此阶段不追求 LOC，必须独立提交。

---

## 6. UI 边界修订：core 保留 ImGui renderer，默认表现层未来进入 `NextUI`

> **本节是架构决策记录，不属于本轮必做 A0/A4/A5/B。执行 agent 不应在本轮自行实施。**

### 6.1 必须留在 Engine core

现 `Runtime/Editor/UserInterface.*` 同时包含 ImGui runtime 和少量默认 UI policy。下列是 NextEngine 的渲染能力，应继续留在 core：

- `ImGui::CreateContext/DestroyContext`；
- SDL3 event backend、WantCaptureMouse/Keyboard；
- Vulkan render pass、pipeline、framebuffer、vertex/index buffer；
- bindless texture id 编解码和 UI texture upload；
- font atlas 与通用 FontLoader；
- `PreRender`、`PrepareDrawData`、`RenderPreparedDrawData`；
- platform viewport draw data 和 `IMultiViewportBackend` 渲染接缝；
- `NextEngine::GetUserInterface()` 及 GameInstance 的 `OnPreConfigUI/OnInitUI/OnRenderUI` 生命周期；
- `gkNextEngine` 对 `imgui::imgui` 的链接依赖。

因此，原计划中的 `IUiRuntimeFactory`、无 UI core-only 路径、整体移动 UserInterface、移除 Engine ImGui 链接，全部取消。

### 6.2 可以进入 `Modules/NextUI` 的默认表现层

当前可识别的 presentation policy 包括：

- 默认 Roboto/DroidSans/Icon 字体组合和 asset path；
- 默认 theme/style；
- loading indicator；
- 默认 statistics/console/graphics/profile panel 的组合顺序；
- `Engine/Utilities/ImGui.hpp` 中面向 application 的 popup、toolbar、show-flag widget helpers；
- 默认桌面 UI content provider。

这些代码可以形成 `Modules/NextUI`，但不能接管 ImGui 的底层渲染职责。建议接缝是小型 `IUiContentProvider`：

```cpp
class IUiContentProvider
{
public:
    virtual ~IUiContentProvider() = default;
    virtual void ConfigureStyle(UserInterface&) {}
    virtual void ConfigureFonts(UserInterface&) {}
    virtual void DrawDefaultContent(const FUiContentContext&) {}
    virtual bool HandleEvent(const SDL_Event&) { return false; }
};
```

接口形状以真实调用面为准。core 在 provider 缺失时仍创建 ImGui context、加载一个最小默认字体并正常渲染 GameInstance UI；缺失的只是默认主题和工具内容。

### 6.3 NextUI 与 DevTools 的边界

- `NextUI`：通用默认 UI presentation、主题、字体策略、widgets、frame composition；
- `DevTools`：CVar editor、console 数据源、physics/graphics/profile debug panel、gizmo、AuxDraw；
- `NextUI` 可以调用已注册的 `IDebugUiProvider`，但 DevTools 不应反向依赖 NextUI 的具体实现；
- application 自定义 UI 只依赖 core ImGui API，是否使用 NextUI widgets 由 target capability 决定。

### 6.4 若未来实施的验证要求

- gkNextEngine 仍直接链接并渲染 ImGui；
- core-only 仍可运行 GameInstance ImGui UI，不要求安装 NextUI；
- 有 NextUI 时默认字体、主题、loading、统计面板与现状一致；
- RemoteImGuiSession、multi-viewport、HDR/SDR UI pipeline 不变；
- 该拆分按 presentation LOC 计算收益，不能把 renderer backend 行数计入预算。

---

## 7. A4：Splat 能力完整回收进 `SplatLoader`

### 7.1 当前泄漏面

GaussianSplatPass 已经外移，但 core 仍直接包含或理解：

- `Assets/Core/GaussianSplat.hpp`；
- `Runtime/Components/GaussianSplatComponent.*`；
- `Scene::GaussianSplats()`、world bounds、proxy hit remap；
- Scene.Build 中约 300 行 proxy voxel mesh 生成；
- SceneList/LoaderRegistry 的 `std::vector<FGaussianSplatData>&` 参数；
- SceneLoadContext 的 splats payload；
- ReflectionRegistry 的 splat component；
- UserSettings/EngineCVars/ShowFlags 中约 18 个 splat 专用字段；
- CPUAccelerationStructure 的 splat component 特判。

它是单一资产格式的完整垂直能力，core-only 和绝大多数 application 不应理解其数据布局。

### 7.2 目标数据模型

让模块 component 拥有数据，不再由 Scene 持有平行 vector、component 保存 model id：

```cpp
class GaussianSplatComponent final : public Assets::Component
{
    std::shared_ptr<const FGaussianSplatData> data_;
    // per-node visible/lighting/proxy settings
};
```

SOG loader 解码后直接把 data 放进 source node component。GaussianSplatPass 遍历 scene nodes/component 收集数据。这样：

- 删除 node 会自然释放数据；
- scene reference 不需要给 splat model id 加 offset；
- Scene 不需要 `gaussianSplats_` 或 Splat getter；
- module 可以自行注册 reflection 和 CVar。

### 7.3 proxy 与 raycast

把 proxy 生成整体移到 SplatLoader，在 SOG load 完成前生成 core Model/RenderComponent 和隐藏 proxy node。proxy node 挂模块自有 marker component，并以 source node 为 parent。

core 增加窄而通用的 raycast post-processor 接缝。Splat module 用它把命中的 proxy node 映射回 source parent。接缝不能带 `Splat` 命名，也不能把 Splat id/字段塞进 RenderComponent 或 Scene。

### 7.4 loader/reflection/CVar 清理

1. `FSceneLoaderFn` 和 `SceneList::LoadScene` 删除 splat vector 参数；Gltf/LDraw/Scad 的空参数同步删除。
2. `SceneLoadContext`、Scene、scene reference resolver 删除 splat payload/offset 逻辑。
3. Splat data/component 头移入模块。
4. `Splat::Install(NextEngine&)` 显式注册 loader、external pass、reflection、raycast post-processor。
5. module 建立 `FSplatSettings` external service，并向 core CVar system 注册 `r.splat.*`、`show.gaussianSplats`。
6. 删除 UserSettings/ShowFlags 的 Splat 字段；pass/proxy 只读 module settings。
7. CPU AS 只处理最终 proxy RenderComponent，不认识 GaussianSplatComponent。

A0 是本项前置。Splat Install 必须发生在 Engine 构造完成（CVar 已存在）后、场景扫描/Start 前；移除各 application 构造器里散落的 `Modules::Splat::Register()`。

### 7.5 分段提交

1. component 改为持有 data，同时保留旧 Scene vector 作为短期过渡；
2. pass、loader、proxy 切到 component data；
3. 删除 Scene/loader/context 的旧 splat payload；
4. 搬 reflection/CVar/settings；
5. 删除所有 core Splat 符号并执行 `rg` 断言。

每一步必须可构建、可回滚，不允许一次提交同时重写 proxy 算法。

### 7.6 验证

- `gkNextUnitTests "[SOG]"`；
- SOG load、reload、node delete、scene reference、selection；
- proxy shadow、ray occlusion、debug visible；
- Splat CVar 注册、持久化以及未安装 module 时 CVar 不存在的明确行为；
- 有可用 SOG pak 时对 Splat/非 Splat 场景各截图；没有资产时以 loader fixture 和 SOG 单测为最低闸门；
- 最终 `rg -n "GaussianSplat|FGaussianSplat" src/Engine` 应为零。

---

## 8. A5：AuxDraw CPU queue 归入 DevTools

### 8.1 证据

GPU `AuxDrawPass` 已在 DevTools，但 `Rendering/AuxDraw/AuxDrawSystem.*` 仍有 161 行 core 代码。它只被 DevTools pass 和 `NextEngineHelper::DrawAux*` 使用；后者的调用者主要是 DevTools、NextPhysics 和游戏 debug 路径。

当前边界导致 core-only target 永久携带一个没有 consumer pass 的 primitive queue。

### 8.2 接缝

core 只保留约 25~35 行语义接口 `Runtime::IDebugDraw`：

```cpp
class IDebugDraw
{
public:
    virtual ~IDebugDraw() = default;
    virtual void AddLine(...) = 0;
    virtual void AddBox(...) = 0;
    virtual void AddPoint(...) = 0;
};
```

`NextEngine` 可选持有该接口；未安装时调用是 no-op。

DevTools 拥有：

- transient/persistent primitive queue；
- GPU primitive layout；
- AuxDrawPass；
- overflow stats。

NextPhysics 不得反向依赖 DevTools；它继续通过 core `IDebugDraw` 或 `NextEngineHelper::DrawAux*` 调用。`NextEngineHelper` 只做接口转发，不再 include AuxDrawSystem。

### 8.3 安装时序

A0 的 DevTools Install 负责同时安装：

- `IDebugUiProvider`；
- `IDebugDraw` 实现；
- AuxDraw external pass factory。

三者应共享同一 module service 生命周期，避免 global queue 在多次 EngineTestFixture 间残留 primitive。

### 8.4 验证

- physics、skeleton、gizmo、游戏 debug draw；
- persistent point duration 和 overflow；
- 多次创建/销毁 EngineTestFixture 不残留 primitive；
- 无 DevTools 的 core-only 调用 debug draw 不崩溃、不积累队列；
- NextPhysics target 不新增对 DevTools 的 link dependency。

---

## 9. B：小范围核心内部精炼

完成 A4/A5 后重新运行 `gnb loc`。B 只允许等价重构和死代码清理，不改变本轮确认的核心归属。

### B1. VulkanBaseRenderer 重复构造收紧

- 合并 `CreateStorageImage` 双重载为单一实现 + 可选 extent；
- 合并 `CreateRenderTargetBank` 的默认 extent 路径；
- renderer descriptor/requirements 查询只保留一个表，不移动具体 logic renderer；
- 删除 A4/A5 后失效的 include、friend 和条件分支。

预估净减：30~60 行。

### B2. DebugUtilities 映射表

- 把 VkResult/device type/object type 等重复 switch 收敛为集中 constexpr/X-macro table；
- 保留未知值 fallback、validation severity/filter 和完整诊断；
- 删除重复 include。

预估净减：30~60 行。

### B3. CVar/Scene 的局部去重

- CVar typed/variant dispatch 只做已有行为的重复分支合并，不拆 CVarConsole module；
- Scene 在移除 Splat 后清理空的 payload/branch/helper；
- LoaderRegistry 清理“glTF built into core”等已过期注释与 hardcode，但 SceneCatalog 继续留 core；
- 运行 include cleaner，删除零引用 private helper。

预估净减：20~50 行。

### B4. 禁止事项

- 不把 LogicRenderer、CommandHistory、Localization、SceneCatalog、CVarConsole 搬出 core；
- 不把多条语句挤到一行；
- 不删除诊断、边界检查或 RAII cleanup；
- 不用宏隐藏复杂控制流来凑行数；
- 不重写 Texture/HDR、CPU AS、Screenshot 或 shader 算法；
- B 达到约 80 行净减即可，不为追求上限扩大改动面。

---

## 10. 明确保留项与原计划处置

| 原项目 | 决策 | 理由 |
|---|---|---|
| A1 ImGui runtime 整体外移 | **取消** | ImGui 的 context/input/Vulkan renderer 是 NextEngine 核心渲染能力；只保留未来 NextUI presentation 拆分方向 |
| A2 logic renderer module | **保留 core** | 五条 renderer 是引擎主要能力与 device fallback 矩阵，约 780 行可接受；外移会让 renderer availability 变成装配问题 |
| A3 CommandHistory/ICommand | **保留 core** | 264 行通用 undo/redo 基础设施，不只是编辑器具体命令；具体命令已在 DevTools，当前边界合理 |
| A6 Localization | **保留 core** | 体量约 140 行，多个游戏/UI 共用，核心 getter 和生命周期简单 |
| A6 SceneCatalog | **保留 core** | 与 loader registry、reference resolution、visual/benchmark 默认场景关系紧密，拆分收益有限 |
| A7 CVarConsole | **保留 core** | 启动 override、AgentDriver、console、editor AI 共用，约 220 行不足以抵消新 module 和装配成本 |

这些项目不是“永不重构”，而是本轮确认其**归属留在 core**。B 可以继续改善其内部写法，但不能借 B 名义重新模块化。

---

## 11. 本轮暂缓的其他热点

| 候选 | 体量 | 暂缓原因 |
|---|---:|---|
| CPUAccelerationStructure 整体模块化 | 1,371 | 同时承担 CPU ray query、Gameplay NavGrid、voxel SDF/ambient bake；需要独立设计 `CpuSceneQuery` 与 `VoxelBakeService` |
| Vulkan RayTracing + BaseRenderer AS | 611 + 222 | 同时服务 PathTracing、硬件 ambient bake、GPUScene TLAS |
| Screenshot | 309 | gnb shot、visual test、benchmark、多个游戏和 core-only 验收共同依赖 |
| Texture + HdrTextureCache | 1,166 | 所有 renderer 共用，上传/格式/HDR 变换性能敏感 |
| FrameProfiler + GpuQueryTimer | ~540 | Engine/renderer 广泛埋点，当前没有低成本接缝 |
| FProcModel | 313 | core-only、多个游戏、QuickJS、ScadLoader、renderer 共用 |
| PipelineCommon / ShadowMapPass | 980 | 所有内建 renderer 和 base prepass 共用 |

未来若继续 Round 6，应优先研究 CPUAccelerationStructure 的双职责拆分，或正式立项 NextUI presentation；不要继续从合理核心 API 中零散搬几十行。

---

## 12. 执行顺序与里程碑

| 阶段 | 内容 | 预计 Engine LOC | 阶段闸门 |
|---|---|---:|---|
| P0 | 记录基线；A0 capability 装配 | 30,324 | core-only/default target 行为不变 |
| P1 | A5 AuxDraw queue → DevTools | ~30,200 | debug draw + 无 DevTools no-op |
| P2 | A4 Splat 完整闭环 | ~29,600 | SOG、reference、proxy、CVar、reflection |
| P3 | B 局部收尾；最终量测 | **≤29,700** | targeted/full build + visual/unit 验收 |

推荐先做 A5：改动小，可验证 A0 建立的 service/capability 安装模式；再以同一模式执行复杂的 A4。

NextUI 不在上述里程碑内。若用户后续单独批准，应另立任务，不与 A4 并行修改 Engine.cpp/UserInterface.cpp。

---

## 13. 通用验证清单

### 13.1 每个独立提交

```powershell
.\gnb.bat build gkNextRenderer gkNextUnitTests
.\out\build\windows\bin\gkNextUnitTests.exe
.\gnb.bat loc
```

涉及渲染/外部 pass：

```powershell
.\gnb.bat shot --target gkNextRenderer --scene assets/models/playground.glb
.\gnb.bat shot --target gkNextMinimalRenderer --frames 30
```

### 13.2 A4 专项

```powershell
.\out\build\windows\bin\gkNextUnitTests.exe "[SOG]"
```

- 有可选 SOG pak 时补跑真实 Splat 场景；
- 非 Splat glTF/LDraw/Scad scene load 必须回归，因为 loader 签名发生变化；
- scene reference 和 proxy raycast 必须有单测。

### 13.3 阶段收尾

- A0/A4 涉及 CMake、loader callback 和广泛 header，阶段末执行一次 `gnb build --reconfigure` 全量构建；
- 跑 `gkNextVisualTest` baseline diff；
- Windows 验证 Streamline 仍在 Vulkan instance 前安装；
- Linux/macOS/Android/iOS 至少由 CI 做 compile gate；
- 检查 `src/Engine` 无 `Modules/` include、无绝对路径、无 ThirdParty 改动。

---

## 14. LOC 账本（执行 agent 填写）

| 阶段 | 前值 | 后值 | 实际净减 | commit/PR | 备注 |
|---|---:|---:|---:|---|---|
| P0 capability | 30,324 |  |  |  |  |
| P1 AuxDraw |  |  |  |  |  |
| P2 Splat closure |  |  |  |  |  |
| P3 cleanup/final |  |  |  |  |  |

---

## 15. 执行 agent 注意事项

1. 动手前重新运行 `gnb loc` 和 `rg`；本文行数是 2026-07-10 快照。
2. 严格执行 A0/A4/A5/B，不自行重启原 A1/A2/A3/A6/A7。
3. 每项先落接缝、验证，再移动实现；不要在同一提交改变算法和目录边界。
4. module service key 必须封装在强类型 accessor 内，业务代码不能散落 raw string key。
5. Install/Register 必须幂等；测试 fixture 会在同一进程多次创建 engine、注册 loader。
6. 可选能力缺失必须是明确 no-op/capability unavailable；不能靠链接空壳 module 让 core-only 通过。
7. Android 虽把 module 源码编入单一 SHARED target，仍必须按 capability 显式安装，不能以“源码存在”等价于“能力启用”。
8. 不修改 `src/ThirdParty`，不提交 `out/`，不夹带 UI backend、renderer mode、CPU AS、Texture/HDR 或 shader 算法重写。

本轮完成的标志是：**Engine 继续拥有完整、好用的核心 renderer/UI/runtime 能力，同时 Splat 和 debug draw 不再让所有程序承担不需要的具体实现；默认 module 装配也从隐式全链接变成可读、可验证的 capability 声明。**
