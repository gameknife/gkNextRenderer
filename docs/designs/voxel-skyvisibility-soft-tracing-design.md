---
title: "VoxelData 体素天光可见度（GPU Soft Tracing）设计与开发计划"
category: design
status: 已实现
owner: engine
created: 2026-06-22
last_updated: 2026-06-22
---

# VoxelData 体素天光可见度（GPU Soft Tracing）设计与开发计划

> 状态：✅ 已实现（Phase 0–2 落地；门控解耦 + age 高字节存储 + GPU 半球 soft-trace 烘焙 + 运行时三线性采样并入 GTAOCompose）。`office.scad` 调试视图（`r.gtao.debugMode 3`）确认室内地面/墙根处 skyVis 明显变暗、开阔处趋近 1，SwModern/PathTracing 无回归。
> 目标渲染器：`Vulkan::SoftwareModernNoAmbient::SoftwareModernNoAmbientRenderer`（枚举 `ERT_SoftwareModernNoAmbient`）
> 本期目标：在**不依赖 AmbientCube** 的前提下，在 **GPU 上对体素距离场做 soft tracing**，把一个 **per-voxel skyVisibility** 标量写回 `VoxelData`，运行时三线性采样，**与已落地的 GTAO 正交叠加**，为 `SwModernNoAmbient` 补上 GTAO 屏幕空间方法天然缺失的「大尺度 / 屏幕外」天光遮蔽。
> 前置依赖：[SwModernNoAmbient 天光遮蔽（屏幕空间 GTAO）](swmodern-noambient-sky-occlusion-design.md)（✅ 已完成）。本设计即该文 **§8「未来工作：体素大尺度天光遮蔽」** 的正式排期方案。
> 关联文件：
> - 数据结构：`assets/shaders/common/BasicTypes.slang`（`VoxelData` / `AmbientCube` / `UniformBufferObject`）
> - 体素 CPU 化：`src/Engine/Assets/Acceleration/ProbeBaker.cpp`、`CPUAccelerationStructure.{h,cpp}`
> - Soft tracing：`assets/shaders/common/RayTracers.slang`（`FHiVoxelDDARayTracer`）、`assets/shaders/common/AmbientCube.slang`（`inSolid`）
> - 既有 GPU 天光烘焙参考：`assets/shaders/common/AmbientCubeBaker.slang`、`assets/shaders/Bake.SwAmbientCube.comp.slang`
> - 渲染器编排 / 门控：`src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.{hpp,cpp}`、`VulkanBaseRenderer.GiBake.cpp`、`VulkanBaseRenderer.hpp`（`FRendererRequirements`）
> - 着色 / 合成：`assets/shaders/Core.SwModernNoAmbient.comp.slang`、`assets/shaders/Process.GTAOCompose.comp.slang`
> - 内存 / arena：`src/Engine/Assets/Core/Scene.cpp`、`src/Engine/Runtime/Engine.cpp`

---

## 0. 结论先行（TL;DR）

**可行**。引擎已具备本特性所需的全部底层零件，本期是「组装 + 解耦门控」而非「从零造轮子」：

1. **Soft tracer 现成**：`FHiVoxelDDARayTracer::TraceOcclusionDDA(origin, dir, maxDist)`（`RayTracers.slang:58-151`）已经是对 `VoxelData` 距离场做 DDA 步进的遮蔽查询，半球射线统计即得 skyVisibility，**无需新写步进核**。
2. **GPU 写 `VoxelData` 有先例**：AmbientCube 烘焙已经在 GPU 上写 `Voxels[idx].age`（`AmbientCubeBaker.slang:182-183`），并把 skyVisibility 烘进 `AmbientCube`（`:185-199`）。本期把「skyVisibility 烘到 voxel 而非 cube、用 soft trace 而非 TLAS、在 NoAmbient 跑」即可。
3. **合成注入点现成**：`Process.GTAOCompose.comp.slang:85` 的 `output = direct + ambient * ao`，把它改成 `ambient * ao * skyVis` 即完成与 GTAO 的叠加。

**两个必须先解决的前提（也是本期主要工作量）**：

- **前提 A — 体素数据在 NoAmbient 下不存在**：体素 CPU 化（`cpuAccelerationStructure_.Tick`）与 GI 烘焙都被 `CurrentRendererRequirements().requestAmbientCube` 门控（`Scene.Update.cpp:212-213`、`VulkanBaseRenderer.cpp:1467`），而 NoAmbient 该标志为 `false`（`VulkanBaseRenderer.cpp:212-213`）。**NoAmbient 当前根本不灌体素**。需新增一个 `requestVoxelGeometry` 门控并把体素化/arena 分配从 `requestAmbientCube` 解耦。
- **前提 B — `VoxelData` 已满 16B，且本期不扩容**：`VoxelData` 是紧打包的 16 字节（`BasicTypes.slang`，`GPU_SCENE_VOXEL_DATA_SIZE = 16`），无空闲字段。本期固定复用 `age` 高字节存 skyVisibility，保持 `VoxelData` 尺寸不变，零显存增长。

---

## 1. 背景与目标

`SwModernNoAmbient` 是「无 AmbientCube」的轻量 deferred 路径。前置 [GTAO 设计](swmodern-noambient-sky-occlusion-design.md)已为它的天光项补上**屏幕空间 GTAO**：天光被拆到全分辨率 `RT_AMBIENT`，`Process.GTAOCompose.comp.slang:85` 做 `output = direct + ambient * ao`。

但 GTAO 是屏幕空间方法，天然丢失两类遮蔽：

