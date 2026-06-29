---
title: "RenderView 核心层清理与 VulkanBaseRenderer 去特化重构计划"
category: plan
status: completed
owner: engine
created: 2026-06-29
last_updated: 2026-06-29
---

# RenderView 核心层清理与 VulkanBaseRenderer 去特化重构计划

> 执行状态：已完成。`VulkanBaseRenderer` 的 preview/secondary/thumbnail public API 已移除，preview 业务迁入 `RenderPreviewServices` 及其 controller，active view state 已由 RAII scope 管理，view target resource 创建与生命周期已集中化，primary/secondary/thumbnail 的相机矩阵构造均走 `ViewCameraUboBuilder`。

> 面向后续接手 agent。本文不是重新设计多视口功能，而是在现有多视口、多 scene、thumbnail 已基本可用的基础上，把近期为了快速交付塞进 `VulkanBaseRenderer` 的特殊用途代码迁出，让核心渲染器重新回到清晰的职责边界。
>
> 前置阅读：`docs/designs/multi-viewport-renderview-design.md`。那份文档描述了 RenderView 的原始目标与踩坑；本文聚焦当前实现后的整理方案。

---

## 0. 目标

本轮目标：

- `VulkanBaseRenderer` 只保留 Vulkan device/swapchain、逻辑 renderer 注册、主帧编排、低层 render target/bindless 能力。
- editor camera view、scene preview、material/mesh thumbnail 这类上层用例从 `VulkanBaseRenderer` 的 public API 和成员里迁出。
- `RenderViewManager` 从“薄调度容器”升级为 view 生命周期与调度入口；缩略图等功能通过通用 preview/offscreen view 服务使用它。
- 行为保持不变优先。重构应先做“搬家 + forwarding wrapper”，再删除旧入口，避免与功能改动混在一起。

非目标：

- 不重写 shader bank 方案；`GPUScene.custom_data_0` 仍是 active view bank base。
- 不重写各条 `LogicRenderer` 的主渲染算法。
- 不在本轮做 OS 独立窗口、多队列并行录制或 `VK_KHR_multiview`。
- 不把 editor 代码反向依赖进 Engine。

---

## 1. 当前实现快照

### 1.1 已经成型的 RenderView 基础

`src/Engine/Rendering/RenderView.hpp` 已有以下核心结构：

- `FViewRenderState`：per-view 的 previous UBO、CSM cache、progressive frame、history reset。
- `FBankAllocator`：固定步长分配 RT bank，bank 0 保留给 primary view。
- `RenderView`：持有 bank base、extent、scene override、camera UBO ring、visibility framebuffer 指针、atrous/temporal helper。
- `RenderViewManager`：持有 primary view、additional views、schedule list。

但该文件开头也明确说明：render resources 仍在 `VulkanBaseRenderer`，`RenderView` 只是携带记录一帧所需的 per-view handle。这就是当前混乱的根源之一：抽象已经出现，但资源和具体业务还留在 base。

### 1.2 VulkanBaseRenderer 里混入的特殊用途 API

`VulkanBaseRenderer.hpp:118-155` 暴露了 editor/thumbnail 语义：

- 固定数量 camera secondary view：`kMaxCameraSecondaryViews = 3`。
- 固定 scene preview view index：`kScenePreviewSecondaryViewIndex`。
- `SetSecondaryViewEnabled`、`RequestSecondaryViewThisFrame`、`SetSecondaryViewCameraOverride` 等 editor camera view API。
- 固定 bindless sample slot：`kSecondaryViewSampleSlotBase = 65000`。
- `RequestMaterialThumbnail`、`RequestMeshThumbnail` 以及 material/mesh thumbnail slot range。

这些 API 对 engine 核心层来说都太具体。它们应该由“offscreen view/preview 服务”或 editor 层持有，而不是挂在最底层 renderer 上。

### 1.3 VulkanBaseRenderer 里混入的特殊用途成员

`VulkanBaseRenderer.hpp:354-418` 当前包含三类 view 资源：

- `referenceViews_`：reference mode 的 2x2 renderer 对照视图。
- `secondaryViews_`：editor camera view 和当前 scene preview 共用的固定数组。
- `thumbnailRenderView_` + material/mesh thumbnail scene/cache/queue/sampler/framebuffer。

