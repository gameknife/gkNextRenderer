---
title: "id Tech 8 启发：世界辐射缓存作唯一 GI Hub（cube 派生读层）+ 间接光 RGB9E5 实施计划"
category: plan
status: 待实现
owner: engine
created: 2026-06-15
last_updated: 2026-06-15
related:
  - docs/notes/idtech8-hybrid-gi-vs-ambientcube.md
  - docs/plans/ambient-cube-memory-reduction.md
  - docs/plans/sharc-integration-plan.md
---

# id Tech 8 启发：世界辐射缓存作唯一 Hub（cube 派生）+ 间接光 RGB9E5

> 背景见 `docs/notes/idtech8-hybrid-gi-vs-ambientcube.md`。本文是给后续 agent 的**可执行**实施计划。
> 含两部分：**A**（间接光 RGB9E5 编码，低风险，独立可做）与 **C**（架构反转：世界哈希辐射缓存作唯一 GI 真值，ambient cube 退化为派生读层）。A 与 C 互不依赖，A 可先合入；C 按相位推进。

---

## 0. 架构决策记录（本次定稿）

经讨论拍板，C 的终局架构如下（取代旧版"把 SHaRC 加进 bake"的写法）：

1. **Hub = SHaRC 世界哈希辐射缓存，作唯一辐射真值。** 世界键（`floor(worldPos/voxelSize)` → hash），跨帧持久、与镜头无关。
2. **cube = 从 hash 每帧派生的读层（决策 B）。** cube 网格不再是独立 baker，而是 hash 的廉价投影：保留平滑方向性查找、方向 SDF 防漏光、时域稳定、移动端格式。彻底退役 cube（决策 A）作为备选记入附录，触发条件见 §C 附录。
3. **"全局化"的真正杠杆 = population 改为探针驱动，而非屏幕驱动。** 关键事实：**本引擎 bake 本就跑在世界固定的 cube 探针网格上**（memory-reduction F1：offset 静态、不跟相机），比 id Tech 8 相机居中 cascade 还更全局。因此只要把 bounce 取光从"读 cube"改成"读/写 hash"，覆盖天然就是全局、与镜头无关——这是相对原计划最重要的认知。
4. **多 bounce 经 hash 完成**（取代 cube 经上一帧自喂），cube 不再 ping-pong 自引用 → 可省 `CubesPong`，部分抵消 hash 显存。
5. **保留方向 SDF 防漏光**：cube 的 `distanceToSolid` 字段仍由 CPU 体素化 + DistanceField pass 算，只有 radiance 来源改成 hash 派生。漏光防护零损失。

> 映射到 id Tech 8 四阶段：① Sample World（探针追可见度）→ ② World Radiance Cache Update（对去重命中点着色写 hash）→ ③ Irradiance Volumes Update（cube 从 hash 派生）→ ④ Consume（实时/PT 读 cube 或直接查 hash）。本计划就是把这四阶段落到现有 cube 网格 + 现有 SHaRC 设施上。

### 目标数据流

```
[Population — 真值写入 hash]
  ① 探针驱动(主, 全局): cube 世界固定网格的探针每帧交错追可见度光线
  ② (可选)PT 屏幕驱动:  PT update 向同一 hash 写(近相机高保真)
  ③ 对命中点着色(direct: sun/emissive[/light grid] + indirect: 查 hash 上一帧)
     → SharcAccumulate 写 hash;  SharcResolve 合并 accumulation→resolved(stale/history)

[Derive — cube 从 hash 派生]
  ④ Cube Project: 每个 cube 探针向 hash gather/积分 6 方向 → 写 cube 6 面 radiance
     (cube 的 SDF/可见度字段不变, 仍由 CPU 体素化 + DistanceField 算)

[Consume]
  实时光栅/软追踪: interpolateAmbientCubes(读派生 cube, 保留 SDF 防漏光 + 时域)
  PT terminal:      读派生 cube 或直接 SharcQuery
```

---

## Part A — 间接光改用 RGB9E5 编码