- **屏幕外遮挡**：视锥外、被自身遮挡的几何对当前像素天光的阻挡。
- **大尺度 / 远距离体积遮挡**：室内整体压暗、天井 / 峡谷 / 巷道的层次感。半径外的样本 GTAO 一律收敛到「无遮蔽 = 1.0」。

本设计补上这块：用 **体素距离场的 GPU soft tracing** 烘一个 **per-voxel 标量 skyVisibility**，描述「该体素朝上半球能看到多少天空」，运行时三线性采样、与 GTAO 相乘。GTAO 管**近场接触级**细节，voxel skyVis 管**大尺度 / 屏幕外**遮蔽，二者正交叠加。

### 目标（In Scope）

- `VoxelData` 在不改变 16B 布局的前提下，通过 `age` 高字节携带一个 per-voxel skyVisibility 标量（0–255）。
- 新增一个 GPU compute 烘焙 pass：对体素距离场做半球 soft tracing（复用 `TraceOcclusionDDA`），时序累积写回 voxel。
- 把体素化 / 距离场 / arena 分配从 `requestAmbientCube` 解耦，使 NoAmbient 独立运行也能维护体素 SDF。
- 运行时在 `Process.GTAOCompose` 三线性采样 skyVis，与 GTAO 相乘只压暗天光项。
- cvar 开关 + 调试视图；体素数据缺失时优雅降级为纯 GTAO。

### 非目标（Out of Scope）

- **方向性（6 面）skyVisibility**：本期只做标量；方向版列入 §8 演进，但不进入本期数据结构方案。
- **AmbientCube 颜色烘焙 / 多次反弹 GI**：那是 `SoftwareModern` 的职责，NoAmbient 不碰。
- **sunVisibility / 体积光**：`VoxelData` 注释里预留的 sunVisibility 不在本期。
- **逐像素运行时 DDA 天光遮蔽**（每帧每像素现算半球，不烘）：成本过高，本期明确走「GPU 烘到 voxel + 运行时采样」的缓存式路线。

---

## 2. 现状分析

### 2.1 `VoxelData` 内存布局（已满 16B）

`BasicTypes.slang` 的 `VoxelData`（`GPU_SCENE_VOXEL_DATA_SIZE = 16`，`ALIGN_16`）：

```slang
public struct ALIGN_16 VoxelData
{
    public uint matId;                   // 0 = void（CPU 写，Minecraft blockId 语义）
    public uint age;                     // 运行时计数器（GPU 烘焙写，CPU voxelize 置 0）
    public uint distanceToSolid_gg_z01;  // 打包 4 字节：见下
    public uint distanceToSolid_x01_y01; // 打包 4 字节：distPX,distNX,distPY,distNY
};
```

`distanceToSolid_gg_z01` 的字节语义（由 `ProbeBaker.cpp:218-220` 写、`AmbientCube.slang` 的 `inSolid` 读）：

| 字节 | 含义 | 写入处 | 读取处 |
| --- | --- | --- | --- |
| X | **SDF skip 距离**（chamfer/jump-flood 结果，单位=体素数） | `ProbeBaker.cpp:273-274`（`RebuildDistanceField`） | `inSolid` 的 `skipStep`（`AmbientCube.slang:719-722`） |
| Y | **inside**（6 轴距离乘积，≈0 表示在固体内） | `ProbeBaker.cpp:218-219` | `inSolid`：`unpack0.y==0 → 实心`（`:683-687`） |
| Z/W | distPZ / distNZ（到表面的方向距离 0–255） | 同上 | soft 命中长度插值 |

> **结论**：16 字节全部占用，没有空闲字段。给 skyVisibility 腾位是本期硬约束（§5.1）。

### 2.2 `age` 是已有的「GPU 可写 + CPU 重置」字段（关键复用点）

`age` 当前**只**被 AmbientCube 烘焙当时序计数器用：

- GPU 累加：`AmbientCubeBaker.slang:182-183`、`:211-212` —— `iterate = Voxels[voxelIdx].age; Voxels[voxelIdx].age = age + 1;`
- 用途：抖动（`grid3x3[iterate % 9]`、`grid4x4[i]+offset`，`:26-31`）+ 时序权重（`accumulatedFrames = min(age+1, kTraceHistoryLength=16)`，`:78-80`）。**有效信息只在低位**（mod 9 与饱和到 16）。
- CPU 重置：`ProbeBaker.cpp:176`（`VoxelizeCube` 写 `cube.age = 0`）、`Bake.ClearAmbientCubeCache.comp.slang:36`。

> 这意味着 `age` 天然是「几何变了就清零、几何不变就跨帧累积」的字段——与 skyVisibility 想要的生命周期**完全一致**。把 skyVis 塞进 `age` 的高字节即可白嫖这套累积/失效语义（§5.1 方案 A）。

### 2.3 既有 skyVisibility（在 AmbientCube 上，NoAmbient 不可用）

skyVisibility 目前**只**存在于 GPU 烘焙的 `AmbientCube`（`skyVisibility_pznzpyny` / `skyVisibility_pxnxs0s1`，每面 1 字节），由 `AmbientCubeBaker.slang` 的 `FaceTask` 用 **TLAS `TraceRay`** 烘焙：射线 miss（打到天空）则 `skyVisibility += 1`（`:57-61`），最终 `255 * skyVis/FACE_TRACING` 时序累积（`:78-80`）。

**NoAmbient 不跑这条**（无 cube、无 brick pool）。本期是把同一个「半球统计 miss 比例」的思路：① 改用 **soft trace（体素 SDF）** 而非 TLAS，② 结果存 **voxel** 而非 cube，③ 在 NoAmbient 跑。