其中 `thumbnail*` 成员明显是 asset preview 业务；`secondaryViews_` 也带有 editor 固定槽位语义。它们让 base 的资源生命周期、scene 切换、swapchain 重建都必须知道具体上层功能。

### 1.4 当前帧编排

核心路径已经基本正确：

- `Render()` 先渲染 primary/reference view，再 resolve 到 swapchain。
- 如果有 secondary/thumbnail 工作，调用 `ScheduleAuxiliaryViews()` 再 `DispatchScheduledRenderViews()`。
- `RenderViewToBank()` 临时设置 active bank、camera address、scene override、visibility framebuffer，然后执行 `PreRenderPerView()` 与 logic renderer。

关键代码：

- `BeginSceneFrame()`：`VulkanBaseRenderer.cpp:1322-1330`。
- `RenderViewToBank()`：`VulkanBaseRenderer.cpp:1332-1381`。
- `ScheduleRenderView()` / `DispatchScheduledRenderViews()`：`VulkanBaseRenderer.cpp:1383-1413`。
- `Render()` 中的 primary/reference/auxiliary 调度：`VulkanBaseRenderer.cpp:1890-1989`。

问题是：调度框架已经存在，但 auxiliary 的业务策略仍由 base 决定。

### 1.5 Thumbnail 实现现状

当前 thumbnail 链路全部在 `VulkanBaseRenderer.cpp`：

- 请求与缓存：`RequestMaterialThumbnail()` / `RequestMeshThumbnail()`，`VulkanBaseRenderer.cpp:2060-2119`。
- 材质球 preview scene：`EnsureMaterialThumbnailScene()`，`VulkanBaseRenderer.cpp:2255-2310`。
- mesh preview scene：`RebuildMeshThumbnailScene()`，`VulkanBaseRenderer.cpp:2355-2414`。
- thumbnail UBO：`BuildThumbnailUbo()`，`VulkanBaseRenderer.cpp:2416-2492`。
- 输出拷贝：`CopyThumbnailViewOutput()`，`VulkanBaseRenderer.cpp:2494-2527`。
- 每帧调度：`ScheduleNextMaterialThumbnail()` / `ScheduleNextMeshThumbnail()`，`VulkanBaseRenderer.cpp:2529-2698`。

`ScheduleAuxiliaryViews()` 目前先调 material thumbnail，再调 mesh thumbnail；只要成功调度一个 thumbnail 就 `return`，因此 pending thumbnail 会让 secondary view 本帧不渲染。这个策略可能是为了限流，但它是隐藏在 base 里的上层策略，应迁到 preview scheduler 中并显式命名。

### 1.6 Secondary view 实现现状

当前 secondary view 也是 base 内置：

- public API：`VulkanBaseRenderer.cpp:488-583`。
- 创建 offscreen image、sampler、visibility framebuffer：`EnsureSecondaryRenderView()`，`VulkanBaseRenderer.cpp:2182-2253`。
- 输出到 ImGui sample image，并在 `GK_MV_DEMO=1` 时额外 blit 到 swapchain PiP：`CopySecondaryViewOutput()`，`VulkanBaseRenderer.cpp:2700-2765`。
- 每帧遍历固定数组并构造 camera UBO：`ScheduleAuxiliaryViews()`，`VulkanBaseRenderer.cpp:2767-2816`。

editor 直接调用这些 base API：

- Camera View panel：`src/Application/Editor/gkNextEditor/Panels/CameraViewPanel.cpp:52-85`。
- Scene card preview：`src/Application/Editor/gkNextEditor/Panels/ContentBrowserPanel.cpp:1057-1064`。
- Camera override 同步：`src/Application/Editor/gkNextEditor/EditorMain.cpp:599-608`。

### 1.7 Active view 状态是全局临时状态

当前 shader 侧 bank 与 camera 地址通过以下全局 active state 注入：

- `activeSceneOverride_`
- `activeRenderView_`
- `activeViewBankBase_`
- `activeViewRenderExtent_`
- `activeViewCameraAddress_`
- `activeVisibilityFrameBuffer_`

