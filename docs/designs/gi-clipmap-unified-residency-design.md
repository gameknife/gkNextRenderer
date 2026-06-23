---
title: "CPU BVH → VoxelData → AmbientCube 链路重构 — 漫游/局部更新/统一稀疏驻留 总体设计"
category: design
status: 讨论中
owner: engine
created: 2026-06-23
last_updated: 2026-06-23
---

# CPU BVH → VoxelData → AmbientCube 链路重构（总体设计，供讨论）

> 范围：只做**总体架构设计**，对齐方向后再单独写开发计划。本文把现有三层链路系统梳理一遍，把用户提出的四个问题逐一映射到代码根因，给出一个**收敛的结构主张**，并明确列出待拍板的取舍与开放问题。
>
> 约定：所有源码引用为 `文件:行号`，便于接手 agent 定位。本文沿用 `ambient-cube-memory-reduction.md` / `ambientcube-hit-driven-residency-design.md` 的术语。

---

## 0. 结论（TL;DR）

四个问题（漫游闪烁、ping-pong 浪费、动画全量重建、voxel 未稀疏）**同根**：当前链路的数据是**「以世界原点为锚、按 cascade 稠密寻址、全局重建」**的。这条不变量在 `ambient-cube-memory-reduction.md` 里被显式当作前提固化（计划 F1：「网格世界固定，不随相机移动 → 活跃集每场景静态」）。它让「原点处 + 静态场景」这条 happy path 极其简单高效，但也正是它，把漫游、局部更新、统一稀疏全部堵死。

主张：**引入一个统一的「clipmap 砖块驻留中枢」（BrickResidency），让 voxeldata 与 ambientcube 共用同一套「跟随相机的、稀疏的、可局部失效的」brick 驻留集**。一个结构性改动，三处直接受益：

- **A｜clipmap/环形寻址** → cascade 体积跟随相机、只重烤进入的「壳层」→ 修漫游 + 抗闪烁（问题 1）。
- **B｜voxel 与 cube 同驻留** → 复用同一活跃 brick 集与 slot 分配器，voxel 顺势稀疏化（兑现 memory-reduction 暂缓的 Phase 4），且与 cube 严格对齐（问题 4）。
- **C｜BVH 脏区域驱动局部更新** → 复用**已存在但未接线**的脏 AABB（`ConsumeNavRelevantDirtyBounds`）把「全网格重体素化」降到「O(脏 brick)」（问题 3）。
- **D｜随之收缩的 ping-pong/scratch** → 工作集变小且已知，ping-pong 与 SDF scratch 可收缩/合并/就地化（问题 2）。

这套主张与既有两份文档**正交可叠加**：`ambientcube-hit-driven-residency-design.md`（命中驱动 insert/evict）是给 BrickResidency 再加一路「需求」输入；`idtech8-hybrid-gi-vs-ambientcube.md` 的世界辐射缓存是给 bake 着色降维，二者都不与本文冲突。

> **⚠ 方向已收敛（2026-06-23）**：经讨论，**去掉稀疏化、改 dense 固定体积 + propagation + memmove clipmap + 动态物体不 bake**，比上面的「稀疏 BrickResidency 中枢」更简单且能覆盖动态物体。**以 §0.5 为准**；本节 A/B/D 的稀疏部分作废，仅 A（clipmap）/C（脏区域局部更新）的思路延续。下文 §1–§2（现状/问题诊断）仍全部有效。

> 关键风险已知：clipmap 流式重烤把**体素化吞吐**推上前台 —— 当前体素化是 CPU 端 tinybvh 扫描（`ProbeBaker.cpp` + `CPUAccelerationStructure.cpp`），快速移动相机时 CPU 可能喂不饱。**「体素化是否从 CPU 迁到 GPU（复用已有 HW RT TLAS）」是本设计最大的一个分叉**，见 §5.3。

---

## 0.5 决策更新（2026-06-23 讨论后方向收敛）

> 与 owner 讨论后，方向显著简化。本节为**当前生效方向**，覆盖下文 §3.2 / §3.5 / §5.1 / §5.2 中与之冲突的部分（原文保留作推导依据）。

### 决策

1. **动态物体不进 bake**（physics body / character controller / 运动物件）。它们只**接收** GI、不**投射** GI（无 bounce 染色 / 接触遮蔽，接受此取舍）。
   - **副作用（大利好）**：把动态物体排除出体素化 BVH 后，其移动**根本不弄脏** baked 数据 → 问题 3 从源头消解。脏区域重建只剩**静态几何编辑**（编辑器 / 可破坏 / 积木放置）一类，复用已有 `mobility != Dynamic` 分类（`CPUAccelerationStructure.cpp:225`）。
   - 体素化 BVH 与 `RayCastInCPU`（拾取/导航，可能要动态物体）按需分流：bake 走静态子集，查询走全集。
2. **去掉稀疏化，改回 dense 固定尺寸**。相机附近一个 `192×192×48`（或定数）的体积**全部** bake 好 → 动态物体走到半空也能采到正确光照。
   - **连带简化**：删除 `FCPUBrickTable` / BrickTable / ActiveBrickList / slot 分配器 / 命中残留等全部稀疏管理结构；采样寻址退化为 `worldPos → 减动态origin → clamp → 线性索引`，DDA 与 8-tap 都简化。
   - → **§3.2 Pillar B（voxel/cube 统一稀疏驻留）与 §3.5 BrickResidency 中枢作废**；voxel 与 cube 都 dense，天然对齐，无需驻留中枢。
3. **propagation 光能传播复活**（dense 的直接红利）。git 证据：`6b12b46a3` 当初移除 propagation 的原因正是「sparse 下 shell-truncated propagation on unallocated bricks」—— dense 解除该限制。
   - 复活的 inject + propagate 两 pass（曾存在于 `Bake.InjectAmbientCube` / `Bake.PropagationAmbientCube`，`6ac111bc9` 引入）作为**低端档** bake；现有 traced bounce（96 ray/probe）作**高端档**。两者并存 = 延续「三档按硬件启用」哲学。
   - 注意：这是**主动回退** `6b12b46a3` 所属的「large-scene streaming GI plan / Part A」。
4. **clipmap 先用数据迁移（memmove），不做环形寻址**。snap 粒度起步 `64×64×16` voxel（相机始终居中 ±8m、四周 ≥16m 余量）。环形寻址留作后续零拷贝终极解。
   - → 取代 §3.1 的环形方案、§5.1 的取舍。

### 收敛后的架构（取代 §3 主体）

