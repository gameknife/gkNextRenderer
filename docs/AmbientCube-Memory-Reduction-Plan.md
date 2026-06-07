# AmbientCube 显存占用降低 — 可行性评估与开发计划

> 状态：**Phase 1–3 + 后续 A/C/E/F 已实现并提交到 `dev`**（2026-06-04）。下方原始计划保留作设计依据；实现实况见「实现状态」。
> 方向：**保持当前 storage buffer / BDA 路径，不引入实时块压缩**；通过稀疏存储 + 右尺寸分配降低显存。

## 实现状态（2026-06-04）

已落地并各自截图验证（playground.glb，`--frames 3000`，单测 4145 断言全过）：

| 阶段 | commit | 内容 | 结果 |
|---|---|---|---|
| 计划 | `3c04b2b8` | 本文档 | — |
| Phase 1+2 | `a7a8fa5a` | 解耦 + 右尺寸 | 608 → ~486 MiB |
| Phase 3a | `8fb48715` | brick 池间接（行为等价） | 内存中性，验证间接管线 |
| Phase 3b | `2b092b8c` | 稀疏分类 + 缩池 | ~486 → **~324 MiB** |
| 后续 A | `d96a4d8f` | pool cap 配置化 + heap 预算按 pool 估算 | 默认 `sys.ambientCubePoolBrickRatio=0.66`；playground arena **384.6 MiB**，active ~3336/6843，无溢出 |
| 后续 C | `537b0265` | 烘焙/propagation/inject 改由活跃 brick list dispatch | 不再遍历 dense probe；playground long-shot 通过 |
| 后续 E | `13618f01` | sparse tap 无效时跳过并下坠到更粗 cascade | 未分配/溢出 brick 不再直接稀释成黑 GI |
| 后续 F | `43f3006e` | HDRI bindless 贴图按当前 sky 需求驻留 | 默认只上传最低 mip；当前 `SkyIdx` 连续需求后升 full，闲置后降回最低 mip |

**实现中发现、与原计划的关键偏差（后续务必知悉）：**

1. **GPUScene 是 128B push constant（`static_assert(sizeof==128)`），塞不下多个独立地址。** 因此 Phase 1 没有内联多地址，而是仿照 `SoftMeshShaderResources` 引入 **`AmbientResources` 间接结构**（`AmbientBase` 指向它，内含 Cubes/Voxels/Pages/CubesPong/SdfScratch/SdfSeedA/BrickTable/PoolParams 八个地址）。所有 GPUScene 取数属性经它转发。
2. **SDF SeedA 必须解耦**：原先 jump-flood 把 `CubesPong` 别名当 SeedA；CubesPong 变 brick 池后失效，故新增独立稠密 `SdfSeedA` 区（+27 MiB，但换来 pong 可缩）。
3. **固定 cap ⇒ 节省是 cap 绑定、不随场景稀疏度缩放。** 决策选了「固定上限」，所以池恒为 `cap × cascade` 大小，稀疏场景只是空 slot 更多、并不更省。cap 受**最密场景的 cascade-0 活跃数**下界约束（playground cascade-0 ~2076）。当前已改为 `UserSettings.AmbientCubePoolBrickRatio` / `sys.ambientCubePoolBrickRatio`（默认 0.66，约 2281/3456 bricks/cascade），playground long-shot 无溢出。想更激进省显存可下调该 cvar。
4. **烘焙已改为遍历活跃 brick list**：CPU flush 时额外上传 `active brick -> brickLinear` 紧凑列表，shader 由 list index 反推 probe/world/voxel/cube pool 索引；Cubes 仍走 pool，Voxels 仍走稠密索引。
5. **Voxels 保持稠密**（按决策），故 cube 池索引与 voxel 稠密索引在烘焙里分离传递（`cubeIdx`/`voxelIdx`）。
6. **验证坑（重要）**：`gnb shot` 默认 90 帧内 **CPU 体素化根本没跑完**，cube 不会烘焙 → 画面主要是天空 IBL，**看不出 cube GI 是否正确**。验证 ambient cube 改动必须用大帧数（`--frames 3000`）让体素化+flush+烘焙跑完，并查 `[AmbientBrick]` 日志确认分类执行。
7. 修了一个 Phase 2 引入的潜在越界：`ClearAmbientCubeCache` 原按 `CUBE_CASCADE_MAX` 清，超出右尺寸后的区域。
8. HDRI 贴图新增按需驻留：`sys.hdrTextureStreaming=true` 时，所有 HDR 环境贴图初始只保留最低 mip；当前 `SkyIdx` 连续使用 8 帧后异步升到 full mip，闲置约 180 帧后降回最低 mip。SH 仍由缓存/CPU 数据更新到 scene dynamic buffer，bindless slot 不变。

