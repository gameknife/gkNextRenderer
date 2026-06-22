---
title: "AmbientCube 命中驱动探针残留（SHARC 式 insert/evict）— 设计方案与开发计划"
category: design
status: 待实现
owner: engine
created: 2026-06-22
last_updated: 2026-06-22
---

# AmbientCube 命中驱动探针残留（SHARC 式 insert/evict）

> 目标：把 path tracer 里 SHARC「命中即插入 / 陈旧即驱逐」的需求驱动思想，移植到 AmbientCube 的**探针激活/残留策略**上。
> 用**实际 ray 命中**（消费侧查询命中 + bake 侧 bounce 命中）替代当前纯几何的近表面启发式来决定「哪些 brick 进池、被烘焙」，从而：
> ① 提升已分配探针的**有效命中率**（被真正消费的占比）；② 省略不贡献的探针；③ 缩小池子；④ 加快 bake。
> 非目标：不替换 HL2 ambient cube 的辐照度表示；不引入 hash 表（brick 规则网格保留）；不动 128B `GPUScene`。
>
> 本文供后续 agent 接手实现。所有源码引用为 `文件:行号`。

---

## 0. 结论（TL;DR）

当前 AmbientCube 的探针池由**几何启发式**决定：CPU 体素化整张 `192×192×48` 网格，凡是 `matId != 0`（近表面）的体素，其所在 brick（`8³`）连同膨胀半径 3 个 brick 一律标活跃、进池、被烘焙（`BrickPageTable.cpp:25-104`、`AmbientCubeBaker.slang:180`）。这套策略**保守、过度分配**：近表面壳层里有大量探针——墙体内部、封闭夹缝、从不被相机/任何被着色表面采样、也从不作为 bounce 源的——照样占 slot、照样吃满 96 条 bake 光线，但对最终画面零贡献。**「分配量」与「真实工作集」之间存在结构性冗余**，这正是 bake 速度与池大小的天花板。

SHARC 在 path tracer 里走的是另一条路（`Sharc.slang`、`Core.Sharc{Update,Resolve,Query}.comp.slang`）：entry **按实际命中按需分配**，resolve pass 对**陈旧**（`staleFrameNum > StaleFrameMax`）entry 驱逐——working set 恒等于「真正被追踪/被看到的部分」，自带自剪枝。

本方案的核心：**不换成 hash，而是把「插入/驱逐」的激活策略搬到现有 brick 网格上**。保留几何分类作为**候选上界**，叠加一层**命中驱动的残留选择**：只有被实际 ray 命中（消费查询 or bake bounce）的 brick 才长期驻留在池里并被持续烘焙；连续若干帧无命中的 brick 老化、驱逐、归还 slot。预期在典型场景把「有效命中率」从几何壳层的偏低水平拉高，并按真实工作集缩池 + 提速 bake。

落地按性价比分阶段：**Phase 0 先做插桩量化真实利用率**（确认收益空间再投入结构性改造），再依次做命中标记 → 残留/驱逐 → GPU 驱动闭环。

---

## 1. 背景与目标

### 1.1 问题陈述

「探针有效命中率」定义为：被消费者真正读取（或真正作为 bounce 源贡献）的探针数 / 已分配并烘焙的探针数。当前几何启发式让分母偏大：

- **激活判据是几何近表面**，而非「是否被消费」。`distanceToSolid` 壳层 + dilation 3 把表面周围一大圈 brick 全收进来（`Internal.hpp:11-13`、`BrickPageTable.cpp:61-77`）。
- **固定 cap、不随真实使用缩放**：池恒为 `ratio × 3456` bricks/cascade（默认 0.66，`EngineCVars.cpp:174`、`Scene.cpp:39-41`）。稀疏场景只是空 slot 更多，并不更省（见 `ambient-cube-memory-reduction.md` 实现偏差 #3）。
- **bake 对全部活跃探针无差别投光**：`BakeAmbientCubeCascade` 按 `activeBrickCount × 512` 派发（`VulkanBaseRenderer.GiBake.cpp:113-120`），每个 `minDist < 8` 的探针 6 面 × 16 光线 = 96 条全套着色（`AmbientCubeBaker.slang:180-200`、`AmbientCube.slang:88`）。被烤但从不被读的探针 = 纯浪费。

