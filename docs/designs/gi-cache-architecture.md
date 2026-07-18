---
title: "GI 缓存与体素资源架构"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-17
last_updated: 2026-07-17
---

# GI 缓存与体素资源架构

引擎当前有两套用途不同的 radiance cache：software paths 使用 VoxelData + AmbientCube，PathTracing 可使用 SHARC。它们共享部分 scene 准备，但不是一个“统一 GI cache”，也没有实现旧设计中的相机跟随统一 clipmap。

## 为什么使用 storage buffer + 稀疏 brick

AmbientCube 是运行时持续分帧写入的缓存。BC6H/ASTC 等硬件块压缩格式不能作为普通 compute 随机写目标；采用它们需要额外的未压缩中间层和实时编码 pass，既没有消除写入工作集，也会拖慢 bake，且 BC 在移动端并非通用方案。因此当前方向是保留 BDA/storage-buffer 路径，通过 right-size cascade、稀疏 cube brick 和 active-list 同时减少常驻容量与无效 dispatch。

RGB9E5 是单个 32-bit radiance 值的编码选择，不是块压缩。它保持 shader 可直接写，并改善相对 HDR 范围；不要把“已使用 RGB9E5”误解成 arena 已可采样 BC/ASTC texture。

## Renderer 使用关系

- SoftwareTracing、SoftwareModern 和 VoxelTracing 请求 Voxel + Ambient。
- PathTracing 的 contract 声明 Voxel、Ambient、TLAS、SHARC；当 SHARC 实际启用时，`ShouldSkipAmbientCubeUpdates()` 会跳过 PT 的 AmbientCube 更新。
- `r.sharc.enable` 的用户默认值当前为 true，但 offline progressive path tracing 会令 `IsEffectiveSharcEnabled()` 返回 false。判断运行时行为要看 effective 值，不只看归档 CVar。
- SoftwareModernNoAmbient 不请求任何 scene GI resource；它使用直接/IBL/CSM 与屏幕空间 GTAO，不会因为 VoxelData 存在就自动获得体素天光遮蔽。

## Ambient arena

`Scene` 为实际分配的 cascade capacity 计算 ambient arena 布局，并通过一个 80-byte `AmbientResources` 地址表把各 region 暴露给 shader，从而保持 `GPUScene` 为 128 bytes。不要恢复依赖编译期固定 offset 的寻址。

当前 region 包括：

- `Cubes`：每 cascade 的稀疏 AmbientCube brick pool。
- `Voxels`：仍为每 cascade dense 的 VoxelData 数组。
- `Pages`：software DDA 的 page index。
- `CubesPong`：仅一条 cascade 大小的 bake ping-pong scratch。
- `BrickTable`：逻辑 brick → pool slot。
- `ActiveBrickList`：本帧参与 bake 的逻辑 brick 列表。
- `Residency`：consumer/bounce 最近命中帧与计数。

`VoxelData` 当前为 8 bytes：完整 `matId` 加一个打包的 8×4-bit distance/inside word。`AmbientCube` 为 40 bytes：六个方向色、sun direct、sky/emissive direct 以 RGB9E5 保存，另有六方向 8-bit sky visibility。不要引用旧文档中的 16-byte VoxelData、56-byte AmbientCube 或 RGBA8 face 布局。

Scene 构造时按当时的 `sys.ambientCubeCascadeCount` 固定本 scene 的 cascade capacity；运行中把 CVar 调大不会越界，但新增 capacity 要到 scene reload 才分配。cube pool 大小由 `sys.ambientCubePoolBrickRatio` 转换成每 cascade 固定 slot 上限，因此同一配置下“更稀疏的场景”主要减少 active/bake 工作，不会继续缩小已经创建的 Vulkan allocation。评估显存必须看启动时 `[AmbientArena]` 日志，不能只看 active brick 数。

## 稀疏 brick 分配

