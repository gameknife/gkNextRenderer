---
title: "VulkanBaseRenderer 架构、LogicRenderer 与渲染正确性审计"
category: note
status: 待核对
owner: engine
created: 2026-07-11
last_updated: 2026-07-11
---

# VulkanBaseRenderer 架构、LogicRenderer 与渲染正确性审计

> 目的:对 `src/Engine/Rendering/` 下的 VulkanBaseRenderer 及各 LogicRenderer 做一次完整的静态审计——架构现状、各渲染路径的实际设计、帧流程正确性检查,并列出问题与改进空间。**所有条目均为代码阅读结论,尚未逐条实测验证**;每条带置信度标注,后续核对时按编号勾销或修订。
>
> 审计范围:`VulkanBaseRenderer.{hpp,cpp}` 及三个分部 TU(GpuDriven / GiBake / RayTracingAS)、`RenderView.{hpp,cpp}`、`Preview/RenderViewServices`、五个 LogicRenderer、`PipelineCommon/{AtrousDenoiser,TemporalResolve}`、`ViewCameraUboBuilder`、以及关联的 `Engine.CameraUbo.cpp`、`Scene::BuildGPUScene`、`Modules/RenderViews/OffscreenRenderViewController`。

---

## 1. 架构总览

### 1.1 分层与职责

```
VulkanBaseRenderer (设备/交换链/帧循环/共享资源/调度)
 ├── DeviceContext      instance / surface / device / 2×commandPool / profiler / GlobalTexturePool
 ├── FrameResources     swapchain / depth / commandBuffers / 同步对象 / per-image UBO / submit serial
 ├── BindlessStorageImages   全部 screen-space RT(按 RenderView bank 分段)
 ├── RayTracingResources     BLAS/TLAS + instance buffer + HW bake pipeline(可选)
 ├── SkinnedMeshResources    蒙皮顶点/关节 buffer + skinning compute
 ├── AmbientCubePipelines    软/硬 ambient cube bake + clear
 ├── OverlayPipelines        visibility / wireframe / CSM / GPU cull / clear / FSR / debugger / 外部 pass
 ├── LogicRendererRegistry   type → LogicRendererBase 实例(懒创建,current 指针)
 ├── RenderViewManager       primary(bank 0)+ additional views(bank 1..7)+ 帧内 schedule
 ├── RenderViewServices      IRenderViewProvider 列表(缩略图/离屏相机/reference 四分屏)
 └── IUpscaler               DLSS/Streamline 封装(经 UpscalerRegistry 注入)
```

扩展点(模块侧注入,避免 Engine 反向依赖模块):

| 扩展点 | 机制 | 用途 |
| --- | --- | --- |
| `IDeviceCreationAugmenter` | 设备创建时链入扩展/feature | NextRemote 视频编码、Streamline |
| `IExternalRenderPass` + `RegisterExternalPassFactory` | 交换链资源期创建、主视口后执行 | DevTools AuxDraw、Splat pass |
| `IRenderViewProvider` + `RenderViewServices` | 每帧调度辅助 RenderView | 离屏相机、缩略图、reference 模式 |
| `UpscalerRegistry` | 启动时注册 IUpscaler 实现 | Streamline DLSS/DLSSG/Reflex |
| `Delegates`(onDeviceSet / createSwapChain / getUniformBufferObject / postRender / afterSubmit …) | Engine 层回调 | 相机 UBO 组装、UI、远程推流 |

这套"registry + delegate"注入总体是健康的:Rendering 层不 include 模块代码,模块在安装期注册工厂。

### 1.2 帧生命周期(DrawFrame 时序)

`DrawFrame()`([VulkanBaseRenderer.cpp:1417](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1417)):