> A 在 B 架构下依旧成立且更有价值：派生 cube 的 6 面仍是 56B 存储，RGB9E5 给同样 32bit/面更好的 HDR 范围与暗部精度；**同一 `packRGB9E5` 助手还可用于压缩 hash 的 resolved entry**（见 §C.2.6 备注）。

### A.0 目标与收益

`AmbientCube` 的 12 个颜色面（6 间接 + 6 直接）从 `RGB10A2`（10-bit unorm + 全局 `MAX_ILLUMINANCE=512` 线性 clamp）换成 **RGB9E5**（共享 5-bit 指数 + 3×9-bit 尾数）：结构仍 56B、零显存增量；HDR 上限从 512 扩到 ~6.5e4；暗部相对精度由共享指数提供，缓解 banding；对齐 Sousa 演讲"用 RGB9E5、别用 R11G11B10F"。

### A.1 现状（已核对）

- `assets/shaders/common/ConstFunc.slang`：`MAX_ILLUMINANCE`(L117)、`packRGB10A2`(L139)/`unpackRGB10A2`(L151)。**`packRGB10A2` 可能被 cube 之外系统使用，A 不改它本身。**
- `assets/shaders/common/AmbientCube.slang`：`PackColor`(L118)/`UnpackColor`(L123) 是 cube 颜色编解码的**唯一收口**；`LerpPackedColor*`(L128/137) 与所有 `sampleAmbientCubeHL2_*` 都经它。
- cube 颜色面**不使用 alpha**；RGB9E5 无 alpha 正好。`skyVisibility_*` 独立字节不动。

### A.2 设计

1. `ConstFunc.slang` 新增 `packRGB9E5(float3)/unpackRGB9E5(uint)`（GL_RGB9_E5 语义；落地写 round-trip 单测验证误差 < 1%、单调、无 NaN；若 Slang 有内建 shared-exp intrinsic 优先用内建）。
2. `AmbientCube.slang` 仅切两个收口函数：`PackColor → packRGB9E5(source.rgb)`、`UnpackColor → float4(unpackRGB9E5(packed),0)`。其余不动。
3. 不改 C++ / buffer 布局 / `skyVisibility`；运行时烘焙不落盘，无磁盘资产兼容问题。

### A.3 阶段 / A.4 验证 / A.5 风险

- **A-1** 加编解码 + 编译通过；**A-2** 切收口 + `./gnb build gkNextRenderer gkNextUnitTests`；**A-3** A/B 截图。
- 验证：round-trip 单测；`gnb shot --scene assets/models/playground.glb --frames 3000`（**必须大帧数**让体素化+flush+bake 收敛，否则只看到 sky IBL——memory-reduction「验证坑」6）。对比强 emissive/sun 旁 bounce 不再截顶、暗部 banding 减轻、无偏色。覆盖室内 + 开阔各一。
- 风险：共享指数在通道亮度极端不均时暗通道精度下降（低-中，A/B 看；必要时间接/直接面分别调 round）；`exp2` 边界（低，单测覆盖 e 边界）。

### A.6 文件清单（A）

`ConstFunc.slang`（加编解码）、`AmbientCube.slang`（切收口）、`tests/`（round-trip 单测）。

---

## Part C — 世界辐射缓存作唯一 Hub，cube 派生（架构反转）

### C.0 不变量（实现时必须始终成立）

- **I1 — hash 是唯一 radiance 真值。** cube 的 6 面 radiance 只能来自 hash 派生，不再由独立 bake 着色直接写。
- **I2 — cube 的可见度/SDF 字段独立保留。** `VoxelData.distanceToSolid_*` 仍由 CPU 体素化 + `Bake.DistanceField*` 算；消费期 8-tap 的方向 SDF 拒绝（`interpolateAmbientCubes`）不变 → 防漏光零损失。
- **I3 — 覆盖全局、与镜头无关。** population 主驱动是世界固定 cube 探针网格（非屏幕）。
- **I4 — 不双计。** direct（sun + emissive[/未来 light grid]）只在 population 着色时计一次写入 hash；cube 派生与消费侧不再叠加 direct。多 bounce 仅经 hash 反馈。
- **I5 — 关闭可降级。** `r.gi.radianceCache.enable=false` 时回退到现有 cube 自烘焙路径，行为等价旧版（保留旧 FaceTask 路径直到 P5 才删）。

