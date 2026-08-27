---
title: "GI Bake 磁盘缓存（voxelize / distance field / ambient bake）开发计划"
category: plan
status: 待实施
owner: engine/rendering
created: 2026-08-17
last_updated: 2026-08-17
---

# GI Bake 磁盘缓存开发计划

目标：把 `voxelize → distance field → ambient bake` 这条每次加载都重跑、且在同一场景同一网格配置下结果
稳定的链路，做成可复用的本地磁盘缓存；提供缓存管理工具；提供一条 background 路径，可以只针对指定区块
生成缓存。实现方式要求与现有 `FCPUAccelerationStructure` / `FCPUProbeBaker` / GPU bake 调度器解耦。

## 结论

1. **缓存单元 = tile：`(cascade, tileX, tileZ)`，tile 覆盖 16×16 XZ voxel、全 48 层 Y。**
   这正是现有 voxelization 的工作单元（`CPUAccelerationStructure.cpp:604` `groupSize = 16`，
   `AsyncProcessGroup` 一次处理 16×16×48 个 voxel），也正好是 brick 的整数倍
   （16/8 = 2，48/8 = 6 → 每 tile 24 个 brick）。因此同一个 tile 坐标同时是：CPU voxel 任务单元、
   voxel 存储的连续 brick 集合、cube pool 的 brick 集合。**不要引入第三套"区块"坐标系。**
   每 cascade 12×12 = 144 tile。
2. **两个独立 key 域，不合并：`geomKey` 决定 voxel/DF 缓存，`lightKey` 决定 ambient cube 缓存。**
   voxel 与 DF 只依赖几何（`VoxelizeCube` 只投六根轴向 ray，只读 material model 判断
   `DiffuseLight`/背面）；cube 还依赖灯光、太阳、天空、材质颜色。合并成一个 key 会导致"只改了太阳角度"
   就把最贵的 voxelization 结果一起丢掉。`lightKey` 定义为 `hash(geomKey, 光照摘要, bake 算法版本)`。
3. **解耦形态：新增 `src/Engine/Assets/Acceleration/GiCache/` 三层，互不反向依赖。**
   - `GiCacheKey`：纯计算。输入 snapshot / 材质 / 灯光 / 网格参数，输出 digest。无 IO、无 Vulkan。
   - `GiCacheStore`：纯 IO + 编解码。只认识 `FGiCacheTileId` 和字节 span，不认识 baker / Scene。
   - `GiCacheCoordinator`：唯一知道"引擎链路时点"的一层，把上面两层接到 `FCPUAccelerationStructure`
     现有的 5 个时点上。
   `FCPUProbeBaker` / `FCPUBrickTable` 只新增**哑**的 tile 拷入/拷出与 dirty 清除方法，
   不含路径、hash、序列化、压缩逻辑。`Scene` 与 `VulkanBaseRenderer` 不新增缓存概念。
4. **DF 是整 cascade sweep，所以 tile 里存的是"DF 之后"的 `VoxelData`。**
   `RebuildDistanceField()`（`ProbeBaker.cpp:237`）用两轮 chamfer 双向扫描整条 cascade，最大传播距离
   15 格，跨 tile 边界。规则：**同一 cascade 的 144 个 tile 全部命中 → 跳过该 cascade 的 DF；
   有任何一个 miss → 该 cascade 正常重跑 DF**（现在是无条件对所有 cascade 跑，本身就该改成 per-cascade）。
   `distanceToSolidSeeds` 完全可由 `matId > 0 ? 0 : 15` 重建（`ProbeBaker.cpp:291` 就是这个定义），
   **不入盘**。
5. **cube 缓存按 logical brick 存，不按 pool slot 存。**
   slot 是 `FCPUBrickTable::UpdateData` 每次重新压缩分配的运行时产物（`BrickPageTable.cpp:176-196`），
   载入时必须经 `brickTable[cascade * 3456 + brick]` 映射到当帧 slot 再写入。
   `sampleCount`（打包在 `SkyVisibilityPXNXS0S1.z`）一起存，累积权重语义
   （`AmbientCubeBaker.slang:322-323`）才能延续。
