---
title: "id Tech 8 混合全局光照 vs gkNextEngine Ambient Cube GI — 技术分析与启发"
category: note
status: 分析
owner: engine
created: 2026-06-15
source: "Tiago Sousa, \"FAST AS HELL: idTech 8 Global Illumination\", Advances in Real-Time Rendering in Games, SIGGRAPH 2025"
source_url: "https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf"
---

# id Tech 8 混合全局光照 vs gkNextEngine Ambient Cube GI

> 阅读对象：Tiago Sousa 在 SIGGRAPH 2025 "Advances in Real-Time Rendering" 上分享的 *FAST AS HELL: idTech 8 Global Illumination*（用于 *Indiana Jones and The Great Circle* 早期版本与 *DOOM: The Dark Ages*）。
> 目的：拆解 id Tech 8 的实时混合 GI 管线，与 gkNextEngine 现有 ambient cube GI 做逐项对照，提炼出对本引擎**可落地**的启发。

---

## 0. TL;DR

id Tech 8 与 gkNextEngine 走的是**同一条大路**：抛弃离线 lightmap / 预烘焙探针，改为**全实时、级联辐照度体 + 多 bounce 复用上一帧结果**的方案。两者在「级联辐照度体 + 交错更新 + 上一帧多次反弹 + 可见度感知防漏光」这几件事上**思路高度一致**——gkNextEngine 其实已经独立做出了 id Tech 8 的核心骨架。

差距主要在 id Tech 8 多出来的几层**解耦与缓存**：

1. **可见度与着色解耦**：先只追「可见度」存 V-buffer，再对**去重后的世界辐射缓存（spatial hash）**单独着色，跨探针、跨帧复用 → 把"着色次数"从 O(探针×光线) 压到 O(唯一缓存格)。这是它能 60Hz 跑 5000km² 的关键，也是本引擎当前**最缺**的一环。
2. **屏幕空间 → 世界辐射缓存 → 辐照度体**三级 final gather，1 ray/pixel、0 着色、半/四分辨率 + 双边降噪上采。
3. **世界空间级联 light grid**：把大量 punctual light / 反射探针 / decal 在世界空间分桶，让缓存着色阶段能高效取光。
4. 一些**便宜但高价值**的工程细节：HDR 用 RGB9E5（而非 10-bit unorm）、async compute 省 ~0.5ms、反射探针按当前帧辐照度 re-fit。

下面 §5 给出按性价比排序的落地建议。其中**「可见度/着色解耦 + 世界辐射缓存」与已有的 `sharc-integration-plan.md` 高度重叠**——建议把那份计划从"只服务 PathTracing"升级为"同时给 ambient cube bake 的 indirect bounce 供能"的统一缓存。

---

## 1. id Tech 8 混合 GI 管线详解

### 1.1 设计约束（为什么这么做）

- **性能高于一切**：所有平台稳定 ≥ 60Hz，低配基线是 Xbox Series S（带 RT 的最低硬件）。
- **关卡爆炸式增大**：*The Dark Ages* 全关卡可行走面积约 5000 km²，是 *Eternal* 的 4–10×，关卡数 ~1.7×。
- **idTech 7 的 lightmap 路线不可扩展**：大图烘焙 4–10 小时/关、单关 ~500MB、网络存储 1.4TB、改任何东西都要重烤。若照搬到新项目，turnaround 估算高达 27–68 天、磁盘 44–110GB。结论：**砍掉一切预烘焙，转全实时**。
- 把硬件 RT 当作"可见性工具"，而**不能追太多光线**（低端硬件可能慢一个数量级）。

### 1.2 每帧管线总览

```
更新结构 → Sample World（只追可见度，存 V-buffer）
        → World Radiance Cache Update（对去重缓存格着色）
        → Irradiance Volumes Update（用缓存结果算辐照度体）
        → Final Gather（1 ray/pixel，0 着色，查 3 级缓存）
        → Denoise → Upscale
```

逐级拆解：

**(a) 世界级联 Light Grid（给 RT 世界打光）**
- 世界空间 clustered binning，`16×16×16` × 8 级联（指数分布），首级 32m、2m³/格。
- 每格最多 64 个 id，三类：光源（16bit）/反射探针（8bit）/decal（16bit）。
- 1 个 async compute dispatch + 分层 gather（粗剔除 → 细剔除，OBB-vs-AABB，groupshared flat bit array）。
- 约 30MB、~0.2ms（PC ~0.1ms）。**意义：脱离视锥的、可被缓存着色阶段统一查询的世界打光结构。**

