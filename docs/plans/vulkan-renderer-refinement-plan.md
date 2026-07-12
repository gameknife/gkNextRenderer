---
title: "Vulkan + Renderer 专项精炼：命名归一 + 渲染器去重"
category: plan
status: 草案
owner: engine
created: 2026-06-15
last_updated: 2026-06-15
---

# Vulkan + Renderer 专项精炼：命名归一 + 渲染器去重

> 状态：分析/待执行 | 编写日期：2026-06-15 | 面向：接手实施的 AI agent
>
> 前置：[engine-core-refactor.md](engine-core-refactor.md)（Round 1，已完成）、[engine-core-refactor-round2.md](engine-core-refactor-round2.md)（god class 拆分 + pipeline 表驱动，部分已落地）、[engine-refactor-plan.md](engine-refactor-plan.md)（§3.1 命名问题首次提出，未执行）。
>
> **本轮范围只有两块：`src/Engine/Vulkan/**` 与 `src/Engine/Rendering/**`。** 不动 Runtime / Assets / Editor。主题不是「砍 LOC」，而是把这两层做到 **命名自洽、无重复、无死代码**。LOC 净减为副产品（预计 −250 ~ −400）。

---

## 0. 与既有 Round 的关系（避免重复劳动）

执行前先确认以下既成事实，本计划**不再覆盖**：

- VulkanBaseRenderer 已按职责拆为 `.cpp` + `.GpuDriven.cpp` + `.GiBake.cpp` + `.RayTracingAS.cpp`（Round 2 §2.1 已落地），资源已分组进 11 个 struct。**本轮不再拆文件。**
- `GraphicsPipelineBuilder`、`RenderPass::FRenderPassSpec/Init`、`PipelineLayout::CreateLayout` 已抽出并在用（Round 2 §5.2 大部分已落地）。**本轮不再做这三项。**
- `SyncAndTiming` / `DebugUtilities` 已有 `.cpp`，实现已下沉。**本轮不动。**

本轮聚焦 Round 们**没碰过**的两类问题：**渲染器五套命名互不对应**（engine-refactor-plan §3.1 提出但一直没做）和**多个 LogicRenderer 之间的实现级重复**（此前从未分析）。外加几处确认的死代码。

---

## 1. 现状快照（2026-06-15）

| 目录 | 文件数 | 非空行(含注释) |
|---|---:|---:|
| `src/Engine/Vulkan/`（不含 RT） | 33 | ~6,120 |
| `src/Engine/Vulkan/RayTracing/` | 12 | 813 |
| `src/Engine/Rendering/` | 21 | ~4,806 |

Rendering 单文件分布（主要矛盾在 `VulkanBaseRenderer.*` 与五个 LogicRenderer）：

| 文件 | 行数 |
|---|---:|
| `VulkanBaseRenderer.cpp` | 1,265 |
| `VulkanBaseRenderer.GpuDriven.cpp` | 656 |
| `PathTracing/PathTracingRenderer.cpp` | 354 |
| `VulkanBaseRenderer.GiBake.cpp` | 326 |
| `VulkanBaseRenderer.RayTracingAS.cpp` | 257 |
| `PipelineCommon/CommonComputePipeline.cpp` | 249 |
| `Shadow/ShadowMapPass.cpp` | 189 |
| `SoftwareModern/SoftwareModernRenderer.cpp` | 162 |
| `SoftwareModern/SwModernNoAmbientRenderer.cpp` | 132 |
| `SoftwareTracing/SoftwareTracingRenderer.cpp` | 104 |

---

## 2. 问题诊断（带证据）

### 2.1 【最高优先】渲染器命名五套并存、语义反转

同一个渲染器，`enum / namespace / 类名 / 目录 / GPU-timer 字符串` 五处各叫各的，且 Modern/Legacy 语义反转。证据：

| `ERendererType` | 类名 | namespace | 目录/文件 | timer 字符串 |
|---|---|---|---|---|
| `ERT_PathTracing` | `PathTracingRenderer` | `Vulkan::RayTracing` | `PathTracing/` | `"PathTracing"` |
| `ERT_ModernDeferred` | `SoftwareTracingRenderer` | `Vulkan::ModernDeferred` | `SoftwareTracing/` | `"SoftTracing"` |
| `ERT_LegacyDeferred` | `SoftwareModernRenderer` | `Vulkan::LegacyDeferred` | `SoftwareModern/` | `"SoftModern"` |
| `ERT_LegacyDeferredNoAmbient` | `Renderer`（裸名） | `Vulkan::NoAmbientDeferred` | `SoftwareModern/SwModernNoAmbient*` | `"SoftModernNoAmbient"` |
| `ERT_VoxelTracing` | `VoxelTracingRenderer` | `Vulkan::VoxelTracing` | 寄生在 `SoftwareModernRenderer.{hpp,cpp}` | `"VoxelTracing"` |