### 2.4 Soft tracing 机器已现成

`RayTracers.slang` 的 `FHiVoxelDDARayTracer`：

- `TraceOcclusionDDA(rayOrigin, rayDir, maxDistance)`（`:58-151`）：选 cascade → `RebuildVoxelDDAState` → 逐体素 DDA，命中 `inSolid` 即返回 `true`。**这正是对 `VoxelData` 距离场的遮蔽查询**。
- `TraceOcclusion(o, d)` = `TraceOcclusionDDA(o, d, 80.0f)`（`:148-151`）。
- `Bake.SwAmbientCube.comp.slang` 已示范 `FHiVoxelDDARayTracer tracer;` 的实例化与 dispatch 模式。

> 半球 N 根射线，`skyVis = (未被 occlusion 的射线数) / N`，即得标量天光可见度。**不需要新写 DDA 步进核。** 注意 `TraceOcclusionDDA` 只依赖 `Voxels`（不依赖 Pages 页索引），故烘焙前提仅是 voxel 的 matId+距离场可用。

### 2.5 NoAmbient 渲染管线与注入点

`SoftwareModernNoAmbientRenderer::Render`（`SoftwareModernNoAmbientRenderer.cpp:58-152`）：

| 顺序 | Pass | Shader | 关键 I/O |
| --- | --- | --- | --- |
| 1 | 着色 | `Core.SwModernNoAmbient.comp.slang` | 写 `RT_SINGLE_DIFFUSE`(direct+emissive) / `RT_AMBIENT`(天光) / `RT_NORMAL` / `RT_PREV_DEPTHBUFFER` |
| 1.5 | GTAO | `Core.GTAO.comp.slang` | 半分辨率 AO → `RT_GTAO` |
| 1.6 | GTAO 合成 | `Process.GTAOCompose.comp.slang` | `output = direct + ambient * ao` |
| 2 | 时序 | `Process.ReProjectSimple.comp.slang` | TAA 累积 |
| 3 | 合成 | `Process.ComposeSimple.comp.slang` | 描边 + tonemap |

**skyVis 采样注入点 = Pass 1.6（`Process.GTAOCompose.comp.slang:85`）**：

```slang
// 现状
float4 output = float4(directOrEmissive.rgb + ambient.rgb * ao, 1.0f);
// 目标
float skyVis = SampleVoxelSkyVisibility(worldPos);   // 三线性采样 voxel
float4 output = float4(directOrEmissive.rgb + ambient.rgb * ao * skyVis, 1.0f);
```

> 注：合成 pass 当前只有 NDC 深度与法线，需要 world position 才能采样 voxel。可由 `RT_PREV_DEPTHBUFFER` + `Camera.ModelViewInverse/ProjectionInverse` 反算（着色 pass 重建主光线已用同一套矩阵，`Core.SwModernNoAmbient.comp.slang:49-52`），或在着色 pass 直接把 skyVis 采样了写进 `RT_AMBIENT.a`（见 §5.4 两种放置）。

### 2.6 门控现状（本期最大改动面）

| 行为 | 门控 | NoAmbient 下 |
| --- | --- | --- |
| 体素化 CPU Tick | `CurrentRendererRequirements().requestAmbientCube`（`Scene.Update.cpp:212-217`） | **不跑**（只 `RebuildBVHOnly`，`:234`） |
| GPU 距离场 / cube 烘焙 | 同上（`VulkanBaseRenderer.cpp:1467-1478`、`GiBake.cpp:29-47`） | **不跑** |
| arena（含 Voxels）分配 | `RegisteredRendererRequirements().requestAmbientCube`（`Scene.cpp:158-171`） | 仅当注册表里**有**需 cube 的渲染器才全量；纯 NoAmbient 平台 capacity=1（Voxels 缓冲存在但不灌） |
| 渲染器 requirements | `VulkanBaseRenderer.cpp:208-213`：NoAmbient = `{}`（`requestAmbientCube=false`） | — |

`FRendererRequirements`（`VulkanBaseRenderer.hpp:41-53`）当前只有 `requestAmbientCube` / `requestRayTracing` 两个标志，并提供 `Merge`。

**memory 量级**（`GPU_SCENE_AMBIENT_PER_CASCADE_COUNT = 192×192×48 = 1,769,472`）：

- 单 cascade Voxels：1.77M × 16B ≈ **28.3 MB**；4 cascade ≈ **113 MB**。
- 本期不放大 `VoxelData`，因此 skyVisibility 存储不增加 voxel 显存；若未来另案扩容，需单独重新评估显存预算。

---

## 3. 可行性结论

**可行，且与现有架构同构。** 风险集中在「门控解耦」而非「算法」。逐条对齐目标：

| 需求 | 现成零件 | 缺口 |
| --- | --- | --- |
| GPU soft trace 体素 SDF | `FHiVoxelDDARayTracer::TraceOcclusionDDA`（`RayTracers.slang:58-151`） | 仅需半球采样包装 |
| GPU 写 skyVis 到 VoxelData | `age` 已是 GPU 可写字段（`AmbientCubeBaker.slang:182`） | skyVis 腾位（§5.1） |
| 时序累积 + 几何失效语义 | `age` 计数器 + CPU `VoxelizeCube` 清零 | 复用即可 |
| NoAmbient 下体素可用 | CPU 化 / arena 全套已存在 | **解耦 `requestAmbientCube` 门控**（§5.2） |
| 与 GTAO 叠加 | `GTAOCompose:85` 注入点 | 乘一个 skyVis 因子 |
| 优雅降级 | 采样缺失返回 1.0 | 采样 helper 写好 fallback |