### C.1 现状核对（关键）

**SHaRC 已实现且可用**（详见 `docs/notes/...` §C.1）：
- API `assets/shaders/common/Sharc.slang`：`SharcIsAvailable / SharcAccumulate(pos,n,rgb) / SharcQuery(pos,n,out rgba)`；哈希 `floor(pos/VoxelSize)`；**单级、无距离 LOD、单 slot 无线性探测（冲突丢）**。
- pass：`Core.SharcUpdate`（PT 屏幕驱动，含 TLAS）、`Core.SharcResolve`（每 entry 合并 + stale eviction + 0.9 history lerp）、`Core.SharcQuery`。
- 资源 `PathTracing/PathTracingRenderer.cpp`：`EnsureSharcResources()` 建 6 buffer；`BuildSharcGPUScene()`(L233) 写 `gpuScene.ReservedAddress0 = sharc resources addr`。
- 参数 `UserSettings.hpp`(L68–74)/`EngineCVars.cpp`(L135–148)：`r.sharc.*`；`SharcVoxelSize=0.75`、`entriesPow2=21`、`kSharcStaleFrameCount=180`。
- 数据结构 `BasicTypes.slang`(L278–329)：`SharcHashEntry/AccumulationEntry/ResolvedEntry(float4 Radiance + normal + frame + count)/Parameters/Resources`。**注意：resolved 每 cell 单 radiance + 单 normal，无方向基** → 这正是 cube 派生层存在的理由（决策 B）。

**cube 现状**：
- `AmbientCubeBaker.slang::FaceTask`(L29–62)：每面 16 射线，命中非 emissive → `bounceColor += albedo * interpolateAmbientCubesStable<DI>(hit, cascade) * 1.25`（**当前 bounce 读上一帧 cube**）。
- `VulkanBaseRenderer.GiBake.cpp::BakeAmbientCubeCascade`(L109)：`gpuScene = FetchGPUScene()`，未设 `ReservedAddress0` → bake 内 `SharcIsAvailable()==false`（安全默认）。
- **关键有利点**：FaceTask 跑在世界固定探针网格上 → population 改 hash 后**天然全局**，无需新建探针系统。

**PT 现状**（`PathTracingRenderer.slang`）：
- `ApplyTerminalRadiance`(L195) 永远读 cube；`EnableSharcUpdate` 时把 terminal/sun/sky/bounce 写 hash(L203/300/318/434)；`EnableSharcQuery` 时路径 `bounce>=minBounce` 提前终结进 hash(L374/409)。→ **cube 已是 PT 基础 terminal，SHaRC 是叠加的加速层**；本计划把它正式收编为唯一 hub。

### C.2 设计

#### C.2.1 缓存所有权上移（基建，前置）

把 SHaRC 资源/参数/clear/resolve pipeline 的所有权从 `PathTracingRenderer` 抽到可共享层（新建轻量 `FSharcCache`，由 `VulkanBaseRenderer` 持有；`PathTracingRenderer` 改引用）。`SharcResources` 地址经 `GPUScene.ReservedAddress0` 暴露（机制已存在）。要求：**PathTracing 行为零变化**为验收门槛。

#### C.2.2 探针驱动 population（"全局化"核心）

复用世界固定 cube 探针网格作为 population 调度器。两种粒度：