6. **cube 缓存的语义是"一份已收敛的 bake 结果"，不是逐位复现。**
   bake 的随机种子含 `camera.TotalFrames`（`Bake.SwAmbientCube.comp.slang:25`），
   并且是按 `AmbientCubeAccumulationWeight(sampleCount)` 逐帧混合的，所以两次运行本来就不 bit-identical。
   缓存命中后默认不再 bake（`sys.giCache.cubeRefreshPasses = 0`），需要对比时用该 cvar 保留 N 轮收敛。
   **不要**在文档或代码注释里承诺 cube 缓存 bit-exact。
7. **只有 full bake（场景载入后的第一次）参与缓存；增量 dirty 更新一律不写盘。**
   写入两个时点：voxel/DF 在 flush 分支（`CPUAccelerationStructure.cpp:855-881`）落盘；
   cube 在 GPU bake 收敛（`ambientBakeIdle_` 置位）后的下一次 `Tick` 里读回落盘。
   增量路径（`MarkDirtyBounds`、hit-driven residency 换入换出）产生的是运行时状态，缓存它会污染
   "干净首帧"的语义。
8. **background 两条路径，共用同一份 tile 级 API。**
   - 引擎内：落盘走 TaskCoordinator 的并行任务（非渲染线程），主线程只做一次 mapped memory 读取。
   - 离线：新增极薄 util target `GiCacheBaker`（参考 `src/Application/Util/Packager` 64 行的形态），
     headless、隐藏窗口、无 per-frame 预算，加载场景 → 按 job 只烘指定 tile → 落盘 → 退出；
     由 `gnb gicache bake` 编排（复用 `gnb shot` / `gnb validate` 已有的 headless 启动与控制通道模式）。
   **不把 bake 模式塞进 `gkNextRenderer`**（它是三个 release target 之一）。
9. **管理工具：`gnb gicache list|info|bake|verify|prune|clear|status`；per-scene `manifest.json` 只是给工具用的索引，引擎运行时只认二进制 tile 文件。**
   manifest 丢失或过期时 `gnb gicache list/info` 必须能从目录扫描重建，不能因为 manifest 不一致就报错。
10. **失效策略：header 严格匹配，不做兼容读。**
    header 里写入 format version、`sizeof(VoxelData)`/`sizeof(AmbientCube)`、`CUBE_SIZE_XY/Z`、
    `GPU_SCENE_AMBIENT_BRICK_EDGE`、cascade 的 `UNIT_SIZE`/`CUBE_OFFSET`、key、payload CRC。
    任一不匹配 → 当作 miss（并按需删除），**不写向后兼容的读路径**。

## 当前实现盘点

### 链路三段与时点

| 阶段 | 位置 | 触发 | 并行方式 |
| --- | --- | --- | --- |
| voxelize | `ProbeBaker.cpp:167` `VoxelizeCube` / `:266` `ProcessCube` | `AsyncProcessGroup`（`CPUAccelerationStructure.cpp:670`）每组 16×16×48 | TaskCoordinator 并行任务，每 voxel 独立 |
| distance field | `ProbeBaker.cpp:237` `RebuildDistanceField` | flush 分支里每 cascade 一个任务（`CPUAccelerationStructure.cpp:843-853`） | 每 cascade 一个任务，内部串行两轮双向扫描 |
| ambient bake | `VulkanBaseRenderer.GiBake.cpp:91` `BakeAmbientCubeCascade` | 每帧一条 cascade、按 `r.ambientCube.bakeTargetFps` 和整体帧耗时自适应控制 group 数 | GPU compute，`convergencePasses` 轮 |

驱动时点：

- `Scene::RebuildMeshBuffer` → `InitBVH`（`Scene.Build.cpp:276`）→ `InitCascadeBakers`。
- `Scene.Build.cpp:699-710`：`levelVoxelBakePending_` 消费点，调用 `AsyncProcessFull`。
- `AsyncProcessFull`（`CPUAccelerationStructure.cpp:635`）：清队列 → `ClearAmbientCubes` + 上传 →
  `QueueFullProbeBake`（`:602`，把 144×cascade 个组 shuffle 后入队，每 cascade 末尾一个 Fence）。