唯一「真新增」是一个烘焙 shader + 一个采样 helper + 一处门控标志。其余皆为既有模式的复制/改写。

---

## 4. 总体方案

```
   场景几何 ──► CPU 体素化 (Tick/VoxelizeCube)  [门控: requestVoxelGeometry, 解耦自 requestAmbientCube]
                 └─► Voxels: matId + 距离场(SDF)   ← TraceOcclusionDDA 的输入
                          │
                          ▼  (每帧时序切片, 预算友好)
   ┌──────────────────────────────────────────────────────────────┐
   │ 新 Pass: Bake.VoxelSkyVisibility.comp.slang                    │
   │   for 每个"近表面" voxel:                                       │
   │     半球 N 根射线 TraceOcclusionDDA(voxel SDF)                  │
   │     skyVis = unoccluded/N  →  时序 lerp(age)                    │
   │     写回 VoxelData (age 高字节)                                  │
   └──────────────────────────────────────────────────────────────┘
                          │  Voxels.skyVis 持久驻留 GPU
                          ▼
   着色 Core.SwModernNoAmbient ──► RT_AMBIENT(天光), RT_NORMAL, depth
                          ▼
   Core.GTAO ──► RT_GTAO (近场 AO)
                          ▼
   Process.GTAOCompose:
       worldPos ← depth 反投影
       skyVis  ← SampleVoxelSkyVisibility(worldPos)   // 三线性
       output = direct + ambient * ao * skyVis        // GTAO×大尺度 正交叠加
                          ▼
   ReProjectSimple → ComposeSimple → history
```

三条设计原则：

1. **缓存式而非逐像素**：天光遮蔽烘到 voxel 上跨帧复用，运行时只做一次三线性采样，避免每像素半球 DDA 的爆炸成本。
2. **正交叠加**：GTAO 负责半径内接触细节，voxel skyVis 负责大尺度/屏幕外；二者相乘（或 `min`，见 §5.4），互不替代。
3. **优雅降级**：voxel 数据缺失（采样无有效邻居）时 skyVis=1.0，自动退化为纯 GTAO，不破坏 GTAO 既有交付。

---

## 5. 详细设计

### 5.1 skyVisibility 落位（固定方案）

`VoxelData` 已满 16B。本期数据结构决策固定为：**不扩张 `VoxelData` 尺寸，不修改 `GPU_SCENE_VOXEL_DATA_SIZE`，skyVisibility 复用 `age` 高字节**。后续实现不得引入 32B voxel 布局作为本计划的一部分。

#### 方案 A（已定）— 复用 `age` 高字节，零显存增长

把 `age`（uint）切分：

```
age:  [ bit31..24 : skyVisibility(0-255) ] [ bit23..0 : 时序计数器 ]
```

- 计数器只需到 `kTraceHistoryLength=16` 与 mod 9，24 位绰绰有余（需 clamp 防溢出进高字节）。
- 提供 helper（建议落在 `AmbientCube.slang` 或新 `VoxelSkyVis.slang`）：

```slang
uint  GetVoxelAgeCounter(VoxelData v)        { return v.age & 0x00FFFFFFu; }
uint  GetVoxelSkyVis(VoxelData v)            { return (v.age >> 24) & 0xFFu; }
void  SetVoxelAgeCounter(inout VoxelData v, uint c)
      { v.age = (v.age & 0xFF000000u) | (min(c, 0x00FFFFFFu)); }
void  SetVoxelSkyVis(inout VoxelData v, uint s)
      { v.age = (v.age & 0x00FFFFFFu) | ((s & 0xFFu) << 24); }
```

- **优点**：不改 `VoxelData` 尺寸 → 不动 `GPU_SCENE_VOXEL_DATA_SIZE`、`ComputeAmbientArenaLayout`（`Scene.cpp:65-92`）、`static_assert(sizeof(VoxelData)==16)`（`Scene.cpp:102`）、CPU memcpy 上传（`ProbeBaker.cpp:241-247`）。零显存增长。复用 `age` 的「几何变更即清零」语义。
- **必须同步处理的副作用**：AmbientCubeBaker 现在对**整个** `age` 自增（`AmbientCubeBaker.slang:182-183,211-212`），会溢出进高字节。**对策**：把这两处也改用 `GetVoxelAgeCounter/SetVoxelAgeCounter`（低 24 位自增 + clamp），高字节永不被它污染。SoftwareModern 模式本就不读 voxel skyVis，但统一用 helper 可保证模式互切时高字节干净（配合 §5.5 的 clear）。

> **已定结论**：本期只实现方案 A。32B `VoxelData` 不作为本计划的备选实施路径；如果未来要做 6 面方向性 skyVis 或 sunVisibility 独立字段，需要另开设计并重新评估显存、arena layout、CPU/GPU 结构一致性和迁移成本。

### 5.2 体素数据可用性解耦（前提 A）

新增渲染器需求标志，把「体素几何」从「ambient cube」解耦：

```cpp
// VulkanBaseRenderer.hpp  FRendererRequirements (:41-53)
struct FRendererRequirements
{
    bool requestAmbientCube   = false;
    bool requestRayTracing    = false;
    bool requestVoxelGeometry = false;   // 新增：需要 voxel SDF（matId+距离场），但不一定要 cube
    void Merge(const FRendererRequirements& o)
    {
        requestAmbientCube   |= o.requestAmbientCube;
        requestRayTracing    |= o.requestRayTracing;
        requestVoxelGeometry |= o.requestVoxelGeometry;
    }
};
```

