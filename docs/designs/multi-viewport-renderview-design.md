---
title: "多视口渲染（RenderView：单窗口分区 + 离屏到纹理 + 多相机/缩略图）设计与开发计划"
category: design
status: 实现中
owner: engine
created: 2026-06-26
last_updated: 2026-06-27
---

# 多视口渲染（RenderView）设计与开发计划

> 状态：**🚧 实现中**。Phase 0 / 1 / 2 / 3 已落地，Phase 4 主体（编辑器多相机面板）完成，剩内容浏览器缩略图。**接手 agent 必读 [§0 实现进度与交接](#0-实现进度与交接接手-agent-必读)** —— 那里有当前已实现的内容、踩过的坑（含几个非常隐蔽的）、以及下一步怎么接。本文其余部分（§1–§10）是原始设计，仍有效，但实现细节以 §0 和实际代码为准。
>
> 本文是设计 + 分阶段开发计划，描述目标架构、数据结构、与现有 GPU Scene 根描述 + Bindless 体系的集成方式、风险与验证手段。**实现前请先通读 §2 现状分析与 §4 核心机制**，再按 §7 路线图分阶段落地。
>
> **已与 owner 对齐的关键范围决策**：
> 1. **呈现方式**：以 **(a) 单窗口内分区 / 分屏** 与 **(b) 离屏渲染到纹理（offscreen → bindless sampled texture → `ImGui::Image`）** 为主。**独立 OS 窗口（多 swapchain）本期不做**，列入 §8 未来工作。
> 2. **首批用例**：**小 scene 缩略图 / preview 生成** + **单 scene 多相机同时观察**。多 scene 同屏作为架构可扩展点保留，不在首批必须交付内。
> 3. **画质 / 时域**：**每个视口保留完整时域历史**（motion vector / 累积 / 降噪 / progressive），因此每视口需要独立的渲染目标资源集与时域状态。owner 明确：**视口可实现为相对独立的 renderer / 视图对象**，而非共享一套全屏 RT 顺序复用。
> 4. **首个落地接入点**：**gkNextEditor 编辑器**（多相机面板 + 内容浏览器缩略图）。
>
> **命名提示（避免歧义）**：仓库已有 `src/Application/Editor/Common/MultiViewportBackend.*`，那是 **ImGui 平台多视口（把 docking 面板拖出成多个 OS 窗口）**，与本文的"**场景 / 渲染多视口**"是两回事。本设计的核心对象统一命名为 **`RenderView`**，系统为 **`RenderViewManager`**，**不要复用 `MultiViewport*` 名字**，以免混淆。

---

## 0. 实现进度与交接（接手 agent 必读）

> 本节是真实实现状态 + 踩坑记录，**优先级高于下面的原始设计**。所有改动已 commit 到 `dev` 分支，提交区间 `bfa0f9c2` → `f162cf99`（约 16 个 commit）。

### 0.1 当前进度（对照 §7 路线图）

| Phase | 状态 | 关键 commit | 说明 |
| --- | --- | --- | --- |
| Phase 0 抽取 RenderView | ✅ | `bfa0f9c2` | `RenderView`/`RenderViewManager` 对象 + `FViewRenderState`（per-view 时域状态）成型，主视口零回归。**注意**：per-view 的 RT/depth/相机 UBO 尚未*全部*收进 `RenderView` 对象（主视口仍用 `frame_`/`bindless_`，第二视口资源挂在 `VulkanBaseRenderer` 成员上）。这是有意的渐进式收口，不影响功能；真正"视口即对象"的彻底封装可后续做。 |
| Phase 1 Bindless RT bank 贯通 | ✅ | `d632ec7e` `f69ee192` `abc35445` `c7db89b7` | `GPUScene.custom_data_0` = view bank base；shader 侧 `Bindless::ViewRT` + `GetViewStorageTexture`，C++ 侧 `GetViewStorageImage`，全量 sweep；并用**统一 push-constant header**解决 custom-pipeline 拿不到 custom_data_0 的问题（见 0.3）。base 0 像素零回归。 |
| Phase 2 离屏渲到纹理 | ✅ | `ffdb1db7` | swapchain 子矩形 PiP（`GK_MV_DEMO`）+ 离屏 sampled image → `BindSampleTexture` → `kSecondaryViewSampleSlot=65000` → ImGui。 |
| Phase 3 每视口时域历史 + per-view 编排 | ✅(核心) | `9cd800f0` `9c0d3c8d` | `PreRender` 拆 `PreRenderSceneGlobal`(每 scene 一次) + `PreRenderPerView`(每视口跑 cull/clear/visibility/shadow)；per-view 独立 RT bank = 独立时域历史；**不同相机**（`ActiveViewCameraAddress` + `MakeOrbitedCameraUbo`）。**未做**：§6 屏障范围收敛到活跃 bank（`InitializeBarriers` 仍遍历整表）；SHARC/shadow 目前所有视口共享。 |
| Phase 4 gkNextEditor 接入 | 🟡 主体完成 | `f162cf99` | **多相机面板 ✅**（`Panels/CameraViewPanel.cpp`，实时显示第二相机）。内容浏览器已接入当前已加载 scene 的实时 RenderView 缩略图链路（`Panels/ContentBrowserPanel.cpp`，匹配 `currentScenePath` 的 scene 卡片显示 secondary offscreen sample slot），图片纹理资产按需加载真实缩略图；Material Browser 已接入单 RenderView 球体预览 scene：逐材质渲染到 secondary view，再 blit 到 per-material sample slot 缓存；thumbnail UBO 使用预览 scene 的相机和 square thumbnail extent，不走主相机 delegate。**未做**：transient view 渲任意资产/小 scene 缩略图缓存与落盘。 |
| Phase 5 多 scene 同屏 | ⚪ 未开始 | | |
| Phase 6 独立 OS 窗口 | ⚪ 不做（本期） | | |

### 0.2 已实现的关键 API / 代码位置

- **bank 解析**：`Assets::Bindless::ViewRT(base, slot)` + `kViewRtBankStride=256`（`assets/shaders/common/BindlessTexture.slang`）；`GPUScene.custom_data_0` = active view bank base，由 `Scene::BuildGPUScene` 注入（`Scene.cpp`）。
- **shader 取 RT**：屏幕空间槽用 `Bindless.GetViewStorageTexture<T>(RT_X)`（GPUScene 管线）；**custom-push-constant 管线**用 `pushConsts._ViewHdr_custom_data_0` 作 base（见 0.3）。全局槽（RT_SWAPCHAIN/REMOTE/TEMP）保持 absolute。
- **C++ 取 RT image**：`VulkanBaseRenderer::GetViewStorageImage(slot)` = `GetStorageImage(activeViewBankBase_ + slot)`。
- **active view 状态**：`SetActiveViewBankBase / ActiveViewBankBase`、`SetActiveViewCameraAddress / ActiveViewCameraAddress`、`activeVisibilityFrameBuffer_`（per-view visibility framebuffer 指针）。
- **第二视口入口**：`VulkanBaseRenderer::DemoRenderSecondView`（在 `Render()` 末尾、present barrier 前调用）——目前是 demo/单一第二视口的实现，**Phase 5 要泛化成 `RenderViewManager::CreateView` + 视口列表循环**。
- **离屏输出**：`secondaryOffscreenImage_`(SAMPLED|TRANSFER_DST) + `secondaryOffscreenSampler_`，`GlobalTexturePool::BindSampleTexture(idx, view, sampler)`，slot=`kSecondaryViewSampleSlot`。runtime 开关 `SetSecondaryViewEnabled` / `IsSecondaryViewReady`。
- **编辑器面板**：`src/Application/Editor/gkNextEditor/Panels/CameraViewPanel.cpp`，用 `ctx.ui.RequestImTextureIdRaw(slot)` 显示。

### 0.3 踩过的坑（务必读，省下大量时间）

1. **custom-push-constant 管线读不到 `gpuScene.custom_data_0`**（最隐蔽、卡最久）。`ZeroBindCustomPushConstantPipeline`（reproject/atrous/upscale/bufferclear/visualdebugger）push 的是自定义结构体，**不 push GPUScene**；Slang 把 shader 里的 `gpuScene` 和 `pushConsts` 两个 `[[vk::push_constant]]` **顺序排布、不在 offset 0 重叠**，所以这些 shader 读 `gpuScene.custom_data_0` 永远是 0（primary base 0 时碰巧正确）。**解法（owner 设计）**：把 GPUScene 的 4-uint header(`SwapChainIndex`+`custom_data_0/1/2`)挪到结构体**最前**（C++/Slang 三份定义都改，size 仍 128），`ZeroBindCustomPushConstantPipeline::BindPipeline` 自己 **stamp** 这 16B header(custom_data_0=ActiveViewBankBase) 到 offset 0、caller params 放 offset 16；custom shader 的 `PushConsts` 前面补 16B header 并从 `pushConsts._ViewHdr_custom_data_0` 取 base。详见 `c7db89b7`。
2. **第二视口全黑 = visibility/minigbuffer pass 没为它跑**。`PreRender` 原来只为主视口做 visibility，第二视口 bank1 的 minigbuffer 是空的 → deferred 主命中重建(`FVisibilityBufferRayCaster`，PathTracing 和 SwModernNoAmbient 都用)全 miss → atrous/compose 塌成黑。解法=`PreRenderPerView` 为每视口跑 visibility + 给第二视口独立 visibility framebuffer（color=bank1 `RT_MINIGBUFFER_DRAW`，depth 经共享 render pass 复用主视口 depth——顺序渲染所以安全）。详见 `9cd800f0`。
3. **dynamic-slot（push-const/UBO 里的槽号值）也要 +base**。atrous 的 In/Out slot、reproject 的 Prev*、`Camera.Denoise*SourceSlot`、TemporalResolve 的 history slot 都是 C++ 算出的*绝对*屏幕空间槽；shader 里要 `ViewRT(base, slotValue)` 包一层（GPUScene 管线用 `gpuScene.custom_data_0`，custom 管线用 `pushConsts._ViewHdr_custom_data_0`），CopyToHistory 这种纯 C++ copy 在 C++ 端 +base。
4. **ImGui 显示离屏纹理**：`RequestImTextureId(slot)` 会因 slot 没有对应的注册 `TextureImage` 而返回 0（面板空白）；要用新加的 `RequestImTextureIdRaw(slot)`，并且 `DecodeBindlessTextureId` 的上限从 `TotalTextures()` 放宽到 `GlobalTexturePool::kMaxBindlessSlots`(65535)（显式 BindSampleTexture 的 render-view slot 在注册纹理范围之外）。
5. **ImGui 面板被拆成独立 OS 窗口截不到**：编辑器开了 ImGui multi-viewport，浮动窗口会变成单独 platform window（`editor.ini` 里是 `ViewportPos`/`ViewportId` 而非 `Pos`）。面板里 `ImGui::SetNextWindowViewport(GetMainViewport()->ID)` 钉回主窗口。
6. **编辑器 ImGui 状态文件是 `editor.ini` 不是 `imgui.ini`**（`out/build/<preset>/`）。调面板默认尺寸/位置时若 stuck，删 `editor.ini` 让 `FirstUseEver` 生效。
7. **HDR swapchain 下离屏面板偏暗**：`RT_DENOISED` 存的是 swapchain 编码值（HDR10 是 PQ），ImGui 当线性采样→偏暗。`--forcesdr` 下完美。要彻底修需为离屏单独做 SDR tonemap，目前可接受。
8. **GPUScene 三份定义**（`BasicTypes.slang`：C++ `#ifdef __cplusplus`、Slang 非 Apple packed `uint64_t2`、Slang Apple）必须同序，改 header 顺序时三份都要改，且 `static_assert(sizeof(GPUScene)==128)` 不能破。
9. **集成测试既有非确定后台线程崩溃**（`EngineTestFixture`，每次命中不同 test）——是既有 flaky，非多视口回归。验证回归用 `gnb shot` 像素对比，别被单测崩溃误导。

### 0.4 验证手段

- 默认渲染零回归：`gnb shot --scene assets/models/playground.glb`（默认是 PathTracing+SHARC）。
- 第二视口 PiP（无需编辑器）：`GK_MV_DEMO=1 gkNextRenderer --load-scene <X> --agent-validation`，右下角出 orbit 第二相机。
- 编辑器面板：`gnb shot --target gkNextEditor --scene <X> --ui`，左上 Camera View 面板出第二相机（HDR 偏暗，加 `--forcesdr` 看真彩）。

### 0.5 建议的下一步（按优先级）

1. **Phase 4 收尾：内容浏览器缩略图**。当前已完成"当前已加载 scene 卡片显示实时 RenderView 输出"的最小接入、普通图片纹理资产的真实缩略图、Material Browser 单 RenderView 球形材质缩略图缓存。下一步可继续扩展任意资产/小 scene 的 `EViewSchedule::kTransient` 缩略图与落盘，并把 Material Browser 缩略图调度做成可配置队列/缓存上限。
2. **泛化为多视口列表**：把 `DemoRenderSecondView` 的"单一第二视口"重构成 `RenderViewManager` 持有 `std::vector<RenderView>`，每个 view 自带 bank/camera UBO/visibility framebuffer/离屏图；`FBankAllocator` 已就绪（上限 8 banks）。编辑器面板支持新建/删除多个相机视口。
3. **Phase 5 多 scene**：`RenderView` 挂独立 `scene`，`PreRenderSceneGlobal` 按 scene 去重。
4. **性能**：§6 屏障收敛到活跃 bank；profiler 每 view 计时；`kOnDemand` 静止不渲。
5. **SHARC/shadow per-view**（若要不同相机的精确 GI/阴影）：目前共享主视口的，第二相机下略不精确但可用。

---

## 1. 目标与范围

### 1.1 目标

让引擎支持在**同一帧内渲染多个相互独立的视图**，每个视图可以：

- 观察**同一个 scene 的不同相机**（单 scene 多相机：分屏、编辑器多视角、后视镜 / 小地图）。
- 观察**各自独立的小 scene**（缩略图 / 资产预览：内容浏览器图标、材质球预览、prefab 缩略图）。
- 把结果**合成到主窗口的子矩形（分屏）**，或**渲染到离屏纹理**后通过 `ImGui::Image` 显示在编辑器面板里、或拷回生成缩略图文件。

并且充分利用引擎现有的两大特性，使新增视图的成本尽量低：

- **GPU Scene 作为"根描述"**：相机本质是 128 字节 push constant 里的一个 buffer device address（`GPUScene.Camera`），换视角 ≈ 换一个指针；scene 几何 / 材质 / TLAS 通过设备地址共享，多相机同 scene **零几何复制**。
- **Bindless 渲染目标**：所有 RT（G-buffer / 累积 / 历史 / 降噪）都是按整型 slot 索引的 bindless storage image，"每视口一套 RT" = 给该视口分配一段 **slot bank**，把 bank 基址通过 `GPUScene.custom_data_0` 传给 shader 即可，**无需新增 descriptor set / 改 pipeline layout**。

### 1.2 非目标（本期不做）

- **独立 OS 窗口 / 多 swapchain**（多显示器、撕下面板成独立窗口的场景渲染）。本期所有视图要么进主 swapchain 子矩形，要么离屏到纹理。见 §8。
- **跨视图并行命令录制 / 多队列 / async-compute 子视图**。首期所有视图在主 graphics queue 上**顺序录制**到同一个 command buffer。
- **视图独立的世界空间 GI 体系拆分**：同 scene 多相机**共享** SHARC 世界辐射缓存、AmbientCube、TLAS、skinning（见 §4.4），不为每个相机单独烘焙世界空间 GI。
- **VR / 立体 / 多 view instancing（`VK_KHR_multiview`）**。本期是 N 次独立 dispatch，不做单 pass 多 view。

### 1.3 成功判据

- 编辑器中可同时显示**主视口 + ≥1 个第二相机视口**，二者相机独立、各自有稳定的 TAA / 累积历史，画面无相互污染。
- 内容浏览器可对一个资产 / 小 scene **离屏生成缩略图纹理**并显示，且支持渲染到收敛后**拷回为图片文件**。
- 主视口在引入 RenderView 抽象后**像素级回归无变化**（`gnb shot` / `gkNextVisualTest` 基线对齐）。
- 新增一个持久第二视口的**增量显存**可预算、可上限（见 §6 显存模型）。

---

## 2. 现状分析（实现前必读）

> 以下为当前引擎与多视口相关的关键事实与约束，全部带 `文件:行号` 引用，便于接手 agent 直接定位。

### 2.1 单视口、单 swapchain、单 scene 的渲染主循环

`Vulkan::VulkanBaseRenderer` 目前持有**唯一**的一套帧资源与场景：

- 唯一 swapchain / depth buffer / per-image uniform buffers（`FrameResources`，`src/Engine/Rendering/VulkanBaseRenderer.hpp:231-253`）。
- 唯一 scene（`std::weak_ptr<Assets::Scene> scene_`，`VulkanBaseRenderer.hpp:311`；`GetScene/SetScene` 在 `VulkanBaseRenderer.cpp:406-414`）。
- 主循环 `DrawFrame`：acquire → `UpdateUniformBuffer` → `PreRender`（skinning / GPU cull / shadow / visibility / AS）→ `Render`（logic renderer 写 RT → resolve/compose 进 swapchain）→ `PostRender`（ambient bake）→ upscaler/imgui → submit → present（`VulkanBaseRenderer.cpp:1180-1351`）。
- `Render()` 当前对**单个 current logic renderer**调用一次，再 compose（`VulkanBaseRenderer.cpp:1408-1536`）。

### 2.2 渲染目标是"单例、全屏、固定 slot"的 bindless storage image

`CreateRenderImages()` 一次性创建**一整套**全屏 RT，按 `Bindless::RT_*` 固定 slot 注册进 bindless storage 数组（`VulkanBaseRenderer.cpp:740-813`）。slot 常量定义在 `assets/shaders/common/BindlessTexture.slang`：

- 屏幕空间 RT：`RT_ACCUMULATE_DIFFUSE=0` … `RT_GTAO=30`（每视口私有的核心区）。
- 全局 / 共享 slot：`RT_SWAPCHAIN0..2 = 100..102`、`RT_REMOTE_ENCODE0..3 = 60..67`、`RT_TEMP_USAGE0 = 50`。
- `RT_COUNT = 128`。
- Bindless 三个数组：`SampleTextureArray`(set0,binding0)、`StorageTextureArray`(set0,binding1，RT_* 在此)、`ShadowMapArray`(set0,binding2)。

**这是多视口最核心的约束**：当前只有"一套"屏幕空间 RT，全屏尺寸，固定 slot。要让每个视口有完整时域历史，必须给每个视口一套独立的屏幕空间 RT（见 §4.2）。

### 2.3 相机 = GPU Scene 根描述里的一个设备地址

`Scene::BuildGPUScene(imageIndex)` 组装 128 字节 push constant（`src/Engine/Assets/Core/Scene.cpp:657-680`）：

- `gpuScene.Camera = renderer.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress();`（`Scene.cpp:660-661`）——**相机就是一个指向 `UniformBufferObject` 的设备地址**。
- `gpuScene.SceneDynamicBase / Vertices / Indices / Offsets / TLAS / AmbientBase …` 都是设备地址（共享 scene 数据的关键）。
- `gpuScene.SwapChainIndex = imageIndex;`（`Scene.cpp:678`）当前被合成 pass 当作"输出到 `RT_SWAPCHAIN0 + imageIndex`"的索引。
- 末尾有 **`custom_data_0 / 1 / 2` 三个空闲 uint**（`GPUScene` 定义见 `assets/shaders/common/BasicTypes.slang` 的 `#ifdef __cplusplus` 块，`static_assert(sizeof(GPUScene)==128)` 在 `Scene.cpp:114`）。**`custom_data_0` 正是本设计要用来携带"视口 RT bank 基址"的字段。**

每个 compute pipeline 都以 `GPUScene` 作为 push constant，并提供一个**直接吃外部 `GPUScene` 的重载**：`ZeroBindPipeline::BindPipeline(cmd, const Assets::GPUScene& gpuScene)`（`src/Engine/Rendering/PipelineCommon/CommonComputePipeline.cpp:133-135`）。`PathTracingRenderer::BuildSharcGPUScene` 已经用这条路径在 `ReservedAddress0` 注入 SHARC 地址（`PathTracingRenderer.cpp:304-308`），**这就是 RenderView 注入"自定义相机地址 + RT bank 基址 + 输出 slot"的现成接口**。

### 2.4 相机 UBO 与时域状态目前是"全局单份"

`NextEngine::GetUniformBufferObject(offset, extent)` 按相机 + 视口 extent 组装 UBO（`src/Engine/Runtime/Engine.CameraUbo.cpp:59-287`）。需要注意几处**目前是单份、必须改成 per-view 的状态**：

- `ubo.ViewportRect` 直接取主 swapchain 的 RenderOffset/RenderExtent（`Engine.CameraUbo.cpp:129-131`）。
- `renderState_.previousUniformBuffer`（上一帧 VP，用于 motion vector / TAA）是**单份全局**（`Engine.CameraUbo.cpp:122-127, 284`）。
- `renderState_.cachedSunCascades` + 各 dirty/update mask（CSM 级联缓存）是**单份全局**（`Engine.CameraUbo.cpp:135-188`）。
- progressive 累积计数（`progressiveRender_`）、denoiser 源 slot 路由（`ubo.DenoiseDiffuseSourceSlot`，`Engine.CameraUbo.cpp:237-242`）也是全局。

这些都属于"屏幕空间时域历史"的一部分，多视口下**必须按视口各存一份**（见 §4.3 `FViewRenderState`）。

### 2.5 SwapChain 已支持"渲染子矩形"和"输出子矩形"

`SwapChain` 区分 RenderViewport（`RenderExtent/RenderOffset`）与 OutputViewport（`OutputExtent/OutputOffset`）（`src/Engine/Vulkan/SwapChain.hpp:32-45`），compose pass 已能把结果 blit/dispatch 进 swapchain 的子矩形——reference 模式就是把不同 renderer 合成到 `column*W/2, row*H/2` 的子矩形（`VulkanBaseRenderer.cpp:1430-1444`）。**分屏合成的底层能力已经存在**，多视口要做的是把它一般化为"按视口列表合成"。

### 2.6 编辑器已能把任意 bindless 纹理喂给 ImGui

`UserInterface::RequestImTextureId(globalTextureId)` / `EncodeBindlessTextureId`（`src/Engine/Runtime/Editor/UserInterface.cpp:381-454`）把一个 bindless **sampled texture** slot 编码成 `ImTextureID`，内容浏览器用 `ImGui::Image(ui.RequestImTextureId(globalId), size)` 显示（`ContentBrowserPanel.cpp:618-625`）。**离屏 RenderView 只要把自己的输出图绑进 `SampleTextureArray` 的某个 slot，就能被编辑器面板直接 `ImGui::Image` 显示——这是首个接入点能"快速跑起来"的关键。**

### 2.7 离屏 / 隐藏窗口路径已存在

`--agent-validation` / `--hidden-window` / TUI 都用 `SDL_WINDOW_HIDDEN` 创建窗口、渲染到稳定帧后截图退出（`Engine.cpp:363-366, 423-425, 1338-1375`；`CaptureScreenShot` 在 `VulkanBaseRenderer.cpp:1038`）。缩略图离屏渲染可复用这套"渲染到收敛 → 拷回"的范式，**不需要可见窗口**。

### 2.8 GlobalTexturePool 是全局单例

`Assets::GlobalTexturePool::GetInstance()`（`src/Engine/Assets/GPU/Texture.hpp:40-115`）是**进程级单例**，bindless slot 绑定走 `BindStorageTexture(slot, view)` / `BindSampleTexture`。多视口的 RT bank、离屏输出图都注册到这同一个池子里——**这是"单 descriptor set + slot 分段"方案成立的物理基础**（所有视图共享同一张 bindless 表，只是用不同 slot 段）。

---

## 3. 设计总览

### 3.1 一句话架构

> 把当前 `VulkanBaseRenderer` 里"per-view 的东西"（一套屏幕空间 RT + depth + 相机 UBO + 时域状态 + logic renderer）抽出成一个**`RenderView`** 对象；`VulkanBaseRenderer` 改为**驱动一个 `RenderView` 列表**。主窗口画面就是"主 RenderView"。新增视口 = 新增一个 `RenderView`（拿自己的 RT bank、自己的相机地址、自己的输出 slot），渲染走和主视口完全相同的 logic renderer 代码，只是 `GPUScene.custom_data_0`（RT bank 基址）/ `GPUScene.Camera`（相机地址）/ 输出 slot 不同。

### 3.2 对象关系

```
NextEngine
 └── VulkanBaseRenderer (持有 device / swapchain / 全局共享资源 / pipelines)
      └── RenderViewManager
           ├── RenderView  #0  (primary, base bank 0, 输出= swapchain 子矩形)
           ├── RenderView  #1  (editor camera B, 离屏 → SampleTexture slot S1)
           ├── RenderView  #2  (thumbnail, 小 scene, 离屏 → slot S2, 渲染 N 帧后回收)
           └── ...
RenderView
 ├── Assets::Scene*               // 可与其它 view 共享（同 scene 多相机）
 ├── FViewCamera                  // 该 view 的相机（或相机回调）
 ├── VkExtent2D renderExtent      // 该 view 分辨率（可 ≠ 主窗口）
 ├── uint32_t  rtBankBase         // 该 view 的 bindless RT slot 段基址 → GPUScene.custom_data_0
 ├── ViewRenderTargets            // 该 view 私有的一套屏幕空间 RT（在 bank 段内）
 ├── DepthBuffer                  // 该 view 私有 depth
 ├── 多帧相机 UniformBuffer       // 该 view 的 Camera 设备地址来源
 ├── FViewRenderState             // prevUBO / sun cascade cache / progressive / denoise 路由
 ├── ELogicRendererType           // 该 view 用哪条管线（可与主视口不同）
 ├── OutputTarget                 // {kSwapchainSubrect: rect} 或 {kOffscreenTexture: sampleSlot, image}
 └── EViewSchedule                // kPersistent(每帧) | kTransient(渲染到收敛后回收) | kOnDemand(脏才渲)
```

### 3.3 与"独立 renderer"的关系（回应 owner 的取向）

owner 倾向"视口可能要实现为独立的 renderer"。本设计的折中是：

- **不**为每个视口克隆一整个 `VulkanBaseRenderer`（那会重复 device / swapchain / pipeline / 全局共享资源，代价过大、且 bindless 是单例表，多实例反而别扭）。
- **而是**把"一个 renderer 里真正 per-view 的部分"抽成 `RenderView` + 它自己的 `LogicRendererBase` 实例。`LogicRendererBase` 本来就是 per-技术对象、构造时只引用 `baseRender_`（`VulkanBaseRenderer.hpp:374-405`），**让每个 RenderView 持有自己的 logic renderer 实例**即可获得"独立 renderer 般"的隔离（独立历史、独立累积），同时共享 device 与全局资源。这既满足"独立性"，又不浪费。

---

## 4. 核心机制详解

### 4.1 用 GPU Scene 根描述切换相机与视口参数（零几何复制）

每个 RenderView 每帧自己组装一份 `GPUScene`（复用 `Scene::BuildGPUScene` 再覆盖少数字段，类似 `BuildSharcGPUScene`）：

```cpp
Assets::GPUScene gs = view.scene->FetchGPUScene(imageIndex); // 共享几何/材质/TLAS 设备地址
gs.Camera        = view.CameraDeviceAddress(frameIndex);     // ← 该 view 的相机 UBO 地址
gs.custom_data_0 = view.rtBankBase;                          // ← 该 view 的 RT slot 段基址
gs.custom_data_1 = view.outputSampleSlot; // 离屏输出 slot（或沿用 SwapChainIndex 走子矩形）
// 之后所有 pass 用 BindPipeline(cmd, gs) 这条重载绑定
```

- 同 scene 多相机：`SceneDynamicBase / Vertices / Indices / TLAS / AmbientBase` **完全共享**，只有 `Camera` 不同 → 几何零复制。
- 多 scene：把 `gs` 换成另一个 `scene->FetchGPUScene()` 即可，scene 几何各自独立。

### 4.2 用 Bindless slot 分段给每个视口一套独立 RT（完整时域历史）

**问题**：屏幕空间 RT（`RT_ACCUMULATE_DIFFUSE` … `RT_GTAO`，约 0..30）当前是单例全屏，多视口要各一套。

**方案（选定）—— RT slot bank 分段 + push constant 携带基址**：

- 定义 **bank 尺寸** `kViewRtBankStride`（建议 256，给屏幕空间 RT 0..~50 + 余量）。
- **Bank 0 = 现有全局布局**，绝对 slot 完全不变（主视口、`RT_SWAPCHAIN*`、`RT_REMOTE_ENCODE*`、`RT_TEMP_USAGE0` 等真正全局的 slot 都留在 bank 0，**向后兼容、主视口零回归**）。
- 视口 k（k≥1）的私有屏幕空间 RT 落在 `[k*kViewRtBankStride + RT_X]`。基址 `k*kViewRtBankStride` 通过 `GPUScene.custom_data_0` 传给 shader。
- **Slang 侧加一个解析 helper**，把"逻辑 RT 常量"翻译成"实际 bank 内 slot"：

  ```hlsl
  // BindlessTexture.slang 新增
  // viewBase 来自 GPUScene.custom_data_0；屏幕空间 RT 加 base，全局 RT 用绝对 slot
  public int ViewRT(uint viewBase, int rtSlot) { return int(viewBase) + rtSlot; }
  ```

  shader 里 `GetStorageTexture(RT_ACCUMULATE_DIFFUSE)` → `GetStorageTexture(ViewRT(gs.custom_data_0, RT_ACCUMULATE_DIFFUSE))`。**主视口 `custom_data_0==0`，行为不变**。

- **哪些 slot 加 base、哪些不加**：屏幕空间、与相机绑定、需要历史的 RT（diffuse/spec 累积、单帧、minigbuffer、objectid、motion、albedo/normal、hitdist、atrous ping/pong、ambient、gtao、prev-depth、单帧 prev）→ **加 base，每视口一套**。真正全局 / 世界空间 / 输出类（swapchain、remote encode、shadow map 在独立 binding、SHARC/Ambient 是设备地址 buffer 而非 bindless image）→ **不加 base，共享**。需要在 §7 Phase 1 给出一张**"逐 RT slot：私有 vs 共享"清单**并据此改 shader。

**为什么不用"每视口一个 descriptor set / 多套 pipeline"**：整个引擎围绕"单 bindless set + GPUScene push constant 作为根"构建，pipeline layout 只有一个 push constant range（`CommonComputePipeline.cpp:55-90`）。slot 分段方案**不改 pipeline layout、不改 descriptor set、不改 BindPipeline 签名**，与现有"GPU Scene 作根 + bindless"哲学完全一致，是改动面最小的路径。代价是要把 `ViewRT(base, …)` 机械地铺到所有碰 RT 的 shader——量大但都是同质修改，且可 Phase 化（先让主视口 base=0 全程跑通，再逐 pass 接 base）。

**显存**：每多一个**持久全历史**视口 ≈ 多一套屏幕空间 RT。按视口分辨率而非主窗口分辨率分配（缩略图 256² 几乎免费；编辑器第二视口可用 0.5x 分辨率）。见 §6。

### 4.3 每视口独立的时域状态：`FViewRenderState`

把 §2.4 列的全局时域状态搬进 per-view 结构：

```cpp
struct FViewRenderState
{
    Assets::UniformBufferObject previousUniformBuffer{};   // 上一帧 VP（motion vector/TAA）
    Assets::CascadeShadowSetup  cachedSunCascades{};       // CSM 级联缓存
    uint32_t sunShadowDirtyMask = 0, sunShadowInitializedMask = 0, sunShadowCascadeUpdateMask = 0;
    bool     cachedSunCascadesValid = false;
    uint32_t progressiveFrame = 0;                          // progressive 累积计数
    bool     resetHistory = true;                           // 相机跳变/视口尺寸变 → 清历史
    // denoiser 源 slot 路由（按 view base 解析）
};
```

`GetUniformBufferObject` 改为 `GetUniformBufferObject(const RenderView& view, FViewRenderState& st, offset, extent)`，所有读写 `renderState_.*` 的地方改读 `st.*`，`ViewportRect` 用 view 自己的 offset/extent。主视口的 `FViewRenderState` 就是从现在的 `renderState_` 平移过来——**Phase 0 做这步抽取时保证主视口逐字节等价**。

### 4.4 帧编排：哪些 pass per-view、哪些 per-scene

一帧的 pass 分两类：

| Pass | 依赖 | 多视口下 |
| --- | --- | --- |
| Skinning / BLAS / TLAS build | **scene 几何**（与相机无关） | **每 scene 每帧一次**（同 scene 多相机只做一次） |
| Ambient cube / voxel sky-vis bake、SHARC update/resolve | **世界空间**（与相机弱相关） | **每 scene 每帧一次**，多相机共享世界缓存（大收益） |
| GPU cull（视锥）、Shadow CSM、Visibility buffer | **相机** | **每 view 一次**（落在 view 的 RT bank / depth） |
| Logic renderer 主着色 + 降噪 + compose | **相机 + view RT** | **每 view 一次** |

编排顺序（单 command buffer、顺序录制）：

```
BeforeNextFrame()
for each scene used this frame:           // 去重
    PreRenderSceneGlobal(scene)           // skinning / AS / ambient / SHARC（一次）
for each active RenderView (主视口优先):
    UpdateViewUniformBuffer(view)         // 用 view 的 FViewRenderState
    gs = BuildViewGPUScene(view)          // Camera 地址 + custom_data_0 bank 基址 + 输出 slot
    PreRenderPerView(view, gs)            // GPU cull / shadow / visibility（写 view bank）
    view.logicRenderer->Render(cmd, gs)   // 主着色（写 view bank 的 RT）
    ComposeViewOutput(view, gs)           // → swapchain 子矩形 或 离屏 SampleTexture slot
PostRender()/ imgui (delegates_.postRender)   // ImGui::Image 采样离屏 view 输出
submit / present
```

**屏障**：scene-global 资源（TLAS / ambient / SHARC / skinned vertices）在所有 view 读之前要有一道写后读屏障；各 view 的私有 RT bank 互不重叠，view 间无需额外同步（但 view i 的 compose 写它自己的输出、imgui 读它，需 compose→fragment 的屏障）。`InitializeBarriers` 当前对整张 bindless storage 表插屏障（`VulkanBaseRenderer.cpp:1366-1373`），多 bank 后需按需缩小范围以免每帧 transition 过多图（性能项，见 §6）。

### 4.5 输出：分屏子矩形 vs 离屏到纹理

`RenderView::OutputTarget` 两种：

- **`kSwapchainSubrect{ VkRect2D rect }`**：compose 直接写主 swapchain 的子矩形（一般化现有 reference-mode 子矩形 compose，`VulkanBaseRenderer.cpp:1430-1444`）。用于分屏。
- **`kOffscreenTexture{ uint32_t sampleSlot; RenderImage color }`**：compose 写一张离屏 color image，并把它绑进 `SampleTextureArray[sampleSlot]`（走 `GlobalTexturePool::BindSampleTexture`）。编辑器面板 `ImGui::Image(ui.RequestImTextureId(sampleSlot), size)` 显示；缩略图落盘走 `CaptureScreenShot` 同款 image→host 拷贝。用于编辑器面板与缩略图。

### 4.6 视图调度策略 `EViewSchedule`

- `kPersistent`：每帧渲染（实时多相机）。
- `kOnDemand`：仅当相机 / scene / 选中态变化（脏）才重渲（编辑器静止预览，省 GPU）。
- `kTransient`：缩略图——创建 → 渲染到收敛（或固定 N 帧，路径追踪需多帧累积，光栅 1 帧即可）→ 回收 RT bank。配合一个**有上限的 view 池**循环利用 bank，避免缩略图风暴打爆显存（见 §6）。

---

## 5. 关键数据结构与 API 草案

> 仅为接口形态示意，命名 / 归属以实现时 `.clang-tidy` 规范为准（类型 PascalCase、成员 camelCase_）。

```cpp
// src/Engine/Rendering/RenderView.hpp（新增）
namespace Vulkan
{
    enum class EViewOutputKind { SwapchainSubrect, OffscreenTexture };
    enum class EViewSchedule   { Persistent, OnDemand, Transient };

    class RenderView
    {
    public:
        RenderView(VulkanBaseRenderer& owner, uint32_t bankBase, VkExtent2D extent);

        void SetScene(std::shared_ptr<Assets::Scene> scene);
        void SetCamera(const Assets::Camera& cam);          // 或 SetCameraProvider(cb)
        void SetLogicRenderer(ERendererType type);
        void SetOutputSwapchainSubrect(VkRect2D rect);
        void SetOutputOffscreen(uint32_t sampleSlot);
        void MarkDirty();                                   // OnDemand 用

        uint32_t      RtBankBase() const { return bankBase_; }
        uint32_t      OutputSampleSlot() const;             // 离屏时有效
        VkExtent2D    RenderExtent() const { return extent_; }
        FViewRenderState& State() { return state_; }

        Assets::GPUScene BuildGPUScene(uint32_t imageIndex) const; // 覆盖 Camera/custom_data_0/输出 slot
        // 由 RenderViewManager 调用的帧钩子
        void UpdateUniform(uint32_t imageIndex);
        void RecordPreRenderPerView(VkCommandBuffer cmd, uint32_t imageIndex);
        void RecordRender(VkCommandBuffer cmd, uint32_t imageIndex);
        void RecordComposeOutput(VkCommandBuffer cmd, uint32_t imageIndex);

    private:
        VulkanBaseRenderer& owner_;
        std::weak_ptr<Assets::Scene> scene_;
        uint32_t bankBase_;
        VkExtent2D extent_;
        ViewRenderTargets targets_;       // 在 bankBase_ 段内创建的私有 RT 集
        std::unique_ptr<DepthBuffer> depth_;
        std::vector<Assets::UniformBuffer> cameraUbos_;     // per frame-in-flight
        FViewRenderState state_;
        std::unique_ptr<LogicRendererBase> logicRenderer_;
        EViewOutputKind outputKind_; VkRect2D subrect_{}; uint32_t sampleSlot_ = 0;
        EViewSchedule schedule_ = EViewSchedule::Persistent;
    };

    class RenderViewManager
    {
    public:
        RenderView* CreateView(const FViewDesc& desc);      // 分配 bank、建 RT/depth/ubo
        void        DestroyView(RenderView* view);          // 回收 bank
        RenderView& PrimaryView();
        void        RecordFrame(VkCommandBuffer cmd, uint32_t imageIndex); // §4.4 编排
        // bank 分配器（slot 段），上限保护
    private:
        std::vector<std::unique_ptr<RenderView>> views_;
        FBankAllocator banks_;        // 以 kViewRtBankStride 为步长分配 slot 段
    };
}
```

GPUScene 侧无需新增字段（复用 `custom_data_0`=bankBase、`custom_data_1`=输出 slot 即可）；如需更多视口元数据，`custom_data_2` 仍空闲，或后续把 `ViewportRect` 已有字段善用起来（`BasicTypes.slang` 的 `UniformBufferObject.ViewportRect`）。

---

## 6. 显存与性能模型

- **单视口增量显存** ≈ 一套屏幕空间 RT（约 15~20 张 image）× **视口分辨率**。建议：
  - 缩略图视口固定小分辨率（128²~256²），可忽略。
  - 编辑器第二视口默认 0.5x 主分辨率，并允许只挂"轻管线"（如 `SoftwareModernNoAmbient`）以省历史图数量。
- **bank 上限**：`RenderViewManager` 设 `kMaxConcurrentBanks`（如 8），缩略图用 `kTransient` 池循环；超限拒绝创建或排队，杜绝显存爆炸。
- **顺序录制成本**：N 个持久视口 ≈ N 倍的 per-view pass（cull/shadow/visibility/着色）。同 scene 多相机共享 skinning/AS/ambient/SHARC，单帧成本 < N 倍。建议给 profiler 每个 view 包一层命名 scope（`SCOPED_GPU_TIMER`），便于在 `ProfileDebugOverlay` 看每视口耗时。
- **屏障范围**：多 bank 后 `InitializeBarriers` 不应再对整张表无脑 transition；按"本帧活跃视口的 bank + 全局 slot"收敛屏障范围（性能项，Phase 3 处理）。

---

## 7. 分阶段开发计划（路线图）

> 每阶段都给出**验证手段**。引擎层改动按 AGENTS.md 默认只构建受影响目标：`./gnb build gkNextRenderer gkNextUnitTests`，渲染回归用 `gnb shot` / `gkNextVisualTest`。

### Phase 0 — 抽取 RenderView，主视口零回归（纯重构，无新功能）  ✅ 已完成（见 §0）
- 把 `VulkanBaseRenderer` 中 per-view 的资源（一套 RT、depth、相机 UBO）与时域状态（`renderState_` → `FViewRenderState`）抽进 `RenderView`，主窗口实例化为 **PrimaryView，bankBase=0**。
- `GetUniformBufferObject` 改签名吃 `RenderView&` + `FViewRenderState&`；`ViewportRect` 改取 view 的 offset/extent。
- `RenderViewManager` 先只管一个 PrimaryView，`RecordFrame` 等价于现在的 `PreRender/Render/PostRender`。
- **验证**：`gnb shot --scene assets/models/playground.glb` 与现有基线**像素级一致**；`gkNextVisualTest` 全场景 baseline diff 无差异；`gkNextUnitTests` 通过。

### Phase 1 — Bindless RT bank 基址贯通（仍单视口，base 恒为 0）  ✅ 已完成（见 §0）
- `GPUScene.custom_data_0` 语义定为"view RT bank base"；`BasicTypes.slang` / `BindlessTexture.slang` 加 `ViewRT(base, rtSlot)` helper。
- 产出**逐 RT slot：私有 vs 共享**清单（§4.2），据此把屏幕空间 RT 的 `GetStorageTexture(RT_X)` 读写改为 `ViewRT(custom_data_0, RT_X)`；全局 slot（swapchain/remote/shadow）保持绝对。
- `ViewRenderTargets` 支持在任意 bankBase 段创建并注册 bindless slot；PrimaryView 仍用 base 0。
- **验证**：base 恒 0，画面应与 Phase 0 完全一致（`gnb shot` 基线对齐）。这一步是"机械改 shader 但不改行为"的安全网。

### Phase 2 — 离屏 RenderView 渲到纹理（首个可见新功能）  ✅ 已完成（见 §0）
- 实现 `EViewOutputKind::OffscreenTexture`：compose 写离屏 color image → 绑进 `SampleTextureArray[slot]`。
- 增加最小 API：`RenderViewManager::CreateView/DestroyView`，bank 分配器。
- 加一个**调试入口**（CVar 或编辑器临时面板）：对当前 scene 建第二个相机的离屏 view，`ImGui::Image` 显示。
- **验证**：编辑器里能看到第二相机的实时小窗；`gnb shot --target gkNextEditor --ui` 截图包含该面板。

### Phase 3 — 每视口完整时域历史 + per-view 编排  ✅ 核心完成（屏障收敛/SHARC per-view 待办，见 §0）
- 落地 §4.4 编排（scene-global pass 去重一次、per-view pass 各一次）与 §4.3 per-view 时域状态（独立 TAA / 累积 / CSM 缓存 / progressive）。
- 收敛 `InitializeBarriers` 屏障范围到活跃 bank。
- **验证**：主视口 + 第二视口各自相机运动时 TAA / 累积稳定、互不污染；切相机触发 `resetHistory` 不串味；profiler 显示两视口独立耗时。

### Phase 4 — gkNextEditor 接入（首个落地接入点）  🟡 多相机面板完成；缩略图待办（见 §0）
- **多相机面板**：编辑器可新建/删除"相机视口"（持久 view），dock 成面板，支持选相机、选管线、选分辨率倍率。
- **内容浏览器缩略图**：对资产 / 小 scene 用 `kTransient` view 离屏渲缩略图，缓存为 sample slot 纹理；可选"渲染到收敛后拷回 `.jpg` 缓存到磁盘"。
- 复用 `UserInterface::RequestImTextureId` 显示；复用 `CaptureScreenShot` 回拷落盘。
- **验证**：编辑器多相机面板可用；内容浏览器图标为真实渲染缩略图；`gnb shot --target gkNextEditor --ui` 回归。

### Phase 5 — 多 scene 同屏 + 小预览 scene（架构兑现）  ⚪ 未开始
- `RenderView` 支持挂独立 scene；`BuildViewGPUScene` 用各自 `scene->FetchGPUScene()`；编排里 scene-global pass 按 scene 去重。
- 小预览 scene（材质球 / prefab）独立加载、独立小 TLAS。
- view 池上限 / `kMaxConcurrentBanks` 保护与 LRU 回收。
- **验证**：同屏渲两个不同 scene 的 view 各自正确；并发缩略图压力下显存不超预算。

### Phase 6 —（可选 / 非本期）独立 OS 窗口、并行录制、multiview  ⚪ 不做
- 多 swapchain / 多 surface、跨 view 并行 command buffer、`VK_KHR_multiview`。仅在 owner 提出多显示器 / 撕下窗口需求时启动。见 §8。

---

## 8. 未来工作 / 明确不做

- **独立 OS 窗口（多 swapchain）**：撕下面板成独立窗口的"场景渲染窗口"。需要多 surface / present 管理，与 ImGui `MultiViewportBackend` 协作。本期离屏到纹理已覆盖编辑器面板诉求，故不做。
- **跨视口并行 / async-compute / 多队列**：首期顺序录制足够；视口数量上规模后再考虑并行。
- **`VK_KHR_multiview` 单 pass 多视图**：相机仅平移/小角度差异时（立体、阵列相机）可大幅省 pass，本期 N 次独立 dispatch 优先保正确性与简单。
- **每视口独立世界空间 GI**：同 scene 多相机共享 SHARC / AmbientCube；若未来要每相机独立 GI 再议。

---

## 9. 风险与缓解

| 风险 | 说明 | 缓解 |
| --- | --- | --- |
| Shader 改动面大 | `ViewRT(base, …)` 要铺到所有碰屏幕空间 RT 的 shader | Phase 1 base 恒 0 做安全网（行为不变）；逐 pass 接入并 `gnb shot` 对基线 |
| 主视口回归 | 抽取 RenderView 可能引入细微差异 | Phase 0/1 以"像素级一致"为硬性验收，bank 0 = 现状布局，全局 slot 不动 |
| 显存膨胀 | 持久全历史视口 / 缩略图风暴 | 视口分辨率倍率、轻管线、`kTransient` 池、`kMaxConcurrentBanks` 上限、LRU 回收 |
| per-view pass 成本 | N 视口 ≈ N 倍着色 | scene-global pass 去重；`kOnDemand` 静止不渲；profiler 每 view 计时 |
| 时域串味 | 共用历史导致相机间污染 | `FViewRenderState` 每视口一份；相机/尺寸跳变触发 `resetHistory` |
| 命名混淆 | 与 ImGui `MultiViewportBackend` 撞概念 | 统一用 `RenderView` / `RenderViewManager`，文档与代码注释标注区别 |
| ImGui 纹理生命周期 | 离屏图 resize / 销毁时 `ImTextureID` 失效 | sample slot 稳定复用、销毁前 `vkDeviceWaitIdle` / 延迟回收，参考现有 bindless 纹理路径 |
| 屏障过宽 | 多 bank 后整表 transition 拖慢 | Phase 3 收敛屏障到活跃 bank + 全局 slot |

---

## 10. 关键源码索引（接手 agent 起步点）

- GPU Scene 根描述 / `custom_data_*`：`assets/shaders/common/BasicTypes.slang`（`GPUScene` 结构）；`src/Engine/Assets/Core/Scene.cpp:657-687`（`BuildGPUScene/FetchGPUScene`，Camera 地址 `:660-661`，`SwapChainIndex :678`）。
- 吃外部 GPUScene 的绑定重载（注入点）：`src/Engine/Rendering/PipelineCommon/CommonComputePipeline.cpp:97-135`；现成范例 `src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp:304-308`（`BuildSharcGPUScene`）。
- Bindless slot 定义 / 数组：`assets/shaders/common/BindlessTexture.slang`。
- RT 单例创建（要改成 per-view bank）：`src/Engine/Rendering/VulkanBaseRenderer.cpp:740-813`（`CreateRenderImages`）。
- 主循环 / 编排：`src/Engine/Rendering/VulkanBaseRenderer.cpp:1180-1351`（`DrawFrame`）、`:1408-1536`（`Render`+compose，子矩形 `:1430-1444`）、`:1563-1567`（`UpdateUniformBuffer`）、`:1366-1373`（`InitializeBarriers`）。
- 相机 UBO / 时域状态（要 per-view 化）：`src/Engine/Runtime/Engine.CameraUbo.cpp:59-287`（`ViewportRect :129-131`，`previousUniformBuffer :122-127,284`，CSM 缓存 `:135-188`）。
- SwapChain 渲染 / 输出子矩形：`src/Engine/Vulkan/SwapChain.hpp:32-45`。
- LogicRenderer 基类 / 注册：`src/Engine/Rendering/VulkanBaseRenderer.hpp:278-282, 374-405`。
- 编辑器 ImGui 显示 bindless 纹理（离屏接入点）：`src/Engine/Runtime/Editor/UserInterface.cpp:381-454`；`src/Application/Editor/gkNextEditor/Panels/ContentBrowserPanel.cpp:618-625`。
- 离屏 / 截图回拷范式：`src/Engine/Rendering/VulkanBaseRenderer.cpp:1038`（`CaptureScreenShot`）；`src/Engine/Runtime/Engine.cpp:1338-1375`（agent validation 渲染到帧后截图退出）。
- 全局纹理池（slot 绑定）：`src/Engine/Assets/GPU/Texture.hpp:40-115`；`VulkanBaseRenderer.cpp:791-794`（`BindStorageTexture`）。
- 命名勿撞的 ImGui 平台多视口：`src/Application/Editor/Common/MultiViewportBackend.cpp`。