**后续可做**：① Phase 4 稀疏 Voxels；② Phase 5 远 cascade 降分辨率；③ `VoxelData` 16B → ~8–12B（age/matId 位打包）；④ 给 HDRI residency 增加编辑器可视化/统计，或扩展到非 HDR 材质贴图的 CPU 侧需求统计。

---

> 以下为原始设计计划（保留作依据）。

## 0. 为什么不走硬件块压缩

前一版计划评估过「纹理化 + BC6H 压缩」。结论是**对本项目不划算**：
- ambient cube 是**实时持续烘焙**（时间分摊，每帧写一批 probe）。块压缩格式（BC6H/ASTC）**不能被 compute 直接写**，必须「写未压缩中间层 → 跑独立编码 pass」。这条额外的实时 BC6H 编码 pass 直接拖慢烘焙，而**烘焙速度是关键指标**。
- 移动端（低 heap 的主战场）根本没有 BC，运行时 ASTC 编码更不现实。

因此本计划转向**结构性冗余**：场景大部分区域是空的（probe 远离任何表面），而当前实现对**所有** probe 一律满额分配。消除这部分冗余既省显存、又因为「只烘焙活跃 probe」而**加快**烘焙——方向一致，无内在矛盾。

---

## 1. 现状分析（与上一版一致，结论复用）

### 1.1 数据结构（`assets/shaders/common/BasicTypes.slang`）

```c
struct AmbientCube {        // 56 B = 14 × uint32
    uint PosZ,NegZ,PosY,NegY,PosX,NegX;             // 6× 间接光, RGB10A2
    uint PosZ_D,NegZ_D,PosY_D,NegY_D,PosX_D,NegX_D; // 6× 直接光, RGB10A2
    uint skyVisibility_pznzpyny, skyVisibility_pxnxs0s1; // 8 B: 6 sky-vis + sun/spare
};
struct VoxelData {          // 16 B
    uint matId;                       // 0 = 空气
    uint age;                         // 时间累积帧数, 上限 kTraceHistoryLength=16
    uint distanceToSolid_gg_z01;      // 打包: SDF 距离 / inside / ±Z 可见度
    uint distanceToSolid_x01_y01;     // 打包: ±X ±Y 可见度
};
```

网格：`CUBE_SIZE_XY=192`, `CUBE_SIZE_Z=48` → 每 cascade `192×192×48 = 1,769,472` probe；`CUBE_CASCADE_MAX=4`。

### 1.2 内存布局（`Scene.cpp:34-44`，单块 `ambientArenaBuffer_`）

| 区域 | 单元 | 数量 | 合计 | 占比 | 性质 |
|---|---|---|---|---|---|
| **Cubes** | 56 B | 1.77M × 4 | **378.0 MiB** | 62% | 常驻 |
| Voxels | 16 B | 1.77M × 4 | **108.0 MiB** | 18% | 常驻 |
| Pages | 16 B | 64×64 | 0.06 MiB | <1% | 常驻 |
| CubesPong | 56 B | 1.77M × 1 | **94.5 MiB** | 16% | 烘焙瞬态（与 SDF scratch 别名复用） |
| SDF scratch | 16 B | 1.77M × 1 | **27.0 MiB** | 4% | 烘焙瞬态 |
| **合计** | | | **≈ 607.6 MiB** | | |

