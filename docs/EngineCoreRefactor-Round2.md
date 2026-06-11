# src/Engine 核心层精炼 Round 2：god class 拆解 + include 卫生

> 状态：分析/待讨论 | 编写日期：2026-06-11 | 前置：[EngineCoreRefactor.md](EngineCoreRefactor.md)（Phase 0~5 已完成）
>
> 本轮主题：**易读优雅**优先，LOC 净减为辅。Round 1 把核心层从 55,279 降到 33,924（gnb loc 口径），距 30k 目标还差约 3.9k；本轮 god class 拆解预计净减有限（~600-900），缺口补齐方案见第 5 节。

---

## 1. 现状快照（2026-06-11）

- 统计口径：非空行（含注释），ripgrep 对 `src/Engine/**/*.{cpp,hpp,h,c}`。
- 文件数：200 | 非空行：**35,476**（gnb loc 纯代码口径上轮为 33,924）
- `#include` 总行数：**1,431**（系统头 581 + 内部头 850），占总行数 **4.0%**

### 单文件 Top 12

| 文件 | 非空行 | Round 1 目标 | 达标 |
|---|---:|---:|:--:|
| Rendering/VulkanBaseRenderer.cpp | 2,120 | ≤1,600 | ❌ |
| Runtime/Subsystems/QuickJSBindings.cpp | 1,811 | （Phase 5 新拆出，未设目标） | — |
| Runtime/Editor/UserInterface.cpp | 1,497 | ~1,400 | ≈ |
| Runtime/Engine.cpp | 1,339 | ≤1,000 | ❌ |
| Assets/Acceleration/CPUAccelerationStructure.cpp | 1,136 | 未列入 | — |
| Runtime/Components/SkinnedMeshComponent.cpp | 988 | 未列入 | — |
| Assets/Loaders/FSceneLoader.cpp | 970 | 核心 loader，保留 | — |
| Runtime/Subsystems/NextPhysics.cpp | 872 | 未列入 | — |
| Runtime/Config/CVarSystem.cpp | 774 | 未列入 | — |
| Assets/Core/Scene.cpp | 761 | ≤800 | ✅ |
| Assets/GPU/Texture.cpp | 693 | ≤900 | ✅ |
| Vulkan/SyncAndTiming.hpp | 479 | 实现下沉 .cpp | ❌（未执行） |

---

## 2. God class 拆解分析（按优先级）

### 2.1 VulkanBaseRenderer.cpp（2,120 行）— 最高优先级

Streamline 已抽离（Round 1），但文件仍承担四种职责。方法级测量（raw 行）：

| 职责块 | 方法 | 体量(≈) |
|---|---|---:|
| GPU-driven 帧内 dispatch | DispatchSkinning(89) / DispatchGpuCulling(127) / DispatchClearPass / DispatchVisibilityPass(63) / DispatchSunShadow(185) / DrawWireframeOverlay / DispatchVisualDebugger / CopyObjectIdHistory | ~650 |
| 软 GI 烘焙（ambient cube / DF） | HandleAmbientCubeCacheInvalidation / ClearAmbientCubeCache(48) / BakeAmbientCubeCascade(117) / RebuildDistanceFieldCascades(113) / ShouldSkipAmbientCubeUpdates | ~320 |
| RT 加速结构管理 | UpdateAccelerationStructuresTop/Bottom / Create/DeleteAccelerationStructures / CreateBottomLevelStructures(84) / CreateTopLevelStructures | ~250 |
| 设备/交换链/帧循环（真正的"Base"） | SetPhysicalDeviceImpl(208!) / CreateSwapChain(109) / DrawFrame(160) / Render(127) / 其余 | ~900 |

**建议**：按职责块拆三个协作文件（同类、分部实现，namespace 不动，与 Engine.Input.cpp 先例一致）：
- `VulkanBaseRenderer.GpuDriven.cpp` —— 全部 Dispatch* + wireframe/visual debugger
- `VulkanBaseRenderer.GiBake.cpp` —— ambient cube / distance field 烘焙
- `VulkanBaseRenderer.RayTracingAS.cpp` —— BLAS/TLAS 生命周期
- 主文件目标 ≤900；`SetPhysicalDeviceImpl` 208 行的 feature-chain 配置可顺手按 feature 分组提子函数。

### 2.2 QuickJSBindings.cpp（1,811 行）— 高