证据位置：
- enum：`Rendering/VulkanBaseRenderer.hpp:21`
- `namespace Vulkan::RayTracing`（PathTracing）：`Rendering/PathTracing/PathTracingRenderer.hpp:7`
- `namespace Vulkan::ModernDeferred` + class `SoftwareTracingRenderer`：`Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp:13`
- `namespace Vulkan::LegacyDeferred` + class `SoftwareModernRenderer`、`namespace Vulkan::VoxelTracing` + class `VoxelTracingRenderer`（**同一文件内两个 namespace**）：`Rendering/SoftwareModern/SoftwareModernRenderer.hpp:11` 与 `:38`
- `namespace Vulkan::NoAmbientDeferred` + class `Renderer`（裸名）：`Rendering/SoftwareModern/SwModernNoAmbientRenderer.hpp:16`
- timer 字符串第六套命名：`Rendering/VulkanBaseRenderer.cpp:1057-1077`

**危害**：「`ERT_ModernDeferred` 这个枚举指向一个叫 `SoftwareTracing` 的类」对任何阅读者都是陷阱；`VoxelTracingRenderer` 与文件名完全无关；裸 `class Renderer` 在 `grep` 时几乎无法定位。这是核心层可读性最差的一处，且**纯重命名、零行为变化、风险最低**。engine-refactor-plan §3.1 早已点名，一直没做。

### 2.2 【高】LogicRenderer 之间存在大段实现级重复

`SoftwareTracingRenderer`、`SoftwareModernRenderer`、`NoAmbientDeferred::Renderer`、`PathTracingRenderer` 共享同一套「时间累积 + 历史回拷」骨架，却各自复制粘贴。三处确认重复：

1. **历史图回拷（copy pass）逐通道复制**。`SoftwareTracingRenderer.cpp` 与 `SoftwareModernRenderer.cpp` 的 copy pass **几乎逐字相同**：对 `RT_ACCUMULATE_{DIFFUSE,SPECULAR,ALBEDO}` 各做一遍「barrier→TRANSFER_SRC / barrier history→TRANSFER_DST / `vkCmdCopyImage`」。
   - 证据：`SoftwareTracing/SoftwareTracingRenderer.cpp:90-126`（三通道）、`SoftwareModern/SoftwareModernRenderer.cpp:103-124`（三通道，与前者同形）、`SoftwareModern/SwModernNoAmbientRenderer.cpp` copy pass（单通道，同形）。
2. **ReferenceMode 历史图分配块**复制。三个渲染器的 `CreateSwapChain` 都有同一段 `if (GOption->ReferenceMode) { prevSingleXxxId_ = GetTemporalStorageImage(...) } else { prevSingleXxxId_ = Bindless::RT_SINGLE_PREV_XXX }`。
   - 证据：`SoftwareTracingRenderer.cpp:30-44`、`SoftwareModernRenderer.cpp:32-42`、`SwModernNoAmbientRenderer.cpp:34-45`。
3. **`prevSingleDiffuseId_/prevSingleSpecularId_/prevSingleAlbedoId_` 三元组成员**在 PathTracing / SoftwareModern / SoftwareTracing 三个头里各声明一遍。
   - 证据：`PathTracingRenderer.hpp`、`SoftwareModernRenderer.hpp:31-33`、`SoftwareTracingRenderer.hpp:24-26`。

**危害**：改一处历史回拷逻辑要同步改三份；新增一个 deferred 变体又得复制一份。

### 2.3 【中】`ERendererType` 四处并行 switch

新增一个渲染器要同时改至少四个 `switch(type)`，少改一处就出隐性 bug：

- `GetRendererRequirements`：`VulkanBaseRenderer.cpp:118-134`
- `RegisterLogicRenderer`（type→具体类工厂）：`VulkanBaseRenderer.cpp:997-1020`
- ReferenceMode 里的 timer-name switch：`VulkanBaseRenderer.cpp:1057-1077`
- ReferenceMode 里的 pushConst 布局 switch：`VulkanBaseRenderer.cpp:1093-1107`

