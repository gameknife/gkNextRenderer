---
title: "AmbientCube 命中驱动驻留"
category: design
status: 已实现（可选路径）
owner: engine
created: 2026-06-22
last_updated: 2026-07-17
---

# AmbientCube 命中驱动驻留

该功能在既有几何候选 brick 上增加实际查询命中记录，用 grace/evict 窗口决定哪些 brick 占用稀疏 pool。它没有把 AmbientCube 改成 SHARC，也没有实现全 GPU free-list；当前默认仍关闭命中驱动模式。

## 当前数据流

1. `Scene` 的 ambient arena 为每个 cascade 分配 `AmbientBrickResidency`，记录 consumer/bounce 的最近命中帧和计数。
2. `assets/shaders/common/AmbientCube.slang` 在消费侧按 `hitMarkTileRatio` 稀疏标记命中；bake bounce 也记录，但默认不参与保活。
3. `CPUAccelerationStructure.cpp` 周期性 readback 驻留数据，并调用 `FCPUBrickTable::UpdateData` 重新分类。
4. `BrickPageTable.cpp` 保留仍请求的旧 slot、给新请求分配空 slot，并把被换主人的 slot 加入清零列表。
5. `VulkanBaseRenderer.GiBake.cpp` 清理复用 slot，避免读到前任 brick 的 cube 数据。

几何占据+dilation 仍是候选集合上界。启用命中驱动后，一个候选 brick 在首次出现后的 grace 期内驻留；之后只有最近 consumer 命中（或显式允许的 bounce 命中）才能在 evict 窗口内保活。

保留候选集合与 grace 是为了解决“探针尚未烘焙就不可能被消费”的启动闭环；仅按命中从空池开始会永久漏掉新区域。默认忽略 bounce 保活，则是为避免 bake 自己命中邻居后把 active set 反馈扩张到接近几何全集。这两项不是随意调参，修改前要分别验证快速镜头移动和封闭室内多次反弹。

`AmbientBrickResidency` 是每逻辑 brick 16 bytes，分别记录 consumer/bounce 的 last-hit frame 与累计 count。消费标记按确定性 hash/tile 比例稀疏执行，last-hit 使用原子 max；count 只用于诊断，不参与分配正确性。CPU readback 天然滞后，因此 grace/evict 提供 hysteresis，不能把它们同时压到接近零再据静态截图判断方案失效。

slot 稳定性是画质契约：仍请求的 brick 优先保留原 slot；slot 改 owner 时必须先清零 cube radiance。否则页面表虽正确，首帧仍会采到前任区域的光，表现为短暂串色/漏光。池满时应通过 overflow 指标暴露，不允许覆盖仍驻留 slot 而不更新表。

## CVar

- `r.ambientCube.hitDrivenResidency`：启用命中驱动；默认 `false`。
- `r.ambientCube.bounceHitAffectsResidency`：允许 bake bounce 保活；默认 `false`，避免 bake 自反馈扩张集合。
- `r.ambientCube.graceFrames`：新候选 grace，默认 30。
- `r.ambientCube.evictFrames`：无命中后驱逐窗口，默认 180。
- `r.ambientCube.hitMarkTileRatio`：消费侧标记比例，默认 0.25。
- `r.ambientCube.residencyDebug`：0 关闭，1 命中年龄，2 resident 状态。
- `sys.ambientCubePoolBrickRatio`：每 cascade pool 相对满 brick 数的容量比例。

## 验证与边界

先用默认模式建立画质/显存基线，再启用 hit-driven，观察日志里的 candidate/recent-hit/resident/overflow，并用 debug 1/2 检查相机路径覆盖。不能只看静态截图判断 eviction，因为默认窗口跨越多帧。

当前未实现：跟随相机的统一 voxel/AmbientCube clipmap、全 GPU 分配/indirect bake、动态场景脏区域闭环。这些不是本文的隐含待办；若要做，应基于当前代码重新立项。