```
静态几何 ──(排除 Dynamic)──> 体素化(CPU 或 GPU, 见 §5.3 分叉)
                                  │ dense 写
   dense 固定体积 (N 个 clipmap cascade, 每个 192×192×48)
     ├ VoxelData  (dense, DDA + 方向 SDF 防漏光)
     └ AmbientCube(dense, bake: 低端 propagation / 高端 traced bounce)
                                  │ recenter: memmove 迁移 + 重烤新板(分帧/propagation 摊销)
                                  │ 静态编辑: 脏 AABB → 重烤受影响子盒(非全量)
   逐像素 8-tap 三线性 + 方向 SDF (寻址: 减动态 origin → clamp → 线性)
```

仍**保留**：Pillar A（clipmap 跟随，改 memmove 版）、Pillar C（脏区域局部重烤，作用于 dense 子盒而非 brick）、Pillar D（pong 仍用于 propagation 双缓冲，可与 SDF scratch 时分合并）。

### 显存（dense 的代价 —— 唯一需明确接受项）

每 cascade cube+voxel = `94.5 + 27 = 121.5 MiB`；pong + SDF scratch ≈ `148.5 MiB`（按 1 cascade，不随 N 变）。

| cascade 数 N | 估算合计 | 对照当前稀疏 ~324 MiB |
|---|---|---|
| 1 | ~270 MiB | 更省 |
| 2 | ~391 MiB | +67 |
| 3 | ~513 MiB | +189（吐回 Phase 3 稀疏收益）|

- **cascade 数 N 是新的显存主旋钮**（稀疏化已无）。clipmap 跟随相机后，覆盖玩家周围可能比原点静态铺满需要**更少 cascade**。
- 其余可叠加旋钮：per-cascade 分辨率（`192³(48)` 降档 / 远 cascade 降分辨率）、`VoxelData 16B→8B`、pong 与 SDF scratch 时分合并（省固定开销 148.5）。
- **已定（2026-06-23）**：**移动端 N=1，桌面 N=2**。低端 `HasFullAmbientCubeBudget`（`Engine.cpp`）阈值按 dense 重估（N=1 ~270 MiB / N=2 ~391 MiB）。

### 覆盖范围（默认参数：baseUnit 0.25m，ratio 2.0，`192×192×48`）

`unit_i = 0.25 × 2^i`；水平外延 = `192 × unit_i`，垂直 = `48 × unit_i`。clipmap 居中相机，可用半径 = 外延 / 2。最外层 cascade 决定总覆盖，内层只在近相机加分辨率。

| N | 最外 cascade | 体素尺寸 | 水平外延 | 垂直外延 | 相机可用半径 |
|---|---|---|---|---|---|
| **1（移动）** | c0 | 0.25 m | 48 m | 12 m | ±24 m 水平 / ±6 m 垂直 |
| **2（桌面）** | c1 | 0.50 m | **96 m** | 24 m | **±48 m 水平** / ±12 m 垂直 |
| 3（参考） | c2 | 1.0 m | 192 m | 48 m | ±96 m / ±24 m |

- **N=2 → 相机周围 96×96×24m 全有 baked GI（水平 ±48m）**。漫游时这是「跟着玩家走的 96m 窗口」，比原点静态铺满更适合大场景；超出 ±48m 回退 sky IBL。
- 覆盖不够时的旋钮：调大 `ratio`（如 3.0 → N=2 覆盖 144m）换取外层更粗；或加 baseUnit。垂直恒为水平的 1/4（`CUBE_SIZE_Z=48` vs `XY=192`）—— 超高空间（中庭）需注意。

### snap 粒度的反直觉点（待定）

memmove 总带宽 ≈ 与移动距离成正比、**与 snap 粒度无关**；粗 snap(64) 只是攒成"更稀但更大"的拷贝，每次 recenter 一次性重烤 1/3 体积 → 易周期性卡顿。两条改良（择一/叠加）：① 细 snap（per-brick 8 voxel）薄板重烤，更平滑、总成本不变；② 保持粗 snap，但拷贝走 GPU compute + 新板用 propagation 分帧摊销。

### 数据结构压缩可行性（讨论中，dense 后这是省显存的第二旋钮）

去稀疏后，**单元字节数 × cascade 体积**直接定显存，因此瘦身 VoxelData/AmbientCube 收益线性可观。已核实的现状语义：

**VoxelData 16B → 8B：可行。** 各字段实际语义与精度需求：
- `matId`（32 bit）：GPU 端**只做 `matId > 0` 的 solid 判定**（`AmbientCube.slang:826` 的 `inSolid`），无任何 shader 用其数值取材质（`Materials[voxel.matId]` 全代码为空）。→ solid + 少量 air 类别 **2–4 bit 足够**，省 ~28 bit。
- `age`：温度累积 ≤16 → 4–5 bit（`kTraceHistoryLength=16`）。
- `skyVisibility`（现寄存在 age 高字节）：6–8 bit。
- SDF 距离：**保 8 bit**（DDA 空跳 + 8-tap 防漏光的精度敏感项，不宜削）。
- 6 向可见度：现各 8 bit = 48 bit → 降到各 4–6 bit（24–36 bit）视觉大概率可接受（软遮蔽项）。
- 8B(64bit) 预算示例：solid/类 3 + age 4 + skyvis 6 + SDF 8 + inside 3 + 6×6 = 60 bit ≤ 64 ✓。
- 收益：N=2 省 `8B×1.77M×2 ≈ 27 MiB`。风险：方向可见度/SDF 精度 → A/B 截图验漏光。

**AmbientCube 56B：direct/indirect 分离是承重的，但 direct 存在结构性冗余。**
- 现状：6 间接 RGB10A2(24B) + 6 直接`_D` RGB10A2(24B) + skyvis(8B)。`_D` 被 `sunIntensity` 独立开关 —— `FullAmbientCubeSampler`(=1, 含烤入太阳) 与 `WNSunSampler`(=0, 太阳走实时 CSM 避免重复计) **两条路都在用**（`RayTracers.slang:352/375`）→ **不能简单合并 direct+indirect**。
- **冗余点（最大头）**：`_D` = 太阳直接 + emissive 直接。太阳是**全局单色方向光**（`UBO.SunColor`），逐探针逐面只需 1 个标量可见度 × 全局 SunColor，存 6×RGB 是浪费。**principled 压缩：把 emissive 并入 indirect bounce，`_D` 退化为 6 个太阳可见度标量**（6B）→ 56B→~38B，且 emissive 当 bounce 源更自洽。收益：N=2 省 `18B×3.54M ≈ 64 MiB`。风险：baker 改造 + emissive 不再随 sunIntensity 开关（但它现在也不该被太阳开关）。
- 易得小头：skyvis 8B→4B（6 向各 5 bit + sun），省 ~14 MiB @ N=2。
- 分档红利：**低端 propagation 档天然产出"合并辐照度"**，可用 32B 合并格式（6 面 + skyvis），高端 traced 档保 56B/38B 拆分格式 —— 格式随档位走，移动端 N=1 直接受益。
- 正交项：间接面 `RGB10A2 → RGB9E5`（同 32bit，HDR 范围/精度更好，idtech8 §A），不变尺寸但提质。