这是「枚举 + 散落 switch」反模式，可收敛为单一描述表。

### 2.4 【中】全屏 dispatch 口径不统一（潜在正确性问题）

Rendering 层里 `vkCmdDispatch` 的网格尺寸两种写法并存：裸 `RenderExtent().width / 8`（整除截断，分辨率非 8 倍数时会漏算边缘像素）**9 处**，`Utilities::Math::GetSafeDispatchCount(w, 8)`（向上取整）**17 处**。同一个 `SoftwareTracingRenderer::Render` 内 shading/compose 用裸 `/8`、reproject 却用 safe（`SoftwareTracingRenderer.cpp:58/66/79` 对比）。属于不一致 + 潜在掉边 bug。

### 2.5 【低-中】确认的死代码

- `BufferUtil::CreateDeviceBufferViolate`：**全仓零调用方**，仅声明(`BufferUtil.hpp:34`)+定义(`:132`)。且 `Violate` 是 `Volatile` 的拼写错误。可整段删除（~25 行）。
- `PipelineCommon::VisibilityPipeline::swapRenderPass_`：成员声明于 `CommonComputePipeline.hpp:61`，构造函数从不创建、全仓无任何读写。死成员，删之。

### 2.6 【低】`BufferUtil` 创建函数家族冗余

删掉 2.5 的 `Violate` 后，`CreateDeviceBuffer<T>`（模板，含 staging 拷贝）与 `CreateDeviceBufferLocal`（裸 size，自定义 memProp）仍有重复主体：`allocateFlags` 推导 + `buffer.reset` + `memory.reset` + 两处 `SetObjectName` 在两个函数里逐行重复（`BufferUtil.hpp:138-176`）。可抽一个私有原语 `CreateRaw(commandPool, name, usage, memProp, size, buffer, memory)`，两者委托。

### 2.7 【低 / 可选】Compute pipeline 三类构造仍重复

`ZeroBindWithTLASPipeline` / `ZeroBindPipeline` / `ZeroBindCustomPushConstantPipeline` 的构造体除「push 常量大小」与「Android-only TLAS 绑定」外几乎相同（`CommonComputePipeline.cpp:47-160`）。Round 2 已抽出 `CreateComputePipeline` / `BindComputeWithPush` 两个 helper，剩余重复已不大。三类对外被引用 9 / 42 / 19 处（共 ~70 个 `new` 点），**合并收益小、改动面大**。本轮**建议保持现状**，仅在 2.1 改完后顺手把三类的注释/空体对齐。列出仅为存档，默认不做。

### 2.8 【低 / 卫生】两处依赖与 include 小问题

- **VideoCaps 位置/命名错配**：`FVulkanVideoCaps` 物理位于 `Engine/Vulkan/VulkanVideoCaps.hpp`，却用 `Runtime::Remote` 命名空间（`VulkanBaseRenderer.hpp:4/98/244`）。它被 `VulkanBaseRenderer` **按值持有**，于是 `Runtime::Remote` 这个远程串流概念被拉进核心渲染器头、随 Engine.hpp 传递给 ~21 个 TU。本轮可做最小修正：把该类型移入 `Vulkan` 命名空间（文件已在 `Vulkan/` 下，名实一致）；是否改为指针持有以断传递留待后续，不在本轮强求。
- `DescriptorSystem.hpp:9` 为了 `DescriptorSets::Handle()` 里一处 `glm::min` 而 `#include <glm/ext/scalar_common.hpp>`，换成 `std::min`（`<algorithm>`，CoreMinimal 视情况已含）即可去掉重 glm 头。

---

## 3. 既定命名映射（已拍板：技术向命名，2.1 用）

> **决策（2026-06-15，owner 已确认）**：锚定词采用**技术向命名**（`Software*` / `PathTracing`），即如实描述算法、与现有目录一致的方案。「管线向」（`Deferred*` / `RayTracing`）方案已否决。下表即最终标识，执行时直接按此改，无需再做选择。

以**目录名为锚**（目录名最具描述性：描述的是实际技术——path tracing / software tracing / software shading），让 `enum / namespace / class / 目录 / 文件 / timer 字符串` 全部对齐到同一个词：