- `Scene.Update.cpp:558-576`：每 30 帧一次 `Tick`。
- `Tick`（`:759`）：退役上批 → 派发下批 → `voxelizationDrained` 后跑 DF → 上传 voxel/page/brick table →
  `MarkAllActiveDirty()` → GPU bake 通过 `AmbientBakeDirtyRevision` 拿到活。
- 收敛：`GiBake.cpp:175-183` 调 `AcknowledgeAmbientBake(revision)` → `ambientBakeIdle_ = true`。

### 数据结构与尺寸

`VoxelData` 8 B（`matId` + 打包的 8×4bit distance/inside word）；`AmbientCube` 40 B；
voxel 存储是 brick swizzle 的（`CPUAccelerationStructure.Internal.hpp:26` `GetVoxelAddress`），
**voxel 的 brick 线性号 = `voxelIndex / 512`**（`BrickPageTable.cpp:74-77` 已经在用这个性质）。

| 单位 | voxel | cube |
| --- | --- | --- |
| 1 brick (8³ = 512) | 4 KiB | 20 KiB |
| 1 tile (24 brick) | 96 KiB | ≤ 480 KiB |
| 1 cascade | 1,769,472 voxel = 13.5 MiB | pool = `ceil(3456 × sys.ambientCubePoolBrickRatio)`，0.5 → 1728 brick = 33.75 MiB |
| 默认 3 cascade | 40.5 MiB | ≤ 101 MiB（只存 active brick，实际远小于此，M0 需实测） |

ambient arena 是 `DEVICE_LOCAL | HOST_COHERENT`（`Scene.cpp:266-277`），已经在 `Tick` 里被 `Map()`
读写（residency 读回、slot 清零），**所以 cube 读回不需要 staging buffer**；但它是 BAR 内存，
读带宽远低于系统内存，只能按 active brick 读、且只在收敛后读一次。

### 确定性分析

- voxelize：tinybvh BVH 构建确定、每 voxel 六根轴向 ray 独立、线程数不影响结果 → **确定**。
- DF：纯函数（输入 seeds，输出 nibble x）→ **确定**。
- brick 分类/分配：候选集 = 占据 brick 膨胀 3（`Internal.hpp:13`）；`hitDriven` 关闭（默认）时
  `requested[b]` 恒为 1，slot 按 brick 升序压缩分配 → **确定**。开启 hit-driven 后 slot 不确定，
  这也是必须按 logical brick 而不是 slot 存 cube 的原因。
- cube bake：随机种子含 `TotalFrames`、结果是逐帧混合 → **不 bit-deterministic，只统计收敛**。

### 已有先例

`CPUAccelerationStructure.cpp:186-205` 已经在给大 BLAS 做磁盘缓存：xxhash 顶点数据 → 
`Utilities::CookHelper::GetCookedFileName(hash, "cpubvh")` → 存在则 `Load` 否则 `Build` + `Save`。
本计划沿用同样的"hash 命名 + 存在即读"思路，但 **不复用 `cooked/` 目录**：GI 缓存体积大、需要独立的
prune/clear/list，混进 texture/BVH cook 目录会让管理工具很难做安全的删除。

## 目标与非目标

目标：

- 同一场景 + 同一网格配置的第二次加载，voxel/DF 不重算、cube 不重烘，首帧即有 GI。
- 缓存粒度到 tile，允许部分命中：命中的 tile 直接拷入，miss 的 tile 走原路径。
- 可以在游戏之外，对指定 `(scene, cascade, tileX, tileZ)` 集合离线生成缓存。
- 有 CLI 能看到缓存有什么、占多大、覆盖率多少、能删。

非目标（本计划明确不做）：

- 不实现相机跟随的 recenter/clipmap（`designs/gi-cache-architecture.md` 已把它列为未实现）。
  缓存正确性依赖网格世界锚定在原点这一现状，一旦引入 recenter，tile 坐标必须改为世界格坐标。
- 不缓存 SHARC（PathTracing 路径），不缓存 residency 运行时状态，不缓存 page index
  （`FCPUPageIndex::UpdateData` 只是从 voxel 派生的 64×64 占据标记，重算比读盘便宜）。
