---
title: "渲染运行时架构与契约"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-17
last_updated: 2026-07-17
---

# 渲染运行时架构与契约

本文提炼自已完成的 Vulkan renderer 重构计划，记录当前代码仍依赖的契约。实现入口是 `src/Engine/Rendering/VulkanBaseRenderer.*`；本文不保存旧阶段号、LOC 目标或迁移步骤。

## 分层与所有权

- `VulkanBaseRenderer` 是 Vulkan 资源、swapchain、每帧编排和 logic renderer 的 facade，不应重新吸收缩略图、Remote 或产品专用业务。
- `FrameSubmission` 收口 acquire/submit/present；`RayTracingSceneBackend`、`AmbientCubeBaker` 和 `GpuDrivenPasses` 分别承担 scene-global 准备和 per-view prepass。
- `LogicRendererBase` 的实现只负责自身着色路径。共享时序后处理位于 `PipelineCommon/TemporalPostChain`。
- `RenderView`/`RenderViewManager` 拥有每视图 RT bank、相机、历史、generation 和资源状态；上层调度见 [RenderView 多视图架构](multi-viewport-renderview-design.md)。
- 可选模块通过 `FExternalPassContract` 注册外部 pass。模块不得靠访问 renderer 私有成员或假定某个 G-buffer 总是存在来插入渲染。

Engine 核心可以定义稳定接缝，但不得依赖 `src/Modules/`。Streamline、Remote、Splat、DevTools 等实现留在各自模块。

## Renderer contract 是调度事实源

`FRendererContract` 同时声明 scene resources、per-view prepass、输出、允许的 post-process、历史通道以及 scene override 能力。新增或修改 renderer 时先更新 contract，再让调度从 contract 派生；不要新增并行的 renderer-type switch。

| Renderer | Scene resources | Prepass | Post/history 要点 |
|---|---|---|---|
| PathTracing | Voxel、Ambient、TLAS、SHARC | Cull、Clear、Visibility | Temporal、DLSS、Frame Generation；Diffuse/Specular/Albedo/ObjectId history |
| SoftwareTracing | Voxel、Ambient | Cull、Clear、Visibility、CSM | Temporal + spatial upscale；四类 history |
| SoftwareModern | Voxel、Ambient | Cull、Clear、Visibility、CSM | Temporal + spatial upscale；四类 history |
| VoxelTracing | Voxel、Ambient | 无 | 只承诺 Color 与 spatial upscale；无 history |
| SoftwareModernNoAmbient | 无 scene GI resource | Cull、Clear、Visibility、CSM | GTAO 后直接 compose；无 temporal/history |

表格是理解入口，精确位集仍以 `RendererDescriptors` 为准。特别注意：NoAmbient 当前不请求 Voxel/Ambient，也没有历史；旧的 voxel sky-visibility 设计没有落地。
NoAmbient 的光照拆分、GTAO 与无 history 语义见 [SoftwareModernNoAmbient 渲染与 GTAO](software-modern-noambient-rendering.md)。

## 每帧顺序

正常非 reference 帧的关键顺序是：

1. 等待上一提交、更新 scene nodes 和容量、更新 primary camera UBO。
2. `BeginSceneFrame()` 只执行一次，准备 camera-independent 的 TLAS/ambient 等 scene-global 资源。
3. primary `RenderView` 执行 contract 指定的 prepass 和 logic renderer。
4. compatible 的 module content pass 在 primary 后执行；当前调度只接受 `AfterPrimaryView + PrimaryView`。
5. primary 通过 DLSS、spatial upscale 或 blit resolve 到 swapchain。
6. `RenderViewServices` 调度 auxiliary/offscreen views，并由 provider 消费输出。
7. scene post/debug、UI/frame consumers、截图，然后 submit/present。

`EExternalPassInsertionPoint::BeforeSwapchainResolve` 和 `EExternalPassScope::EveryView/Scene` 虽已定义，当前执行器并未开放对应路径。不要仅因 enum 存在就声称支持；扩展时必须补调度、资源状态和多视图验证。

## 资源状态规则

`PipelineCommon::FResourceStateTracker` 是受管 image 当前 layout/stage/access 的单一事实源。一个 pass 应通过 `FImageUse` 声明目标状态，让 tracker 生成 barrier。

- 只有确实不需要旧内容时才能设置 `discardPreviousContents=true`；discard 会把旧 layout 当成 `UNDEFINED`。
- 外部系统直接改变 image 状态后，必须用明确 import 重新建立 tracker 状态。
- 不要恢复“每帧先把所有 image 统一初始化/全量 barrier”的做法；它掩盖所有权错误并产生无谓同步。
- swapchain、每个 view 的 RT bank、temporal history 各有自己的状态域，不能跨 view 复用最后状态。

## Scene、View 与提交生命周期

- camera-independent 工作每 scene 每帧一次；camera-dependent prepass 每 view 一次。
- `SetScene` 增加 scene generation、使所有 view history 失效、清 AmbientCube cache，并 reset upscaler history。
- renderer 切换、swapchain recreate、extent 改变、camera cut、temporal 配置改变、view handle 复用都必须失效对应历史。
- handle 必须同时校验 bank base 与 generation；只保存 bank index 会在 view 回收后指向新 owner。
- `kFramesInFlight` 当前明确为 1。资源回收、readback 和 submit serial 的推理都应基于此事实；若要改为多帧并行，需重新审计所有 per-frame ring、host-visible 写入和 external consumer。
- CSM 可按 scene generation 与 camera family 有界复用，但不同相机族不得共享错误的 cascade 结果。

## 修改检查表

新增 renderer/pass/view consumer 时至少核对：contract 的输入输出是否完整、scene-global 与 per-view 工作是否分离、RT bank 是否加 base、history 是否独立并可失效、tracker 是否覆盖每次 layout 变化、external pass 在缺少输出时是否安全跳过，以及 primary/secondary/reference 三种调度是否都成立。

渲染行为改动按 `AGENTS.md` 做 targeted build，并用 `gnb shot` 验证；涉及 history 或多视图时至少再切换 renderer/scene、改变 extent，并渲染两个不同相机。