### 1.2 目标与约束

- **提升有效命中率**：让分配/烘焙集合逼近真实工作集。
- **缩池 + 提速 bake**：派发线程数随真实使用下降。
- **不退化画质 / 不漏光**：保留方向 SDF 防漏光（`AmbientCube.slang:519-547`）与 cascade 回退。
- **不破坏现有资产 / ABI**：ambient 数据全程运行时烘焙不落盘（memory-reduction 计划 F6）；`GPUScene` 维持 128B，新增地址走 `AmbientResources` 二级表（`Scene.cpp:235`、memory-reduction 计划实现偏差 #1）。

---

## 2. 现状分析

### 2.1 当前 AmbientCube 探针启发式（几何近表面）

数据流（静态场景，网格世界固定不跟相机，memory-reduction 计划 F1）：

1. **CPU 体素化**：`FCPUProbeBaker::ProcessCube → VoxelizeCube`（`ProbeBaker.cpp:278-298,173-221`）对每个体素发 6 轴 + 8 对角线 ray，写 `matId`（是否紧贴表面）与方向 `distanceToSolid` SDF。
2. **活跃 brick 分类**：`FCPUBrickTable::UpdateData`（`BrickPageTable.cpp:25-104`）——只要 brick（或其膨胀半径 `kAmbientBrickDilationRadius=3` 邻域，`Internal.hpp:13`）内有任一 `matId != 0` 体素，整块标活跃；按 brick-linear 顺序压入池 slot（稳定、cap 外标 `INVALID`）。
3. **池/表上传**：`Scene::SetAmbientActiveBrickCounts` + brick 表/活跃列表上传（`CPUAccelerationStructure.cpp:503-507`、`Scene.cpp:248-260`）。
4. **GPU 烘焙**：`BakeAmbientCubeCascade` 遍历活跃 brick（`VulkanBaseRenderer.GiBake.cpp:98-213`）；shader `Render` 内再卡 `minDist < 8 && cubeIdx >= 0`（`AmbientCubeBaker.slang:180`），`FaceTask` 当场 TraceRay + 当场着色，indirect bounce 取上一帧 cube（`AmbientCubeBaker.slang:29-62`）。
5. **运行期消费**：着色时逐像素 `interpolateAmbientCubes<...>(worldPos, normal)`（`AmbientCube.slang:477-566`），8-tap 三线性 + 每 tap 方向 SDF 几何拒绝防漏光。消费方：`Core.VoxelTracing`、`RayTracers.slang`、raster `SoftwareModern` 等。

**关键观察**：第 2 步的判据是「**几何上靠近表面**」，与「**这个探针会不会被第 5 步读到 / 会不会在第 4 步当 bounce 源**」完全脱钩。激活集是消费集的保守超集，二者差额即冗余。

### 2.2 path tracer 下的 SHARC（需求驱动）

引擎封装在 `Sharc.slang`（官方 header 在 `assets/shaders/third_party/sharc/`，宏适配见 `Sharc.slang:14-31`），三 pass（`PathTracingRenderer.cpp`、`Core.Sharc*.comp.slang`）：

- **Update**：稀疏追踪（5×5 tile 轮转，`FSharcUpdateCache::ShouldShadePixel`，`Sharc.slang:228-246`）。每个 surface hit `SharcUpdateHit`，miss `SharcUpdateMiss`，按 throughput 累积（`Sharc.slang:250-263`）。**entry 由命中按需分配**（hash key = 量化位置 + 法线 + 相机距离 LOD level，`SharcGetVoxelSizeAt`/`HashGridGetLevel`，`Sharc.slang:63-68`）。
- **Resolve**：`SharcResolveOfficialEntry`（`Sharc.slang:178-194`）合并本帧 accumulation + 历史 resolved，**对陈旧 entry 驱逐**（`staleFrameNum > StaleFrameMax`，`EngineCVars.cpp:203` 默认 180）。
- **Query**：`FSharcQueryCache::TryQueryRadiance`（`Sharc.slang:317-338`）在非 primary、diffuse/rough、`segmentLength >= voxelSize` 时读缓存 radiance 并 early-out。