- 不做跨机器/跨平台缓存分发（key 里含浮点网格参数与算法版本，跨 GPU 的 cube 结果不保证一致）。
- 不动 `VoxelData` / `AmbientCube` 的 GPU 布局。

## 架构

```
src/Engine/Assets/Acceleration/GiCache/
  GiCacheKey.hpp/.cpp          # FGiCacheGridDesc, FGiCacheTileId, ComputeGeometryDigest/ComputeLightingDigest
  GiCacheFormat.hpp            # FGiTileHeader, magic/version 常量, payload 布局 static_assert
  GiCacheCodec.hpp/.cpp        # EGiCacheCodec { Raw, ZeroRle, LZ4 }, Encode/Decode
  GiCacheStore.hpp/.cpp        # IGiCacheStore + FGiCacheDiskStore（原子写、目录布局、manifest）
  GiCubePoolAccessor.hpp/.cpp  # 对 mapped ambient arena 的 scoped brick 读写，只依赖 Vulkan::DeviceMemory
  GiCacheCoordinator.hpp/.cpp  # 唯一接触引擎时点的一层
  GiCacheBakeJob.hpp           # FGiCacheBakeJob { cascadeMask, tile 列表, 写模式 }
```

依赖方向严格单向：`Coordinator → {Key, Store, CubePoolAccessor, Codec}`；`Store → {Format, Codec}`；
`Key → Format`。`FCPUAccelerationStructure` 只持有 `std::unique_ptr<FGiCacheCoordinator>`（前向声明，
不污染 `CPUAccelerationStructure.hpp` 的包含面）。

### 对现有类新增的哑接口（全部无 IO）

```cpp
// FCPUProbeBaker（ProbeBaker.cpp）
void CopyTileOut(int tileX, int tileZ, std::span<VoxelData> dst) const;   // 24 brick，按 brick 升序
void CopyTileIn(int tileX, int tileZ, std::span<const VoxelData> src);    // 同时重建 distanceToSolidSeeds

// FCPUBrickTable（BrickPageTable.cpp）
void ClearDirtyBricks(std::span<const uint32_t> globalBrickIndices);      // 命中的 brick 不参与 GPU bake

// Internal.hpp（纯索引数学，和 GetVoxelAddress 放在一起）
constexpr uint32_t kGiTileEdgeXZ = 16;
constexpr uint32_t kGiTileBrickCount = 24;
void ForEachTileBrick(int tileX, int tileZ, auto&& fn);                   // 产出 24 个 local brick 线性号
```

### 数据流

**读（load 路径，在 `AsyncProcessFull` 内）**

1. Coordinator 用当前 snapshot + 材质表 + 网格配置算 `geomKey`；`sys.giCache.mode` 允许读时继续。
2. 对每个 `(cascade, tileX, tileZ)`：`store.TryLoadVoxelTile(id)`。
   - 命中 → `baker.CopyTileIn(...)`，计 hit，**不入 `needUpdateGroups`**，
     `completedVoxelGroups_` 自增（进度 UI 保持正确）。
   - miss → 照原样入队。
3. 该 cascade 全命中 → 不安排 `RebuildDistanceField`；否则安排（改成 per-cascade 列表）。
4. flush 阶段结束、`brickTable` 建好后：对每个 tile 全命中且 `lightKey` 命中的 tile，
   经 `GiCubePoolAccessor` 把 cube 写进对应 slot，并 `ClearDirtyBricks(...)`。
   剩下的 dirty brick 交给 GPU bake，行为与今天完全一致。

**写（store 路径）**

1. voxel：flush 分支上传完成后，Coordinator 把每个"本次实际算过"的 tile 拷出到 payload，
   投递到并行任务序列化 + 原子写盘。
2. cube：`ambientBakeIdle_` 从 false → true 的那次 `Tick`，按 active brick 读回 mapped 内存
   （一次 `Map` 覆盖整条 cascade pool 区间，循环内不重复 map），再交给并行任务落盘。
3. 两者写完后更新 `manifest.json`（tmp + rename）。

### 缓存文件布局

```
<writable runtime root>/gicache/
  <geomKey:016x>/
    manifest.json                        # 场景标签、网格参数、key、tile 覆盖、时间戳、字节数
    voxel/v<cascade>_<tileX:02d>_<tileZ:02d>.gnvox
    cube/<lightKey:016x>/c<cascade>_<tileX:02d>_<tileZ:02d>.gncube
```