Round 1 从 QuickJSEngine 拆出，但本身是单体：67 个 `static JSValue` 绑定函数挤在一个文件。Round 1 计划原本就是"按域拆分部文件"（`.Scene.cpp` / `.Engine.cpp` / `.Input.cpp`），只执行了第一步（搬出主类）。

**建议**：按已注册的 JS 命名空间域拆 4 个文件 + 1 个内部头：
- `QuickJSBindings.Scene.cpp` —— Scene / SceneBuild / 节点与组件反射对象（最大块）
- `QuickJSBindings.Global.cpp` —— Global / Engine / 生命周期 hooks / 文件 IO
- `QuickJSBindings.InputUi.cpp` —— Input / Audio / UI
- `QuickJSBindings.TsDefs.cpp` —— `UpdateTypeScriptDefinitions` 与 TS 定义字符串生成（~150 行，纯开发期工具，也可论证移 DevTools）
- 单文件目标 ≤600。纯机械移动，绑定注册表（函数指针表）留主文件。

### 2.3 Engine.cpp（1,339 行）— 高

Round 1 已拆 Input/SceneLoad 分部，剩余两个大块不属于"主循环"：

1. **RegisterReflection()：72~403 行（~330 行）**。组件/类型反射注册表，是数据不是逻辑。建议移 `Runtime/Reflection/EngineReflection.cpp`（Reflection 目录本就存在）；若改用 X-macro/表驱动还能净减 ~100 行。
2. **GetUniformBufferObject()：1135~1343（~208 行）的 god function**。投影矩阵 + TAA jitter + Android pre-rotate + UBO 装配混在一起。建议抽 `Runtime/CameraUbo.{hpp,cpp}` 或并入 Runtime/Camera，内部按 BuildProjection / ApplyJitter / FillUbo 提纯函数。注意 VulkanBaseRenderer 里有同名虚函数，确认去重机会。

目标 ≤800，达成 Round 1 未竟的 ≤1,000 并留余量。

### 2.4 UserInterface.cpp（1,497 行）— 中（需讨论）

现状是干净的 ImGui Vulkan 后端，但其中 **~700 行**是 multi-viewport platform window 支撑（helper structs 96~500 + 平台窗口 swapchain 280~500 + viewport 回调 1286~1588）——基本是自研版 `imgui_impl_vulkan` 的 viewport 部分。

**两个方案，需要决策**：
- A（保守）：拆 `ImGuiViewportBackend.cpp` 分部文件，主文件留 pipeline/字体/RenderDrawData/事件（~800）。
- B（激进）：multi-viewport 只有编辑器场景在用——如果 gkNextEditor 之外无人依赖，整块挂 `IDebugUiProvider` 式注入点移 DevTools，核心层直接 -700。**需先 grep 各 Application 的 viewport flag 使用情况再定。**

### 2.5 CPUAccelerationStructure.cpp（1,136 行）— 中

一个文件四个类 + 自由函数：`FCPUProbeBaker`、`FCPUAccelerationStructure`、`FCPUBrickTable`、`FCPUPageIndex` + tinybvh TraceRay/chamfer DF pass。另有文件级裸 static 全局（`GCpuBvh`、三个裸指针 list）。

**建议**：按类拆 `CpuBvh.cpp`（tinybvh 封装 + TraceRay，顺手把 static 全局收进一个 struct）、`ProbeBaker.cpp`（含 chamfer DF）、`BrickPageTable.cpp`（BrickTable + PageIndex）；主文件留 FCPUAccelerationStructure 调度逻辑（~450）。

### 2.6 SkinnedMeshComponent.cpp（988 行）— 中低

组件接口 + ozz 采样后端 + **程序化 IK（foot placement / look-at / MakeRotationBetween 等，561 行以后 ~400 行）** 三种职责。建议拆 `SkinnedMeshComponent.IK.cpp`（或 `ProceduralPose.cpp`）；ozz 采样（SampleOzz/FinalizePose/AdvanceAnimationState）如再要瘦身可下沉 `OzzPlayback.cpp`。

### 2.7 低优先级（可顺手做，不单独立项）