1. `requestRecreateSwapChain_` → 重建并 return。
2. Streamline `BeginFrame` / Reflex sleep / SimulationStart 标记。
3. `BeforeNextFrame()`:所有 LogicRenderer + RenderViewServices + engine tick 回调。
4. `AcquireNextImageKHR`(imageAvailableSemaphores[currentFrame])。
5. **等待上一次 submit 的 fence + 本 frame slot 的 fence**(见 C4:实际单帧在飞)。
6. `Scene::UpdateNodes()` + GPU-driven buffer 扩容 → 需要时 `AfterUpdateScene()`(重建 TLAS instance 列表)。
7. `UpdateUniformBuffer(imageIndex)`:engine delegate 组装 UBO → 写 per-image UBO + primary RenderView 的 camera UBO ring。
8. 录制 command buffer:
   - `BeginSceneFrame`(场景全局 pre-pass,一帧一次):TLAS update → skinning buffer 上传 → `InitializeBarriers`(全部 bindless RT UNDEFINED→GENERAL)→ ambient cache 失效处理 → skinning dispatch → BLAS update。
   - `Render`:清空 schedule → 调度 primary(或 reference 四分屏)→ `DispatchScheduledRenderViews` 逐视口执行 `RenderViewToBank` → 外部 pass → wireframe overlay → `ResolvePrimaryViewToSwapchain`(DLSS / FSR / blit)→ 辅助视口调度与执行 → 转 PRESENT 布局。
   - `PostRender`:ambient cube cascade bake(分帧摊销)+ visual debugger。
   - DLSSG hudless 捕获 + TagFrameGeneration。
   - UI delegate(`postRender`)。
9. Submit(renderFinishedSemaphores[currentFrame] + 排队的外部 timeline 信号量)→ Present → `afterSubmit` delegate → currentFrame 前进。

### 1.3 每视口渲染(RenderViewToBank)

[VulkanBaseRenderer.cpp:1312](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1312):`FActiveRenderViewScope` 把 bank base / render extent / camera 地址 / visibility framebuffer / scene override 压入 renderer 的"活动视口"槽位(RAII 恢复),然后:

```
(非 primary 且 prevDepth 无效: 先 DispatchClearPass 初始化 RT_PREV_DEPTHBUFFER)
PreRenderPerView:   DispatchGpuCulling → DispatchClearPass → DispatchVisibilityPass → DispatchSunShadow
logicRenderer.Render(...)
(可选)CopyObjectIdHistory
```

shader 侧通过 `GPUScene.custom_data_0` 拿 bank base,`Bindless::ViewRT(base, slot)` 解析槽位;C++ 侧对应 `GetViewStorageImage`。bank 0 与旧全局布局逐位一致,这个兼容设计是多视口改造能零回归落地的关键,值得肯定。

### 1.4 RT bank 布局

`BindlessTexture.slang`:槽位 0..30 为 screen-space RT,50/60../100.. 为全局共享槽(temp、remote encode、swapchain),`RT_COUNT=128`,`kViewRtBankStride=256`,`FBankAllocator` 上限 8 个 bank(含 primary)。bindless 容量 65535,槽位空间充裕。每个非 primary bank 由 `CreateRenderTargetBank(bankBase, extent)` 创建**全套 ~31 张 RT**(含完整时域历史),即便 Transient 缩略图也是全套——这是有意为之的简单方案,但 VRAM 成本与 bank 数线性相关(见 D5)。

---

## 2. 各 LogicRenderer 实际设计

LogicRendererBase 契约:`OnDeviceSet / CreateSwapChain / DeleteSwapChain / Render / BeforeNextFrame / ReloadShaders / Requirements / RequiresObjectIdHistory`。所有渲染器都是纯 compute 链(除共享的 visibility raster pass),向 bank 内 RT 写、最终落 `RT_DENOISED`。

| | PathTracing | SoftwareTracing | SoftwareModern | SwModernNoAmbient | VoxelTracing |
| --- | --- | --- | --- | --- | --- |
| 需求声明 | cube+RT | cube | cube | 仅 voxel geometry | cube |
| 主 shading | `Core.PathTracing`(ray query)或 SHARC 三段(update/resolve/query) | `Core.SwTracing`(DDA on ambient cubes) | `Core.SwModern`(光栅 visibility + cube GI) | `Core.SwModernNoAmbient`(Lambert+IBL+CSM)+ GTAO | `Core.VoxelTracing` |
| 时域 | ReProject(慢路径)+ TemporalResolve 历史拷贝 | ReProject(fast)+ 历史拷贝 | ReProject(fast)+ 历史拷贝 | 无(TAA 由 Engine 侧特判关闭) | 无 |
| 降噪 | per-view AtrousDenoiser(离线渐进模式跳过)+ JBF compose | 同左 | 同左 | GTAOCompose(无 atrous) | 无(直写 RT_DENOISED) |
| ObjectId 历史 | 需要 | 需要 | 需要 | 不需要(override false) | 需要(默认值,疑似应为 false) |
| 额外状态 | FSharcState(6 块 buffer、光照状态签名、pendingClear) | 无 | 无 | 无 | 无 |

