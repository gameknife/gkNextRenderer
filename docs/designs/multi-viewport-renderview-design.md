---
title: "RenderView 多视图架构"
category: design
status: 现行
owner: engine/modules/editor
created: 2026-06-26
last_updated: 2026-07-17
---

# RenderView 多视图架构

`RenderView` 是“一份独立场景视图”的渲染单位，不是 ImGui platform multi-viewport。后者位于 `src/Application/Editor/Common/MultiViewportBackend.*`，负责把 ImGui 窗口拖成 OS 窗口；两者不要混名。

## 当前所有权

- `src/Engine/Rendering/RenderView.*`：view identity、RT bank、相机 UBO、时域历史、resource state、输出类型和调度类型。
- `RenderViewManager`：在 `RenderView.hpp` 内管理 handle/generation 和 bank 生命周期。Bank 0 永久属于 primary view；总 bank 上限是 8。
- `src/Engine/Rendering/Preview/RenderViewServices.*`：按优先级管理上层 `IRenderViewProvider`，把预览/离屏业务从核心 renderer 隔离。
- `src/Modules/RenderViews/OffscreenRenderViewController.*`：共享的离屏相机 provider；当前最多 3 个 secondary camera view。
- `src/Application/Editor/Common/Preview/AssetThumbnailRenderer.*`：材质/mesh 缩略图和材质实时预览。
- `src/Application/Editor/gkNextEditor/Panels/CameraViewPanel.cpp`：编辑器多相机面板。
- `src/Modules/NextRemote/RemoteServer.cpp`：Remote MultiView 的每会话画面消费者。

`VulkanBaseRenderer` 仍拥有 Vulkan 资源和实际录制能力，但业务专用 view 的创建、请求与回调不应重新塞进它的 public API。

## 每个 view 必须独立的状态

- camera UBO ring 和 camera address
- bindless RT bank
- previous UBO、ObjectId/depth 有效性、ReSTIR reservoir history generation
- previous depth/object-id 有效性
- CSM cache 与 dirty/update mask
- resource-state tracker

场景变化、renderer 变化、extent 变化、camera cut、temporal 配置变化、handle 复用或 swapchain recreate 都必须调用 history invalidation。不能让 secondary view 借用 primary 的历史；这种问题通常表现为 ghost、错误 motion vector 或跨相机阴影污染。

## 调度与输出

`EViewSchedule`：`Persistent` 每帧、`OnDemand` 脏时、`Transient` 收敛/渲染若干帧后回收。`EViewOutputKind` 当前支持 swapchain subrect 与 offscreen texture；实际已广泛使用的是 offscreen texture。

Provider 由 `RenderViewServices` 调度。较低 priority 先执行；一个 exclusive transient provider 可把低优先级工作推迟到下一帧。新增 preview 类功能应实现 `IRenderViewProvider`，而不是在主帧里再加用途特判。

一帧只执行一次 camera-independent 的 `BeginSceneFrame()`，随后每个已调度 view 各自执行 camera-dependent prepass 与 logic renderer。正常模式下顺序是 primary view → primary external passes → primary resolve → auxiliary views；因此 secondary view 不是一次完整主帧的简单复制品。

`FRenderViewPostCallback` 在 `DispatchScheduledRenderViews()` 录制 command buffer 的同一渲染线程上、紧跟对应 view 后执行。它适合立即拷贝或合成输出，不是可跨线程长期持有 `RenderView&` 的异步通知。

`SceneOverride` 只有在 renderer contract 的 `supportsSceneOverrideWithoutPrepare` 为真时才合法；当前真正允许这一点的是 `SoftwareModernNoAmbient`。其他 renderer 依赖 scene-global TLAS、AmbientCube 或体素准备，强行换 scene 会被拒绝，而不是自动为 secondary view 准备另一套场景资源。

External pass API 虽声明了 `BeforeSwapchainResolve` 与 `EveryView` / `Scene`，当前调度器实际只执行 `AfterPrimaryView + PrimaryView`。Splat、模块调试绘制等 external content 因而不会自动出现在 secondary view。需要扩展时应先实现并验证调度与资源契约，不能只修改枚举或文档。

## 容量与限制

- 8 个 RT bank 包含 primary，因此最多 7 个并发 full-history secondary view。
- `OffscreenRenderViewController` 当前进一步限制为 3 个 camera secondary view；Remote MultiView 会把 `maxClients` clamp 到这个值。
- 缩略图可复用 transient view，不应为每个资产常驻完整历史。
- 不要假定所有 logic renderer 都能免费支持多 view；新增 renderer 时需验证 RT bank 偏移、history invalidation、CSM camera family 和 output copy。
- 不要承诺 secondary view 与 primary 逐像素或内容完全一致；先核对 external pass、scene override、upscaler 和最终 compose 是否覆盖目标用途。