`GetScene()` 会优先返回 `activeSceneOverride_`（`VulkanBaseRenderer.cpp:417-424`），`Scene::BuildGPUScene()` 读取 active camera address 与 active bank base（`Scene.cpp:668-695`）。`ZeroBindCustomPushConstantPipeline` 也会 stamp active bank base（`CommonComputePipeline.cpp:165-180`）。

这个机制能工作，但现在是手动保存/恢复。后续至少应先用 RAII scope 包住，长期再把 `FRenderContext` 显式传给 pass。

---

## 2. 问题诊断

### 2.1 核心 renderer 知道太多上层业务

`VulkanBaseRenderer` 现在同时知道：

- editor 最多有 3 个 camera view；
- content browser 有一个 scene preview slot；
- material thumbnail 用球体 preview scene；
- mesh thumbnail 每次重建小 scene；
- thumbnail slot ranges 是 64000 和 63200；
- secondary view slot 从 65000 开始；
- `GK_MV_DEMO` 要把 secondary view blit 到右下角。

这些都不是 Vulkan base renderer 的核心职责。

### 2.2 RenderViewManager 还没有真正拥有 view 生命周期

`RenderViewManager` 现在只创建 view 和保存 schedule；具体资源仍散落在 base：

- RT bank 由 `VulkanBaseRenderer::CreateRenderTargetBank()` 创建。
- visibility framebuffer 分别由 reference/secondary/thumbnail 三套代码创建。
- offscreen sample image 和 sampler 分别由 secondary/thumbnail 两套代码创建。
- swapchain delete/recreate 需要 base 手动清每个特殊用途成员。

结果是新增一种 preview 用例时，最容易继续往 base 里加成员。

### 2.3 Camera UBO 构造逻辑分裂

目前至少有三条 UBO 构造路径：

- primary view：`NextEngine::GetUniformBufferObject()`，仍从 `renderer_->PrimaryViewState()` 取 state，并直接用 swapchain render rect。
- secondary view：`VulkanBaseRenderer.cpp` 的匿名 `MakeCameraUbo()` / `MakeOrbitedCameraUbo()`。
- thumbnail view：`BuildThumbnailUbo()`。

这三条路径有不同的 temporal、jitter、CSM、HDR、denoiser 参数处理。短期可以接受，长期会变成多视口行为差异的来源。

### 2.4 Scene override 依赖隐式全局

为了让 existing logic renderer 继续调用 `GetScene()`，当前用 `activeSceneOverride_` 改变 `VulkanBaseRenderer::GetScene()` 的返回值。这个桥接设计适合渐进落地，但不应无限扩大。至少需要把“进入某个 RenderView 上下文”的设置/恢复做成一个小的 RAII 对象，避免异常或早退时污染后续 view。

### 2.5 Sample slot 分配是硬编码的

secondary/material/mesh preview 都直接定义固定 slot range。短期冲突风险不高，但随着更多 preview 类型出现，应该有一个 `FPreviewSampleSlotAllocator` 或静态 slot registry，集中声明：

- 谁使用哪个 range；
- 最大数量；
- 释放/复用策略；
- 是否允许跨 scene 保留缓存。

### 2.6 Auxiliary 调度策略隐藏且会互相影响

`ScheduleAuxiliaryViews()` 对 thumbnail 优先且成功后立即返回。大量 thumbnail 请求会延迟 camera secondary view 刷新。这个策略也许是合理的 GPU 限流，但它应该由 preview scheduler 明确表达，例如：

- `maxTransientPreviewsPerFrame = 1`
- `persistentViewsAlwaysRender = true`
- `transientPreviewsAfterPersistent = true`

---

## 3. 目标架构

### 3.1 分层原则