`geomKey` 目录让 `gnb gicache clear --scene` 变成一次目录删除；cube 按 `lightKey` 再分一层，
使"只改光照"不牵动 voxel 缓存，也让 prune 可以只清旧 lightKey。

tile 文件 header（固定 64 B，`GiCacheFormat.hpp` 里用 `static_assert` 锁死）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| magic | u32 | `'GNVX'` / `'GNCB'` |
| formatVersion | u32 | 结构或算法变更即 +1 |
| geomKey / lightKey | u64 | 内容 key，读时必须相等 |
| cascade, tileX, tileZ | u16×3 | 冗余存放，防错位读 |
| unitSize | f32 | 该 cascade 的 `UNIT_SIZE` |
| cubeOffset | f32×3 | 该 cascade 的 `CUBE_OFFSET` |
| elementSize | u16 | `sizeof(VoxelData)` / `sizeof(AmbientCube)` |
| brickEdge, brickCount | u16×2 | 8 / 24 |
| codec | u8 | Raw / ZeroRle / LZ4 |
| payloadBytes, payloadCrc | u32×2 | 解码前校验 |

`.gncube` 的 payload 前置一个 24 bit 的 brick 存在位图（哪些 brick 有数据），只写有数据的 brick。

## 里程碑

### M0 度量与决策门

- 给三段各加 `SCOPED_CPU_TIMER` / 日志：full bake 的 voxelize 总耗时、DF 每 cascade 耗时、
  GPU bake 从 revision 开始到 converged 的墙钟帧数与累计 GPU ms；同时打印每 cascade 的
  active brick 数和 candidate/overflow。
- 在 `playground.glb` 与一个大场景（TruckerDemo/NextDayz 级别）上各测一次，记录到本文件的"实测"段。
- **决策门**：若 voxelize 占比 <30% 且 cube 收敛 <300 帧，则只做 M1（voxel/DF），M2 降级为可选。

### M1 voxel + DF tile 缓存（读写闭环）

- 新增 `GiCacheKey` / `GiCacheFormat` / `GiCacheCodec(Raw+ZeroRle)` / `GiCacheStore` / `GiCacheCoordinator`。
- `geomKey`：对每个参与 bake 的 instance 算 `hash(模型三角形 hash, world transform 浮点位, matIdxs,
  participation)`，再用与顺序无关的方式（逐项 hash 后求和/异或 + 计数）合并；并入网格参数与 format version。
  模型三角形 hash 复用 `InitBVH` 里已有的 xxhash（提升为对所有模型都算，缓存在 `FCPUBLASContext`）。
- `FCPUProbeBaker::CopyTileIn/Out`、`ForEachTileBrick`。
- `AsyncProcessFull` 接入读路径；DF 改为 per-cascade 条件调度。
- flush 后写路径（只在 `fullProbeBakePending_` 为真的那次）。
- 新增 cvar：`sys.giCache.mode`（off/read/readwrite，默认 readwrite）、`sys.giCache.dir`。
- 单测（`src/Tests/Test_GiCacheStore.cpp`）：
  - header 不匹配（version/elementSize/unitSize/key）一律 miss；
  - `CopyTileOut → 序列化 → 反序列化 → CopyTileIn` 后 voxel 数组逐字节相等，且 seeds 正确重建；
  - ZeroRle 编解码往返、CRC 损坏检测；
  - key 对 instance 顺序不敏感、对 transform 微小变化敏感。
- 验收：同场景第二次加载，日志显示 `voxel tile hit 432/432`、DF 被跳过；
  `gnb shot --scene <X> --frames 60` 与冷启动 3000 帧的截图在 voxel/SDF 可视化下一致。

### M2 ambient cube tile 缓存

- `lightKey`：`hash(geomKey, 灯光数组, EnvironmentSetting 的太阳/天空/HDR 名, 参与材质的 albedo/emissive/model,
  bakeAlgorithmVersion)`。