- **C.2.2-简（P1 落地）**：保留 FaceTask 的"逐探针追+着色"，但：(a) indirect bounce 读取从 `interpolateAmbientCubesStable`（cube）改为 `SharcQuery`（hash），(b) 把命中点着色出的**出射 radiance**经 `SharcAccumulate(hitPos, hitNormal, outgoing)` 写回 hash。此时 hash 成为多 bounce 真值源，覆盖已全局（探针世界固定）。cube 暂仍由 FaceTask 写（P2 才翻转为派生）。
- **C.2.2-全（P3 落地，dedup）**：拆成两 pass —— ① 可见度 pass：每探针追 N 射线，记录命中（V-buffer：SBT/inst/prim/bary/dist + 标记活跃 hash cell，原子去重）；② 着色 pass：对**唯一活跃 cell** 各着色一次写 hash。对应 id Tech 8 的"shading 降维到唯一缓存格"，是 perf 头牌。

> 着色语义（两粒度一致）：cell 出射 radiance = direct(sun via TraceOcclusion + emissive 命中)[+ 未来 light grid 多光] + indirect(`SharcQuery` 该 cell 取上一帧入射) × albedo。**emissive/sun 只在此计一次（I4）。**

#### C.2.3 cube 从 hash 派生（决策 B 落地，P2）

FaceTask 的"写 cube 6 面"职责改为**从 hash gather**：每个 cube 探针对 6 个面，用其半球射线命中的 hash cell radiance 积分（cosine 加权，类 `sampleAmbientCubeHL2`），写入 cube 6 面（经 `PackColor`，即 A 的 RGB9E5）。

- cube 的 `distanceToSolid_*` / `age` / `matId` 字段**不动**（I2）。
- **可省 `CubesPong`**：多 bounce 经 hash 反馈（C.2.4），cube 不再 ping-pong 自引用 → 释放 `CubesPong`（~单 cascade 大小）显存，部分抵消 hash 成本。SDF scratch 解耦保持（见 memory-reduction）。
- 可作单 pass（追+着色 hash+派生 cube 合一，简单）或拆 pass（配合 P3 dedup）。

#### C.2.4 多 bounce 经 hash（反馈环稳定，I4）

cube ← hash ← (cube 派生喂 population 着色的 indirect 查询) 形成反馈边。处理：
- 沿用 `SharcResolve` 的 history lerp（当前 0.9）做时域稳定；评估按动态程度自适应。
- population 着色的 indirect 取 hash **上一帧 resolved**（避免同帧自反馈）。
- 能量：每 bounce 经 albedo × throughput 衰减，配合 stale eviction（180 帧）防 stale 残留与发散。

#### C.2.5 消费（不变 + PT 收编）

- 实时光栅/软追踪：仍 `interpolateAmbientCubes`（读派生 cube，SDF 防漏光 + 时域不变）。
- PT terminal：`ApplyTerminalRadiance` 读派生 cube（默认）或直接 `SharcQuery`（P5 评估哪个更优）。PT 的 update/query 从"实验旁路"转为"hub 的近相机高保真 feeder/consumer"。

#### C.2.6 hash 硬化（承重必需，P4）

单 slot/无 LOD 做 PT 加速器可以，做唯一真值会出空洞/糊：
- **少步线性探测**处理冲突（Gautron/id Tech 8）：`SharcMakeSample`/`SharcQuery`/`SharcAccumulate` 在 slot 命中 key 不符时线性探测 N 步。
- **距离/尺寸 LOD**：`SharcMakeSample` 把 lod 拌进 cell 量化与 key（id Tech 8：`lod=exp2(floor(log2(1+dist/REF)))`，25cm³ 起步），近细远省。距离基准可用到相机或到最近探针。
- 备注：评估 `SharcResolvedEntry.Radiance` 用 **RGB9E5（复用 A 的助手）** 压缩，降低 hash 显存。

#### C.2.7 GPUScene 接线 + cvar

- `BakeAmbientCubeCascade`（及派生/着色 dispatch）push 前补 `gpuScene.ReservedAddress0 = sharcCache_.ResourcesAddress()`（0 → 安全降级 I5）。
- realtime 帧在 population/派生后插一次 `Core.SharcResolve` + barrier（无 TLAS，便宜）。
- 新 cvar：`r.gi.radianceCache.enable`(默认 false)；复用 `r.sharc.voxelSize/entriesPow2/stale`；新增 `r.gi.radianceCache.queryNormalThreshold`、（P4）`r.gi.radianceCache.lodRefDist`、`r.gi.radianceCache.probeSteps`。