观察:

- **四条路径共享同一"标准后处理骨架"**(shading → barrier 列表 → ReProject → Atrous → Compose → CopyToHistory),PathTracing/SwTracing/SwModern 三份实现几乎逐行相同(含相同的 `FReprojectPushConstants` 匿名结构体复制三份)。这是明确的抽取机会(见 C11)。
- PathTracingRenderer 的 SHARC 生命周期管理(参数签名比较、光照变化清缓存、帧回绕检测)全部内嵌在渲染器内,与 `ShouldSkipAmbientCubeUpdates()`([GiBake.cpp:44](../../src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp:44))形成跨层耦合:base renderer 知道"PathTracing+SHARC 时跳过 ambient bake"。
- `RendererDescriptor` 静态表([VulkanBaseRenderer.cpp:229](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:229))与虚函数 `Requirements()` 双轨并存,虚函数实现全部只是回查静态表——冗余但无害(见 C5)。

---

## 3. 正确性检查结果

分级:**A = 规范正确性 / 潜在崩溃**,**B = 多视口正确性**,**C = 架构/设计债**,**D = 性能观察**。置信度:★★★ 基本确定 / ★★ 很可能 / ★ 待验证。

### A. 规范正确性与潜在崩溃

#### A1. 全部 RT 每帧 `UNDEFINED→GENERAL` 转换,时域历史依赖 UB 存活 ★★★(影响:规范层面;实机默认无症状)

`InitializeBarriers`([VulkanBaseRenderer.cpp:1720](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1720))对**所有** bindless RT(含 `RT_ACCUMULATE_*`、`RT_SINGLE_PREV_*`、`RT_OBJECTID_1`、`RT_PREV_DEPTHBUFFER` 等历史图)每帧插入 `oldLayout=UNDEFINED, srcAccess=0` 的转换。Vulkan 规范允许实现此时**丢弃图像内容**;整个时域累积/重投影/prev-depth 链路事实上依赖桌面驱动"UNDEFINED 转换不真丢数据"的实现行为。同一模式还出现在:

- `TemporalResolve::CopyToHistory`([TemporalResolve.cpp:37](../../src/Engine/Rendering/PipelineCommon/TemporalResolve.cpp:37))拷贝后**不把 layout 转回 GENERAL**,留在 TRANSFER_SRC/DST,靠下一帧的 UNDEFINED 转换"归位"。
- `SwapChain::InsertBarrierToWrite`([SwapChain.cpp:355](../../src/Engine/Vulkan/SwapChain.cpp:355))同样 UNDEFINED→GENERAL:在 `ComposeViewToSwapchainSubrect`(reference 四分屏逐视口合成)中,合成第 2..4 个子矩形时会对**已含前面子矩形内容**的 swapchain image 做 UNDEFINED 转换——按规范前面的内容可被丢弃。`DispatchVisualDebugger` 叠加绘制同理。

风险点:移动 GPU(tiler)、MoltenVK、未来驱动优化下可能真实丢内容;同时 `srcAccess=0` 意味着上一帧写入无 availability 保证(目前被 A5/C4 的全串行掩盖)。
建议:给 `RenderImage` 增加 host 侧 layout/access 追踪(或至少区分"scratch RT 可 UNDEFINED / 历史 RT 必须 GENERAL→GENERAL"两类),`CopyToHistory` 补回转屏障;`InsertBarrierToWrite` 提供"保留内容"变体供子矩形合成使用。

#### A2. 交换链重建后 `frame_.currentFrame` 不重置,image 数变化时数组越界 ★★★

