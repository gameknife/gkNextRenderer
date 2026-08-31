---
title: "CPU TLAS 快照与后台重建架构"
category: design
status: 现行
owner: engine/assets
last_updated: 2026-08-31
---

# CPU TLAS 快照与后台重建架构

`FCPUAccelerationStructure` 不再原地修改一棵进程级 TLAS。每次构建都产生独立的
`FCPUTLASSnapshot`，完成后一次发布；ray query、ProbeBaker 和 AmbientCube 工作批次在整个操作期间
持有同一份不可变快照。这一结构把运行时 TLAS rebuild 从主线程移开，也消除了旧实现中
`GCpuBvhState`、裸指针换绑和 reader/build 并发的数据竞争。

## 所有权与发布

快照同时拥有本次查询所需的完整 revision：

- `FCPUBLASSet`：BLAS context 与 tinybvh 指针表；
- `FCPUMaterialTable`：CPU query 所需的材质投影；
- `instances` / `contexts`：TLAS instance、node/material 映射与 nav bounds；
- `tinybvh::BVH tlas` 和 `sceneRevision`。

active snapshot 通过 `std::atomic<std::shared_ptr<const FCPUTLASSnapshot>>` 发布；不支持该特化的标准库
使用互斥保护的等价路径。发布后的对象不再修改，旧 reader 依靠 shared ownership 延长其生命周期。
空场景也发布显式 empty snapshot，query 直接 miss，不能复用上一棵树。

## Capture、Build、Publish

```text
Scene mutation
  → 主线程 CaptureBuildInput（冻结 transform、bounds、材质与 revision）
  → QueueBuildInput（单 in-flight + latest 合并）
  → TaskCoordinator 后台 BuildSnapshot
  → 主线程 ConsumeCompletedBuild
  → 校验 BLAS generation → PublishSnapshot → 合并 nav dirty bounds
```

初次场景提交仍同步构建，以保证 `OnSceneLoaded`、NavGrid 和第一批 probe 立即有可用快照。运行时更新
允许在后台构建期间继续读旧 snapshot；若构建期间又出现更新，只保留最新 input，避免形成无界 backlog。
`GetBuildStats()` 暴露 requested/published revision、in-flight 状态和 snapshot staleness。

Nav dirty bounds 是 build result 的一部分，只随成功发布的快照合并。generation 已过期的结果会被丢弃，
不能单独污染当前 NavGrid 更新范围。

## Reader 契约

1. 一次逻辑 query 或 worker batch 只 acquire 一次 snapshot，并持有到结束。
2. query 不从 `NextEngine` 的 active Scene 隐式寻找 BVH 或材质。
3. snapshot 中的 BLAS、instance、context、material generation 必须来自同一 capture。
4. 删除节点后，旧 snapshot 可能短暂返回旧 node id；面向 Scene 的调用方仍要验证节点是否存在。
5. worker 不调用会等待全局任务队列的 `WaitForAllParralledTask()`；CPU TLAS 使用自己的完成状态。

## 当前边界

- tinybvh TLAS `Build` 本身仍是单线程；当前收益来自后台构建与不可变发布，而不是第三方 builder 内并行。
- Capture 目前在主线程完成。只有 profiling 再次证明它构成 frame spike，才考虑固定区间并行输入准备。
- 不调用 tinybvh `BVH::Refit()` 更新 TLAS；该 API 的语义不适用于当前 TLAS。
- tinybvh 升级、静态/动态双 TLAS 和 TLAS forest 都不是活动任务。需要时应先用
  10k～131k instance benchmark 重新立项，不能在现有路径旁偷偷增加第二套所有权模型。

主要实现位于 `src/Engine/Assets/Acceleration/CPUAccelerationStructure.*`、
`CPUAccelerationStructure.Internal.hpp` 与 `CpuBvh.cpp`。