> 这些都是**质量/精度权衡项**，落地需 A/B 截图回归（防漏光、cascade 接缝、emissive 反弹色）。建议作为 dense 跑通后的独立调优阶段，而非首版必做。

### VoxelData 升级为 GI-lite 载体（sun-vis + emissive，讨论中）

**洞察**：`VoxelData` 已携带单标量 sky-vis（`age` 高字节，`AmbientCube.slang:85`），且 `SwModernNoAmbient` 已通过 `SampleVoxelSkyVisibility` 消费它（`Core.SwModernNoAmbient.comp.slang:169`，带 SDF 防漏光的三线性）。把**太阳可见度**与 **emissive 受光**一并写进 voxel，让无 ambient-cube 的廉价路径也能拿到大尺度太阳遮蔽 + 一次 emissive 反弹。

- **现状**：`SwModernNoAmbient` = IBL×skyvis + 实时 CSM 太阳 + 原始 emissive，**无 GI 反弹**。
- **字节**：现 16B 中 `matId`(32bit→solid 只需 ~4) + `age` 计数器(24bit→只需 ~5) 共 **~47 bit 死空间**。新增 sun-vis(8) + emissive RGB565(16) 仅 24 bit，**塞进现有 16B 仍有余**（甚至可保 6 向可见度满 8-bit）。→ 这是「把瘦身省下的位拿来**增益**而非纯缩」的更优用法。

**三档阶梯（与"三层可单独启用"哲学吻合）**：

| 档 | 消费 | 得到 | 成本 |
|---|---|---|---|
| 0 最廉价 | IBL + CSM | 直接光 + 环境光 | — |
| **1 lite（新）** | + VoxelData{sky-vis, **sun-vis**, **emissive**} | + 大尺度太阳软遮蔽（补 CSM 的近距/视锥外）+ **平坦一次 emissive 反弹** | voxel tap，dense 全覆盖 |
| 2 full | + AmbientCube{6 面 direct+indirect} | 方向性多次反弹 GI | 56/38B + 烘焙 |

- **sun-vis**：单标量足够（太阳是单方向，NdotL 着色时算）。复用 `SampleVoxelSkyVisibility` 路径**几乎零新代码**，只换采样的字节。给 `SwModernNoAmbient` 补上 CSM 覆盖不到的大尺度/视锥外/视角无关的太阳遮蔽。**这不是"反弹"，是直接光软遮蔽**。
- **emissive 受光**：存"该 voxel 收到的 emissive 辐照"RGB565 → lite 路径得到一次 emissive/局部光反弹。**可搭体素化已发的射线顺手收集**（命中 emissive 面即累加），近乎免费的粗版。
- **大协同（统一太阳表示）**：sun-vis 放在 voxel 后可**被 cube 路径与 lite 路径共享** → 上一节 cube 的 `_D` 不必再各面存 6×RGB 太阳，太阳改由"voxel sun-vis × 全局 SunColor"在采样时算，`_D` 退化为纯 emissive 或直接去掉。**一份 sun-vis，三档共用**。

**取舍/风险**：
- emissive 单 voxel 是**非方向、平坦、voxel 粗粒度、仅 diffuse** —— 正是 lite 档定位，比 cube 6 面糙；防漏光仍靠现有 SDF 拒绝。
- 语义上 `VoxelData` 从"纯几何/可见度代理"变成"也带 radiance（emissive）"——一次有意的定位转变。
- 需要一次 emissive→voxel 的 gather（可搭体素化射线 / 复用 cube bake 的 `TraceOcclusion` 太阳遮蔽），不是完全免费，但很便宜。

---

## 1. 现状梳理：三层链路职责与数据流

### 1.1 链路总览

```
                   场景 mesh / node tree
                          │
              ┌───────────▼────────────┐   InitBVH / UpdateBVH (TLAS 重建; BLAS 缓存)
   ① cpubvh   │ FCPUAccelerationStructure│   CPUAccelerationStructure.cpp:121,183
              │   tinybvh TLAS/BLAS      │   CpuBvh.cpp (TraceRay 入口)
              └───────────┬────────────┘
                          │ 每体素发 6 轴 + 8 对角线 ray 求交（CPU）
              ┌───────────▼────────────┐   FCPUProbeBaker::ProcessCube/VoxelizeCube
   ② voxeldata│ voxels[]  16B/voxel      │   ProbeBaker.cpp:173,278
              │ 192×192×48 稠密/ cascade │   matId(固/空) + age(skyvis) + 方向 SDF
              └───────────┬────────────┘   GPU jump-flood 精修 SDF + skyvis bake
                          │ CPU 上传 → GPU
              ┌───────────▼────────────┐   BakeAmbientCubeCascade (GPU compute)
   ③ ambient  │ AmbientCube 56B          │   VulkanBaseRenderer.GiBake.cpp:97
      cube     │ 稀疏 brick 池(8³)        │   HL2 6 面 RGB10A2; 时间切片烘焙; ping-pong
              └───────────┬────────────┘
                          │ 逐像素 8-tap 三线性 + 方向 SDF 防漏光
                   着色消费 interpolateAmbientCubes
                   (SoftwareModern / SoftwareTracing / VoxelTracing …)
```

三层可按硬件档位单独/全部启用 —— 这是现状的优点，需在重构中**保留**。

### 1.2 ① cpubvh

- `FCPUAccelerationStructure`（`CPUAccelerationStructure.h:113`）持 tinybvh：每个 model 一个 BLAS（`InitBVH`，`.cpp:121`，>16384 tri 的 BLAS 落盘缓存），全场景一个 TLAS（`UpdateBVH`，`.cpp:183`）。
- 它是**体素化的「真值 tracer」**：`TraceRay`（`CpuBvh.cpp:70`）被 `VoxelizeCube` 调用做近表面/方向距离判定。
- **重建粒度**：`UpdateBVH` 每次**全量重建 TLAS**（遍历所有 render node 重新塞 instance，`.cpp:200-279`）；BLAS 持久缓存不重建。`UpdateBVH` 里**已经**顺带算了「静态几何变更的世界脏 AABB」（`navRelevant` 基于 `PhysicsComponent` mobility，`.cpp:225-274`），通过 `ConsumeNavRelevantDirtyBounds`（`.cpp:611`）暴露 —— 目前只喂给导航，**没喂给 GI**。