低 heap 设备：`HasFullAmbientCubeBudget()`（`Engine.cpp:117-140`）若 heap 装不下整块就**彻底关闭 GI**（降级 `ERT_LegacyDeferredNoAmbient`）。

### 1.3 决定方案的几个事实

- **F1 — 网格世界固定，不随相机移动。** offset 来自静态 `UserSettings.AmbientCubeOffset*`（`Engine.cpp:1226-1233`），无每帧相机跟随。→ **活跃 probe 集由场景几何决定、每场景静态**，稀疏分配可在 (重)构建时算一次，无需逐帧 streaming。
- **F2 — CPU 已有完整 per-probe 分类。** `FCPUProbeBaker.voxels[]` 持有每个 probe 的 `matId` 与 `distanceToSolid`（CPU 体素化 + SDF），`FCPUPageIndex::UpdateData` 已在遍历 `matId != 0` 体素（`CPUAccelerationStructure.cpp:1016-1050`）。**「probe 是否靠近表面 / 活跃」的判据数据已存在**，稀疏分类可直接复用。
- **F3 — GPU 只烘焙光照。** GPU `Render*`（`AmbientCubeBaker.slang`）写 Cubes 的光照；`matId`/距离由 CPU 填（见 `AmbientCubeBaker.slang:161` 注释）。烘焙按 `custom_data_0` 线性切片**遍历全部 probe**（`Bake.SwAmbientCube.comp.slang:12`），空 probe 也会 early-out 但**仍占线程**。
- **F4 — region 间偏移是编译期 `CASCADE_MAX` 常量。** `GPU_SCENE_AMBIENT_VOXELS_OFFSET` 等（`BasicTypes.slang:42-56`）假定 4 个 cascade 的 Cubes 之后才是 Voxels。→ **任何「少分配 / 变布局」都必须先把各区改成运行时独立地址**，否则 GPU 指针算错。
- **F5 — `PageIndex.probeDataIdx` / `voxelDataIdx` 字段已声明但全代码无人读写**（`BasicTypes.slang:241-242`）——显然为稀疏间接寻址预留、从未接线。可直接拿来用。
- **F6 — ambient 数据全程运行时烘焙、不落盘**，改布局/格式不破坏任何磁盘资产。
- **F7 — 运行时插值是 visibility-aware 8-tap**（`interpolateAmbientCubes`，靠 Voxel 距离场逐 tap 防漏光），稀疏化后这套逻辑要保留，只是 fetch 多一层 brick 间接。

---

## 2. 可挖掘的冗余

1. **空间稀疏**（最大头）：probe 只在表面附近有意义。烘焙仅处理 `distanceToSolid < 16`（约 4m）的 probe，其余恒为默认/零。典型场景活跃 probe 远小于 100%。
2. **过量 cascade 分配**：默认 `AmbientCubeCascadeCount=3`，但 arena 永远按 `CUBE_CASCADE_MAX=4` 分配（`Scene.cpp:105`）——**白白多分配 1/4**。
3. **字段冗余**：`VoxelData.age`（上限 16，1 字节够）、`matId`（≤16384，14 bit 够）占了整整 8 字节；`skyVisibility` 有 2 个 spare 字节。
4. **远 cascade 过采样**：所有 cascade 同为 192×192×48，但远 cascade（unit 翻倍、覆盖更大）本就粗糙，未必需要同等 probe 数。

---

## 3. 设计：稀疏 brick 存储

### 3.1 核心结构（沿用 buffer/BDA，不上纹理）

把每 cascade 的 `192×192×48` 划成 **brick**，边长做成可配置常量 `AMBIENT_BRICK_EDGE`。**已定起步 8³ = 512 probe**（先落地跑通，按实测活跃占比 / 采样耗时再决定是否切 4³）：
- **Brick 表（密集，小）**：每 brick 一个 `uint` slot 指针（或 `INVALID`）。8³ 时每 cascade `24×24×6 = 3456` 项 × 4 B ≈ **13.5 KB**；4³ 时约 110 KB。可塞进现有 `Pages` 体系或新开一个小 buffer，并接线 `PageIndex.probeDataIdx`（F5）。
- **Brick 池（稀疏，大头）**：只有活跃 brick 在池里占 `512 × 56 B = 28 KB`。池大小 = 活跃 brick 数 + headroom。