| 层 | 应该负责 | 不应该负责 |
| --- | --- | --- |
| `VulkanBaseRenderer` | device/swapchain/frame resources、logic renderer registry、primary frame stages、低层 RT bank/storage image/bindless helper | editor camera view 数量、thumbnail cache、preview scene 构造、sample slot 业务分配 |
| `RenderViewManager` | view 创建/销毁、bank 分配、view schedule、active view dispatch | 材质球长什么样、content browser 什么时候请求缩略图 |
| `RenderViewResources` / `ViewRenderTargetBank` | 每个 view 的 RT bank、visibility framebuffer、camera UBO ring、offscreen output image/sampler | editor UI 状态 |
| `RenderPreviewService` | transient/on-demand preview request、preview queue、每帧预算、输出 sample slot、拷贝/缓存策略 | Vulkan device 初始化、swapchain present |
| `AssetThumbnailRenderer` | material/mesh thumbnail 的 preview scene 构造、hash/cache、slot range、请求 API | 主帧 render/pass 细节 |
| Editor `EditorRenderViewController` | 把 editor camera panel/content browser 状态映射到 generic render view/previews | 直接操作 `VulkanBaseRenderer` 的内部 view 数组 |

### 3.2 建议新增/调整文件

建议的文件布局：

```text
src/Engine/Rendering/
  RenderView.hpp
  RenderView.cpp                         # 把 inline manager/allocator 逐步下沉，减少头复杂度
  RenderViewResources.hpp
  RenderViewResources.cpp                # RT bank、visibility framebuffer、offscreen output
  RenderViewContext.hpp                  # FActiveRenderViewScope / FRenderViewRecordContext
  Preview/
    RenderPreviewService.hpp
    RenderPreviewService.cpp             # 通用 transient/on-demand preview 调度
    AssetThumbnailRenderer.hpp
    AssetThumbnailRenderer.cpp           # material/mesh thumbnail 业务

src/Application/Editor/gkNextEditor/
  EditorRenderViews.hpp
  EditorRenderViews.cpp                  # camera view / scene preview 到 engine preview API 的适配
```

如果担心一次新增文件太多，可以按 phase 渐进落地；但目标边界应按上表收敛。

### 3.3 `VulkanBaseRenderer` 最终保留的 view 相关 API

长期希望 `VulkanBaseRenderer` 只暴露这些通用入口：

```cpp
RenderViewManager& RenderViews();
RenderPreviewService& Previews();

uint32_t ActiveViewBankBase() const;
VkDeviceAddress ActiveViewCameraAddress(uint32_t imageIndex) const;
VkExtent2D ActiveViewRenderExtent() const;

const RenderImage* GetStorageImage(uint32_t bindlessIdx) const;
const RenderImage* GetViewStorageImage(uint32_t slot) const;
```

`RequestMaterialThumbnail()`、`RequestMeshThumbnail()`、`SetSecondaryViewEnabled()` 这类 API 应先保留一轮 forwarding wrapper，等 editor 迁完后删除。

### 3.4 RenderView resource 目标形态

当前 `RenderView` 只保存 `FrameBuffer*`、camera UBO ring 等少量 handle。建议引入：

```cpp
struct FRenderViewTargetResources
{
    VkExtent2D allocatedExtent{};
    std::unique_ptr<FrameBuffer> visibilityFramebuffer;
    std::unique_ptr<RenderImage> offscreenImage;
    std::unique_ptr<Sampler> offscreenSampler;
    uint32_t outputSampleSlot = std::numeric_limits<uint32_t>::max();
};
```

第一步可以让 `RenderViewManager` 拥有这个结构，`RenderView` 只持非 owning 指针；第二步再把 owning resources 收进 `RenderView`。不要同时大改所有调用点。

### 3.5 Active view scope

先引入最小 RAII，降低当前隐式 active state 的风险：

```cpp
class FActiveRenderViewScope
{
public:
    FActiveRenderViewScope(VulkanBaseRenderer& renderer, RenderView& view);
    ~FActiveRenderViewScope();
};
```

它只封装当前 `RenderViewToBank()` 里的保存/设置/恢复逻辑，不改变现有 pass 签名。后续再考虑把 `Scene&`、`GPUScene`、bank、camera address 组成显式 `FRenderViewRecordContext`，逐步传给 pipeline/pass，减少对 `GetScene()` 的隐式依赖。

---

## 4. 分阶段开发计划

每个 phase 独立提交。常规 engine 层验证按 AGENTS.md 使用 targeted build：`./gnb.bat build gkNextRenderer gkNextUnitTests`。涉及 editor 调用点时额外构建 `gkNextEditor`。

### Phase 0 - 基线与保护

目的：记录当前行为，避免后续“搬家”时混入功能变化。

步骤：