`frame_.currentFrame` 仅在 `Start()`([VulkanBaseRenderer.cpp:460](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:460))置 0,`RecreateSwapChain`/`CreateSwapChain` 均不重置;同步对象数组按新 swapchain image 数重建。若重建后 image 数变小(present mode 切换——DLSSG 开关会切 IMMEDIATE、HDR 切换、跨显示器),`frame_.imageAvailableSemaphores[currentFrame]` 即越界(UB)。
建议:`CreateSwapChain` 末尾 `frame_.currentFrame = 0`。

#### A3. TLAS instance 数量不做 kMaxInstanceCount 钳制 ★★★

`CreateTopLevelStructures` 固定分配 65535 个 instance 槽([RayTracingAS.cpp:213](../../src/Engine/Rendering/VulkanBaseRenderer.RayTracingAS.cpp:213));`AfterUpdateScene`([VulkanBaseRenderer.cpp:2242](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:2242))按 node proxy 数无上限收集并 `memcpy` 进映射内存——大场景(SCAD 城市类已接近十万 node)会写穿 buffer。
建议:收集时 `if (instances.size() >= kMaxInstanceCount) break;` 并告警;kMaxInstanceCount 提为共享常量。

#### A4. renderFinished 信号量按 frame slot 复用(WSI 已知隐患) ★★☆

`renderFinishedSemaphores[currentFrame]` 被 submit 签名并交给 present([VulkanBaseRenderer.cpp:1466,1611,1658](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1466))。等待 submit fence 并不保证 presentation engine 已消费该信号量;规范建议 present 等待信号量按 **swapchain image** 索引。当前 image 数 == slot 数 + 实际单帧在飞,风险低,但是新版验证层(present semaphore tracking)会报错的模式。
建议:renderFinished 改为按 `imageIndex` 索引。

#### A5. 索引域混用:commandBuffer/fence 按 currentFrame,UBO/查询按 imageIndex ★★☆

`frame_.commandBuffers->Begin(frame_.currentFrame)` vs `UpdateUniformBuffer(imageIndex)`、`EndGpuFrame((*commandBuffers)[currentImageIndex])`。两个索引域在 FIFO 下通常同步推进,但 MAILBOX/掉帧时会漂移:写 `uniformBuffers[imageIndex]` 只被"上一 submit fence"保护,若未来去掉全串行等待(C4),就会出现 CPU 覆写在飞 UBO 的竞争。当前无症状,属**结构性隐患**。
建议:统一以 imageIndex 为资源域、以 frame slot 为同步域,并写下注释契约;或都收敛到 imageIndex。

#### A6. 蒙皮 joint buffer 申请 `DEVICE_LOCAL|HOST_COHERENT` 却直接 Map ★★☆

[GpuDriven.cpp:61](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:61) 未显式请求 `HOST_VISIBLE`。目前能跑是因为内存类型匹配落到 BAR/ReBAR(DEVICE_LOCAL+HOST_VISIBLE+COHERENT 超集)——在无该类型的设备(部分 iGPU 拆分堆、老驱动)上会分配失败或 Map 非法。
建议:显式加 `HOST_VISIBLE`,或走 staging。

#### A7. 个别屏障 access mask 与实际操作不符 ★★★(低危)

- `DispatchVisibilityPass` 拷贝 `RT_MINIGBUFFER_DRAW→RT_MINIGBUFFER` 后,MINIGBUFFER 的转出屏障 srcAccess 写的是 `COLOR_ATTACHMENT_WRITE`,实际待可见的是 `TRANSFER_WRITE`([GpuDriven.cpp:321](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:321))。
- `CopyObjectIdHistory` 转入屏障 srcAccess 用 `SHADER_READ`(应含 SHADER_WRITE,依赖前序屏障兜底)([GpuDriven.cpp:503](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:503))。

`ImageMemoryBarrier::FullInsert` 若为 ALL_COMMANDS 级屏障则实际被掩盖(待核对 stage 参数),但 mask 语义应修正。

#### A8. ReferenceMode 下无 provider 时 swapchain 不转 PRESENT 布局 ★★☆(边缘)