| 现 enum | → 统一标识 | enum | namespace | class | 目录 | timer |
|---|---|---|---|---|---|---|
| `ERT_PathTracing` | **PathTracing** | `ERT_PathTracing` | `Vulkan::PathTracing` | `PathTracingRenderer` | `PathTracing/` | `"PathTracing"` |
| `ERT_ModernDeferred` | **SoftwareTracing** | `ERT_SoftwareTracing` | `Vulkan::SoftwareTracing` | `SoftwareTracingRenderer` | `SoftwareTracing/` | `"SoftwareTracing"` |
| `ERT_LegacyDeferred` | **SoftwareModern** | `ERT_SoftwareModern` | `Vulkan::SoftwareModern` | `SoftwareModernRenderer` | `SoftwareModern/` | `"SoftwareModern"` |
| `ERT_LegacyDeferredNoAmbient` | **SoftwareModernNoAmbient** | `ERT_SoftwareModernNoAmbient` | `Vulkan::SoftwareModernNoAmbient` | `SoftwareModernNoAmbientRenderer` | `SoftwareModern/`（文件 `SwModernNoAmbientRenderer.*`→`SoftwareModernNoAmbientRenderer.*`） | `"SoftwareModernNoAmbient"` |
| `ERT_VoxelTracing` | **VoxelTracing** | `ERT_VoxelTracing` | `Vulkan::VoxelTracing` | `VoxelTracingRenderer` | **新建 `VoxelTracing/` 目录**，从 `SoftwareModernRenderer.{hpp,cpp}` 抽出 | `"VoxelTracing"` |

净改动：PathTracing 仅改 namespace（`RayTracing`→`PathTracing`，注意别与 `Vulkan::RayTracing`「BLAS/TLAS 加速结构」那一层撞名——后者保持不变，所以这里更要改掉）；SoftwareTracing/SoftwareModern 改 enum+namespace 对齐；NoAmbient 给裸 `Renderer` 起正式名；Voxel 从寄生文件搬出独立目录。

> 备注：上表为已定稿标识，无遗留决策点。VoxelTracing 锚定词同属技术向（描述其体素追踪算法），保持不变。

---

## 4. 分阶段执行计划

> 每个 Phase 独立可提交、可单独验证。顺序：**先死代码（最稳）→ 命名（机械量大）→ 去重（动行为，需截图）→ 收尾卫生**。

| Phase | 内容 | 行为变化 | 风险 | LOC | 验证 |
|---|---|:--:|:--:|---:|---|
| **P1** | 删死代码：`CreateDeviceBufferViolate`、`VisibilityPipeline::swapRenderPass_` | 无 | 极低 | −26 | `gnb build gkNextRenderer gkNextUnitTests` |
| **P2** | 渲染器命名归一（§3 映射表），含 VoxelTracing 抽出独立目录 | 无 | 低 | ~±0 | 三条渲染路径 + Voxel 各 `gnb shot` 一张 |
| **P3** | `ERendererType` 四处 switch 收敛为单一描述表 | 无 | 中 | −60 | 编译 + 五渲染器逐个 `gnb shot` |
| **P4** | 抽时间累积/历史回拷公共骨架，三/四个渲染器复用 | 无（像素应一致） | 中 | −120 | SoftwareTracing/SoftwareModern/NoAmbient/PathTracing 各 `gnb shot`，与改前像素比对 |
| **P5** | dispatch 口径统一为 `GetSafeDispatchCount`（或 `DispatchFullScreen` helper） | 边缘像素可能变化（修正掉边） | 中 | −10 | 非 8 倍数分辨率截图重点看右/下边缘 |
| **P6** | 收尾卫生：`BufferUtil` 抽 `CreateRaw`；`FVulkanVideoCaps`→`Vulkan` 命名空间；`DescriptorSystem` 去 glm 重头 | 无 | 低 | −30 | 全量 `gnb build --reconfigure`（VideoCaps 改命名空间影响面广） |

预计净减 ~250，真正价值在可读性与可维护性。