### 1.3 ② voxeldata（16B）

```c
struct VoxelData {                 // BasicTypes.slang:266
    uint matId;                    // 0 = 空气（默认即空）
    uint age;                      // 时域累积帧数(上限16) + skyVisibility 高字节
    uint distanceToSolid_gg_z01;   // 打包: chamfer SDF 距离 / inside / ±Z 方向可见度
    uint distanceToSolid_x01_y01;  // 打包: ±X ±Y 方向可见度
};
```

- 每 cascade `192×192×48 = 1,769,472` 个，**稠密** `std::vector`（`FCPUProbeBaker.voxels`，`Init` resize，`ProbeBaker.cpp:232`）。
- CPU 体素化写 matId + 方向 SDF（`VoxelizeCube`，`ProbeBaker.cpp:173`）；CPU chamfer（`RebuildDistanceField`，`.cpp:249`）或 GPU jump-flood（`RebuildDistanceFieldCascades`，`GiBake.cpp:283`）精修距离场；GPU `BakeVoxelSkyVisibility`（`GiBake.cpp:214`）写 skyvis 进 `age` 高字节。
- 用途：着色期 DDA 空跳（`inSolid`）、8-tap 防漏光的方向几何拒绝、大尺度天光遮蔽。
- **稠密标记**：`Voxels` 注释明写「kept dense; Phase 4 may sparsify」（`BasicTypes.slang:293`）。

### 1.4 ③ ambientcube（56B）

- `AmbientCube`（`BasicTypes.slang:237`）：HL2 6 面间接 + 6 面直接 + skyvis，`RGB10A2`。
- **已稀疏**：`192×192×48` 切 `8³` brick（每 cascade `24×6×24 = 3456` brick），`FCPUBrickTable`（`BrickPageTable.cpp:25`）把近表面（matId≠0）+ 膨胀半径 3 的 brick 压进池 slot，其余标 `INVALID`。
- GPU 烘焙 `BakeAmbientCubeCascade`（`GiBake.cpp:97`）：每帧只烤 1 个 cascade（`frameCount % cascadeCount`），按 `temporalFrames`(30/120/300) 再时间切片；遍历活跃 brick list；每面 16 ray 当场 TraceRay + 当场着色，indirect bounce 取上一帧 cube。
- **ping-pong**：bake 前把当前 cascade 的整个 cube 池 `vkCmdCopyBuffer` 到 `CubesPong`（`GiBake.cpp:142-193`），bake 读 pong（稳定快照、供邻居/多 bounce）写 Cubes，规避同 dispatch 内的 read-after-write。

### 1.5 内存布局（单 arena + AmbientResources 二级表）

运行时布局由 `ComputeAmbientArenaLayout`（`Scene.cpp:65`）确定，地址经 `AmbientResources`（`BasicTypes.slang:290`，8×u64）转发，`GPUScene` 维持 128B。各区性质：

| 区域 | 单元 | 规模 | 性质 | 备注 |
|---|---|---|---|---|
| **Cubes** | 56B | poolBricks×512 × cascadeCap | 常驻 | 已稀疏（pool ratio 默认 0.66）|
| **Voxels** | 16B | 1.77M × cascadeCap | 常驻 | **仍稠密**（Phase 4 暂缓）|
| Pages | 16B | 64×64 | 常驻 | 世界对齐 1024m 粗页（DDA 加速）|
| **CubesPong** | 56B | poolBricks×512 ×**1** | 常驻 | **烘焙瞬态却永久占用**（问题 2，`Scene.cpp:75`）|
| SdfScratch | 16B | 1.77M ×1 | 常驻 | jump-flood SeedB |
| SdfSeedA | 16B | 1.77M ×1 | 常驻 | jump-flood SeedA |
| BrickTable + ActiveList | 4B | 3456×cap + pool×cap | 常驻 | brick→slot 表 + 紧凑活跃列表 |

> memory-reduction 已把总量从 ~608 MiB 压到 ~324 MiB。但 **Voxels 稠密 + 三块 dense scratch（Pong/SeedA/SeedB ≈ 一个半 cascade）** 仍是大头，且全部**按最坏情况静态分配**，不随真实工作集缩放。

### 1.6 关键不变量 F1（四问题的共同根）

`CalculateAmbientCubeOffset`（`UniformBuffer.hpp:28`）= `vec3(-96, -1.375, -96) × unit + 静态bias`，bias 来自 `UserSettings.AmbientCubeOffset*`（`Engine.CameraUbo.cpp:260`、`Engine.cpp:305`）。**全代码无任何 recenter/clipmap/toroidal/相机跟随**（已 grep 确认）。即：**cascade 体积永远钉死在世界原点附近**，活跃集每场景算一次（`AsyncProcessFull`），漫游与局部更新在架构上被排除。

---

## 2. 问题诊断（用户四点 → 代码根因）

| # | 用户描述 | 根因（file:line） | 现有可复用脚手架 |
|---|---|---|---|
| **P1** | 漫游时跟随差、闪烁 | F1：体积钉死原点（`UniformBuffer.hpp:28`）；无 recenter。越出 cascade 范围即无 GI。bake 时间切片(30/120/300)使任何变化收敛慢 → 闪烁 | UBO 已有 `AmbientCubeOffset` 通道（`CameraUbo.cpp:273`），只是喂的是静态值；cascade 已是多级体积 |
| **P2** | ping-pong 显存浪费 | `CubesPong` 是「一整个 cascade 池」的永久常驻区（`Scene.cpp:75`），仅 bake 瞬态用；外加 SeedA/SeedB 两块 dense scratch | 每帧只烤 1 个 cascade → pong 本就只需 1 个；scratch 各 pass 不同时活跃，有合并空间 |
| **P3** | 动画致 cpubvh 重建 → voxel+cube 全量重建 | `MarkDirty()`（`Scene.cpp:878`，大量 gameplay/editor 调用）→ `sceneDirtyForCpuAS_` → 每 30 帧 `AsyncProcessFull(incremental)`（`Scene.Update.cpp:228`）→ 全 TLAS 重建 + **整个 192×192 网格 × 全 cascade 重体素化**（`CPUAccelerationStructure.cpp:385-407`，shuffle 全扫）| **`RequestUpdate(worldPos,radius)` 局部路径已存在但未接线**（`.cpp:590`）；`UpdateBVH` 已算静态脏 AABB（`.cpp:611`）；mobility 已分静/动 |
| **P4** | voxel 未稀疏；应与 cube + 局部更新 + clipmap 联立 | `Voxels` 注释「kept dense; Phase 4 may sparsify」（`BasicTypes.slang:293`）；cube 走 pool slot、voxel 走 dense 索引，二者**索引体系分离**（memory-reduction 偏差 #5）| brick 表/活跃列表机制已成熟（`BrickPageTable.cpp`），可被 voxel 直接复用 |

