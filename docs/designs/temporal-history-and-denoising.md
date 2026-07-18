---
title: "时序历史与降噪链"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-17
last_updated: 2026-07-17
---

# 时序历史与降噪链

当前 PathTracing、SoftwareTracing 和 SoftwareModern 共用 `src/Engine/Rendering/PipelineCommon/TemporalPostChain.*`。本文记录实际数据语义和明确缺口，避免把旧 ReLAX 研究计划或 NoAmbient TAA 计划误当成已实现功能。

## 每 view 独立

`RenderView` 拥有 `TemporalResolve`、A-trous denoiser、previous UBO、object-id/depth 有效性和 history generation。primary、缩略图、camera preview、Remote session 之间不得共享历史。

历史只有在 `TemporalResolve::IsHistoryValidForFrame()` 为真时可读：它既要求显式 valid，也要求当前引擎帧恰好是上次渲染帧加一。因此 OnDemand view 中断若干帧后不会继续使用陈旧历史。

显式失效原因包括 initial、scene changed、renderer changed、extent changed、camera cut、temporal config changed、view reused 和 swapchain recreated。添加会改变重投影语义的设置时，必须把它纳入失效路径，不能只清一个 shader buffer。

## 当前链路

一帧时序后处理按以下顺序执行：

1. 将当前 `RT_SINGLE_DIFFUSE/SPECULAR`、Albedo、Normal、ObjectId、MotionVector 和 MotionMoment 声明为输入。
2. `Process.ReProject.comp.slang` 做 motion reprojection、object boundary/disocclusion rejection、firefly pre-clamp、YCoCg history clamp 和自适应混合。
3. 可选 `Process.AtrousWavelet.comp.slang` 同一 dispatch 过滤 diffuse/specular，使用 object/normal/depth edge stop。
4. `Process.DenoiseJBF.comp.slang` compose 到 `RT_DENOISED`。
5. `TemporalResolve::CopyToHistory()` 将累计 Diffuse、Specular、Albedo 复制到当前 view bank 的 history，并记录 rendered frame。

输入光照是 diffuse/specular 分离的，alpha 在当前实现中复用为 history length 或方差载体。A-trous 首轮从共享 3×3 邻域估计空间方差，后续迭代传播方差；specular footprint 受 roughness 控制。

ObjectId history 由 renderer contract 单独声明并复制。NoAmbient 的 contract 没有 history，当前只运行 shading → 半分辨率 GTAO（启用时）→ compose；VoxelTracing 也没有 temporal chain。

## 不是当前实现的内容

- 没有独立的 temporal luminance first/second-moment history texture；现有 MotionMoment 用于运动/拒绝语义，不能称作 SVGF temporal moments。
- 没有 ReLAX 风格的 specular virtual reprojection、hit-distance reconstruction 或完整 anti-lag 状态机。
- NoAmbient 没有 TAA，也没有 voxel sky-visibility history。
- DLSS 激活时的 jitter、输入选择与 reset 规则属于 upscaler contract，见 [DLSS / Streamline 指南](../guides/dlss-streamline.md)，不能通过重复运行引擎 TAA 来“加强”抗锯齿。

仍在讨论的 history clamp Phase B/C 见 [条件性计划](../plans/reproject-history-clamp-blackdot-fix.md)。只有先补齐对应 G-buffer/历史数据并重新验证收益，才执行其中的 temporal moments 或 virtual reprojection；未勾选项不是自动任务。

## 修改护栏

- 任何新增 history channel 都要同时定义 RT slot/bank 布局、首次使用、copy/read barrier、失效和 extent recreate。
- rejection 必须保守处理屏幕外、object-id 不匹配、camera cut 与非连续帧；不可用的 history 应退化为当前样本。
- 调参先区分 firefly clamp、history clamp、temporal alpha 和 A-trous edge stop，避免用一个更强的 blur 掩盖错误 motion/object id。
- 验证至少包含静止收敛、相机平移、快速旋转、物体边缘/反射高光、renderer 切换和两个不同 RenderView；只看一张稳定帧截图无法验证 history 生命周期。