[VulkanBaseRenderer.cpp:1979](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1979):`renderedAnyReferenceView == false` 时直接走 present,image 停留在 UNDEFINED——`--reference` 但 RenderViews 模块未注册 provider 的组合会触发验证错误/黑屏。低优先级,建议兜底 clear + 转换。

#### A9. `GetScene()` 对过期 weak_ptr 直接解引用 ★★☆(低危)

[VulkanBaseRenderer.cpp:495](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:495) `*scene_.lock()`:scene 释放但渲染帧仍在跑时为空指针解引用。当前 Engine 生命周期管理保证不发生,但 API 层面无保护。

### B. 多视口(RenderView)正确性

#### B1. CSM 阴影贴图跨视口共享 + 每视口重渲,staggered 缓存被互相覆写 ★★★(多视口开启时)

现状链条:

1. 4 张 cascade 阴影贴图为 **Scene 拥有、全局一套**(`ShadowMapPass` framebuffer 绑定 scene 的 sunShadowMap;bindless slot SM_SUN_CASCADE_0..3)。
2. `DispatchSunShadow` 在 `PreRenderPerView` 中**每个视口执行一次**,cascade VP 来自该视口 camera UBO(`FetchGPUScene` → ActiveViewCameraAddress)——CSM 拟合是相机相关的。
3. 但更新 mask 固定读 **primary** 的 staggered 状态:`GetSunShadowCascadeUpdateMask()` → `PrimaryViewState().sunShadowCascadeUpdateMask`([Engine.hpp:179](../../src/Engine/Runtime/Engine.hpp:179),[GpuDriven.cpp:337](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:337))。
4. primary 的 cascade VP 有逐帧交错缓存(`cachedSunCascades`,[Engine.CameraUbo.cpp:130](../../src/Engine/Runtime/Engine.CameraUbo.cpp:130));secondary 视口的 UBO 则每帧全量重算 cascade VP(`BuildViewCameraUbo` 默认 `fillSunCascades=true`,无缓存,[ViewCameraUboBuilder.cpp:45](../../src/Engine/Rendering/ViewCameraUboBuilder.cpp:45))。

后果(开启第二个 Persistent 视口 + 有太阳的场景):同一帧内 secondary 视口按 primary 的 mask 只重渲部分 cascade(用自己的相机拟合)覆写共享贴图 → 下一帧 primary 未被 mask 的 cascade 采样到的是 **secondary 相机拟合的贴图 + primary 缓存的旧 VP**,矩阵与贴图不匹配;secondary 自身未被 mask 的 cascade 同样错配(新 VP + 旧贴图)。表现为多视口下阴影闪烁/错位,交错更新(BakeSpeedLevel 相关)越激进越明显。

建议方向(三选一,按成本递增):
a) 多视口激活时强制每视口全 cascade 重渲(mask=全量),并让 secondary 的 UBO VP 与本次渲染一致——正确但每视口 4 pass;
b) secondary 视口禁用 CSM(HasSun 视口级覆写,离屏预览多数可接受);
c) cascade 贴图纳入 RenderView bank(每视口独立 4 张 + per-view mask/缓存迁入 FViewRenderState,该结构里字段已就位)。

#### B2. VoxelTracingRenderer 使用 SwapChain().RenderExtent() 而非 ActiveViewRenderExtent ★★★

[VoxelTracingRenderer.cpp:51](../../src/Engine/Rendering/VoxelTracing/VoxelTracingRenderer.cpp:51)。其余四个渲染器都已换成 `ActiveViewRenderExtent()`。VoxelTracing 被用作 secondary 视口渲染器时 dispatch 尺寸错误(小 bank 图上越界线程仅靠 shader 边界检查兜底)。顺带:其 `RequiresObjectIdHistory()` 保持默认 true,但它不产出 ObjectId——`CopyObjectIdHistory` 会拷贝未写入的图(无害但浪费,且是 A1 模式的又一处依赖)。

#### B3. ambient cube / TLAS / 蒙皮等场景级资源只跟 primary 语义 ★★☆(设计边界,需文档化)

`PostRender` 的 ambient bake 在所有视口渲染完后以 bank0/primary 相机语境执行一次;residency(相机居中驻留,见 GI clipmap 设计)只追踪主相机。远离主相机的 secondary 视口会采到未驻留/低质量 GI。这是当前"场景级 GI 单实例"的合理妥协,但应在 multi-viewport 设计文档中明示为已知限制,避免后续当 bug 追查。