- `GiCubePoolAccessor`（scoped map，一次覆盖一条 cascade pool）。
- 读：flush 建好 brickTable 后写入 slot + `ClearDirtyBricks`。
- 写：`ambientBakeIdle_` 上升沿读回 active brick。
- **必须一并修的现存缺口**：`GiBake.cpp:117-127` 在 `dirtyBrickTotal == 0` 时直接 return，
  不会调 `AcknowledgeAmbientBake`，因此"全部命中缓存"这条新路径下 `ambientBakeIdle_` 永远不置位，
  `Tick` 会每 30 帧继续跑一次 residency 重建。修法：`dirtyBrickTotal == 0` 时也 Acknowledge 当前 revision。
- 新增 cvar：`sys.giCache.cubeRefreshPasses`（默认 0）。
- 验收：第二次加载首帧即有间接光；`gnb shot --frames 60` 与冷启动收敛后的截图 RMSE 低于
  visual test 现有阈值；`sys.giCache.cubeRefreshPasses=1` 后画面无跳变（证明 sampleCount 语义正确）。

### M3 管理工具 `gnb gicache`

- `tools/gnb/internal/gicache/`：目录扫描、header 解析（Go 侧重写 header 结构，与 C++ 保持一份
  文档化布局；用一个共享的 golden 文件做交叉校验测试）。
- 子命令：
  - `list`：按场景列出 geomKey、标签、cascade 覆盖率、字节数、最后更新时间。
  - `info <geomKey|scene>`：12×12 ASCII 覆盖图（voxel / cube 分别一张），列出 lightKey 变体。
  - `verify [--scene]`：校验 magic/version/CRC，报告坏文件；`--fix` 删除坏文件。
  - `prune [--older-than 30d] [--max-size 8G] [--keep-latest-light]`。
  - `clear [--scene <X>] [--all]`。
- 文档：`docs/guides/gnb-cli.md` 增一节；`docs/README.md` 索引本计划。
- 验收：`gnb gicache list` 在冷/热两种状态下输出正确；`prune` 只动 `gicache/` 目录；
  Go 侧 header 解析与 C++ 写出的 golden 文件一致（`gnb` 自带 test）。

### M4 background 生成（指定区块）

- `FGiCacheBakeJob`：`{ cascadeMask, tiles(list of (cascade,tileX,tileZ) 或 rect), mode: voxel|cube|both }`。
- `FCPUAccelerationStructure::AsyncProcessJob(scene, job)`：只入队 job 里的 tile；
  DF 在这种模式下必须整 cascade 跑（因为 tile 边界会被邻居影响），**并且只在 job 覆盖整条 cascade 时
  才允许写 voxel tile 缓存**——部分 tile 的 DF 依赖尚未烘的邻居，写盘会固化错误的 clearance。
  这是本里程碑最容易做错的一点：区块级 *voxelize* 可以局部，区块级 *DF* 不可以。
  折中方案：job 至少以 cascade 为单位收敛 DF，tile 粒度用于分批推进和进度恢复，而不是缩小 DF 范围。
- 新 target `src/Application/Util/GiCacheBaker`：解析 `--scene/--cascade/--tiles/--mode`，
  headless + 隐藏窗口，把 `r.ambientCube.bakeTargetFps` 调低以优先完成 bake、关闭 hit-driven residency，
  轮询直到 `HasPendingWork()` 为假且 cube 收敛，落盘后退出（非零退出码表示未完成）。
- `gnb gicache bake --scene X [--cascade N] [--tiles x0,z0-x1,z1] [--mode both] [--background]`：
  编排进程、把 job 切成批次、写进度 json 到 `out/build/<preset>/gicache/jobs/<id>.json`；
  `gnb gicache status` 读它。`--background` 时 detach 并把日志落到同目录。
- 验收：删除某个 tile 的缓存文件 → `gnb gicache bake --tiles` 只重建该 tile 所在 cascade →
  `gnb gicache info` 覆盖率回满；`--background` 期间 `status` 能看到推进。

### M5 收尾与可选增强

- codec：评估在 `vcpkg.json` 加 `lz4`（跨 Windows/Linux/macOS/Android/iOS，解压 96 KiB 是微秒级），
  实测压缩比与 ZeroRle 对比后决定是否切默认。