### C.3 分阶段计划

> 依赖：P0 前置。P1（bounce 走 hash，覆盖即全局）与 P2（cube 翻转为派生）是本架构的最小可见闭环。P3/P4 是 perf 与承重硬化。P5 收编 PT 并删旧路径。

| 相位 | 内容 | 验收 |
|---|---|---|
| **P0 基建** | 抽 `FSharcCache` 上移所有权；realtime 帧跑 Resolve；`ReservedAddress0` 接线（bake 暂不读写 hash）。 | PathTracing sharc on/off **零回归**；`r.gi.radianceCache.enable=true` 下 `gnb shot playground 300` 不崩、validation 无错；日志打印容量/显存。 |
| **P1 bounce 走 hash（全局覆盖）** | FaceTask：indirect 读 `SharcQuery`、命中写 `SharcAccumulate`（C.2.2-简）。cube 暂仍 FaceTask 写。 | hash occupancy/hit rate 随帧上升；覆盖不随镜头丢失（移动相机后回看，GI 仍在）；关开行为可切。 |
| **P2 cube 派生** | FaceTask 写 cube 改为从 hash gather（C.2.3）；移除 cube bounce 自引用；释放 `CubesPong`。 | 与 P1/旧版截图 diff（漏光/接缝/反弹色不退化）；显存：`CubesPong` 释放可见；`sw-lightbake` timer。 |
| **P3 dedup（perf 头牌）** | 拆可见度/着色两 pass，唯一 cell 着色一次（C.2.2-全）。 | bake 着色工作量↓（GPU timer）；画质等价；活跃 cell 去重率有数据。 |
| **P4 hash 硬化** | 线性探测 + 距离 LOD（C.2.6）；评估 resolved RGB9E5 压缩。 | 大场景/封闭薄墙空洞与糊明显改善；冲突丢率↓；显存账本更新。 |
| **P5 PT 收编 + 删旧路径** | PT terminal 走 hub；把 SHaRC update/query 从实验 toggle 收编为默认 hub 的一部分；删除 cube 独立着色旧路径（I5 退役）。 | PT 与实时 GI 一致性提升；旧路径删除后全 program 编译 + 视觉回归通过。 |
| **P6 移动端预算 + 调参** | 低 heap 下 hash entriesPow2 下调 / 与 cube arena 统一预算；反馈环 history、stale、voxelSize/LOD 标定。 | 低 heap 设备不降级黑 GI；运动镜头 ghosting 可接受；性能/显存有数据。 |

### C.4 验证计划（C）

- 构建：`./gnb build gkNextRenderer gkNextUnitTests`（改 CMake/新增 shader 加 `--reconfigure`）。
- 截图：`gnb shot --scene assets/models/playground.glb --frames 3000`（cube 必须大帧数收敛）；另跑开阔 + 室内薄墙 + **一个"移动相机后回看"序列**专测全局覆盖。
- 指标（GPU timer / debug readback）：hash occupancy / hit rate / collision drop；`sw-lightbake` 与新派生/着色 pass GPU time（P3 后着色应↓）；Resolve time；显存（hash 按 entriesPow2 估 + cube arena，注意 `HasFullAmbientCubeBudget` 低 heap 联动；`CubesPong` 释放计入）。
- 正确性：开/关缓存截图 diff（漏光/接缝/反弹色/亮度一致性）；**全局覆盖专项**（回看不丢 GI）；PT sharc on/off 回归不退化；I4 不双计抽查（关 sun 看是否只 direct 一次）。

### C.5 风险