- **语义**：`requestAmbientCube` 蕴含 `requestVoxelGeometry`（cube 烘焙依赖 voxel）。建议在 `GetRendererRequirements` 里令 cube 渲染器同时置二者，NoAmbient 只置 `requestVoxelGeometry`（`VulkanBaseRenderer.cpp:208-213`）。
- **改门控**（把 `requestAmbientCube` 换成「需要 voxel」的判断）：
  - **CPU 体素化 Tick**：`Scene.Update.cpp:212-213` 的 `shouldUpdateAmbientCube` 拆成两级——`shouldUpdateVoxel`（`requestVoxelGeometry || requestAmbientCube`）驱动 `cpuAccelerationStructure_.Tick`（voxelize + CPU chamfer SDF + page index），`shouldBakeCube`（`requestAmbientCube`）才驱动 cube 相关。
  - **arena 分配**：`Scene.cpp:158-171` 的 `allocateAmbientCube` 拆成 `allocateVoxel`（`requestVoxelGeometry || requestAmbientCube`，决定 Voxels/Pages 是否右尺寸分配）与 `allocateCube`（`requestAmbientCube`，决定 Cubes/CubesPong 池）。NoAmbient 独立运行时 Voxels 按 1 cascade 实灌；Cubes 池可不分配（省显存）。
  - **GPU 距离场 / sky-vis 烘焙调度**：`VulkanBaseRenderer.cpp:1467` 改为：`requestVoxelGeometry` → 跑 `RebuildDistanceFieldCascades`（可选，CPU chamfer 已够用）+ 新的 sky-vis 烘焙；`requestAmbientCube` → 才跑 `BakeAmbientCubeCascade`（cube 颜色）。
- **最小依赖澄清**：sky-vis soft trace 仅需 **Voxels 的 matId + 方向距离场**（`VoxelizeCube` 输出，`ProbeBaker.cpp:173-221`）。SDF skip 字节（chamfer/jump-flood）只是 DDA 加速，缺了也正确（`inSolid` 退化为步长 1）。Pages 页索引 `TraceOcclusionDDA` 不用，可不强制。→ **NoAmbient 跑通 sky-vis 的最小前提就是让 `Tick` 在该模式下执行**。

### 5.3 新 GPU sky-vis 烘焙 Pass

新增 `assets/shaders/Bake.VoxelSkyVisibility.comp.slang`（结构对照 `Bake.SwAmbientCube.comp.slang`）：

```slang
import Common;
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FHiVoxelDDARayTracer tracer;
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];

    uint cascadeIndex = Bindless.GetGpuscene().custom_data_1;
    uint voxelIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;   // 本帧时序切片偏移
    // ... 越界返回；由 voxelIdx 解出 probePos(int3)

    // 1) 只处理"近表面 air"体素：matId==0(非实心) 且 距离场 skip 字节小(贴近表面)
    uint4 dgg = UnpackBytes(Voxels[voxelIdx].distanceToSolid_gg_z01);
    bool nearSurface = (Voxels[voxelIdx].matId == 0u) && (dgg.x < kSkyVisSurfaceBand);
    if (!nearSurface) return;   // 开阔 air 维持默认 255；实心不着色

    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    float3 origin  = float3(probePos) * cubeUnit + GetAmbientCubeOffsetAtCascade(cascadeIndex);

    // 2) 时序计数 + 抖动（复用 age 低 24 位）
    uint iterate = GetVoxelAgeCounter(Voxels[voxelIdx]);

    // 3) 上半球 N 根 cosine-weighted 射线，软追踪遮蔽
    uint unoccluded = 0;
    const uint N = kSkyVisRayCount;            // 建议 8–16，按 quality cvar
    for (uint i = 0; i < N; ++i)
    {
        float3 dir = CosineHemisphereUp(i, N, iterate);   // 朝 +Y 半球 + 每帧抖动
        if (!tracer.TraceOcclusionDDA(origin, dir, kSkyVisMaxDistance))   // maxDist 大尺度(如 16–64m)
            unoccluded++;
    }
    float skyVisFrac = float(unoccluded) / float(N);      // [0,1]

    // 4) 时序 lerp（同 AmbientCubeBaker:78-80 的权重）
    float acc = min(float(iterate) + 1.0f, kTraceHistoryLength);
    float w   = 1.0f / acc;
    float prev = float(GetVoxelSkyVis(Voxels[voxelIdx]));
    uint  next = uint(lerp(prev, 255.0f * skyVisFrac, w));

    SetVoxelSkyVis(Voxels[voxelIdx], next);
    SetVoxelAgeCounter(Voxels[voxelIdx], iterate + 1);
}
```

调度与预算（对照 `BakeAmbientCubeCascade`，`GiBake.cpp:97-211`）：

- **Dispatch 域**：NoAmbient 无 brick pool，**按 dense voxel grid** 线性 dispatch（每 cascade `1.77M` 体素，本期 NoAmbient 通常 1 cascade）。用 `custom_data_0` 做帧间 offset、`custom_data_1` 传 cascadeIndex（沿用既有 push-const 约定 `GiBake.cpp:205-210`）。
- **时序切片**：按 `BakeSpeedLevel` 把全量体素拆 N 帧烘（`GiBake.cpp:122-140` 的 `groupPerFrame` 模式），避免单帧打满。
- **方向**：天光 = 上半球，`+Y` 为主轴的 cosine 加权；可整体仍打 4π 但只统计上半球，或直接半球采样（更省）。`AlignWithNormal` / `grid4x4`（`AmbientCube.slang:103-108`、`AmbientCubeBaker.slang:31-32`）可复用做抖动基。
- **maxDistance**：决定「大尺度」范围，建议 16–64m（远大于 GTAO 的 0.5–2m 半径），cvar 化。
- **barrier**：烘焙写 Voxels 后、着色/合成读 Voxels 前插 buffer barrier（对照 `GiBake.cpp:78-95,143-191` 的 Voxels 区段 barrier）。