**(b) Sampling the World —— 可见度与着色解耦（核心思想 1）**
- 级联辐照度体 `16×16×16` × 6 级联 + 手放 local volumes `10×10×10`；采样点 = 格中心。
- **交错更新**：每帧只更 1 个级联 + 1 个 local volume。
- 每个探针每帧只追 **N 条"可见度"光线**（按平台 64/32/16），**先不着色**。
- 命中只存 128bit 打包数据：`SBT index + primitiveID + instanceID + barycentrics + hitDistance`（典型 V-buffer / deferred 思路）。
- 命中即触发"世界辐射缓存"更新。

**(c) World Radiance Cache —— spatial hashing（核心思想 2）**
- 基于 Gautron 2020 的空间哈希：**1D 数组 + N 维哈希函数**，量化 25cm³ cell + 按到相机距离做 LOD，仅 14MB。哈希冲突用 linear search 处理。
- 每个命中：原子自增本帧活跃 cell 计数，记录 cell hash / rayID / probeID。
- **着色阶段**：用活跃 cell 计数建 indirect dispatch，每个 cell **只着色一次**（按 SBT 取三角形/材质，做与 raster 几乎相同的光照循环，取**上一帧辐照度体**做多 bounce）。每帧约 **20k cache entries**，且可复用 N 帧。
- **这是关键：着色被从"每探针每光线"降维到"每唯一缓存格一次 + 跨帧复用"。**

**(d) Irradiance Volumes Update**
- 1 个 compute dispatch，仅处理当前帧的级联/volume。
- 读世界辐射缓存结果（probeID+rayID 索引），无命中查 sky。
- 用**八面体环境映射**（DDGI 风格）存到 probe image atlas：`RGB9E5 颜色 + RG16F 可见度`，2560×1585，64MB（低配 30MB），~0.08ms。
- 可见度的 `RG16F` = 深度均值/方差 → 供 Chebyshev/variance 测试做**防漏光**（DDGI moment）。

**(e) Final Gather —— 三级缓存级联（核心思想 3）**
- 1 ray/pixel，cosine-weighted，蓝噪声扰动方向；½ 或 ¼ 分辨率；**0 着色**。
- 三级缓存依次回退：① 屏幕空间缓存（命中在视锥内且未遮挡）→ ② 世界辐射缓存（命中有有效 cell）→ ③ 辐照度体（兜底）。
- 每像素结果存 **2-band SH**（`3×RGBA16F`，R=L0、GBA=L1）——保留法线贴图细节，"每像素 = 一个 SH 辐照度探针"。

**(f) Denoise + Upscale**
- final gather 分辨率做可分离双边高斯（按 normal/depth/hitDistance 加权，groupshared+FP16）。
- 双边上采（4-tap poisson）+ 时域滤波，存 RGB9E5（或 RGBA16F）。
- **明确建议避免 R11G11B10F**（绿/黄偏色、无纯白、banding），呼吁硬件统一支持 RGB9E5。

**(g) 间接镜面 / 透明 / 合成**
- 反射探针：BC6H cube array + light grid/tile binning；**re-fit 技巧**：`Reflections = (Reflections / ProbeIrradiance) × FrameIrradiance`，把静态探针对齐到当前帧辐照度（消除发光墙角、拾取 bounce 色）。
- 间接镜面 = SSR 优先，按 smoothness 回退到探针；RT 反射在主机上为性能砍掉。
- 透明：froxel irradiance volume（体积雾共享，2-band SH，50MB）。
- 合成期用 directional occlusion（沿半球 ray-march + POM）补 BLAS 缺失的高频细节。

### 1.3 性能（出货数据，mission 4 大视野热点）

| 阶段 | Series S(900p) | Series X(1440p) | PS5(1440p) | PS5 Pro(1800p) | PC(4k) |
|---|---|---|---|---|---|
| World Sampling | 0.38 | 0.27 | 0.45 | 0.083 | 0.091 |
| Radiance Cache | 0.20 | 0.21 | 0.109 | 0.11 | 0.109 |
| Irradiance Vol. | 0.08 | 0.08 | 0.046 | 0.035 | 0.07 |
| Final Gather | 0.60 | 0.535 | 0.489 | 0.385 | 0.50 |
| Denoise | 0.208 | 0.18 | 0.266 | 0.22 | 0.52 |
| Upscale | 0.65 | 0.59 | 0.69 | 0.72 | 0.445 |
| **串行合计** | ~2.11 | ~1.9 | ~2.05 | ~1.92 | ~1.7 |
| **async** | ~1.71 | ~1.4 | ~1.55 | ~1.82 | NA |