| 对象 | 问题 | 动作 |
|---|---|---|
| CVarSystem.cpp (774) | RegisterInt/UInt/Float/Bool/String 五连每个 ~28 行结构相同 | 模板归并 `RegisterTyped<T>`，净减 ~110 |
| NextPhysics.cpp (872) | 前 470 行 Jolt 配置 boilerplate + body 工厂 | 可拆 `NextPhysics.Bodies.cpp`；优先级低 |
| FSceneLoader.cpp (970) | glTF 核心 loader，职责单一 | 不动 |

### 2.8 实现驻留头文件（Round 1 Phase 5 计划未执行项）

| Header | 非空行 | 内容 |
|---|---:|---|
| Vulkan/SyncAndTiming.hpp | 479 | Fence/Semaphore/VulkanGpuTimer/Scoped timers 全 inline 实现 |
| Runtime/Subsystems/TaskCoordinator.hpp | 340 | tsqueue/TaskThread/TaskCoordinator header-only |
| Vulkan/DebugUtilities.hpp | 304 | 被 include 34 次（见 3.3），实现下沉收益最大 |

下沉 .cpp 对 LOC 中性，但 3 个头合计被 include 80+ 次，编译时间与重编译范围收益显著。

---

## 3. #include 分析

### 3.1 总量判断

1,431 行，占 4.0%——**include 行数本身不是 LOC 的主要矛盾**（全删也不够补 3.9k 缺口）。真正的收益在：编译时间、重编译范围、依赖方向卫生。以下按"净减行数"与"卫生"两类列出。

### 3.2 可直接削减的行（~200 行）

| 问题 | 测量 | 动作 |
|---|---|---|
| CoreMinimal 已含的 std 头被重复 include | `<vector>`13 / `<string>`12 / `<memory>`12 / `<array>`7 / `<functional>`7 / `<filesystem>`8 / `<cstring>`4 / `<cstdint>`3，共 **66 处** | 直接删除（仅限已 include CoreMinimal 的文件） |
| spdlog 单独 include | **35 处**；AGENTS.md 声称 CoreMinimal 含 spdlog，实际没有 | 二选一：spdlog 进 CoreMinimal（删 35 行，与文档一致）或改文档。推荐前者——这 35 个文件本来就全量重编 |
| `<algorithm>` 27 处 | CoreMinimal 未含 | 加入 CoreMinimal 后删 27 处，或保持现状（`<algorithm>` 较重，C++20 后尚可） |
| Scene.hpp 的死 include | `NextPhysics.h` + `NextPhysicsTypes.h` 在头内**零符号使用**，经 Scene.hpp 传递给 25 个文件 | 删除。这也是 **Assets→Runtime 的层次反向依赖**，与 Round 1 修掉的 Texture→Engine 同类 |

### 3.3 路径风格不统一（卫生，~60 处）

Vulkan 目录内部互相用相对路径，目录外用全路径，同一个头两种写法并存：

- `"DebugUtilities.hpp"` 18 处 vs `"Engine/Vulkan/DebugUtilities.hpp"` 16 处
- `"Device.hpp"` 12 处、`"Instance.hpp"` 6 处、`"GpuResources.hpp"` 6 处等同类 ~60 处

**建议**：统一为 `Engine/...` 全路径（.clang-tidy include cleaner 可强制），grep 工具链和人都不再需要猜上下文。

### 3.4 重 header 传递链（编译时间主矛盾）

| Header | 被 include 次数 | 问题 |
|---|---:|---|
| Engine.hpp | 21 | 拉满 Scene.hpp + VulkanBaseRenderer.hpp + Model.hpp + RenderingPipeline.hpp + SceneList.hpp…改动几乎全量重编 |
| Scene.hpp | 25 | 拉 CPUAccelerationStructure.h（值成员，难免）+ Model.hpp + 死的 NextPhysics 头 |
| GpuResources.hpp | 23 | — |
| Node.h | 23 | — |

**建议**：已有三个 Fwd 头（AssetsFwd 33 / VulkanFwd 25 / RuntimeFwd）习惯良好，下一步把 Engine.hpp 的成员持有改 fwd + 引用/指针可达处全部 fwd 化；`cpp 文件 include 数` Top（Engine.cpp 55、VulkanBaseRenderer.cpp 45、UserInterface.cpp 40）会随 §2 拆分自然下降。

### 3.5 执行方式

不要手工扫：`.clang-tidy` 已配 include cleaner，生成 compile db 后跑 `tools/clang-tools/run-clang-tidy.py` 让工具列清单，人只做删除与 CoreMinimal 调整两类决策。