**关键观察**：SHARC 的 working set = 实际被路径命中的体素；不被命中的 entry 自动老化释放。这就是「命中驱动 + 自剪枝」，正好是 AmbientCube 激活策略缺的那一环。引擎已有的 `IRadianceCache` 抽象（`RadianceCache.slang:11-40`）和「命中即记账」的插桩点，是现成的可复用脚手架。

### 2.3 差距对照

| 维度 | 当前 AmbientCube | SHARC | 本方案要补的 |
|---|---|---|---|
| 激活判据 | 几何近表面 + dilation 3 | 实际 ray 命中 | 命中驱动激活 |
| 空间结构 | 规则 brick 网格（`8³`） | spatial hash | **保留 brick 网格** |
| 残留/剪枝 | 无（固定 cap，静态） | stale 帧数驱逐 | brick 老化驱逐 |
| 池大小 | cap 绑定、不随使用缩放 | ≈ 真实工作集 | 逼近工作集 |
| bake/着色成本 | O(活跃探针 × 96) | O(唯一命中 cell) | O(被命中 brick × 96) |
| 防漏光 | 方向 SDF（确定性） | normal hash | 保留方向 SDF |

> 注意定位：本方案与 `idtech8-hybrid-gi-vs-ambientcube.md` 的「Item C：统一世界辐射缓存」**正交且互补**。Item C 是「加一层 hash radiance cache 给 bounce 供能（着色去重）」；本方案是「**改 brick 的激活/残留策略**（分配去冗余）」。两者可独立推进，也可叠加（命中残留 + 缓存着色）。

---

## 3. 核心思想

**把 SHARC 的 insert/evict 映射到 brick 网格，而不是替换网格本身。**

brick 网格本身已是一个量化的空间结构（等价于 SHARC hash 的「桶」，只是寻址是直接计算而非 hash）。因此无需引入 hash 表，只要把**激活策略**从「几何静态标记」换成「命中动态残留」：

```
候选集（几何，上界）  ∩  需求集（命中，残留选择）  =  真实驻留探针
   ↑ 现有 UpdateData                ↑ 新增：命中标记 + 老化驱逐
```

- **候选集**（几何近表面，沿用现有分类）= 允许进池的**白名单上界**，保证不会激活一个物理上不可能有 GI 的位置（防止 hash 式漏光/野命中），并解决「先有鸡还是先有蛋」（探针必须先被烤才能被消费）。
- **需求集**（命中驱动）= 在候选集内，按**实际命中**选择真正驻留并持续烘焙的 brick；连续 N 帧无命中 → 老化 → 驱逐归还 slot。

命中来源两路（取并集，保证 off-screen 的 bounce 源不被误删）：
- **消费侧 usage feedback**：`interpolateAmbientCubes` 实际读取的 8-tap 所在 brick = 被消费 → 命中。直接度量「对画面的贡献」。
- **bake 侧 bounce feedback**：`FaceTask` 的 indirect bounce ray 命中表面 → 命中点所在 brick 是「被需要的 bounce 源」。覆盖相机看不到但参与多次反弹的探针。

---

## 4. 设计方案

### 4.1 每 brick 残留状态（GPU 端）

新增一个与 brick 表同规模的 per-brick 残留缓冲（每 cascade `3456` 项，4–8 B/项，极小）：

```c
struct AmbientBrickResidency {   // 建议 8 B
    uint  lastHitFrame;          // 最近一次被命中的全局帧号（消费 or bounce 命中时原子 max）
    uint  hitCountAccum;         // 命中计数（可选，用于优先级/调试热力图）
};
```

- 消费/烘焙 shader 命中某 brick 时：`InterlockedMax(residency[brick].lastHitFrame, currentFrame)`（再可选 `InterlockedAdd(hitCountAccum, 1)`）。
- 与 SHARC 的 `accumulatedFrameNum / staleFrameNum`（`Sharc.slang:105-110`）同构——这里用 `currentFrame - lastHitFrame` 当 stale 计量。

### 4.2 命中标记插桩点

- **消费侧**：在 `interpolateAmbientCubes` / `interpolateAmbientCubesStable`（`AmbientCube.slang:477,568`）每个有效 tap 命中 `TryFetchCubePool` 成功（`AmbientCube.slang:419-429`）后，对该 brick 标记命中。
  - 成本顾虑：消费在逐像素热路径。**用稀疏标记**（类似 SHARC 5×5 tile 轮转，`Sharc.slang:239-245`）：每像素只有按 `pixel % tile + frameOffset` 命中的子集真正写原子，降低原子争用。命中标记只是「这个 brick 被人要了」，无需每像素都记。