async compute 在 Series X/PS5 省 ~0.5ms、Series S 省 ~0.4ms。**收益：从小时级预烘焙降到毫秒级、0 磁盘、art 即时反馈、所有表面类型统一代码路径。**

---

## 2. gkNextEngine 当前 Ambient Cube GI 架构梳理

> 来源文件：`AmbientCube.slang`、`AmbientCubeBaker.slang`、`ProbeBaker.cpp`、`VulkanBaseRenderer.GiBake.cpp`、`docs/plans/ambient-cube-memory-reduction.md`。

### 2.1 数据结构

```c
struct AmbientCube {        // 56 B
    uint PosZ,NegZ,PosY,NegY,PosX,NegX;             // 6×间接光  RGB10A2
    uint PosZ_D,NegZ_D,PosY_D,NegY_D,PosX_D,NegX_D; // 6×直接光  RGB10A2
    uint skyVisibility_pznzpyny, skyVisibility_pxnxs0s1; // 6 面 sky-vis + sun/spare
};
struct VoxelData {          // 16 B
    uint matId;                  // 0 = 空气
    uint age;                    // 时域累积帧数, 上限 16
    uint distanceToSolid_gg_z01; // 打包 SDF 距离 / inside / ±Z 可见度
    uint distanceToSolid_x01_y01;// 打包 ±X ±Y 可见度
};
```

- **辐照度表示 = HL2 风格 Ambient Cube**（6 个轴对齐 basis），`sampleAmbientCubeHL2_*` 用 `max(±n,0)` 加权 6 面。便宜、各向角分辨率低（6 lobe）。
- 颜色用 `RGB10A2`，`packRGB10A2` 以全局 `MAX_ILLUMINANCE` 做线性 clamp 缩放，**10-bit unorm**——HDR 范围被统一上限硬截、暗部精度有限、A2 基本浪费。

### 2.2 网格 / 级联

- 每级联 `192×192×48`，最多 `CUBE_CASCADE_MAX=4`，默认 `AmbientCubeCascadeCount=3`，单元尺寸按 `pow(cascadeRatio, i)` 指数放大。
- **世界固定网格**（offset 来自静态 `AmbientCubeOffset`，**不跟随相机**）→ 活跃集由场景几何决定、每场景静态。

### 2.3 烘焙（GPU compute，交错更新）

- `VulkanBaseRenderer.GiBake.cpp`：每帧只烤 **1 个级联**（`frameCount % cascadeCount`），并按 `temporalFrames` 把一个级联的探针切片分多帧 dispatch；时域累积权重 `1/min(age,16)`。
- `AmbientCubeBaker.slang::FaceTask`：每个面发 **16 条**半球光线（`FACE_TRACING=16`，grid4x4 抖动），**当场 TraceRay + 当场着色**：
  - 命中 emissive（`MaterialDiffuseLight`）→ 累加 directColor；
  - 命中其它表面 → `albedo × interpolateAmbientCubesStable<DI>(...)`（**取上一帧的 cube 做下一次 bounce**，× 1.25 magic 近似多次反弹）；
  - 未命中 → 采 IBL/sky 并累加 skyVisibility。
- 太阳光：`TraceOcclusion` 单独算 sun 直接项。
- **只处理近表面探针**（`minDist < 8`），配合 Phase 3 稀疏 brick：仅 dispatch 活跃 brick。

### 2.4 运行期消费（防漏光的 8-tap 插值）

- `interpolateAmbientCubes(Stable)`：在级联内做 8-tap 三线性，但**每个 tap 用 VoxelData 的方向 SDF（`distanceToSolid`）做几何拒绝**——若探针到采样点的距离超过该方向"到固体"的距离，丢弃该 tap → **确定性防漏光**。失败/未分配则回退更粗级联。
- `inSolid()` 用同一 SDF 给 DDA 空跳（`skipStep`）。
- 另有独立的 chamfer + jump-flood 距离场重建（`Bake.DistanceField*`）做空间跳步加速。