1. 记录当前 `gnb shot --scene assets/models/playground.glb` 输出。
2. 记录 `gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui`，确认 Camera View panel 与 content browser preview 仍可见。
3. 用 `GK_MV_DEMO=1` 跑一次 `gkNextRenderer --load-scene assets/models/playground.glb --agent-validation`，确认 secondary view PiP 路径仍有效。
4. 不改代码，必要时只补一份简单 checklist 到执行日志。

验收：无代码变化，只产出可对照的截图/命令记录。

### Phase 1 - 把 thumbnail 业务迁出 VulkanBaseRenderer

目的：先移除最明显的特殊业务，行为保持一致。

新增：

- `src/Engine/Rendering/Preview/AssetThumbnailRenderer.hpp`
- `src/Engine/Rendering/Preview/AssetThumbnailRenderer.cpp`

迁移内容：

- `kMaterialThumbnailSampleSlotBase` / `kMaterialThumbnailMaxSlots`
- `kMeshThumbnailSampleSlotBase` / `kMeshThumbnailMaxSlots`
- material/mesh thumbnail image/hash/pending queue/cache
- material preview scene 与 mesh preview scene 构造
- thumbnail UBO 构造
- thumbnail output copy
- 每帧调度一个 thumbnail 的策略

保留一轮 wrapper：

```cpp
uint32_t VulkanBaseRenderer::RequestMaterialThumbnail(uint32_t materialIndex, uint64_t materialHash)
{
    return thumbnailRenderer_->RequestMaterialThumbnail(materialIndex, materialHash);
}
```

`VulkanBaseRenderer::SetScene()` 改为调用 `thumbnailRenderer_->OnMainSceneChanged()`，不再手动清一堆 thumbnail 成员。

注意：

- 不要在本 phase 改 thumbnail 调度策略。当前“一帧最多一个 thumbnail 且优先于 secondary”的行为先保持。
- `AssetThumbnailRenderer` 需要访问 low-level 能力时，通过窄接口或回调拿：
  - `Device()`
  - `CommandPool()`
  - `SwapChain()`
  - `CreateRenderTargetBank()`
  - `SetRenderViewUbo()`
  - `ScheduleRenderView()`
  - `GlobalTexturePool::BindSampleTexture()`
  - renderer selection（优先 `ERT_SoftwareModernNoAmbient`，否则 current renderer）
- 如果因为 private 访问太多导致 friend 滥用，先接受一个 `FRenderPreviewHost` 小接口，而不是把更多 public 方法加回 base。

验收：

- `rg -n "materialThumbnail|meshThumbnail|thumbnailRenderView|RequestMaterialThumbnail|RequestMeshThumbnail" src/Engine/Rendering/VulkanBaseRenderer.hpp` 只应剩 wrapper 和一个 service 成员。
- `ContentBrowserPanel.cpp` 不需要立刻改。
- `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests`
- `gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui`

### Phase 2 - 把 secondary/offscreen view 业务迁出 VulkanBaseRenderer

目的：去掉 editor camera view 与 scene preview 的固定槽位语义。

新增：

- `src/Engine/Rendering/Preview/OffscreenRenderViewController.hpp`
- `src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp`

迁移内容：

- `SecondaryRenderViewResources`
- `EnsureSecondaryRenderView()`
- `CopySecondaryViewOutput()`
- `SetSecondaryViewEnabled()` / `RequestSecondaryViewThisFrame()` / camera override 等状态逻辑
- secondary sample slot 分配
- `GK_MV_DEMO` 的 PiP copy 路径

目标 API 形态：

```cpp
struct FOffscreenRenderViewDesc
{
    VkExtent2D extent{};
    Assets::Camera camera{};
    bool persistent = false;
    bool copyObjectIdHistory = true;
};

class OffscreenRenderViewController
{
public:
    FRenderViewHandle EnsurePersistentView(std::string_view name, const FOffscreenRenderViewDesc& desc);
    void RequestOneFrame(FRenderViewHandle handle);
    void SetEnabled(FRenderViewHandle handle, bool enabled);
    bool IsReady(FRenderViewHandle handle) const;
    uint32_t OutputSampleSlot(FRenderViewHandle handle) const;
};
```