- DevTools：`GraphicsDebugPanel` 加一段缓存命中/字节数/key 显示（放在 Modules/DevTools，不进 Engine）。
- 可选：visual test 增加一条"冷 vs 热缓存"对比场景；`gnb gicache verify` 进 CI（只在有缓存时运行）。

## 新增配置面

| 名称 | 位置 | 默认 | 说明 |
| --- | --- | --- | --- |
| `sys.giCache.mode` | EngineCVars | `readwrite` | off / read / readwrite |
| `sys.giCache.dir` | EngineCVars | 空 = writable root/gicache | 覆盖缓存目录 |
| `sys.giCache.cubeRefreshPasses` | EngineCVars | `0` | 命中后仍执行的收敛轮数 |
| `sys.giCache.maxBytes` | EngineCVars | `8589934592` | 引擎侧写盘上限，超过则跳过写并告警 |
| `--gi-cache-job <file>` | GiCacheBaker | — | job 描述文件 |
| `gnb gicache …` | gnb | — | 见 M3 / M4 |

## 风险与已知边界

1. **网格参数运行时可改，缓存却是加载期产物。** `sys.ambientCube*` 的注释已经说明改动只在下次 level load
   生效（`EngineCVars.cpp:116-117`）。key 里必须含 `UNIT_SIZE`/`CUBE_OFFSET`/cascade 数与 ratio，
   并且以 **`committedAmbientGrid_`（实际建 baker 用的那份）** 为准，不能读 pending 的 settings。
2. **程序化/运行时改动的场景**（MagicaLego、SCAD 热重载、terrain 生成）会每次产生新 geomKey，
   缓存只会增长不会命中。对策：`sys.giCache.maxBytes` + `gnb gicache prune`，并在这些 app 的
   config 里默认把 mode 设为 `read`（不写）。这点必须在 M1 的验收里显式检查一遍 MagicaLego。
3. **BAR 内存读回慢。** 只在收敛后读一次、只读 active brick、一次 map 覆盖整条 cascade。
   若 M0 实测读回超过 100 ms，把读回也切到并行任务里分帧做。
4. **hit-driven residency 开启时 slot 不稳定**，这是按 logical brick 存 cube 的前提；
   离线 baker 必须强制关闭 hit-driven，否则会只烘到"当时被看到"的 brick。
5. **DF 的跨 tile 传播**是这套设计里唯一真正的耦合点。M4 的折中（tile 用于分批、DF 以 cascade 为单位）
   必须写进代码注释，否则后人很容易"优化"成局部 DF 并悄悄产生穿墙的 clearance。
6. **并发写**：游戏内写盘与离线 baker 可能同时进行。tile 文件独立 + tmp/rename 原子替换即可；
   manifest 是 advisory，最后写者赢，工具侧必须能从目录重建。
7. **bake 算法变更不会自动失效 cube 缓存。** 只有手动 bump `bakeAlgorithmVersion` 才失效。
   改 `AmbientCubeBaker.slang` 的人必须同时 bump —— 在 slang 文件顶部加一行注释指向该常量。

## 验证方式

- 单测：`gkNextUnitTests` 新增 `Test_GiCacheStore.cpp`（格式/编解码/key），
  `Test_GiCacheTileMapping.cpp`（tile↔brick↔voxel 索引往返，含 `GetVoxelAddress` 一致性）。
- 构建：Engine 层改动按 AGENTS 的 targeted build 规则 → `gnb build`（`gkNextRenderer` + `gkNextUnitTests`）；
  M4 新增 target 时额外 `gnb build GiCacheBaker`。
- 视觉：`gnb shot --scene assets/models/playground.glb --frames 60` 冷/热两次对比；
  大场景走 `gkNextVisualTest` baseline。
- 日志断言：热启动必须出现 `[GiCache] voxel hit N/M`、`cube hit N/M`，且**不出现**
  `Ambient bake revision ... started` 的大 dirty brick 数（全命中时）。
- gnb：`gnb gicache` 各子命令的 Go 单测 + header golden 交叉校验。

## 实测（M0 填写）

待 M0 完成后补：各阶段耗时、active brick 数、缓存实际字节数、读回耗时、决策门结论。