### P2 执行要点（命名归一）
1. 目录划库靠 `GLOB_RECURSE`（见 round3「关键基建事实」），新建 `Rendering/VoxelTracing/` 目录即自动入 `gkNextRenderer`，无需手改 `SourceFiles.cmake`；但**新增/移动文件需要 `--reconfigure` 一次**让 glob 重新收录。
2. 改名顺序：先改 `enum`（一处定义 + 全仓引用），再逐个渲染器 `namespace`+class+文件名，最后把 §2.3 那四个 switch 的 case 标签同步（P3 会重写这些 switch，可与 P3 合并提交以免改两遍——见下）。
3. `Vulkan::RayTracing` 现同时被「加速结构」(`Vulkan/RayTracing/*`) 与「PathTracingRenderer」占用。PathTracing 渲染器改到 `Vulkan::PathTracing` 后，`Vulkan::RayTracing` 只剩加速结构一处含义，歧义消除。
4. 全程 `grep` 驱动：每改一个标识先 `grep -rn` 数清引用点（含 `Application/**` 里切换渲染器的调用），再动手。

> **P2 与 P3 建议合并提交**：P3 要重写那四个 switch，P2 又要改它们的 case 标签；合并做可避免对同一批行改两次。拆成两个 Phase 仅为表述清晰。

### P4 执行要点（去重，最易引入回归）
- 落点二选一（执行 agent 决定，倾向后者）：
  - (a) 在 `VulkanBaseRenderer` 上加两个公共方法：`uint32_t AcquireHistoryImage(VkFormat, const char* refModeName, uint32_t bindlessFallback)` 与 `void CopyAccumulationToHistory(VkCommandBuffer, uint32_t srcBindless, uint32_t dstBindless)`，各渲染器调用。
  - (b) 在 `PipelineCommon/` 下新建一个轻量 `TemporalResolve` helper（持 `prevSingle*Id_` 三元组 + `SetupHistory(...)` + `CopyToHistory(...)`），各渲染器组合持有。**(b) 更内聚**，不再往已是 god-class 的 base 上加方法。
- 三通道 vs 单通道：helper 接口按「通道列表」设计（`std::initializer_list<std::pair<src,dst>>`），SoftwareTracing/Modern 传三对、NoAmbient 传一对。
- 验证铁律：**改前后同场景同帧截图必须像素一致**（reproject/compose 不变，仅回拷代码合并）。P5 单独做，避免与 P4 的像素一致性验证混在一起。

---

## 5. 验收 Checklist

- [ ] `enum / namespace / class / 目录 / 文件名 / timer 字符串` 对每个渲染器一一对应（§3 表），无裸 `class Renderer`、无寄生 namespace。
- [ ] `grep -rn "CreateDeviceBufferViolate\|swapRenderPass_"` 全仓零结果。
- [ ] `ERendererType` 的 case 分支集中在**一处**描述表；新增渲染器只需加一行表项 + 一个文件。
- [ ] Rendering 层 `vkCmdDispatch` 不再出现裸 `/ 8`（`grep -rn "Extent().width / 8" Rendering` 为空）。
- [ ] 历史回拷逻辑只有一份实现。
- [ ] `gnb build gkNextRenderer gkNextUnitTests` 通过；五条渲染路径 `gnb shot` 与基线对比无非预期差异（仅 P5 边缘像素允许变化）。
- [ ] 全量 `gnb build --reconfigure` 确认所有 program 仍编译（P6 改了命名空间/公共头）。

---

## 6. 执行约束（必读）

1. 继承 Round 1 §7 / Round 2 §7 全部约束：先 `grep` 再改、行为零变化（P5 例外且需明确说明）、ThirdParty 不动、CMake 三平台分支同步、每步独立提交。
2. AGENTS.md 的定向构建原则：本轮全在 Engine 层，验证默认 `gnb build gkNextRenderer gkNextUnitTests`；仅 P2（移动文件）、P6（动公共头/命名空间）需要 `--reconfigure` / 全量。
3. 渲染改动一律 `gnb shot` 肉眼验证，不靠「编译过 = 没问题」。涉及多渲染路径的 Phase（P3/P4/P5）每条路径各一张图。
4. 命名是机械但量大的改动，**一次只改一个标识、改完即编译**，不要一把全替换后再 debug。

---

## 7. 量测登记（执行 agent 填写）

| 时间 | Phase | gnb loc (Vulkan+Rendering) | 备注 |
|---|---|---:|---|
| 2026-06-15 | 基线 | （执行前 `./gnb loc` 记录） | |
| | P1 | | |
| | P2+P3 | | |
| | P4 | | |
| | P5 | | |
| | P6 终值 | | |