### 2.5 稀疏存储（已落地）

- 把 `192×192×48` 切成 `8³` brick + brick 表（brick→pool slot 或 INVALID），仅活跃 brick 进池。
- 显存 608 → ~324 MiB；烘焙改为遍历活跃 brick list（空探针不占线程）。
- 详见 `docs/plans/ambient-cube-memory-reduction.md`（Phase 1–3 + A/C/E/F 已实现）。

### 2.6 消费方

- `ERT_SoftwareModern`（raster + ambient cube GI，主力实时路径）、`ERT_SoftwareTracing`、`ERT_VoxelTracing` 等通过 `interpolateAmbientCubes` 在着色时**逐像素内联**取 GI。
- `ERT_PathTracing` 是独立离屏路径；`sharc-integration-plan.md` 计划在其中试验 SHaRC。

---

## 3. 逐项对照

| 维度 | id Tech 8 | gkNextEngine | 评价 |
|---|---|---|---|
| 总体路线 | 全实时、无预烘焙、上一帧多 bounce | **相同** | ✅ 同一条路 |
| 辐照度载体 | 八面体 atlas（DDGI 风格） | HL2 ambient cube（6 面 RGB10A2） | gk 更省、角分辨率更低 |
| 级联体 | 16³×6 + 手放 local 10³ | 192³(48) × 最多4 | 网格策略不同；gk 分辨率高得多 |
| 交错更新 | 每帧 1 级联 + 1 volume | 每帧 1 级联（再切片） | ✅ 同思路 |
| 多次反弹 | 缓存着色取上一帧辐照度体 | FaceTask 取上一帧 cube | ✅ 同思路 |
| **可见度/着色解耦** | **是**（V-buffer→缓存着色） | **否**（FaceTask 当场着色） | ❌ **最大差距** |
| **世界辐射缓存（hash）** | **是**（spatial hash, 跨帧/跨探针复用） | **无**（brick 是规则网格，无去重缓存） | ❌ 大差距（与 SHaRC 计划重叠） |
| 防漏光 | DDGI variance（RG16F moment, Chebyshev） | 方向 SDF 距离（确定性几何拒绝） | 两套都有效，philosophy 不同 |
| Final gather | 屏幕空间 1ray/px + 3 级缓存 + SH | 逐像素内联 8-tap，无屏幕空间级 | ❌ 缺屏幕空间复用与降噪上采 |
| 多光源取光 | 世界级联 light grid（光/探针/decal 分桶） | bake 仅取 emissive + sun | ❌ 缺 punctual light 进 GI 的可扩展通道 |
| 间接镜面 | 反射探针(BC6H) + SSR，按帧辐照度 re-fit | （另路；ambient cube 仅 diffuse） | 可借鉴 re-fit |
| HDR 格式 | RGB9E5（强烈推荐，避开 R11G11B10F） | RGB10A2（10-bit unorm + 全局 clamp） | ❌ 可低成本升级 |
| async compute | 是，省 ~0.5ms | bake 在 compute，未明确 async overlap | 机会点 |
| 降噪/上采 | 可分离双边 + 时域 + 双边上采 | 主要靠 bake 内时域累积 | 低端可补半分辨率 resolve |

---

## 4. 关键观察

1. **gkNextEngine 已经独立长出了 id Tech 8 的"骨架"**：级联辐照度体、交错更新、上一帧多 bounce、可见度感知防漏光——这四件最难的事都已具备。下面的启发都不是"推倒重来"，而是**在现有骨架上加缓存层和工程细节**。

2. **本引擎防漏光走的是"确定性方向 SDF"路线**，比 id Tech 8 的 variance/Chebyshev 更"硬"、对薄墙更不易漏（只要 SDF 分辨率够），但也更易在边界产生硬过渡。这是本引擎的**特色优势**，不必盲目换成 DDGI moment。

3. **最大的结构性差距是"着色没有被复用"**：`FaceTask` 对每探针每面 16 条光线**当场全套着色**（取材质、采贴图、再 interpolate 一次 bounce）。id Tech 8 把这步拆成"先存命中、再对**去重后的世界缓存格**着色一次、跨帧复用"。这正是它在巨大关卡上 60Hz 的根本原因，也是本引擎烘焙开销/可扩展性的天花板所在。