**核心判断**：P1 是结构性的「锚点错了」，P3/P4 是「重建粒度太粗 + 两套索引」，P2 是「瞬态当常驻」。**只要把「锚点」从世界原点换成「跟随相机的 clipmap」，并让 voxel/cube 共用一套稀疏驻留 + 局部失效**，四点会一起松动。

---

## 3. 设计主张：统一的「clipmap 砖块驻留中枢」

### 3.0 核心洞察

把现有「每场景静态、稠密、原点锚定」的活跃集，升级为一个**带生命周期的 brick 驻留集**，由三类输入共同决定：

```
驻留集 = 几何候选(近表面)  ∩  clipmap 范围(相机)  ⊕  脏区域(动画/编辑)
            ↑ 现有 UpdateData       ↑ 新增 A           ↑ 新增 C（复用已有脏 AABB）
         （可选再叠加：命中需求，见 §4 hit-driven）
voxel 池 与 cube 池 共用同一驻留集与 slot 分配器（新增 B）
```

下面四个 Pillar 各自独立有意义，但**共享同一个 BrickResidency 数据结构**（§3.5），这正是「联立」的落点。

### 3.1 Pillar A —— clipmap / 环形寻址 cascade（修 P1）

**目标**：cascade 体积跟随相机移动，且移动时**只重烤新进入的壳层**，不动内部 —— 既跟随又抗闪烁。

- **锚点动态化**：每 cascade 的 origin 从静态 `CUBE_OFFSET` 改为「相机位置**按 brick 粒度 snap**」。snap 到 brick（8 voxel × unit）保证寻址整数稳定、只整 brick 进出。设阈值滞后（hysteresis）：相机跨过 N 个 brick 才 recenter，避免抖动。
- **环形（toroidal）寻址**：体积用 modulo 寻址 wrap，recenter 时**不搬数据**，只把「相机移动暴露出来的壳层 brick」标记为 dirty 待重烤；离开的 brick 自然被新内容覆盖。这是 clipmap/DDGI scroll 的标准做法。
- **GPU 采样适配**：`interpolateAmbientCubes`（`AmbientCube.slang`）的 `worldPos → cell` 映射改为「减动态 origin → 环形 mod」。UBO 已有 `AmbientCubeOffset` 通道，扩成**每 cascade 一个动态 origin**即可，不破 128B。
- **抗闪烁的来源**：内部 brick 不重烤 → 时域累积不被全局清零；只有壳层是新数据，且壳层有 grace 收敛窗口。

**必须处理的正确性问题**：
- **环形接缝**：8-tap + 方向 SDF 会读邻居 voxel，跨 wrap 接缝的邻居会环绕到体积另一侧（错误几何）。需「接缝处禁止跨界 tap」或留 1-brick guard 带。
- **cascade 间一致性**：不同 cascade 用不同 snap 粒度，重叠区过渡要平滑（现有 cascade 回退逻辑可复用）。

### 3.2 Pillar B —— voxel 与 cube 统一稀疏驻留（修 P4）

**观察**：cube GI 只在「近表面」有意义，voxel 也只在近表面才携带信息 —— 二者的空间支撑**高度重合**。现状却是 cube 稀疏、voxel 稠密、且两套索引。

**主张**：voxel 与 cube **共用同一个 brick 驻留集与 slot 分配器**。一个 brick 激活，就同时在 voxel 池与 cube 池占一个对齐的 slot；`brickTable[brick] → slot` 同时索引两者。

- 直接兑现 memory-reduction 暂缓的 Phase 4（voxel 稀疏），voxel 显存按活跃占比下降（估算 ~81 MiB → ~10–25 MiB）。
- 索引统一：消除 memory-reduction 偏差 #5 的「cubeIdx/voxelIdx 分离传递」。
- **DDA 热路径**（`inSolid` / `RayTracers.slang`）改走 brick 间接：空 brick 返回统一「空、跳 N 单位」默认 —— 既省显存又**兼当 DDA 加速结构**（这正是 memory-reduction Phase 4 暂缓时担心的点，需 profiling 验证间接代价）。
- 与 A 叠加：驻留集 = 近表面 ∩ clipmap 范围 —— 远 cascade、相机背后的空旷区自然不占。

### 3.3 Pillar C —— BVH 脏区域驱动的局部增量更新（修 P3）

**主张**：把「scene dirty → 全量重体素化」改成「scene dirty → 仅失效受影响的 brick」。**所需数据已经存在，只差接线**：

1. `UpdateBVH` 已算出「静态几何变更的世界脏 AABB」（`CPUAccelerationStructure.cpp:611`，目前喂导航）。把它**同时**喂给 GI。
2. 脏 AABB → 覆盖的 brick 集合 → 标记这些 brick 的 voxel/cube 为 dirty，入 `RequestUpdate` 已有的局部队列（`.cpp:590`，把现成但未用的路径接上）。
3. `Tick` 的 flush 只重烤 dirty brick，不再 `AsyncProcessFull` 全扫。
4. TLAS 仍全量重建（BLAS 缓存、TLAS 重建相对便宜）；重活（体素化 + bake）变成 O(脏 brick)。

**动态物体（骨骼/刚体动画）的取舍**（见 §5.2）：当前体素化对全 TLAS（含动态物体）求交，所以动起来就脏全图。需决定动态物体是否进 baked voxel 场 —— 复用已有 `mobility != Dynamic` 分类。

### 3.4 Pillar D —— ping-pong / scratch 收缩与合并（修 P2）

随 A/B 落地，工作集变小且**每帧已知**，scratch 有三条可叠加的省法（按风险排序，供讨论）：

1. **就地读（去 pong）**：bake 直接读 Cubes（接受邻居 1 帧 staleness）。GI bounce 低频 + 现有 age 时域累积本就容忍滞后 → 大概率可接受。**省掉整块 CubesPong**。风险：同 dispatch 内 read-after-write 的视觉影响需 A/B 截图验证。
2. **scratch 合并/别名**：`CubesPong` / `SdfSeedA` / `SdfScratch` 都是 bake 瞬态，且 SDF jump-flood 与 cube bake 对同一 cascade 不在同一子 pass 同时活跃 → 可时分复用**一块共享 scratch**，三块永久区收敛成一块。
3. **随驻留集缩放**：clipmap + 统一稀疏后，scratch 只需覆盖「当前帧工作 brick 集」而非整 cascade 稠密 → 自然变小。