editor 迁移：

- 新建 `EditorRenderViews`，在 editor 层保存 3 个 camera view handle 和 1 个 scene preview handle。
- `CameraViewPanel.cpp` 不再直接调用 `VulkanBaseRenderer::SetSecondaryViewEnabled()`；改为调用 editor controller。
- `ContentBrowserPanel.cpp` 的 scene preview 不再依赖 `kScenePreviewSecondaryViewIndex`。

兼容策略：

- 和 Phase 1 一样，`VulkanBaseRenderer` 可短期保留旧 secondary API wrapper。
- wrapper 内部映射到 `OffscreenRenderViewController` 的 legacy handles。
- editor 迁完后删除 wrapper 与 `kMaxCameraSecondaryViews` 等常量。

验收：

- `rg -n "kMaxCameraSecondaryViews|kScenePreviewSecondaryViewIndex|SecondaryRenderViewResources|SetSecondaryView" src/Engine/Rendering/VulkanBaseRenderer.hpp` 为空或只剩临时 deprecated wrapper。
- `CameraViewPanel.cpp` 不再 include `VulkanBaseRenderer.hpp`，只通过 editor controller 或 engine preview API。
- `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests`
- `gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui`
- `GK_MV_DEMO=1` 验证路径仍可用；若保留，建议改名为 preview debug overlay，而不是 multiViewDemo。

### Phase 3 - 让 RenderViewManager 拥有 view resources

目的：停止在 base 上为每种 view 类型维护一套资源结构。

步骤：

1. 新增 `RenderViewResources.hpp/cpp`，封装：
   - RT bank 创建；
   - visibility framebuffer 创建；
   - offscreen sample image/sampler 创建；
   - output sample slot 绑定；
   - resize invalidation。
2. `RenderViewManager::CreateView()` 返回的 `RenderView` 带 resource owner 或 resource handle。
3. `EnsureReferenceView()` 迁到 `RenderViewManager` 或 `ReferenceRenderViewController`；`referenceViews_` 不再由 base 持有。
4. `CreateRenderTargetBank()` 可先留在 base 作为 low-level helper，但调用入口应集中到 resource factory。
5. `DeleteSwapChain()` / `CreateRenderImages()` 不再逐个认识 reference/secondary/thumbnail，只通知 `RenderViewManager::OnSwapChainDestroyed()` / `OnSwapChainCreated()`。

验收：

- `VulkanBaseRenderer` 不再有 `ReferenceRenderViewResources`、`SecondaryRenderViewResources`、`thumbnailRenderView_`。
- swapchain lifecycle 中没有按用途清理 view 资源的 if 分支。
- reference mode 2x2 渲染仍正常。
- `./gnb.bat build gkNextRenderer gkNextUnitTests`
- reference mode 与 editor UI 各截图一张。

### Phase 4 - 引入 ActiveRenderViewScope

目的：保留当前 active-state 桥接，但让进入/退出 view 上下文可靠且可读。

步骤：

1. 新增 `RenderViewContext.hpp/cpp`，实现 `FActiveRenderViewScope`。
2. 用 scope 替换 `RenderViewToBank()` 中手动保存/恢复的 6 个 active 成员。
3. 所有 early return 或 callback 仍走正常析构恢复。
4. 在 scope 内部集中设置：
   - active scene override
   - active render view
   - active bank base
   - active extent
   - active camera address
   - active visibility framebuffer

验收：

- `RenderViewToBank()` 主体只表达渲染步骤，不再直接写一堆 active 成员。
- `GetScene()` 的 override 行为不变。
- `Scene::BuildGPUScene()` 与 custom push-constant stamp 逻辑不变。
- `./gnb.bat build gkNextRenderer gkNextUnitTests`

### Phase 5 - 统一 View UBO 构造

目的：消除 primary/secondary/thumbnail 三套 UBO 构造逻辑差异。

建议新增：

- `src/Engine/Rendering/ViewCameraUboBuilder.hpp/cpp`，或放入更合适的 Runtime camera 模块。

目标形态：