4. **这件事和已有的 `sharc-integration-plan.md` 是同一个东西**——SHaRC 本身就是 Gautron 式 spatial hash radiance cache。当前计划把它**限定在 PathTracing**，但 id Tech 8 证明同一个世界辐射缓存可以**直接给实时 ambient cube bake 的 indirect bounce 供能**。建议重新定位这份计划。

---

## 5. 对 gkNextEngine 的启发（按性价比排序）

> 每条给出：**思想 → 现状 → 落点 → 收益/风险**。优先做"低成本高确定收益"的 A/B，再评估结构性的 C/D。

### A.（低成本，强烈推荐）间接光改用 RGB9E5 存储

- **思想**：id Tech 8 反复强调 RGB9E5（共享指数）相对 R11G11B10F/10-bit unorm 在同样 32bit 下 HDR 范围与精度都更好，并避免偏色/无纯白/banding。
- **现状**：`packRGB10A2` 用 10-bit unorm + 全局 `MAX_ILLUMINANCE` 线性 clamp。亮部被硬截、暗部 bounce 精度差、A2 几乎浪费。
- **落点**：`ConstFunc.slang` 增加 `packRGB9E5/unpackRGB9E5`，把 `AmbientCube` 的 12 个颜色面从 `packRGB10A2` 切到 RGB9E5（结构仍 56B，零显存增量）。`skyVisibility` 字节不动。
- **收益**：HDR 间接光范围/精度立增，强光源旁的 bounce 不再被 clamp，暗部 banding 减轻。**风险低**（纯编码替换，可 A/B 截图对比）。注意保留对不支持 RGB9E5 硬件的回退（与 id Tech 8 抱怨的硬件碎片化同因）。

### B.（中低成本）bake 走 async compute + 半分辨率 GI resolve

- **思想**：id Tech 8 async 省 ~0.5ms；final gather/降噪在 ½–¼ 分辨率跑再双边上采。
- **现状**：bake 在 compute 但未见明确 async overlap；GI 在着色阶段全分辨率逐像素内联，无独立 resolve/降噪。
- **落点**：① 把 ambient cube bake dispatch 放到 async queue 与 raster 重叠（`VulkanBaseRenderer.GiBake.cpp` + 队列/barrier）。② 可选：给 `ERT_SoftwareModern` 增一个半分辨率"GI resolve"目标（每像素只做一次 `interpolateAmbientCubes` + 2-band SH 存储），再双边上采到全分辨率——复用现有 `Upscaler` 模块与 `Process.DenoiseJBF`。
- **收益**：低端设备帧时间下降；GI 成本与场景着色复杂度解耦。**风险中**（需重排 barrier、验证时域稳定）。

### C.（结构性，高收益）统一"世界辐射缓存"：把 SHaRC 计划升级为 bake 的供能层

- **思想**：id Tech 8 的世界辐射缓存（spatial hash + 去重着色 + 跨帧/跨探针复用）是它可扩展的核心。
- **现状**：`FaceTask` indirect bounce 当场 `interpolateAmbientCubesStable`；无去重、无跨探针复用。`sharc-integration-plan.md` 已调研 SHaRC 但仅限 PathTracing。
- **落点**：把 SHaRC（或自研 Gautron 式 hash cache）做成**引擎级共享缓存**：
  - bake 的 indirect bounce 命中点先查 hash cache；miss 时记录命中（V-buffer 式），由独立 resolve pass 对**唯一 cell** 着色一次、复用 N 帧。
  - 这恰好把 §1.2(b)(c) 的"可见度/着色解耦"引入本引擎 bake。
  - GPU 资源沿用现有 `AmbientResources` 二级表模式（不动 128B `GPUScene`），与 SHaRC 计划 §2.3 一致。
- **收益**：bake 着色次数从 O(探针×16×6) 降到 O(唯一 cell)，**烘焙更快、收敛更稳、可扩展到更大场景**；同一缓存同时服务 PathTracing 与实时路径，统一代码。**风险中高**：Slang 编译适配（见 SHaRC 计划 §3.1）、hash 冲突/漏光、与现有 SDF 防漏光的协同。建议先按 SHaRC 计划 Phase 1–2 做 compile spike + 资源，再把 bake indirect 接进去做对照。
- **备注**：本引擎的方向 SDF 可作为 hash cache 的**防漏光补充**——cache 给 radiance，SDF 给可见度拒绝，二者正交，是相对 id Tech 8 的潜在差异化优势。