---

## 4. 建议执行顺序（每步独立可验证）

| 步骤 | 内容 | 净减(≈) | 风险 |
|---|---|---:|---|
| R2-P1 | include 卫生：Scene.hpp 死头、66 处 std 冗余、spdlog 进 CoreMinimal、路径统一 | −150 | 低（机械，构建即验证） |
| R2-P2 | Engine.cpp：RegisterReflection 移出（表驱动）+ GetUniformBufferObject 抽离 | −100 | 低 |
| R2-P3 | VulkanBaseRenderer 三分部拆分 | −100 | 中（纯移动，gnb shot 三渲染路径各一张） |
| R2-P4 | QuickJSBindings 按域拆 4 文件 | −0 | 低（FlappyJs replay 回归） |
| R2-P5 | CPUAccelerationStructure 按类拆 + static 全局收口 | −50 | 中（软 GI 路径 gnb shot 验证） |
| R2-P6 | SkinnedMeshComponent IK 拆分；CVar 模板归并 | −150 | 低 |
| R2-P7 | 大 header 实现下沉（SyncAndTiming/TaskCoordinator/DebugUtilities） | −0 | 低 |
| R2-P8 | UserInterface viewport：~~方案 B~~ 已否决，仅方案 A（分部拆分） | −0 | 中 |
| R2-P9 | 死代码清理：§5.1 确认清单（−115）+ 构建期工具扫描一轮 | −115~−400 | 低 |
| R2-P10 | Pipeline 样板表驱动（§5.2 四项） | −400 | 中（4 个创建点全路径 gnb shot） |

合计净减 ~1,050~1,350。预计终值 32,600~32,900（gnb loc 口径）。

---

## 5. 30k 缺口怎么补（已决策 2026-06-11）

> **用户拍板：只执行第 2、3 项，其余不动。** 即：UserInterface multi-viewport 留在核心（§4 R2-P8 方案 B 否决，仅可做方案 A 的分部拆分）；FSceneSaver、HdrTextureCache 保留核心；不为凑 30k 数字做额外外移。

1. ~~UserInterface multi-viewport → DevTools~~ **否决**
2. **死代码扫描** ✅ 执行 —— 实测结果见 §5.1
3. **Pipeline 样板表驱动压缩** ✅ 执行 —— 具体设计见 §5.2
4. ~~FSceneSaver 外移~~ **否决**
5. ~~HdrTextureCache 外移~~ **否决**

### 5.1 死代码扫描实测（2026-06-11，grep 启发式）

扫描方法：a) Engine 全部头文件 basename 与全仓 include 行比对，找零 include 头；b) Engine 头中定义的 225 个 class/struct 名在全仓（src 除 ThirdParty）做全词引用计数，≤3 次逐个人工核验；c) Utilities/DebugUtilities 自由函数同法。

**确认死代码（可直接删，合计 ≈115 行）：**

| 对象 | 位置 | 行数 | 证据 |
|---|---|---:|---|
| `GlmJsonConverter.h` 整文件 | Runtime/Reflection/ | 75 | 全仓零 include |
| `class BufferMemoryBarrier` | Vulkan/DebugUtilities.hpp:238 | ~30 | 全仓仅定义处 1 次出现 |
| `class AccumulatePipeline;` 前置声明 | Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp:11 | 1 | 类已不存在，残留 fwd decl |
| `class PathTracingPipeline;` 前置声明 | Rendering/PathTracing/PathTracingRenderer.hpp:9 | 1 | 同上 |
| `FileHelper::SetRunMode()` | Utilities/FileHelper.hpp:151 | 1 | 仅定义处出现，无调用方 |

**结论与修正**：grep 能确认的死代码远低于原估的 300~600 行——Round 1 搬家做得干净。方法级死代码（类活着但个别方法没人调）grep 测不准，完整清单需编译期工具：`-Wunused-function`、链接器 `--gc-sections --print-gc-sections` 报告、clang-tidy `misc-unused-*`。建议作为 R2-P9 在有构建环境的机器上跑一轮，**预期再收 100~300 行**，不应预算更多。

### 5.2 Pipeline 样板表驱动设计（估净减 ~400）

样板分布实测：`vkCreateComputePipelines` 仅 CommonComputePipeline.cpp 3 处；`vkCreateGraphicsPipelines` 4 处（CommonComputePipeline×2、ShadowMapPass×1、UserInterface×1）；`VkPipeline*StateCreateInfo` 样板结构体 29 个散布这 3 个文件。