```cpp
struct FViewCameraUboRequest
{
    Assets::Scene& scene;
    Assets::Camera camera;
    VkOffset2D offset{};
    VkExtent2D extent{};
    bool temporalEnabled = true;
    bool taaEnabled = true;
    bool progressiveEnabled = false;
    bool hdrOutput = false;
    uint32_t maxBounces = 1;
};

Assets::UniformBufferObject BuildViewCameraUbo(
    const FViewCameraUboRequest& request,
    FViewRenderState& state);
```

迁移路径：

1. 先让 secondary view 和 thumbnail 使用新 builder。
2. 再让 `NextEngine::GetUniformBufferObject()` 调用同一 builder 构造 primary UBO。
3. 最后删除 `MakeCameraUbo()`、`MakeOrbitedCameraUbo()`、`BuildThumbnailUbo()` 的重复实现。

注意：

- primary view 必须保留现有 user settings、DLSS/TAA jitter、HDR、denoiser routing。
- thumbnail 可以通过 request flags 关闭 temporal/TAA/progressive/HDR。
- secondary view 如果需要真实 temporal history，应使用自身 `FViewRenderState`，而不是从 primary `frame_.lastUBO` 派生 prev matrix。

验收：

- `rg -n "MakeCameraUbo|MakeOrbitedCameraUbo|BuildThumbnailUbo" src/Engine/Rendering src/Engine/Runtime` 为空。
- 主视口截图无非预期差异。
- secondary camera view 相机移动时 motion/TAA 不串 primary。
- thumbnail 输出仍稳定。

### Phase 6 - 调度策略显式化与旧 API 删除

目的：完成表面清理，让 base header 不再暴露特殊功能。

步骤：

1. 在 preview scheduler 中显式配置：
   - 每帧 transient preview 数量；
   - persistent offscreen view 是否总是优先渲染；
   - thumbnail 队列优先级；
   - on-demand view 的 dirty 策略。
2. 删除 `VulkanBaseRenderer` 上所有 legacy forwarding wrapper：
   - `RequestMaterialThumbnail`
   - `RequestMeshThumbnail`
   - `SetSecondaryViewEnabled`
   - `RequestSecondaryViewThisFrame`
   - `SetSecondaryViewCameraOverride`
   - `SecondaryViewSampleSlot`
   - `IsSecondaryViewReady`
3. 删除 base 里的固定 sample slot constants。
4. 更新 `docs/designs/multi-viewport-renderview-design.md` 的“实现进度与交接”，指向本文或归档旧状态。

验收：

- `VulkanBaseRenderer.hpp` 中不再出现 `thumbnail`、`SecondaryView`、`CameraSecondary`、`ScenePreviewSecondary`。
- editor still builds and runs.
- `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests`
- `gnb shot --scene assets/models/playground.glb`
- `gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui`

---

## 5. 推荐实施顺序

最小风险顺序：

1. Phase 1 先迁 thumbnail。它最独立，最能立刻降低 base 噪音。
2. Phase 2 迁 secondary/offscreen view。它牵涉 editor 调用点，但依然不应改渲染核心。
3. Phase 4 提前做也可以。如果接手 agent 对 RAII scope 很有把握，可在 Phase 1 前单独做，降低后续迁移风险。
4. Phase 3 再收 RenderView resources。它影响 swapchain lifecycle 和 reference mode，风险高于前两步。
5. Phase 5 最后做 UBO 统一，因为它最容易造成画面细节差异。
6. Phase 6 删除旧 API，只在所有调用点迁完后做。

---

## 6. 验证矩阵

| 改动范围 | 构建 | 运行/截图 |
| --- | --- | --- |
| thumbnail service 迁移 | `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests` | editor UI 截图，确认 material/mesh thumbnail 生成 |
| secondary/offscreen service 迁移 | `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests` | Camera View panel 截图；`GK_MV_DEMO=1` agent validation |
| RenderView resources 迁移 | `./gnb.bat build gkNextRenderer gkNextUnitTests`，如移动/新增文件未被 glob 收录则加 `--reconfigure` | primary shot、reference mode shot、editor UI shot |
| Active scope | `./gnb.bat build gkNextRenderer gkNextUnitTests` | primary shot、secondary view shot |
| UBO builder 统一 | `./gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests` | primary/secondary/thumbnail 都截图，重点看 TAA、CSM、motion |