> 注意 §1.4 的事实：pong 之所以是「整 cascade」，是因为时间切片的 brick 在 pool-slot 序里连续、但其**空间邻居**散布全 cascade，要读邻居就得整份快照。所以「缩小 pong 到切片」会破坏邻居读 —— 真正干净的路是方案 1（就地读）或方案 2（合并）。

### 3.5 把四者粘合的中枢：`BrickResidency`

四个 Pillar 不是四个独立特性，而是**一个数据结构的四个输入/视角**。建议显式抽出一个 per-cascade 的驻留管理器（CPU 侧重构 `FCPUBrickTable` + 新 per-brick 状态；GPU 侧扩 `AmbientResources` 挂一个 residency buffer，不破 128B）：

```
BrickResidency (per cascade)
  ├─ 输入1 几何候选   : 近表面分类（现 UpdateData，BrickPageTable.cpp:25）
  ├─ 输入2 clipmap    : 动态 origin + 环形范围（Pillar A）→ 决定哪些 brick 在界内
  ├─ 输入3 脏区域     : BVH 脏 AABB（Pillar C）→ 哪些 brick 需重烤
  ├─ (可选)输入4 命中 : hit-driven 残留（见 §4）
  ├─ slot 分配器      : voxel 池 + cube 池共用（Pillar B），稳定复用历史 slot 防 churn
  └─ 输出            : brickTable / activeBrickList（现有上传管线，CPUAccelerationStructure.cpp:503）
```

- 「brick 进/出驻留」是唯一的真相来源；clipmap 移动、动画脏、相机命中都只是往里加/减 brick。
- voxel 与 cube 跟着同一进/出决策走 → 天然联立。
- ping-pong/scratch 只需覆盖「本帧 dirty/壳层 brick」→ 自然收缩。

---

## 4. 与既有方向的关系（正交/叠加，不冲突）

| 既有文档 | 它解决什么 | 与本设计的关系 |
|---|---|---|
| `ambient-cube-memory-reduction.md`（已完成） | 稀疏 cube + 间接表 + 右尺寸 | **本设计的地基**。AmbientResources 间接表、活跃 brick list、pool 机制全部复用；本设计是其「Phase 4（稀疏 voxel）+ 漫游」的超集化重写 |
| `ambientcube-hit-driven-residency-design.md`（待实现） | brick 的命中驱动 insert/evict | **第 4 路输入**叠加到 §3.5 的 BrickResidency。clipmap 决定「界内候选」，命中决定「界内谁真正驻留」—— 完全互补 |
| `idtech8-hybrid-gi-vs-ambientcube.md`（分析） | 可见度/着色解耦 + 世界辐射缓存 | **bake 着色降维**，与本文的「数据驻留/寻址」正交。可在本设计稳定后叠加，给壳层重烤进一步提速 |

**优先级建议**：本设计（驻留/寻址）应在 hit-driven 之前或同步推进 —— 因为 clipmap 一旦引入，hit-driven 的「界内候选集」定义会变；先把锚点/驻留中枢立起来，hit-driven 作为其一路输入更顺。

---

## 5. 关键取舍（供讨论，本文不下结论）

### 5.1 寻址：环形 vs 数据搬移；snap 粒度
环形寻址省搬运但接缝处理复杂（§3.1）；数据搬移（每次 recenter memmove）实现直观但带宽高、且仍需处理被搬空的壳层。snap 粒度选 brick(8 voxel) 还是 cascade-unit？粒度大省 recenter 频次但相机贴边时覆盖浪费大。

### 5.2 动态物体进不进 baked 场
- **方案甲（排除）**：动态物体（`mobility==Dynamic`）不写入 baked voxel/cube，只作为 GI 接收者；动态遮蔽/反弹另想办法（如屏幕空间或独立小 overlay）。优点：动画不再脏 baked 场，P3 根治；缺点：动态物体不投 GI 反弹/遮蔽。
- **方案乙（逐 brick 刷新）**：动态物体占的 brick 每帧/按预算重烤。优点：动态 GI 正确；缺点：吞吐压力回到体素化（接 §5.3）。
- 现状是「乙的最坏版」（全图重烤）。多数实时引擎选甲或「甲 + 关键动态物体的廉价代理」。

### 5.3 ★体素化留 CPU 还是迁 GPU（本设计最大分叉）

> **已定（2026-06-23）：本轮留 CPU，不做 GPU 体素化。** 动态物体排除后体素化负载已从「每次漫游」降到「clipmap 换板 + 静态编辑」，CPU 路径足以跑通；GPU 化留作后续优化。下文分析保留作依据。
clipmap 流式重烤把体素化吞吐推上前台。现状体素化是 **CPU tinybvh 扫描**（`ProbeBaker.cpp` 单体素 6+8 ray，`TaskCoordinator` 多线程切组）。两条路：
- **留 CPU**：改动小、跨平台稳；但快速漫游时壳层 brick 洪峰可能喂不饱，需严格预算 + 优先级队列 + 容忍多帧补烤。
- **迁 GPU**：引擎**已有 HW RT TLAS**（`GPUScene.GetTLAS()`）与 HW bake 路径（`directLightGenPipeline`，`GiBake.cpp:97`）。壳层体素化直接在 GPU compute 对 RT TLAS 求交，吞吐高一个量级，天然契合 clipmap streaming。代价：与「软件 tracing 档/无 RT 硬件」的兼容性要分档（保留 CPU 路径作 fallback）。
- 折中：**clipmap 壳层走 GPU、CPU BVH 退化为「软件档 fallback + 非渲染查询（RayCastInCPU/导航）」**。这关系到三层「可单独启用」的现状能否维持，需重点讨论。

### 5.4 voxel/cube 同驻留 vs 各自驻留
同驻留（§3.2）最简洁、最省，但强绑定二者生命周期。若未来想让 voxel（DDA 用）比 cube（GI 用）覆盖更大范围（DDA 需要更远的空跳信息），可能需要「voxel 驻留 ⊇ cube 驻留」的包含关系而非完全相等。需确认 DDA 对 voxel 范围的真实需求。

### 5.5 cube 颜色精度（顺带项）
若动到 bake/采样路径，`idtech8` 笔记建议的 `RGB10A2 → RGB9E5`（§A）可低成本顺带做，改善 HDR bounce 范围。与本设计正交，仅在「反正要改这些 shader」时合并以省一次回归。

---

## 6. 开放问题（明确留给讨论）