| 风险 | 等级 | 缓解 |
|---|---|---|
| 单 slot 冲突丢 / 无 LOD → 唯一源出空洞糊 | 中-高 | P4 线性探测 + 距离 LOD（承重前必做）；miss 有 cube 派生兜底不黑 |
| 反馈环发散 / 残影 | 中 | indirect 取上一帧 resolved、history lerp、stale eviction；P6 标定 |
| 双计 sun/emissive | 中 | I4：direct 只在 population 着色计一次；cube 派生/消费不叠加 |
| 跨 cell 漏光（薄墙） | 中 | 法线阈值 + 保留 cube 方向 SDF 消费拒绝（正交双保险，I2） |
| 移动端显存：hash 从可选变强制 | 中-高 | P6 低 heap 降 entriesPow2 / resolved RGB9E5；`CubesPong` 释放部分抵消；低 heap 强制关时降级旧路径 |
| 缓存上移重构波及 PT | 低-中 | P0 以 PT 零回归为门槛 |
| Slang 适配 | 低 | SHaRC 已编译运行；线性探测/LOD 为纯 ALU 扩展 |

### C.6 文件清单（C）

**Shader（`assets/shaders/`）**
- `common/Sharc.slang` — 线性探测 + 距离 LOD（P4）；（可选）resolved RGB9E5。
- `common/AmbientCubeBaker.slang` — FaceTask：indirect 走 `SharcQuery`、命中 `SharcAccumulate`（P1）；写 cube 改 hash gather（P2）；拆可见度/着色（P3）。
- `Bake.*AmbientCube*.comp.slang` / 新增 `Bake.ProjectCubeFromHash.comp.slang`（若 P2 拆独立派生 pass）。
- 复用现有 `Core.SharcResolve.comp.slang`（realtime 帧调度）。
- `common/PathTracingRenderer.slang` — P5 terminal 走 hub / 收编 update-query。

**C++（`src/Engine/`）**
- `Rendering/` 新增 `FSharcCache`（所有权上移）。
- `Rendering/PathTracing/PathTracingRenderer.cpp/.hpp` — 改引用共享缓存（P0 行为不变；P5 收编）。
- `Rendering/VulkanBaseRenderer.GiBake.cpp` — `ReservedAddress0` 接线；realtime Resolve dispatch + barrier；派生/着色 dispatch 调度。
- `Rendering/VulkanBaseRenderer.cpp/.hpp` — 共享缓存生命周期 / realtime 调度；释放 `CubesPong`（P2）。
- `Assets/Core/Scene.cpp/.hpp` — `CubesPong` 释放 / 预算调整（P2/P6）。
- `Runtime/Config/UserSettings.hpp` + `EngineCVars.cpp` — `r.gi.radianceCache.*` 新 cvar。
- `Runtime/Engine.cpp` — `HasFullAmbientCubeBudget` 与 hash 预算联动（P6）。

### C 附录：决策 A（彻底退役 cube）备选

若未来要完全去掉 cube：需把 hash entry 升级为**方向性辐照度**（2-band SH 或 per-face）并自带可见度（如 RG16F variance / Chebyshev）以替代 cube 的方向 SDF；所有消费方直接 `SharcQuery`。改动大、且要重新解决 cube 已解决的漏光与实时时域稳定。**触发条件**：cube 派生层的 gather 成本或显存成为瓶颈、或角分辨率/格式不再满足需求。在此之前按决策 B 推进。

---

## 总体落地次序

1. **A 全做**（A-1→A-3）：独立、低风险、确定收益，先合入作为基线刷新（其 RGB9E5 助手 P4 复用于 hash 压缩）。
2. **C-P0**：基建（缓存上移 + realtime Resolve + 接线），PT 零回归为门槛。
3. **C-P1 + C-P2**：bounce 走 hash（覆盖即全局）+ cube 翻转为派生 —— 本架构最小可见闭环。
4. **C-P3 / C-P4**：dedup 提速 + hash 硬化（承重）。
5. **C-P5 / C-P6**：PT 收编删旧路径 + 移动端预算与调参。

> 验证遵循 `AGENTS.md`：`./gnb build gkNextRenderer gkNextUnitTests` + `gnb shot ... --frames 3000`，截图 diff 漏光/接缝/反弹色，**加测移动相机回看的全局覆盖**，记录 GPU timer 与缓存/显存指标。