#### B4. `PreRenderPerView(..., bool isPrimaryView)` 形参与实参语义错位 ★★★(可读性)

[VulkanBaseRenderer.cpp:1331](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1331) 传入的是 `clearSwapchain`(调度项属性),形参名叫 `isPrimaryView`,内部又转手当 `clearSwapchain` 用。reference 模式下第一个 reference 视口 `clearSwapchain=true` 但并非 primary——目前逻辑恰好正确(它确实要清 swapchain),纯命名问题,但极易在后续修改中引入错误。

#### B5. InitializeBarriers 遍历全部 bank,N 视口下 O(N²) 冗余屏障 ★★★(性能+A1 叠加)

`BeginSceneFrame` 调一次 + **每个 LogicRenderer::Render 开头再调一次**(全部五个渲染器都有 `baseRender_.InitializeBarriers(commandBuffer)`),每次遍历**所有 bank 的所有图**。1 主 + 3 副视口 ≈ 5 次 × ~124 张 = 620 个 image barrier/帧。除性能外,这也意味着"视口 B 渲染开始时对视口 A 刚写完的历史图再次做 UNDEFINED 转换"——A1 的丢弃语义风险被放大。
建议:LogicRenderer 内的调用移除(BeginSceneFrame 已做),或改为仅活动 bank。

### C. 架构 / 设计债

#### C1. VulkanBaseRenderer 仍是上帝类 ★★★

主 TU 2272 行 + 3 个分部 TU(~970 行)+ 头 487 行,聚合了:WSI/帧循环、同步策略、bindless RT 池、GPU-driven cull、skinning、CSM 调度、AS 生命周期、ambient bake、upscaler 桥接、DLSSG hudless、截图、多视口调度、shader 热重载分发。分部 TU 只是物理拆分,状态仍全在一个类里(`overlay_` 一个 struct 里混着 wireframe、visibility、CSM、cull、FSR、debugger 七类东西)。
可行的下一步(与 Round4/5 精炼方向一致,按耦合度从低到高):
1. `FramePacer/PresentLoop`:acquire/fence/submit/present + serial 管理,独立可测(顺带修 A2/A4/A5);
2. `GpuDrivenPasses`:cull/skinning/visibility/shadow dispatch(已天然聚在 GpuDriven.cpp,状态搬进去即可);
3. `AmbientCubeBaker`:GiBake.cpp + `ambient_` + `ShouldSkipAmbientCubeUpdates` 的策略判断上移到 Engine;
4. `RayTracingSceneBackend`:RayTracingAS.cpp + `rt_`。

#### C2. 层次反转:Assets/Rendering 反向依赖 Runtime 单例 ★★★

- `Scene::BuildGPUScene`(Assets 层)直接调 `NextEngine::GetInstance()->GetRenderer().ActiveViewCameraAddress/ActiveViewBankBase`([Scene.cpp:808,828](../../src/Engine/Assets/Core/Scene.cpp:808))——Assets → Runtime → Rendering 的环。
- Rendering 层大量 `NextEngine::GetInstance()->GetUserSettings()/GetShowFlags()`(DrawFrame、PostRender、各 LogicRenderer、GiBake……)。
- Engine 侧反过来特判渲染器类型:`Engine.CameraUbo.cpp:83`(NoAmbient 关 TAA)、`218`(TemporalFrames=1)。

建议:GPUScene 的 view 相关字段(camera 地址、bank base)由调用方(FetchGPUScene 的 renderer 侧)注入而非 Scene 反查;设置类只读参数打包成 `FFrameRenderSettings` 每帧传入;渲染器能力(是否支持 TAA/时域)改为 LogicRendererBase 虚属性,消除 Engine 侧 type 特判。

#### C3. 相机 UBO 双真源 ★★☆