1. **clipmap 范围 vs Pages 粗页**：现有 `PageIndex` 是世界对齐 1024m 粗页（DDA 加速，`UniformBuffer.hpp:13`）。它与跟随相机的 fine clipmap 是两套坐标 —— 是并存（page 管粗剔除、clipmap 管 fine 驻留）还是统一？
2. **recenter 与 bake 时间切片的相位**：bake 已按 `frameCount % cascadeCount` + temporalFrames 切片。clipmap recenter 事件如何与切片相位协调，避免「刚 recenter 又被切片推迟收敛」导致的可见 pop？
3. **§5.3 的分叉**：体素化迁 GPU 是否纳入本轮？还是先 CPU 预算化跑通 clipmap，再单独评估 GPU 化？这决定改动面与跨平台策略。
4. **动态物体**（§5.2）选甲/乙/折中？影响 P3 的彻底程度与 gameplay 观感。
5. **驻留 churn 稳定性**：clipmap 移动 + 命中驱动叠加后，slot 抖动如何抑制（历史 slot 复用、滞后驱逐、清零防串味）？
6. **三层可单独启用的现状**：clipmap/统一驻留后，「只开 cpubvh」「只开 voxel」「全开」三档的边界如何重新定义？
7. **跨平台显存档**：`HasFullAmbientCubeBudget`（`Engine.cpp`）的降级逻辑在「工作集动态化」后如何重估预算（不再是静态最坏值）？

---

## 7. 非目标 / 暂不做

- 不替换 HL2 ambient cube 辐照度表示（6-lobe 保留）；不引入八面体/DDGI（idtech8 §F，另案）。
- 不引入硬件块压缩（BC6H/ASTC）—— memory-reduction §0 已否决。
- 不动 `GPUScene` 128B push constant 约束；新增地址一律走 `AmbientResources` 二级表。
- 首版不做 toroidal 环形寻址（P6 暂缓）、不做 GPU 体素化（§5.3 已定留 CPU）、不做数据压缩进首版（归 P5 调优）。

---

## 8. 开发计划（2026-06-23 拍板后）

> 排序原则：**先收割低风险确定收益（P0）→ 结构地基（P1 dense → P2 clipmap）→ 独立增值（P3 lite / P4 propagation）→ 调优（P5）**。每个 Phase 自成可构建、可截图验证的单元。
>
> 验证通用规约（AGENTS.md）：engine 改动 `./gnb build gkNextRenderer gkNextUnitTests`（新增 shader 文件首次加 `--reconfigure`）；ambient 改动**必须** `gnb shot --scene <X> --frames 3000` 让体素化 + flush + bake 收敛（90 帧只有 sky IBL，看不出 GI 对错 —— memory-reduction「验证坑」#6）；至少覆盖**开阔 + 封闭**两类场景；高风险 Phase（P1/P2/P4）用 subagent 跑截图回归 + diff 复核。

### Phase 总览与依赖

| Phase | 内容 | 修复/价值 | 依赖 | 风险 |
|---|---|---|---|---|
| **P0** | 静态/动态 BVH 分离 + 脏区域局部更新接线 | P3（动画不再全量重建）| — | 低 |
| **P1** | 去稀疏 → dense 固定体积 | P4 前置 + 大幅简化 | P0 | 中 |
| **P2** | clipmap 跟随 + memmove recenter | P1（漫游 + 抗闪烁）| P1 | 中高 |
| **P3** | VoxelData GI-lite（sun-vis + emissive）+ SwModernNoAmbient 消费 | 中间档增值 | P1 | 中 |
| **P4** | propagation 低端 bake 档复活 | 低端提速 | P1 | 中 |
| **P5** | 数据压缩 + 统一太阳表示 + scratch 收缩 | P2 显存（吃回 dense 代价）| P3 | 低中 |
| P6（暂缓）| toroidal 环形寻址（零拷贝 recenter）| memmove 带宽 | P2 | 中高 |

### P0 — 静态/动态 BVH 分离 + 脏区域局部更新接线（低风险，先收割 P3）

> 可在**当前稀疏 + 原点**架构上独立完成、独立收益，不依赖后续结构改动。

- 体素化 BVH 只收静态几何：`UpdateBVH`（`CPUAccelerationStructure.cpp:200`）按 `PhysicsComponent` `mobility==Dynamic` 过滤出 bake 子集；`RayCastInCPU`（`.cpp:312`）/ 导航仍用全集（拆两份 instance list，或 trace 时按 flag 过滤）。
- 脏区域接线：`Scene::MarkDirty`（`Scene.cpp:878`）不再无脑触发全量；把 `ConsumeNavRelevantDirtyBounds`（`.cpp:611`，**已存在**的静态脏 AABB）→ 覆盖体素范围 → 走 `RequestUpdate`（`.cpp:590`，**已存在**的局部入队）。
- 驱动：`Scene.Update.cpp:226-238` dirty 时只跑局部增量，删除 `AsyncProcessFull(incremental)` 的全网格 shuffle 重扫（`CPUAccelerationStructure.cpp:385-407`）。
- **验收**：动态物体运动时 bake timer 不出现全量峰值；移动静态物体只重烤其 AABB 邻域；开阔/封闭截图 GI 与改前一致（动态物体此后不投 GI = 预期）。

### P1 — 去稀疏 → dense 固定体积（结构地基，中风险）

- 删除稀疏管理：`FCPUBrickTable`（`BrickPageTable.cpp` 整文件）、brick 表 / 活跃列表上传（`CPUAccelerationStructure.cpp:503-507,540-544`）、`Scene` 的 pool/activeBrick 接口（`Scene.cpp:35-42,426`、`AmbientPoolBricksPerCascade` / `SetAmbientActiveBrickCounts`）。
- arena 布局 `ComputeAmbientArenaLayout`（`Scene.cpp:65`）：Cubes 回 dense（`per-cascade × cascadeCap`），删 BrickTable / ActiveList 区；`AmbientResources`（`BasicTypes.slang:290`）去掉 `BrickTable` / `PoolParams`。
- 采样去间接：`AmbientCube.slang` 的 `FetchCube` / `FetchVoxel` / brick 解码（`:339-410` 一带）→ `worldPos → 线性索引`；`interpolateAmbientCubes(Stable)` 去掉 brick 查表层。
- bake/clear dispatch：`GiBake.cpp:97-212`（bake）去 active-brick-list 改 dense 区遍历（保留 temporalFrames 切片）；`ClearAmbientCubeCache`（`:49`）dense 清。
- cvar：删 `AmbientCubePoolBrickRatio`（`EngineCVars.cpp` / `UserSettings.hpp`）。保留 memory-reduction 的 AmbientResources 间接（Phase 1）+ 右尺寸 cascade 数（Phase 2）。
- **验收**：dense 与稀疏版近表面 GI 截图 parity（仅空区也分配）；显存符 §0.5 表（N=2 ~391 MiB）；单测过。

### P2 — clipmap 跟随 + memmove recenter（结构核心，中高风险）