**Fetch（运行时）**：`probePos → brickCoord → 查表得 slot`；slot 无效返回默认零 ambient（自然对应「该处无 GI」），有效则 `pool[slot*512 + localOffset]`。8-tap 插值逻辑（§F7）不变，只是每 tap 多一次 brick 表查找（表极小，常驻 cache）。

### 3.2 分配：CPU 构建期一次性（复用 F2）

在场景 (重)构建 / `InitCascadeBakers` 之后：
1. CPU 遍历每 cascade 的 `voxels[]`，按 **`distanceToSolid < 16`（≈4m 近表面壳层，与现有烘焙阈值一致）** 标记**活跃 brick**：一个 brick 只要含任一活跃 probe 即整块分配。
2. 给活跃 brick 顺序分配池 slot，填 brick 表。
3. 按**场景预设的固定 brick 上限**一次性创建 GPU 池 buffer（不随活跃数动态重建），上传 brick 表，初始化池。
4. **池溢出**（活跃数 > 预算）优雅降级：超出的 brick 标 `INVALID`（局部丢 GI）而非崩溃，并记日志。

> 因为网格世界固定（F1），活跃集只在「场景几何变化触发重构建」时重算（MagicaLego 增删积木、编辑器改场景等已有的重建时机），**无逐帧开销**。

### 3.3 烘焙：只 dispatch 活跃 brick（省显存 + 提速）

- 当前烘焙线性遍历全部 probe（F3）。改为**遍历活跃 brick 列表**：`custom_data_0` 改成「活跃 brick 起始 index」，shader 内 `brickList[brickIdx] → 池 slot + cascade/local 坐标`。
- 收益：空 probe 不再占线程，**烘焙更快**——直接回应「烘焙速度关键」。
- ping-pong（`vkCmdCopyBuffer` 整 cascade）改为拷池的活跃区，更小。
- propagation/inject 的邻居访问需经 brick 表（邻居可能在别的 brick 或为空 → 默认零）。

### 3.4 SDF scratch 解耦（F4 相关）

DistanceField pass 现把 `CubesPong` 强转 scratch 复用。稀疏化后池布局变了，给 SDF 用**独立 scratch buffer**（现有 `AmbientSdfScratch` 区即可独立出来），不再别名 Cubes 池。

---

## 4. 分阶段计划

> 依赖链：**Phase 1（独立地址）是一切的前置**（F4）。之后右尺寸、稀疏可逐步上。

### Phase 1 — 拆分 arena 为独立 buffer + 运行时地址（前置，低风险）
把 `ambientArenaBuffer_` 拆成 Cubes / Voxels / Pong / Scratch / Pages 各自独立 buffer，`GPUScene` 为每区携带独立 device address（取代编译期 `*_OFFSET` 常量）。
- 改：`Scene.cpp`/`Scene.hpp`（多 buffer + 多地址）、`BasicTypes.slang`（`GPUScene` 指针来源改运行时地址、删 `CASCADE_MAX` 偏移耦合）、各 bake/采样 shader 的指针获取（多数已走 `GetGpuscene().Cubes` 等 property，集中改 property 实现即可）。
- 收益：本身不省内存，但**解锁**右尺寸与稀疏；顺带消除单块巨型分配的失败风险。
- 验证：build + run 见 `uploaded scene [...] to gpu`，GI 视觉零变化。

### Phase 2 — 右尺寸 cascade 分配（快、近零风险，~20% 立省）
Cubes/Voxels 按 `AmbientCubeCascadeCount`（默认 3）分配，而非恒定 4；cascade 数变化时重分配。`HasFullAmbientCubeBudget` 改按实际配置估算 → 更多设备保住 GI。
- 收益：默认 3 cascade 时 **608 → ~486 MiB**。
- 风险：低（依赖 Phase 1 的运行时地址）。