- **bake 侧**：在 `FaceTask` indirect 分支（`AmbientCubeBaker.slang:53-55`）命中表面、取 `interpolateAmbientCubesStable` 前，对命中点 world→brick 标记命中。这天然是稀疏的（探针数 << 像素数）。

### 4.3 残留管理（候选 ∩ 需求 + 驱逐）

把「分类」从一次性几何标记，升级为**带残留状态的重分类**。两条实现路线：

**路线 A（CPU 残留，先落地）**——沿用现有 CPU 构建 + flush 时机：
1. GPU 命中标记缓冲在 flush 时 readback（或用 N 帧前的快照，容忍一帧延迟）。
2. CPU 在 `FCPUBrickTable::UpdateData`（`BrickPageTable.cpp:25`）里把判据从「几何活跃」改成「几何候选 **且**（`currentFrame - lastHitFrame < EvictFrames` **或** 新激活 grace 期内）」。
3. 新出现的几何候选给一个 **grace 烘焙窗口**（先烤 K 帧再判命中，解决鸡蛋问题）；超窗口仍无命中 → 不驻留。
4. slot 分配沿用 brick-linear 稳定顺序；被驱逐的 brick 释放 slot，其 cube 数据清零（`ClearAmbientCubeCache` 局部，`VulkanBaseRenderer.GiBake.cpp:50`）。
- 优点：复用现有 CPU 分类/上传管线，改动集中、低风险。
- 缺点：有 readback 同步与一帧延迟；动态镜头下残留集变化滞后。

**路线 B（GPU 驱动闭环，进阶）**——更贴近 SHARC，无 readback stall：
1. GPU compute「residency resolve」pass（仿 `Core.SharcResolve`）每帧扫 brick 表：命中近 → 保持/分配 slot；陈旧 → 驱逐 + 清零。
2. GPU 端 free-list / 原子计数器分配 slot，直接产出 `AmbientActiveBrickList`（`AmbientCube.slang:372-410` 的解码沿用）。
3. bake 派发用 `vkCmdDispatchIndirect`，组数由 GPU 写的活跃计数驱动（去掉 CPU `activeBrickCount` 依赖，`VulkanBaseRenderer.GiBake.cpp:113`）。
- 优点：真正的命中驱动闭环、零 readback、响应快。
- 缺点：slot churn 下的稳定性、跨 brick 邻居（propagation/8-tap）与驱逐时序、indirect dispatch 改造，复杂度高。建议路线 A 验证收益后再上。

### 4.4 与现有机制协同

- **方向 SDF 防漏光**（`AmbientCube.slang:519-547,640`）：不变。命中残留只影响「哪些 brick 在池里」，tap 拒绝逻辑照旧；被驱逐 brick 的 tap 自然走 `TryFetchCubePool` 失败回退（`AmbientCube.slang:507-510`），等价于现有「未分配 → 跳过 → 下坠更粗 cascade」（memory-reduction 后续 E）。
- **cascade**：每 cascade 独立残留状态（远 cascade 命中更稀 → 自然更省）。
- **ping-pong**：`BakeAmbientCubeCascade` 的 pong 拷贝按 pool slot 跨度（`GiBake.cpp:147-153`），驱逐后活跃区更小 → 拷贝更小，顺带省。
- **GPUScene**：新增 `ResidencyBuffer` 地址挂到 `AmbientResources`（`Scene.cpp:235` 一带），不动 128B。

### 4.5 「省略不贡献探针」的两个判据

1. **未被命中**（主信号）：上面的残留驱逐。
2. **辐照度恒零/极低**（可选副信号）：完全被遮蔽、烤出来恒黑的探针（sky/sun/bounce 均无贡献）即便几何活跃也可降级。可在 bake 后检查 cube 6 面 + skyVisibility 是否全 ~0 并持续多帧，作为额外驱逐条件。优先级低于命中信号，作为 Phase 4 调优。

---

## 5. 改动清单（file:line）