> **默认值策略**：只烘「近表面 air」体素；开阔 air 体素 skyVis 保持初值（建议初始化为 255=全可见），实心体素不参与着色。这样凹角/墙根/室内（贴近表面）拿到烘焙遮蔽，开阔处恒为 1.0。

### 5.4 运行时采样与 GTAO 合成

新增三线性采样 helper（对照 `interpolateAmbientCubes` 的 8 邻居权重与有效性门控，`AmbientCube.slang:477-566`，但只取 skyVis 标量）：

```slang
public float SampleVoxelSkyVisibility(float3 worldPos)
{
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    // 选 cascade → nearpos → floor/frac → 8 邻居
    // 每邻居: 取 GetVoxelSkyVis(voxel)/255; 用 distanceToSolid_gg_z01.Y(inside) 做有效性过滤
    // 三线性加权; totalWeight==0 → 返回 1.0 (降级)
    return result; // [0,1]
}
```

合成（`Process.GTAOCompose.comp.slang:74-99`）：

```slang
float skyVis = camera.SkyVisEnable ? SampleVoxelSkyVisibility(worldPos) : 1.0f;
// 基线：相乘（GTAO 近场 × voxel 大尺度）
float4 output = float4(directOrEmissive.rgb + ambient.rgb * ao * skyVis, 1.0f);
```

- **worldPos 来源**：合成 pass 由 `RT_PREV_DEPTHBUFFER`(NDC) + `Camera.ProjectionInverse`/`ModelViewInverse` 反投影（同 `Core.SwModernNoAmbient.comp.slang:49-52` 的矩阵）。
- **两处放置可选**：
  - (i) **在合成 pass 采样**（如上）：改动集中，但每像素一次三线性 voxel 读。
  - (ii) **在着色 pass 采样**并写进 `RT_AMBIENT.a`，合成只读回 `ambient.rgb * ao * ambient.a`：着色 pass 已有 worldPos，省一次反投影；`RT_AMBIENT` 是 RGBA16F，alpha 现未用。**推荐 (ii)**。
- **叠加算子**：基线用乘法（物理上两种遮蔽独立 → 概率相乘）。若出现接触处「GTAO 已暗 + skyVis 又暗」的双重压暗，可改 `ao_total = min(ao, skyVis)`（前置 GTAO 文 §8 的建议），或对 skyVis 做近场提亮 `skyVis' = lerp(1, skyVis, saturate(dist/closeBand))`。三者都用 `r.skyvis.combineMode` cvar 暴露，实测选型。
- **调试**：扩展 `GTAODebugMode`（现 0/1/2，`EngineCVars.cpp:89-90`）增加「仅 skyVis」「仅 ao」「ao×skyVis」视图。

### 5.5 与 AmbientCube 烘焙 / 模式切换的交互

- **`age` 共享**（方案 A）：AmbientCubeBaker 的两处 `age++` 改用 `SetVoxelAgeCounter`（§5.1），保证高字节 skyVis 不被 cube 烘焙污染。
- **模式切换清理**：切渲染器时已有 `RequestClearAmbientCubeCache`（`VulkanBaseRenderer.cpp:403`）；`Bake.ClearAmbientCubeCache.comp.slang:36` 已把 `Voxels[idx].age=0`（同时清计数器与 skyVis）。确认 NoAmbient↔SoftwareModern 互切时触发该 clear，避免读到陈旧 skyVis。
- **CPU flush 覆盖**：`Tick` flush 时 `UploadGPU` memcpy 整 cascade（`ProbeBaker.cpp:241-247`），会把 GPU 烘的 skyVis（在 age 里）连同 age 一起重置为 0 → 之后由 sky-vis pass 重新累积（~16 帧）。这与 AmbientCube 现有「几何变更即重烘」行为一致，**属预期**，仅需注意动态场景频繁 flush 时 skyVis 会反复重收敛（§9 风险）。

---

## 6. 数据结构与接口改动清单

| # | 文件 | 改动 |
| --- | --- | --- |
| 6.1 | `assets/shaders/common/BasicTypes.slang` | 不改变 `VoxelData` 尺寸；仅补充 `age` 高字节语义注释，确认 `GPU_SCENE_VOXEL_DATA_SIZE` 仍为 16 |
| 6.2 | `assets/shaders/common/AmbientCube.slang`（或新 `VoxelSkyVis.slang`） | 新增 `Get/SetVoxelSkyVis`、`Get/SetVoxelAgeCounter`、`SampleVoxelSkyVisibility` |
| 6.3 | `assets/shaders/common/AmbientCubeBaker.slang` | `:182-183,211-212` 两处 `age++` 改用计数器 helper（方案 A 必做） |
| 6.4 | `assets/shaders/Bake.VoxelSkyVisibility.comp.slang`（新） | sky-vis 烘焙核（§5.3） |
| 6.5 | `assets/shaders/Core.SwModernNoAmbient.comp.slang` 或 `Process.GTAOCompose.comp.slang` | 采样 skyVis 并并入天光项（§5.4，推荐写 `RT_AMBIENT.a`） |
| 6.6 | `src/Engine/Rendering/VulkanBaseRenderer.hpp` | `FRendererRequirements` 加 `requestVoxelGeometry` + `Merge` |
| 6.7 | `src/Engine/Rendering/VulkanBaseRenderer.cpp` | `:208-213` NoAmbient 置 `requestVoxelGeometry`；`:1467` 调度按新门控拆分 |
| 6.8 | `src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp` | 新增 `BakeVoxelSkyVisibility` 调度（dispatch + barrier + timer，仿 `:97-211`） |
| 6.9 | `src/Engine/Assets/Core/Scene.Update.cpp` | `:212-217` `shouldUpdateAmbientCube` 拆 voxel/cube 两级门控 |
| 6.10 | `src/Engine/Assets/Core/Scene.cpp` | `:158-171` arena 分配按 voxel/cube 拆分；Cubes 池 NoAmbient 可省 |
| 6.11 | `src/Engine/Runtime/Config/EngineCVars.cpp` | 新增 `r.skyvis.*`（见 §6.12）；`UserSettings.hpp` + UBO 字段 |
| 6.12 | `assets/shaders/common/BasicTypes.slang`（UBO） | 加 `SkyVisEnable / SkyVisStrength / SkyVisMaxDistance / SkyVisRayCount / SkyVisCombineMode`（UBO 现有 padding 可吸收，仿 GTAO 字段） |