### Phase 3 — 稀疏 brick 存储 Cubes（头牌，省最多 + 提速）
落地 §3：CPU 分类活跃 brick（`distanceToSolid < 16`）+ **固定上限**池 + brick 表（接线 `probeDataIdx`）；GPU 烘焙改遍历活跃 brick；运行时 fetch 走间接。
- 收益（场景相关）：Cubes（3 cascade ~283 MiB）→ **40–100 MiB**；烘焙更快。
- **CubesPong 随之缩小**：propagation 的 ping-pong 只拷活跃池区（≈活跃占比 × 单 cascade）。**SDF scratch 解耦但保持稠密**（Voxels 本轮不动，见 Phase 4）。
- 风险：中。重点：fetch 间接的正确性与性能、固定上限溢出降级、propagation 邻居跨 brick。
- 验证：与稠密版截图 diff（重点墙角漏光 / cascade 接缝 / 反弹色）；记录活跃 brick 占比与 GPU timer。

### Phase 4（暂缓）— 稀疏 Voxels
**本轮决策：Voxels 先保持稠密不动。** 待 Cubes 稀疏稳定、并确认 DDA 热路径可接受额外间接后再评估。届时用同一活跃集稀疏化，空 brick 对 DDA 返回统一「空、跳 N 单位」默认（兼当 DDA 加速）。
- 预期收益：Voxels（3 cascade ~81 MiB）→ ~10–25 MiB。
- 风险：中（Voxels 在 DDA 热路径，需 profiling）——这正是本轮暂缓的原因。

### Phase 5（可选，正交调优）
- 远 cascade 降 probe 分辨率（如 96×96×24，8× 省）。
- `VoxelData` 16 B → ~8–12 B（`age` 4-5 bit、`matId` ~14 bit 打包）。
- 二者均为质量/性能权衡，独立可做。

---

## 5. 显存收益汇总（估算）

| 阶段 | Cubes | Voxels | Pong+scratch | 合计 | 相对现状 |
|---|---|---|---|---|---|
| 现状（4 cascade 满配） | 378 | 108 | 121.5 | ≈ 608 | 1.0× |
| Phase 2（右尺寸 3 cascade） | 283 | 81 | 121.5 | ≈ 486 | 1.25× |
| **Phase 3（+稀疏 Cubes/Pong，假设 15% 活跃；scratch 解耦保持稠密）** | ~43 | 81 | ~41 | **≈ 165** | **~3.7×** |
| Phase 4（暂缓：再稀疏 Voxels） | ~43 | ~12 | ~18 | ≈ 73 | ~8× |
| Phase 5（暂缓：远 cascade 降分辨率等） | 更低 | 更低 | — | 视配置 | — |

> 稀疏收益**强场景相关**：开阔/散布场景接近 5–10×，全封闭房间（表面铺满）可能仅 2–3×。Phase 2 的 ~20% 是**保底确定收益**。

---

## 6. 风险与开放问题

**风险**
- R1（中）：fetch 多一层 brick 间接，8-tap × 多 cascade 的随机查表——brick 表极小应常驻 cache，但需实测采样耗时。
- R2（中）：池预算 / 溢出策略——预算太小局部丢 GI，太大吃掉收益；需按场景估活跃占比并留 headroom。
- R3（中）：动态几何改变活跃集——依赖现有「重建」时机重算；连续移动物体本就是缓存 GI 的已知局限。
- R4（中）：Voxels 稀疏化触及 DDA 热路径，可能影响追踪性能（Phase 4 单独验证）。
- R5（低-中）：Phase 1 改 `GPUScene` 指针来源面广（多 shader），但多为机械替换。