**Shader（`assets/shaders/`）**
- `common/BasicTypes.slang:70-76` 附近 — 新增 `AmbientBrickResidency` 结构；`AmbientResources` 增 `ResidencyBuffer` 地址 + `GetGpuscene()` property。
- `common/AmbientCube.slang:419-429,477-566,568-660` — `TryFetchCubePool` 命中后稀疏标记 brick 命中；新增 `MarkAmbientBrickHit(cascade, probePos)` helper。
- `common/AmbientCubeBaker.slang:53-55` — indirect bounce 命中点标记 brick 命中。
- 新增 `Bake.AmbientResidencyResolve.comp.slang`（路线 B）— 扫 brick 表做 keep/evict + slot 分配。
- `Bake.ClearAmbientCubeCache.comp.slang` — 支持按 brick 局部清零（驱逐时）。

**C++（`src/Engine/`）**
- `Assets/Acceleration/BrickPageTable.cpp:25-104` — `UpdateData` 判据改「几何候选 ∩ 命中残留 + grace 窗口」（路线 A）。
- `Assets/Acceleration/CPUAccelerationStructure.cpp:503-507,540-544` — flush 时 readback 命中缓冲并喂入 `UpdateData`（路线 A）。
- `Assets/Core/Scene.cpp:39-41,235-260,422-433` — residency buffer 分配/地址/上传；活跃计数来源（路线 B 改 indirect）。
- `Rendering/VulkanBaseRenderer.GiBake.cpp:98-213` — 命中缓冲 barrier；路线 B 改 `vkCmdDispatchIndirect`；驱逐 brick 的局部清零。
- `Runtime/Config/EngineCVars.cpp:162-177` 一带 — 新增 cvar（见 §7）。
- `Runtime/Config/UserSettings.hpp` — 对应字段。

---

## 6. 分阶段开发计划

> 依赖链：Phase 0 量化是一切前提（先证明冗余真实存在、有多大）。Phase 1–2 是 MVP（CPU 残留），Phase 3 是 GPU 闭环，Phase 4 调优。

### Phase 0 — 插桩量化真实利用率（必做，低成本，决定是否继续）
- 加 GPU per-brick「本帧是否被消费查询」标记 + per-brick「本帧是否被 bounce 命中」标记（先不驱逐，只统计）。
- 输出指标：每 cascade「被消费 brick 数 / 几何活跃 brick 数」「被 bounce 命中 brick 数」「从不命中的活跃 brick 占比」。
- 验收：playground / GIBootcampLarge / 一个封闭室内场景，`gnb shot --frames 3000` 后日志打印利用率。**若从不命中占比 < ~20%，收益有限，应止步或只做轻量优化**；若显著（预期开阔场景高），继续。

### Phase 1 — 命中标记 + 残留状态缓冲（无行为变化）
- 落地 `AmbientBrickResidency` 缓冲、`MarkAmbientBrickHit`、消费/bake 两路稀疏标记、GPUScene 二级表接线。
- 此阶段**不改激活集**（仍几何），只让命中数据流通 + 可视化（debug 热力图，仿 `SharcDebugSampleStale`，`Sharc.slang:75-111`）。
- 验收：build 通过；GI 视觉零变化；debug view 能看到命中热力图与几何活跃集的差异。

### Phase 2 — CPU 残留 + 驱逐（路线 A，MVP 收益）
- `UpdateData` 改「候选 ∩ 命中 + grace 窗口」；flush readback 命中；驱逐 brick 局部清零归还 slot。
- 验收：与 Phase 1 截图 diff（重点墙角漏光 / cascade 接缝 / 反弹色无退化）；记录池实际驻留 brick 数（应**下降**）、bake GPU timer（应**下降**）、有效命中率（应**上升**）。

### Phase 3 — GPU 驱动闭环（路线 B，进阶）
- `AmbientResidencyResolve` pass + GPU slot 分配 + `vkCmdDispatchIndirect`，去掉 readback 与一帧延迟。
- 验收：动态镜头/动态光下残留集响应及时、无明显 GI 闪烁/popping；性能优于路线 A。

### Phase 4 — 调优与边界
- grace 窗口长度、`EvictFrames`、稀疏标记 tile 比例、辐照度恒零副判据。
- 动态几何（MagicaLego 增删、编辑器改场景）下与现有重建时机协同；快速移动镜头的 hysteresis（防驻留集抖动）。

---

## 7. CVar / 配置（建议）