| 改造 | 现状 | 设计 | 净减(≈) |
|---|---|---|---:|
| Compute 三连 | `ZeroBindWithTLASPipeline` / `ZeroBindPipeline` / `ZeroBindCustomPushConstantPipeline` 三个 ctor 60 行近乎逐行相同（layout + ShaderModule + vkCreateComputePipelines），BindPipeline 亦两两重复 | 合并为单类 `ZeroBindComputePipeline`，构造参数：`pushConstantSize`（默认 `sizeof(GPUScene)`）、`bool bindTlas`（仅 Android 分支用）。或保守版：三类保留、ctor 委托共享 `BuildComputePipeline()` helper | −110 |
| GraphicsPipelineBuilder | VisibilityPipeline / GraphicsPipeline / ShadowMapPass / UserInterface 四个创建点各自手写全套 state-info（每处 100~150 行，绝大部分是相同默认值） | `Vulkan/GraphicsPipelineBuilder.{hpp,cpp}`（~120 行）：常用默认值 + 链式 setter（`SetShaders/SetVertexInput/SetDepth/SetBlend/SetDynamicStates`），四处调用点各收敛到 30~50 行 | −180 |
| RenderPass 四 ctor | RenderingPipeline.cpp 17/66/135/199 行四个重载，每个 ~55 行 attachment/subpass 手写 | 一个 `FRenderPassSpec`（attachment 数组：format/loadOp/finalLayout + 可选 depth），四个重载变薄壳或直接换调用方 | −110 |
| PipelineLayout 三 ctor | 369/396/414 三个重载主体重复（cachedLayouts 组装 + CreateInfo） | 委托到单一私有 `Init(layouts, ranges)` | −40 |

注意：UserInterface 的 UI pipeline 创建点同时是 §2.4 拆分对象，先做本表改造再做 §2.4 拆分可避免二次搬动。验证：三条渲染路径 + 编辑器 UI 各 `gnb shot` 一张；CSM 阴影场景一张（ShadowMapPass 受影响）。

---

## 6. 量测登记（执行 agent 填写）

| 时间 | 步骤 | gnb loc (src/Engine) | 备注 |
|---|---|---:|---|
| 2026-06-11 | Round 2 基线 | 33,924（上轮 Phase 5 终值；本轮 ripgrep 含注释口径 35,476） | `./gnb loc` 复测确认 33,924 / 200 files |
| 2026-06-11 | R2-P1 include 卫生 | 33,807 | spdlog 进 CoreMinimal（37 处）、66+ 处 std 冗余、116 处相对路径改全路径；NextPhysicsTypes.h 实际被 Scene.hpp 值成员使用，仅 NextPhysics.h 是死头 |
| 2026-06-11 | R2-P2~P9 | 33,924 (214 files) | god class 拆分净减被 22 个新 TU 的 include/注释样板抵消；P2 中 RegisterReflection 已在 Round 1 表驱动化，仅做了 GetUniformBufferObject 抽离 + 删 4 个无人调用的 jitter helper |
| 2026-06-11 | R2-P10 终值 | **33,690 (216 files)** | 较基线 −234。P9 实收：GlmJsonConverter/BufferMemoryBarrier/残留 fwd decl/HdrsHs(写后不读)/GenShadowMap(仅注释引用)≈200 行；SetRunMode 误判，MagicaLego 在用，保留 |

---

## 7. 执行约束

Round 1 第 7 节全部约束继续生效（AGENTS.md 规范、ThirdParty 不动、CMake 三分支同步、先 grep 再动手、行为零变更、每步独立提交）。本轮补充：

1. 分部文件命名沿用既有先例：`Engine.Input.cpp` / `Scene.Build.cpp` 风格（`类名.职责.cpp`）。
2. 拆分部文件**不新增公共头**；内部协作声明放 `*.Internal.hpp` 或匿名 namespace。
3. R2-P1 的 CoreMinimal 变更触发全量重编，单独提交、单独验证。
4. 每步收尾跑 `./gnb build gkNextRenderer gkNextUnitTests` + 对应 `gnb shot`；R2-P3/P5 涉及三条渲染路径，PathTracing/SoftwareTracing/SoftwareModern 各截一张。