- 动态 origin：每 cascade origin = 相机位置 snap 到 `64×64×16` voxel + hysteresis（跨界才 recenter）。
- UBO：`AmbientCubeOffset` 扩成 per-cascade（`Engine.CameraUbo.cpp:260-273`、`UniformBuffer.hpp` `AmbientCubeCascadeParams` 一带），维持 128B。
- 采样寻址：`worldPos − 动态origin → clamp → 线性`（`AmbientCube.slang`）；越界回退更粗 cascade（dense 无 wrap，边界 tap 钳制）。
- recenter：GPU compute 拷贝重叠区（新 `Bake.AmbientClipmapShift.comp.slang` 或扩 clear），暴露 slab 标 dirty → 入 P0 局部重烤队列，分帧 / temporal 收敛。
- 驱动：`Scene.Update.cpp` 每 N 帧检测 snap、触发 shift + slab 重烤；cvar：snap 粒度 / hysteresis / recenter 预算。
- **验收**：相机长距漫游（>100m）GI 持续跟随、无大面积丢失；recenter 无明显 pop（录屏 + diff）；N=1 移动 / N=2 桌面覆盖符 §0.5 覆盖表。

### P3 — VoxelData GI-lite 载体 + SwModernNoAmbient 消费（中风险，独立增值）

- 结构：`VoxelData`（`BasicTypes.slang:266`）`matId`→solid/类(~4bit)、`age` 计数器收窄，腾位放 `sunVis`(8) + `emissive` RGB565(16)，**保持 16B**；加访问器（`AmbientCube.slang:84` 一带，仿 `Get/SetVoxelSkyVis`）。
- bake：sun-vis 复用 cube bake 的 `TraceOcclusion`（`AmbientCubeBaker.slang`）或扩 `Bake.VoxelSkyVisibility.comp.slang` 同写；emissive gather 搭体素化射线（`ProbeBaker.cpp` `VoxelizeCube`，命中 emissive 面累加）。
- 消费：`Core.SwModernNoAmbient.comp.slang:151-184` 扩 `SampleVoxelSkyVisibility` → 加 sun-vis（×SunColor×NdotL 大尺度软遮蔽）+ emissive（平坦一次反弹）。
- **验收**：SwModernNoAmbient 下 CSM 范围外大物体投出软太阳遮蔽；emissive 物件周围柔和染色一次反弹；薄墙防漏光无退化。

### P4 — propagation 低端 bake 档复活（中风险，依赖 dense）

- 恢复 `Bake.InjectAmbientCube.comp.slang` + `Bake.PropagationAmbientCube.comp.slang`（参考被删 commit `6ac111bc9`，适配 dense + clipmap）；inject 源可用 P3 的 voxel emissive + sun。
- `AmbientCubeBaker.slang`：恢复 `6b12b46a3` 删去的 propagation / inject / gather helper。
- `VulkanBaseRenderer`（`.cpp/.hpp/GiBake.cpp`）：恢复 `BakeAmbientCubePropagation` + pipelines + 档位分支；`UserSettings` / `EngineCVars` 加 bake 档开关（low=propagation / high=traced）。
- **验收**：低端档 bake GPU timer 显著低于 traced；收敛后与 traced 无重大质量差（接受方向性略糙）；dense 下不再有 `6b12b46a3` 所述「shell-truncated propagation」。

### P5 — 数据压缩 + 统一太阳 + scratch 收缩（调优，低中风险，吃回 dense 代价）

- AmbientCube `_D`：太阳改由 voxel sun-vis × `SunColor` 采样时算（`AmbientCube.slang:242-287` `_Full`），`_D` 退化为纯 emissive 或删除 → 56→~38B（`BasicTypes.slang:237`）。
- skyvis 8B→4B；间接面 `RGB10A2 → RGB9E5`（`ConstFunc.slang` 加 pack/unpack）。
- ping-pong 就地读评估（去 `CubesPong`）/ 或 `CubesPong`+`SdfSeedA`+`SdfScratch` 时分合并一块（`Scene.cpp` arena + `GiBake.cpp` barrier）。
- **验收**：N=2 显存回落（目标 cube+voxel ~78 MiB/cascade，见 §0.5）；逐项 A/B 截图（漏光 / 接缝 / emissive 反弹 / HDR 范围）无退化；ping-pong 改动验时域稳定。

### P6（暂缓）— toroidal 环形寻址

P2 稳定后按需：以模运算 wrap 替代 memmove，零拷贝 recenter；接缝跨界 tap 钳制 / 1-brick guard。

### 设置 / CVar 汇总（最终名以实现为准）

- **删**：`sys.ambientCubePoolBrickRatio`
- **改**：`sys.ambientCubeCascadeCount`（移动 1 / 桌面 2，随平台默认）
- **增**：`sys.ambientClipmapSnap`、`sys.ambientClipmapHysteresis`、`sys.ambientBakeMode`(0 propagation / 1 traced)、`sys.voxelSunVis`、`sys.voxelEmissiveBounce`
- **重估**：`HasFullAmbientCubeBudget`（`Engine.cpp`）按 dense（`N×121.5 + 148.5 MiB`，P5 后更低）

### 风险与回滚

- 每 Phase 独立开关 / 可回退；**P1（去稀疏）是最大不可逆结构改动** —— 建议在分支上完成 P1+P2 截图回归确认后再合并。
- 三个重点 A/B 验证项：P2 recenter 的 pop、P4 propagation 的质量、P5 压缩的漏光。

---

## 9. 参考

- 本仓库：`docs/plans/ambient-cube-memory-reduction.md`（地基，已完成）、`docs/designs/ambientcube-hit-driven-residency-design.md`（命中驱动，待实现）、`docs/notes/idtech8-hybrid-gi-vs-ambientcube.md`（世界辐射缓存分析）、`docs/designs/voxel-skyvisibility-soft-tracing-design.md`、`docs/designs/swmodern-noambient-sky-occlusion-design.md`。
- 源码：`src/Engine/Assets/Acceleration/{CPUAccelerationStructure,CpuBvh,ProbeBaker,BrickPageTable}.cpp(.h)`、`src/Engine/Assets/Core/Scene.cpp` / `Scene.Update.cpp`、`src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp`、`src/Engine/Runtime/Engine.CameraUbo.cpp`、`assets/shaders/common/{BasicTypes,AmbientCube,AmbientCubeBaker}.slang`、`src/Engine/Assets/GPU/UniformBuffer.hpp`。
- 外部：clipmap（Tanner 1998 / Geometry clipmaps）、DDGI scrolling volumes（Majercik 2019）、id Tech 8 GI（Sousa, SIGGRAPH 2025）、SHARC（NVIDIA-RTX，spatial hash radiance cache）。