通用命令：

```bash
gnb shot --scene assets/models/playground.glb
gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui
```

Windows 下使用：

```bat
gnb.bat build gkNextRenderer gkNextEditor gkNextUnitTests
```

---

## 7. 风险与约束

### 7.1 不要一次性删除旧入口

editor 当前直接调用 base API。先 wrapper，再迁调用点，最后删 wrapper。否则一轮里同时动 engine + editor + behavior，定位回归会很困难。

### 7.2 注意 sample slot 生命周期

`GlobalTexturePool::BindSampleTexture()` 允许把非注册纹理绑定到高位 bindless slot。迁移后必须保证：

- output image 生命周期覆盖 ImGui 使用期；
- resize 后重新 bind sample slot；
- 销毁 view 前不要留下指向已释放 image view 的 UI handle；
- slot range 有集中声明，避免 future collision。

### 7.3 Preview scene 不要默认分配昂贵资源

当前 material/mesh thumbnail scene 构造时 `allocateAmbientResources=false`、`enableCpuAcceleration=false`。迁移后保持这个行为，避免 thumbnail 触发完整 scene 资源。

### 7.4 先保持调度行为，再改策略

当前 thumbnail 会优先 auxiliary，并且每帧最多调度一个。是否让 persistent secondary view 永远优先，可以在迁移完成后单独改；不要和搬文件混在一起。

### 7.5 `GetScene()` override 是过渡机制

短期通过 `FActiveRenderViewScope` 包住即可。不要在本轮强行把所有 logic renderer 改成显式 `Scene&` 参数；那会扩大到 PathTracing/SoftwareModern/GpuDriven/Shadow/GaussianSplat 等多个系统。

### 7.6 RenderView resource ownership 要分两步

先集中到 `RenderViewManager` 或 resource factory，再决定是否塞进 `RenderView` 对象本身。直接把所有资源一次性搬进 `RenderView`，容易和 swapchain lifecycle、bindless vector resize、reference mode 交织。

---

## 8. 完成后的检查清单

- [x] `VulkanBaseRenderer.hpp` 不再暴露 thumbnail 或 editor secondary view API。
- [x] `VulkanBaseRenderer` 成员里不再有 material/mesh thumbnail cache、pending queue、preview scene。
- [x] fixed sample slot range 集中在 preview/thumbnail service，不散落在 base。
- [x] editor camera view 通过 editor controller 或 generic offscreen view handle 工作。
- [x] content browser material/mesh thumbnail 通过 `AssetThumbnailRenderer` 工作。
- [x] `RenderViewManager` 是 view lifecycle/schedule 的唯一入口。
- [x] active view state 通过 RAII scope 设置/恢复。
- [x] primary view bank 0 行为不变。
- [x] `gnb shot --scene assets/models/playground.glb` 通过肉眼检查。
- [x] `gnb shot --target gkNextEditor --scene assets/models/playground.glb --ui` 通过肉眼检查。
- [x] targeted build 通过。

---

## 9. 接手起步索引

优先读这些文件：

- `src/Engine/Rendering/VulkanBaseRenderer.hpp`
- `src/Engine/Rendering/VulkanBaseRenderer.cpp`
- `src/Engine/Rendering/RenderView.hpp`
- `src/Engine/Runtime/Engine.CameraUbo.cpp`
- `src/Engine/Assets/Core/Scene.cpp`
- `src/Engine/Rendering/PipelineCommon/CommonComputePipeline.cpp`
- `src/Application/Editor/gkNextEditor/Panels/CameraViewPanel.cpp`
- `src/Application/Editor/gkNextEditor/Panels/ContentBrowserPanel.cpp`
- `docs/designs/multi-viewport-renderview-design.md`

优先 grep：

```bash
rg -n "thumbnail|Thumbnail|SecondaryView|kScenePreviewSecondaryViewIndex|activeSceneOverride|activeRenderView|ScheduleAuxiliaryViews" src/Engine src/Application/Editor/gkNextEditor
```

如果下一位 agent 只做一件事，建议先做 Phase 1：把 thumbnail 迁出 `VulkanBaseRenderer`，保留 wrapper，验证 editor thumbnail 不变。这一步收益最大、行为风险最低。