新增 cvar（仿 `r.gtao.*`，`EngineCVars.cpp:79-90`）：

```
r.skyvis.enable        (bool)  voxel 天光遮蔽总开关
r.skyvis.rayCount      (int)   半球射线数(8/16)，按 quality
r.skyvis.maxDistance   (float) soft trace 最大距离(m)，大尺度范围
r.skyvis.strength      (float) 遮蔽强度
r.skyvis.combineMode   (int)   0=mul 1=min 2=near-bright
```

> RT slot：本设计**不新增 storage image**（skyVis 走 buffer + `RT_AMBIENT.a`）。若调试需要单独 target，空闲段 `31..49` 可用（`BindlessTexture.slang`，`RT_AMBIENT=29`/`RT_GTAO=30` 已占）。

---

## 7. 开发计划（分阶段）

> 验证遵循 AGENTS.md「定向构建」：Engine/shader 改动用 `./gnb build gkNextRenderer gkNextUnitTests`；shader 需重编 `.spv`；肉眼用 `gnb shot --scene <X>`。

### Phase 0 — 门控解耦（前提，最高风险，先做）

- `FRendererRequirements` 加 `requestVoxelGeometry`；NoAmbient 置位（§6.6-6.7）。
- 拆分 `Scene.Update.cpp` 体素化门控与 `Scene.cpp` arena 分配（§6.9-6.10）。
- **验证**：NoAmbient 独立运行（低配平台路径，`Engine.cpp:217`）下，确认 `cpuAccelerationStructure_.Tick` 执行、Voxels 被灌（可临时把 voxel matId/距离场可视化，或单测断言 Voxels 非零）。确认 SoftwareModern / PathTracing 既有行为不回归。

### Phase 1 — 存储 + 烘焙（核心交付）

- 方案 A 的 `age` 高字节 helper + AmbientCubeBaker 改计数器 helper（§5.1,5.5）。
- 新 `Bake.VoxelSkyVisibility.comp.slang`（§5.3）+ `BakeVoxelSkyVisibility` 调度（§6.8），时序切片。
- **验证**：把 voxel skyVis 直接可视化（调试视图）；凹角/室内/墙根处 skyVis 明显 <1，开阔处 ≈1；`gnb shot` 在 living_room（室内）与 playground（户外）对比。

### Phase 2 — 运行时采样 + GTAO 叠加

- `SampleVoxelSkyVisibility` 三线性 helper（§5.4）；着色 pass 写 `RT_AMBIENT.a` 或合成 pass 采样。
- `GTAOCompose` 并入 `ambient * ao * skyVis`；`r.skyvis.*` cvar + 调试视图。
- **验证**：A/B（`r.skyvis.enable` 开关）；确认室内整体压暗、屏幕外遮挡补足、太阳 CSM/自发光不受影响；与纯 GTAO 对比看「大尺度层次」增益；`r.skyvis.combineMode` 三模式选型。

### Phase 3 — 调参、降级与文档

- maxDistance / strength / rayCount 标定；动态场景 flush 重收敛观感核对。
- 降级路径确认（voxel 缺失 → skyVis=1.0 → 纯 GTAO）。
- `gnb benchmark` 量烘焙 + 采样增量；更新 `docs/README.md` 索引、本文转「已完成」、回填前置 GTAO 文 §8 链接到本文。

---

## 8. 演进方向（本期不做）

- **方向性 skyVis（6 面）**：另开数据结构设计，评估是否需要独立存储或新 buffer；不在本期把 `VoxelData` 扩到 32B。方向版可把标量换成 6 字节方向可见度，采样时按法线加权（同 `sampleAmbientCubeHL2_*` 思路），方向感更准、可驱动 bent-normal 式天光偏移。
- **sunVisibility / 体积**：`VoxelData` 注释已预留 sunVisibility（大尺度软阴影 / 体积光遮蔽），但本期不新增字段；未来可在另案中复用同一 soft-trace 框架朝太阳方向烘。
- **稀疏化**：voxel 也可像 cube 走 brick pool（`BasicTypes.slang` Phase 4 备注），仅近表面 brick 分配 skyVis 存储，进一步省显存。

---

## 9. 性能预算与风险

**预算（参考量级，需 `gnb benchmark` 实测）**：