每帧同一份 UBO 写两处:`frame_.uniformBuffers[imageIndex]`(供 descriptor-set 图形管线)+ primary RenderView 的 `cameraUboRing`(供 BDA/GPUScene.Camera)([VulkanBaseRenderer.cpp:2054](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:2054))。`ActiveViewCameraAddress` 的"0 == primary 用 per-image UBO"回退路径实际永远不会走到(primary 的 cameraAddress_ 每帧被 SetRenderViewUbo 置为 ring 地址)。两处内容一致所以无症状,但属于容易漂移的双份状态。
建议:图形管线(visibility/wireframe 的 vertex 阶段本就走 push-constant GPUScene 取相机)确认不再消费 descriptor UBO 后,删除 `frame_.uniformBuffers`,统一走 view ring;或反之。

#### C4. 帧同步实际为"单帧在飞",多缓冲同步对象名不副实 ★★★(有意的取舍,需文档化)

[VulkanBaseRenderer.cpp:1487](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1487):每帧等待**上一次 submit** 的 fence(为了 `UpdateNodes` 的 GPU stats 回读一致性),等价于 CPU 录制严格串行于 GPU 完成——多组 fence/semaphore/UBO 的轮转仅在防御性意义上存在。这解释了为何 A5 的索引混用无症状。若未来要恢复 2-frame overlap,需要:stats 回读改异步(N-2 帧数据)+ 修复 A5 + 每 image UBO 保护。建议在代码处加注释说明这是刻意串行,避免误"优化"。

#### C5. Renderer 元数据双轨 ★★☆

静态 `RendererDescriptors` 表(名字/需求/reference 布局/工厂)与虚函数 `Requirements()` 并存,后者全部实现为回查前者;`CurrentRendererRequirements` 先查实例再回落静态表,两条路径结果恒等。删虚函数、只留表即可。另:`RegisterLogicRenderer(type)` 有隐藏副作用——顺带把 `current` 设为该 type([VulkanBaseRenderer.cpp:1730](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1730)),"注册最后一个即当前",调用顺序敏感,建议拆成显式两步。

#### C6. `modelId * 10` 魔数编码散布三处 ★★☆

node proxy 的 modelId 编码(`modelId*10 + lod?`)在 `DispatchSkinning`(×10)、`AfterUpdateScene`(/10)、`CreateBottomLevelStructures`(隐含逐 model 对应 BLAS)手写展开,无常量、无编解码函数。任何 LOD 编码调整都会静默破坏 TLAS/蒙皮。建议提 `Assets::EncodeProxyModelId/DecodeModelIndex`。

#### C7. DeleteSwapChain 与 RefreshSceneSwapChainResources 大段复制粘贴 ★★★(维护性)

两者各自手写 reset 序列([VulkanBaseRenderer.cpp:1030 与 1072](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:1030)),其中约 30 行逐项 reset(overlay/ambient/skin/fsr 等 scene 级管线)完全相同,需要人工保持同步——新增一条 scene 级管线时漏改任意一处即泄漏或悬空。建议提取 `DestroySceneScopedPipelines()` 共用,两函数只保留各自的差异部分(Refresh 保留交换链级资源,Delete 全部销毁)。

#### C8. LogicRenderer 三份重复的后处理链 ★★☆

见 §2:`FReprojectPushConstants` 定义 ×3、ReProject→Atrous→Compose→CopyToHistory 序列 ×3(仅 FastReproject 与 TemporalFrames 来源不同)。建议提 `PipelineCommon::TemporalPostChain::Run(commandBuffer, view, settings, FTemporalPostDesc)`,渲染器只保留自己的 shading 段。

#### C9. 杂项(低优先级)

- `wireframeFrameBuffers` 按 swapchain image 数创建 N 份,但全部绑定同一张 RT_DENOISED view——一份即可([VulkanBaseRenderer.cpp:884](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:884))。
- `LogicRendererBase::baseRender_` 公有成员但带私有命名后缀;`GetBaseRender<T>()` 的 static_cast 无类型保证。
- `DrawFrame` 中 `frame_.currentFence` 赋值两次(1505 与 1595);acquire 错误检查里 `result != VK_SUBOPTIMAL_KHR` 分支不可达(前面已 return)。
- `RT_MINIGBUFFER_DRAW` 复用 finalLayout=PRESENT_SRC 的 render pass,离屏 RT 走 PRESENT 布局语义——能跑但奇怪,若 visibility pass 独立建 render pass 可顺手改成 COLOR_ATTACHMENT_OPTIMAL。
- `PrintVulkanDevices` 里 `hasRayTracing` 计算后未使用。
- `GetReferenceViewLayout`:`ERT_VoxelTracing` 与 `ERT_SoftwareModernNoAmbient` 同占 (0,1),若未来把 VoxelTracing 加进 reference 集会重叠。