逻辑网格仍是 `192×192×48`，以 `8×8×8` probe 组成 brick。CPU 从占据 voxel 加 dilation 得到候选集，`FCPUBrickTable` 尽量保留旧 slot，再按稳定的逻辑 brick 顺序分配空 slot。slot owner 改变时必须清零对应 cube，防止新 brick 读到旧 owner 的 radiance。

候选数超过固定 pool 时，未分配 brick 保持 invalid 并沿明确 fallback/更粗 cascade 取样；不能让 pool index wrap、覆盖已有 owner 或把缺页默认为一块随机旧 radiance。降低 pool ratio 是画质/显存取舍，不是无损压缩开关。

命中驱动是可选的二次筛选，不改变候选集合定义；详细 grace/evict/marking 语义见 [AmbientCube 命中驱动驻留](ambientcube-hit-driven-residency-design.md)。当前没有 GPU free-list，也没有把 dense VoxelData 一起稀疏化。

## Bake 与消费

camera-independent scene prepare 每帧只运行一次。AmbientCube bake 每帧轮转一条 cascade，只 dispatch active brick 对应的 probes；hardware/soft bake 共用 active-list 解码。消费 shader 先由 cascade/逻辑 probe 找 `BrickTable`，缺页时使用明确 fallback，不能直接把逻辑 voxel index 当 pool index。

AmbientCube 同时服务间接光、sky visibility、Splat 的可选场景光照以及 software tracing。修改 packed layout、brick edge 或 cascade 参数时必须同步 CPU static_assert、arena sizing、shader 寻址、readback 和 Splat consumer。

CPU voxelization、brick classification、flush 与首轮 bake 可能远晚于应用启动。默认短帧 `gnb shot` 可能只看到 sky IBL，却被误判为 AmbientCube 正常；验证此路径要把 `--frames` 提高到足以出现分类/bake 日志（复杂场景历史上常用约 3000 帧），并确认 `[AmbientArena]`、brick classification/overflow 与对应 GPU timer，而不是只凭最终截图。

## HDR 环境贴图驻留

`sys.hdrTextureStreaming=true` 时，HDR 环境贴图以 lowest mip 起步；当前 active `SkyIdx` 连续需求 8 帧后异步升为 full mip，离开 active 状态 180 帧后降回 lowest mip。bindless texture id 保持不变，payload/SH 在完成回调中刷新。关闭 streaming 会请求所有 HDR texture 升为 full mip。

这套策略减少未使用 sky 的显存，与 AmbientCube brick residency 是两套独立状态机。修改阈值或异步替换时要验证当前 sky 不闪黑、SH 与 texture 同步、scene 切换和 shutdown；不要把 HDR texture 的“命中”接入 AmbientCube residency 计数。

## SHARC 边界

SHARC 使用 vendored NVIDIA header，经 `assets/shaders/common/Sharc.slang` 适配 BDA/Slang。通用 path loop 只依赖 `IRadianceCache`：

- `FNullRadianceCache`：普通 path，无 cache 行为，编译期内联消失。
- `FSharcUpdateCache`：稀疏选像素，记录 surface/direct/throughput/miss。
- `FSharcQueryCache`：在 bounce、材质 roughness 和 segment length 条件满足时用缓存 radiance 终止路径。

不要把 SHARC 接口塞回 `FPathTracingRenderer` 的通用状态，也不要让 software renderer 假装使用 SHARC。SHARC 更新、resolve、query 的资源由 PathTracing 路径拥有；AmbientCube 仍是 software paths 的独立方案。

## 明确未实现

- 相机 recenter 的 ring-addressed unified voxel/AmbientCube clipmap。
- BVH dirty-region 驱动的完整局部增量闭环。
- VoxelData 内 sun visibility/emissive 的额外 GI-lite 字段。
- NoAmbient 的 voxel sky-visibility soft trace。

这些旧设计可以作为历史研究，但不是当前 roadmap。若重新立项，应先用现有 arena/brick/effective renderer contract 建立基线，不能从旧结构尺寸或旧阶段继续施工。