```text
r.ambientCube.hitDrivenResidency   = false   # 总开关（默认关，保持现有行为）
r.ambientCube.evictFrames          = 180      # 连续无命中超过此帧数 → 驱逐（对齐 sharc.staleFrameMax）
r.ambientCube.graceFrames          = 30       # 新候选先烤这么多帧再判命中（解决鸡蛋问题）
r.ambientCube.hitMarkTileRatio     = 0.25     # 消费侧稀疏标记比例（仿 sharc.updateSampleRatio）
r.ambientCube.residencyDebug       = 0        # 0=off 1=命中热力 2=驱逐/grace 状态
```

首版默认 `false`：开关一关即回退到纯几何启发式，零风险对照。

---

## 8. 风险与开放问题

- **R1 鸡蛋问题（中）**：探针必须先被烤才能被消费、但只有被命中才驻留。→ 候选集白名单 + grace 烘焙窗口兜底。
- **R2 一帧延迟 / 镜头响应（中，路线 A）**：readback 滞后导致快速移动时残留集追不上 → 走 grace + hysteresis；根治靠路线 B。
- **R3 off-screen bounce 源被误删（中）**：相机看不到但参与反弹的探针若只靠消费命中会被删。→ **bounce 命中并入命中信号**（§4.2 第二路），保证多次反弹链上的探针被保活。
- **R4 原子争用（低-中）**：消费侧逐像素标记 → 稀疏 tile 标记降争用；命中标记是幂等 max，无需精确计数。
- **R5 slot churn 稳定性（中，路线 B）**：驱逐/重分配导致 slot 抖动 → 优先复用同 brick 历史 slot、驱逐设滞后阈值；驱逐时务必清零旧 radiance 防串味。
- **R6 收益场景相关（已知）**：开阔/散布场景收益大，全封闭铺满表面的房间命中集≈几何集、收益小——Phase 0 正是为量化这一点。
- **开放问题**：路线 A 的 readback 用同步拷贝还是 N 帧前快照？grace/evict 默认值需实测回填；是否把 §4.5 辐照度副判据纳入 MVP。

---

## 9. 验证策略

构建（AGENTS.md targeted build）：
```bash
./gnb build gkNextRenderer gkNextUnitTests          # 新增 shader 文件首次需 --reconfigure
```

快速验证（ambient cube 改动**必须**大帧数让 CPU 体素化 + flush + bake 收敛，否则画面只是 sky IBL，看不出 GI 对错——memory-reduction 计划「验证坑」#6）：
```bash
./gnb shot --scene assets/models/playground.glb --frames 3000
./gnb shot --scene assets/models/GIBootcampLarge.proc --frames 3000   # 开阔场景，收益预期大
# 再补一个封闭室内场景做对照（收益预期小）
```

指标（每 cascade 记录，对照开/关）：
- 有效命中率 = 被命中 brick / 几何活跃 brick（应上升）。
- 实际驻留 brick 数 / 池 cap（应下降）。
- bake GPU timer（`sw-lightbake`/`hw-lightbake`，`GiBake.cpp:131`）应下降。
- 截图 diff：墙角漏光、cascade 接缝、反弹色——必须无退化。
- popping/闪烁：动态镜头录制肉眼检查（路线 A 重点看延迟，路线 B 看 churn）。

> 高风险阶段（Phase 2/3 的正确性）建议用 subagent 跑一遍截图回归 + diff 复核。

---

## 10. 参考

- 本仓库：`docs/plans/sharc-integration-plan.md`、`docs/plans/ambient-cube-memory-reduction.md`、`docs/notes/idtech8-hybrid-gi-vs-ambientcube.md`。
- 源码：`assets/shaders/common/{AmbientCube,AmbientCubeBaker,Sharc,RadianceCache}.slang`、`assets/shaders/Core.Sharc{Update,Resolve,Query}.comp.slang`、`src/Engine/Assets/Acceleration/{BrickPageTable,ProbeBaker,CPUAccelerationStructure}.cpp`、`src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp`、`src/Engine/Assets/Core/Scene.cpp`。
- 外部：NVIDIA-RTX/SHARC（spatial hash radiance cache，insert/evict 模型）、Gautron 2020（spatial hashing）、Majercik 2019（DDGI，探针残留/可见度思想）。