### D. 性能观察(不改行为的优化空间)

| # | 观察 | 位置 | 备注 |
| --- | --- | --- | --- |
| D1 | 每帧 5×全 bank image barrier(见 B5) | InitializeBarriers | 与 A1 一并处理 |
| D2 | 多视口下每视口全套 CSM cull+draw | DispatchSunShadow | 与 B1 一并处理 |
| D3 | 蒙皮 update 每请求线性扫 nodeProxies | [GpuDriven.cpp:120](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:120) | modelId→proxyIdx 建 map |
| D4 | AtrousDenoiser/TemporalResolve 管线按 view 各建一份 | RenderView::CreateSwapChain | 管线可共享,仅状态 per-view |
| D5 | 非 primary bank 全套 31 张 RT(Transient 缩略图同价) | CreateRenderTargetBank | 可按 schedule 裁剪历史类 RT |
| D6 | UpdateSkinningBuffers 每帧遍历全部 node 求 joint 总数 | [GpuDriven.cpp:47](../../src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:47) | 可由 Scene 维护计数 |
| D7 | 单帧在飞(C4)是当前吞吐上限的结构性因素 | DrawFrame | 恢复 overlap 需先还 A5 的债 |

---

## 4. 修复优先级建议(供核对后排期)

**立即可修、低风险(每项 ≤ 半日)**:
1. A2 `CreateSwapChain` 重置 currentFrame。
2. A3 TLAS instance 钳制。
3. B2 VoxelTracing 换 ActiveViewRenderExtent(+ RequiresObjectIdHistory=false)。
4. B5 移除各 LogicRenderer 里冗余的 InitializeBarriers 调用。
5. A6 joint buffer 补 HOST_VISIBLE。
6. A7 两处 access mask 修正。
7. B4 形参改名 clearSwapchain。
8. C5/C9 小清理(顺手)。

**需要设计讨论**:
- A1 layout 追踪策略(与 B5 联动;决定是否要支持 tiler/MoltenVK)。
- B1 多视口 CSM 方案(a/b/c 三选一)。
- A4/A5/C4 帧同步整体梳理(一次性把 present 信号量、索引域、串行策略讲清楚并文档化)。
- C2 层次反转解耦(GPUScene 注入 + FFrameRenderSettings)。
- C1 拆类(建议随 Round5 核心层精炼推进,先 FramePacer 后 GpuDrivenPasses)。
- C8 TemporalPostChain 提取(可与 C1 并行,收益是三渲染器逐行 diff 消失)。

## 5. 值得保留的设计(核对时请勿"顺手重构"掉)

- RenderView bank0 == 旧全局布局的兼容策略,以及 `FActiveRenderViewScope` 的 RAII 视口切换——多视口零回归的基石。
- ZeroBind push-constant(GPUScene 单结构体)管线族:无 per-pass descriptor 管理,shader/C++ 同源 `BindlessTexture.slang` 常量。
- Requirements 驱动的按需资源创建(ambient/RT 管线只在有渲染器声明需求时创建)+ 设备能力降级链(无 RT→SwTracing、显存不足→NoAmbient,[VulkanBaseRenderer.cpp:405](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:405))。
- 外部注入点四件套(§1.1 表):模块化边界清晰,Engine 不反向依赖模块。
- 增量 shader 热重载(按文件名分发 + 未处理集合回落全量重建)。

---

*审计方法:纯静态代码阅读(2026-07-11 工作树,dev 分支,含未提交改动);未运行验证层/实机复现。下一步核对建议:1) 开验证层跑 `gnb shot` 确认 A1/A7 是否有报错;2) 写一个双视口 + 太阳场景的 agentscript 复现 B1;3) 强制切换 present mode 复现 A2。*