**已拍板决策（owner 定）**
1. **Brick 尺寸：8³ 起步**，做成可配置常量 `AMBIENT_BRICK_EDGE`；落地后按实测活跃占比 / 采样耗时再定是否切 4³。
2. **活跃判据：`distanceToSolid < 16`（≈4m 近表面壳层）**，与现有烘焙阈值一致——漏光风险低、池大小中等。
3. **池预算：固定上限 + 溢出降级**——按场景预设上限一次性分配，超出 brick 标 `INVALID`（局部丢 GI）+ 记日志，不做运行时动态重建。
4. **Voxels：本轮保持稠密**，只稀疏 Cubes；Voxels 稀疏（Phase 4）暂缓，待 Cubes 稳定 + DDA profiling 后再评估。
5. **远 cascade 降分辨率（Phase 5）：暂缓。**

> 仍待落地后用实测数据回填的调参项：brick 尺寸是否切 4³、固定上限的具体取值（按场景活跃占比 + headroom）、活跃阈值微调。

---

## 7. 验证策略
1. **Build**：`./gnb.bat build --reconfigure`，修净编译错误（含 Slang）。
2. **Run**：`gkNextRenderer` 见 `uploaded scene [...] to gpu`，GI 正常。
3. **视觉回归**：`gkNextVisualTest`（`assets/configs/visual_test.json`）固定场景与稠密版截图 diff；重点漏光/接缝/反弹。
4. **性能**：`SCOPED_GPU_TIMER`（`sw-lightbake`/`propagation-lightbake` 等）对比烘焙耗时（应**下降**）与采样耗时；记活跃 brick 占比。
5. **显存**：打印 ambient 各 buffer `VkMemoryRequirements` 合计，对照 §5。
6. **低 heap 实测**：限制 heap 环境验证不再降级 `NoAmbient`。
7. **多场景**：稀疏收益场景相关——至少覆盖开阔 + 封闭两类。

---

## 8. 涉及文件清单

**C++（`src/Engine/`）**
- `Assets/Core/Scene.cpp` / `Scene.hpp` — 拆 buffer、多地址、池/brick 表创建与上传、右尺寸、重分配。
- `Assets/Acceleration/CPUAccelerationStructure.cpp` / `.h` — 复用 `voxels[]` 分类活跃 brick、分配池 slot、填 brick 表（接线 `probeDataIdx`）。
- `Rendering/VulkanBaseRenderer.cpp` / `.hpp` — 烘焙 dispatch 改遍历活跃 brick、ping-pong/barrier 适配新布局、SDF scratch 解耦。
- `Runtime/Engine.cpp` — `HasFullAmbientCubeBudget` 按实际配置估算。
- `Runtime/Config/UserSettings.hpp`、`EngineCVars.cpp` — 如需 brick 尺寸 / 池预算 / 阈值开关。

**Shader（`assets/shaders/`）**
- `common/BasicTypes.slang` — `GPUScene` 指针改运行时独立地址；`PageIndex`/brick 表语义。
- `common/AmbientCube.slang` — `FetchCube`/`FetchVoxel`/`interpolateAmbientCubes(Stable)` 加 brick 间接；默认零路径。
- `common/AmbientCubeBaker.slang` — 烘焙/propagation/inject 经 brick 间接。
- `Bake.SwAmbientCube / HwAmbientCube / PropagationAmbientCube / InjectAmbientCube / ClearAmbientCubeCache.comp.slang` — dispatch 索引改 brick 列表驱动。
- `Bake.DistanceField{Init,Jump,Resolve}.comp.slang` — 改用独立 SDF scratch（不再别名 Cubes 池）。
- `common/RayTracers.slang` — `inSolid`/DDA 的 Voxel 访问适配稀疏（**Phase 4，暂缓**）。

---

## 附录：已否决 / 暂缓的方向
- **纹理化 + BC6H/ASTC 实时块压缩**：编码 pass 拖慢实时烘焙、移动端无 BC、需未压缩中间层——见 §0，否决为主路径。若将来只为采样带宽，可在稀疏化之上**独立**评估「未压缩 3D 纹理 + 手动 8-tap」，与本计划不冲突。
- **离线烘焙 + BC 压缩落盘**：本系统是实时烘焙缓存（F6 不落盘），与离线预计算 GI 是不同产品形态，超出本次范围。