### D.（结构性，按需）世界空间 light grid，让 GI 吃到 punctual light

- **思想**：id Tech 8 世界级联 light grid 把光/反射探针/decal 分桶，缓存着色阶段统一取光。
- **现状**：`FaceTask` 的 GI 只拾取 emissive 材质 + sun；大量动态 punctual light 不进 bounce（只在 raster 直接光里）。场景一旦以点光/聚光为主，GI 会"缺一块"。
- **落点**：建世界空间级联 light cluster（AABB 格 + per-cell light id list），bake/缓存着色时按 cell 取光做光照循环。可与 C 的缓存着色 pass 合并实现。
- **收益**：多动态光源下 GI 正确性显著提升、可扩展。**风险中**：又一套数据结构与显存（id Tech 8 ~30MB）；建议在 C 落地后再做，复用其着色 pass。

### E.（小技巧，按需）反射/环境探针按帧辐照度 re-fit

- **思想**：`Reflections = (Reflections / ProbeIrradiance) × FrameIrradiance`，把静态探针对齐当前动态 GI，消除发光墙角、拾取 bounce 色。
- **落点**：若引擎引入反射/环境探针，采样时除以烘焙时探针辐照度、乘当前帧辐照度（可直接用 ambient cube 的辐照度估计）。
- **收益**：动态光照下反射不再"过时"。**风险低**，但依赖是否已有探针系统。

### F.（评估项）高端档可选八面体/DDGI 辐照度，移动端保留 ambient cube

- **思想**：八面体 + RG16F variance 可见度（DDGI moment）角分辨率与方向性优于 6 面 cube。
- **现状**：HL2 cube 是为移动端/显存刻意选的（见 memory-reduction 计划）。
- **落点**：作为**高端质量档**的可选 GI 表示，与 ambient cube 并存（采样接口已是 `IAmbientCubeSampler` 泛型，易并行扩展）；移动/低 heap 仍用 cube。
- **收益**：高端方向性细节更好。**风险中高**：显存上升、两套表示维护成本；优先级最低，先量化"cube 6-lobe 是否真的是当前画质瓶颈"再决定。

---

## 6. 建议落地次序

1. **A（RGB9E5）**：最快、风险最低、收益确定，先做，作为基线刷新。
2. **B（async + 半分辨率 resolve）**：独立于缓存改造，可与 A 并行，直接改善低端帧时间。
3. **C（统一世界辐射缓存 / SHaRC 升级）**：结构性主线。按 `sharc-integration-plan.md` Phase 1–2 起步，目标从"PathTracing 专用"扩成"bake indirect 供能"。这是对齐 id Tech 8 的关键一步。
4. **D（世界 light grid）**：在 C 之上做，复用其缓存着色 pass，解决多动态光 GI。
5. **E / F**：按是否有探针系统 / 是否需要高端质量档，机会性推进。

> 验证一律遵循 AGENTS.md：`./gnb build gkNextRenderer gkNextUnitTests` + `gnb shot --scene assets/models/playground.glb --frames 3000`（ambient cube 改动**必须**用大帧数让 CPU 体素化 + flush + bake 收敛，否则画面只是 sky IBL，看不出 GI 对错——见 memory-reduction 计划"验证坑"第 6 条）；漏光/接缝/反弹色做截图 diff，并记录 bake GPU timer 与活跃 brick 占比。

---

## 7. 参考

- Tiago Sousa, *FAST AS HELL: idTech 8 Global Illumination*, SIGGRAPH 2025 Advances in Real-Time Rendering（本文主源）。
- 关键引用链：Gautron 2020（spatial hashing AO/cache）、Majercik 2019（DDGI）、Wright 2021（radiance caching）、Ouyang 2021（ReSTIR GI）、Halén 2021（Surfels）、Greger 1998（Irradiance Volume）、Dachsbacher 2008（octahedron env map）、Drobot 2017（tiled/clustered culling）。
- 本仓库相关：`docs/plans/ambient-cube-memory-reduction.md`、`docs/plans/sharc-integration-plan.md`、`assets/shaders/common/AmbientCube.slang`、`assets/shaders/common/AmbientCubeBaker.slang`、`src/Engine/Assets/Acceleration/ProbeBaker.cpp`、`src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp`。