- 烘焙：dense voxel × N 射线 × DDA 步进，**靠时序切片摊到多帧**（同 cube 烘焙），单帧目标 < 0.5ms。近表面体素占比低，实际工作量远小于 1.77M。
- 运行时采样：每像素一次三线性 voxel 读（8 邻居），< 0.1ms；放进 `RT_AMBIENT.a` 则与着色合批。

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 门控解耦影响其它渲染器 | SoftwareModern/PathTracing 回归 | Phase 0 单独验证三模式；`requestVoxelGeometry` 仅加法，cube 路径维持 `requestAmbientCube` |
| `age` 高字节被 cube 烘焙污染 | SoftwareModern 切回 NoAmbient 时 skyVis 脏 | AmbientCubeBaker 必须改计数器 helper；切换 clear（§5.5）；本期不通过扩容规避 |
| voxel 分辨率粗 → skyVis 块状/漏光 | 大尺度遮蔽边界硬 | 三线性插值 + 有效性门控；与 GTAO 叠加由 GTAO 补细节；maxDistance/band 调参 |
| 动态场景频繁 flush 重收敛 | skyVis 闪烁/迟滞 | 复用 cube 同款时序；必要时对 skyVis 单独限制重烘频率 |
| 未来方向性数据需求 | 标量 skyVis 不够表达方向遮蔽 | 本期保持 16B；方向性/独立字段另开设计，不在当前计划扩容 |
| 双重压暗（GTAO×skyVis） | 接触处过暗 | `combineMode`（mul/min/near-bright）实测选型（§5.4） |
| 体素未灌就采样 | 全黑/错误遮蔽 | 采样 fallback=1.0；Phase 0 确保灌入早于采样 |

---

## 10. 验证方案

- **调试视图**：cvar 切「voxel skyVis / ao / ao×skyVis / 未遮蔽天光」，逐路肉眼核对。
- **A/B**：`r.skyvis.enable` 与 `r.gtao.enable` 四象限对比（凹角、室内整体、天井、屏幕外遮挡）。
- **回归**：`docs/gallery` 场景（living_room 室内、playground 户外、debug_draw）跑一遍；确认 GTAO 既有交付不回归、太阳 CSM / 自发光不受影响。
- **门控回归**：SoftwareModern（cube）、PathTracing、NoAmbient 三模式互切，确认体素灌入/清理正确、无崩溃（注意 `AmbientCubeBaker` 注释提到的 AMD GPU 写 voxel 崩溃前例，`:145-146`）。
- **性能**：新 pass 加 `SCOPED_GPU_TIMER("voxel skyvis bake")`，`gnb benchmark` 量增量。
- **编译**：`./gnb build gkNextRenderer gkNextUnitTests` 通过；`.slang → .spv` 无误。

---

## 11. 关键文件索引

| 用途 | 文件:行 |
| --- | --- |
| `VoxelData` / `AmbientCube` / UBO | `assets/shaders/common/BasicTypes.slang` |
| soft trace DDA | `assets/shaders/common/RayTracers.slang:58-151` |
| 体素遮蔽测试 `inSolid` | `assets/shaders/common/AmbientCube.slang:662-737` |
| 三线性采样参考 | `assets/shaders/common/AmbientCube.slang:477-566` |
| 既有 skyVis 烘焙（TLAS） | `assets/shaders/common/AmbientCubeBaker.slang:13-84,182-199` |
| GPU 烘焙调度参考 | `src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp:97-211` |
| 门控（Tick / arena / bake） | `Scene.Update.cpp:212-237`、`Scene.cpp:158-171`、`VulkanBaseRenderer.cpp:1467-1478` |
| 渲染器 requirements | `VulkanBaseRenderer.hpp:41-53`、`VulkanBaseRenderer.cpp:208-213` |
| CPU 体素化 | `src/Engine/Assets/Acceleration/ProbeBaker.cpp:173-298` |
| NoAmbient 渲染编排 | `SoftwareModernNoAmbientRenderer.cpp:58-152` |
| 着色 / GTAO 合成 | `Core.SwModernNoAmbient.comp.slang`、`Process.GTAOCompose.comp.slang:74-99` |
| GTAO cvar 参考 | `src/Engine/Runtime/Config/EngineCVars.cpp:79-90` |
| 前置设计 | `docs/designs/swmodern-noambient-sky-occlusion-design.md`（§8） |

---

## 12. 给后续开发 agent 的提示

- **先做 Phase 0 门控解耦**：没有体素数据，后面全是空中楼阁。先用调试可视化确认 NoAmbient 下 Voxels 真被灌入，再写烘焙。
- **storage 固定取方案 A（`age` 高字节）**，零显存、复用失效语义；**不要扩张 `VoxelData` 尺寸**。务必同步把 `AmbientCubeBaker` 的两处 `age++` 改成低 24 位计数器 helper，否则 SoftwareModern 模式会污染 skyVis 高字节。
- **soft trace 不要重写步进核**：直接 `FHiVoxelDDARayTracer::TraceOcclusionDDA`，它已对 voxel 距离场做好 cascade 选择与 DDA。
- **只烘近表面 air 体素**，开阔处维持默认 255、实心不烘；采样 fallback=1.0 保证优雅降级为纯 GTAO。
- **采样优先放在着色 pass 写 `RT_AMBIENT.a`**（已有 worldPos，省反投影），合成 pass 只读回相乘。
- **叠加算子先用乘法**，遇双重压暗再切 `min` / near-bright，用 `r.skyvis.combineMode` 暴露，别写死。
- 注意 `AmbientCubeBaker.slang:145-146` 提到的「GPU 直接写 `Voxels[].matId` 在 AMD 上可能崩溃」前例——写 voxel 字段时遵循其规避方式（只写自己负责的字段，避免与 CPU 上传竞争同一字段）。
- 体素分辨率决定 skyVis 是「大尺度」而非「接触级」——别期望它替代 GTAO，二者是正交分工。
