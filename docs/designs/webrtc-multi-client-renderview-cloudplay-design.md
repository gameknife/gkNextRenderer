---
title: "WebRTC 多客户端多视口 Cloud Play 设计与开发计划"
category: design
status: 未开始
owner: engine
created: 2026-07-02
last_updated: 2026-07-02
---

# WebRTC 多客户端多视口 Cloud Play 设计与开发计划

> 目标：在一个引擎进程里打开多个浏览器客户端，每个浏览器连接到同一个 scene，但拥有自己的相机 / 视口 / 输入上下文。以 `gkNextRenderer` 为首个落地对象：每个客户端按 `Space` 都从自己的视口相机方向向共享场景发射一个动态 box。
>
> 本文给后续接手 agent 使用。实现前建议先读：
> - `docs/designs/multi-viewport-renderview-design.md`
> - `docs/designs/webrtc-remoteplay-design.md`
> - `src/Engine/Rendering/RenderView.hpp`
> - `src/Modules/NextRemote/VideoPipeline.hpp`

---

## 0. 结论

现有代码已经具备两块关键能力，但它们目前还没有按“多客户端 = 多视口”连接起来：

1. **RenderView 已能在单 scene 下渲染多个独立视口**。`RenderView` 拥有独立 RT bank、相机 UBO、时域状态；`RenderViewManager` 负责 bank 分配和调度（`src/Engine/Rendering/RenderView.hpp:146`、`src/Engine/Rendering/RenderView.hpp:275`）。编辑器的 `OffscreenRenderViewController` 已经用它做了多相机离屏视图（`src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp:264`）。
2. **Remote Play 已能把主 swapchain 编成 WebRTC 视频流**。当前 `FVideoPipeline` 是单一 engine-wide pipeline：读 `RT_SWAPCHAIN*`，转 NV12，Vulkan Video 编码，然后把同一份 bitstream fan-out 给所有 session（`src/Modules/NextRemote/VideoPipeline.hpp:48`、`src/Modules/NextRemote/VideoPipeline.cpp:346`、`src/Modules/NextRemote/VideoPipeline.cpp:778`）。

因此本功能的正确方向不是多开引擎进程，也不是多开 OS 窗口，而是：

> **每个 WebRTC session 分配一个 `RenderView` + 一个远程相机控制器 + 一个独立 video stream。所有 session 共享同一个 `Assets::Scene` 和物理世界；每帧把活跃 session 的 RenderView 渲染到自己的 RT bank，再把该 view 的 SDR 输出送入该 session 的 Vulkan Video encoder。**

首期建议新增 opt-in 模式，例如：

```bash
gnb run gkNextRenderer -- --remote --remote-multiview --remote-max-clients 2 --remote-res 960x540
```

保留现有 `--remote` 行为不变：普通 Remote Play 仍然是一条主 swapchain 直播流，兼容编辑器 UI / 调试使用；`--remote-multiview` 才进入多客户端云游戏模式。

---

## 1. 目标与非目标

### 1.1 目标

- 多个浏览器客户端同时连接一个引擎进程。
- 每个客户端拥有独立相机，可分别旋转 / 平移 / 缩放自己的视口。
- 每个客户端看到同一个 scene 的实时状态；任意客户端生成的动态物体会出现在所有客户端画面中。
- `gkNextRenderer` 中，每个客户端按 `Space` 时从该客户端相机位置和方向发射 box，而不是从主窗口相机发射。
- 继续复用 Vulkan Video H.264 路线；不恢复 openh264 软编。
- 对非 remote、多视口编辑器、`gnb shot` 路径保持零行为变化。

### 1.2 非目标

- 不做公网穿透 / 鉴权 / TLS / 房间系统。首期仍按现有 LAN / loopback Remote Play 模型。
- 不做多人游戏同步协议。这里是“共享进程内 scene + 多远程视口”，不是 lockstep 或 client prediction。
- 不让浏览器客户端控制 ImGui。多视口 Cloud Play 默认只控制自己的 game/view 输入；需要调试 UI 时继续用旧 `--remote`。
- 不做跨 GPU / 多进程编码调度。硬件编码 session 数不足时限制客户端数量并给出清晰日志。
- 不做每客户端独立 scene。所有客户端首期共享 `renderer.GetScene()`。

---

## 2. 当前实现地图

### 2.1 RenderView 侧

- `RenderView` 保存 `RtBankBase()`、`RenderExtent()`、`CameraAddress()`、per-view `FViewRenderState`、`AtrousDenoiser`、`TemporalResolve` 等状态（`src/Engine/Rendering/RenderView.hpp:146`）。
- `FBankAllocator::kMaxConcurrentBanks = 8`，bank 0 是 primary view，其它 bank 可给 secondary/offscreen view（`src/Engine/Rendering/RenderView.hpp:93`）。
- shader 侧 `kViewRtBankStride = 256`，screen-space RT 通过 `Bindless.ViewRT(base, RT_X)` 解析；`RT_REMOTE_ENCODE*` 和 `RT_SWAPCHAIN*` 是全局 slot，不属于 view bank（`assets/shaders/common/BindlessTexture.slang:47`、`assets/shaders/common/BindlessTexture.slang:70`、`assets/shaders/common/BindlessTexture.slang:108`）。
- `VulkanBaseRenderer::RenderViewToBank()` 已能在一个 command buffer 中切换 active view、跑 per-view prerender 和 logic renderer（`src/Engine/Rendering/VulkanBaseRenderer.cpp:1467`）。
- `VulkanBaseRenderer::ScheduleRenderView()` / `DispatchScheduledRenderViews()` 是当前调度入口，但在 header 中仍是 private（`src/Engine/Rendering/VulkanBaseRenderer.hpp:401`、`src/Engine/Rendering/VulkanBaseRenderer.cpp:1498`）。
- `BuildViewCameraUbo()` 可从任意 `Assets::Camera` 生成每视口 UBO（`src/Engine/Rendering/ViewCameraUboBuilder.cpp:119`）；`FinalizeTemporalUbo()` / `SetRenderViewUbo()` 会补 per-view temporal 状态并上传（`src/Engine/Rendering/VulkanBaseRenderer.cpp:1530`、`src/Engine/Rendering/VulkanBaseRenderer.cpp:1541`）。
- 编辑器 offscreen view 已经有可复用范本：`EnsureView()` 分配 RenderView 和 framebuffer，`ScheduleViews()` 每帧组 UBO 并调度，`CopyViewOutput()` 从 `view.RtBankBase()+RT_DENOISED` 拷到 sampled texture（`src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp:165`、`src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp:223`、`src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp:264`）。

### 2.2 Remote 侧

- `--remote` 由 `DesktopMain` 创建 `RemoteServer` 作为 `IRenderFrameConsumer`（`src/DesktopMain.cpp:95`、`src/Modules/NextRemote/NextRemoteModule.cpp:56`）。
- `IRenderFrameConsumer::RecordFrame()` 在 `NextEngine::OnRendererPostRender()` 中调用，此时主 swapchain 和 UI 已录制到当前 frame command buffer（`src/Engine/Runtime/RenderFrameConsumer.hpp:22`、`src/Engine/Runtime/Engine.cpp:1329`、`src/Engine/Runtime/Engine.cpp:1346`）。
- `IRenderFrameConsumer::Tick()` 已在主线程每帧调用，可用于消费 WebRTC 线程入队的输入命令（`src/Engine/Runtime/Engine.cpp:996`）。
- `RemoteServer` 目前只持有一个 `FVideoPipeline` 和一个 `FSignalingServer`（`src/Modules/NextRemote/RemoteServer.hpp:22`）。
- `FSignalingServer` 已用 client `id` 管理多个 `FRemoteSession`（`src/Modules/NextRemote/SignalingServer.hpp:54`、`src/Modules/NextRemote/SignalingServer.cpp:52`、`src/Modules/NextRemote/SignalingServer.cpp:74`）。
- `FRemoteSession` 为每个 session 创建一个 video track 和一个 input DataChannel，但 video track 都订阅同一个 `FVideoPipeline::AddSink()`，所以所有浏览器收到同一画面（`src/Modules/NextRemote/RemoteSession.cpp:119`、`src/Modules/NextRemote/RemoteSession.cpp:143`）。
- `FInputRouter` 目前把 DataChannel 输入翻译成全局 SDL 事件，目标是主 SDL window；它没有 session/view 身份（`src/Modules/NextRemote/InputRouter.cpp:92`、`src/Modules/NextRemote/InputRouter.cpp:179`、`src/Modules/NextRemote/InputRouter.cpp:193`）。
- 浏览器端已经生成唯一 `clientId`，并在 signaling `request` 中发送（`assets/remote/index.html:334`、`assets/remote/index.html:946`）。

### 2.3 gkNextRenderer 侧

- `NextRendererGameInstance` 目前只有一个 `ModelViewController`（`src/Application/Render/gkNextRenderer/gkNextRenderer.hpp:54`）。
- `OverrideRenderCamera()` 用这个 controller 生成主渲染相机（`src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:667`）。
- `OnKey()` 收到 `Space` 时调用 `CreateBoxAndPush()`（`src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:677`、`src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:700`）。
- `CreateBoxAndPush()` 用同一个 `modelViewController_` 的 position / forward / right / up 计算发射位置和方向（`src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:826`）。

这意味着：如果只继续走全局 SDL 注入，多个浏览器会争抢同一个主相机，`Space` 也只会从主相机发射。新模式必须把输入和相机状态按 session 拆开。

---

## 3. 目标架构

### 3.1 对象关系

```text
RemoteServer (--remote-multiview)
 ├── FSignalingServer
 │    └── FRemoteSession(id = browser clientId)
 ├── FRemoteViewHub
 │    ├── FRemoteClientView(session A)
 │    │    ├── RenderView*                 # bank1, same scene
 │    │    ├── FRemoteViewController       # per-client camera/input state
 │    │    ├── FRemoteVideoStream          # per-client encoder + capture slots
 │    │    └── pending input/action queue  # WebRTC thread -> main/render thread
 │    ├── FRemoteClientView(session B)
 │    │    ├── RenderView*                 # bank2
 │    │    ├── FRemoteViewController
 │    │    └── FRemoteVideoStream
 │    └── ...
 └── shared engine scene / physics / renderer
```

关键约束：

- `FRemoteSession` 仍负责 WebRTC PeerConnection / DataChannel / signaling。
- `FRemoteClientView` 是 session 在引擎侧的实体，生命周期和 session 绑定。
- 一个 `FRemoteClientView` 对应一个 `RenderView` 和一个 video stream。
- 所有 `FRemoteClientView` 的 `RenderView::SceneOverride()` 首期保持 `nullptr`，即使用 renderer 主 scene。
- 所有 scene / physics 修改只在主线程执行，不能从 WebRTC 回调线程直接改。

### 3.2 数据流

```text
Browser key/mouse/gamepad
  -> FRemoteSession input DataChannel callback
  -> FRemoteClientView pending input queue
  -> RemoteServer::Tick() on main thread
  -> update per-client camera controller / enqueue gameplay actions
  -> gkNextRenderer handles Space with that client's camera snapshot
  -> scene/physics mutated on main thread

Renderer frame
  -> primary view renders as before
  -> remote view scheduler schedules all active FRemoteClientView RenderViews
  -> for each view:
       BuildViewCameraUbo(client camera)
       SetRenderViewUbo(view)
       RenderViewToBank(view)
       RemoteViewToNv12(view.RtBankBase()+RT_DENOISED -> stream NV12 slot)
  -> per-client encoder thread encodes its own NV12 image
  -> FRemoteSession sends only its own encoded packets
```

### 3.3 模式分离

保留两种 Remote 模式：

| 模式 | CLI | 视频源 | 输入目标 | 用途 |
| --- | --- | --- | --- | --- |
| Legacy Remote Play | `--remote` | 主 swapchain | 全局 SDL window | 远程调试、操作主窗口 / UI |
| Cloud Play MultiView | `--remote --remote-multiview` | 每 session 的 RenderView | session camera/gameplay context | 多浏览器共享 scene，各控各视口 |

这样可以避免一次性把现有 Remote Play 的 UI 调试能力改坏。

---

## 4. 核心设计

### 4.1 Renderer 接缝：把外部 RenderView 调度做成公开、受控 API

当前 `ScheduleRenderView()`、`SetRenderViewUbo()`、`FinalizeTemporalUbo()` 都是 `VulkanBaseRenderer` private；`OffscreenRenderViewController` 能用，是因为它在 rendering 模块内部并通过 friend / 近邻实现访问。`NextRemote` 作为 module 不应直接摸 renderer private 成员。

建议新增一个小的公开 facade，名字可以是：

```cpp
// src/Engine/Rendering/ExternalRenderViewHost.hpp
struct FExternalRenderViewDesc
{
    VkExtent2D extent;
    EViewSchedule schedule = EViewSchedule::Persistent;
    std::string debugName;
};

struct FExternalRenderViewFrame
{
    RenderView& view;
    Assets::Camera camera;
    Assets::Scene* sceneOverride = nullptr;
    FRenderViewPostCallback postRender;
};

class IExternalRenderViewHost
{
public:
    RenderView* CreateExternalRenderView(const FExternalRenderViewDesc& desc);
    void DestroyExternalRenderView(RenderView* view);
    void ScheduleExternalRenderView(VkCommandBuffer cmd, uint32_t imageIndex, const FExternalRenderViewFrame& frame);
};
```

实际可直接由 `VulkanBaseRenderer` 暴露这些方法，也可由 `RenderViewServices` 持有。重点是让 `NextRemote` 只调用稳定接口，不复制 `OffscreenRenderViewController` 里的 private 访问模式。

调度时机建议新增到 renderer frame 内部，而不是继续只靠 `OnRendererPostRender()`：

```cpp
// 伪代码：VulkanBaseRenderer::Render()
RenderPrimaryView();
ResolvePrimaryViewToSwapchain();

if (renderViewServices_) {
    renderViewServices_->ScheduleViews(cmd, imageIndex);
}
if (delegates_.scheduleExternalRenderViews) {
    delegates_.scheduleExternalRenderViews(cmd, imageIndex);
}
DispatchScheduledRenderViews(cmd, imageIndex);

// 之后再由 Engine::OnRendererPostRender() 录 UI / legacy consumers
```

原因：Cloud Play 的每客户端 view 必须先被真正渲染出来，才能编码。当前 `IRenderFrameConsumer::RecordFrame()` 位于 `Engine::OnRendererPostRender()`，只适合消费已完成主 swapchain；它太晚，也缺少 public API 去渲染新 view。

### 4.2 每 session 的 RenderView 资源

每个 session 创建：

- 一个 `RenderView`，`outputKind = OffscreenTexture` 或新增 `VideoStream`。首期不需要 ImGui sampled output，可以不创建 sampled offscreen image。
- 一个 visibility framebuffer，逻辑与 `OffscreenRenderViewController::EnsureView()` 相同。
- 一套 RT bank，由 `RenderViewManager` 分配。
- 一个 `FRemoteVideoStream`，包含本 session 的 NV12 staging images、Vulkan Video encoder、packet sink。

注意 slot 分配：

- 当前 `FVideoPipeline` 固定使用 `RT_REMOTE_ENCODE0_Y..RT_REMOTE_ENCODE3_UV` 四组全局 slot（`assets/shaders/common/BindlessTexture.slang:47`、`src/Modules/NextRemote/VideoPipeline.cpp:84`）。
- 多 session 下不能继续用这 8 个固定 slot，否则多个 encoder 会互相覆盖。
- 需要新增 `FRemoteEncodeSlotAllocator`，从安全高位区间分配每 session 的 capture slots，例如：

```text
kRemoteEncodeSlotBase = 62000
session i, slot j:
  y  = kRemoteEncodeSlotBase + i * kSlotsPerSession * 2 + j * 2
  uv = y + 1
```

要确保不和现有 preview/sample slot 冲突：

- material thumbnail: `63200+` / `64000+`
- secondary offscreen view sampled slot: `65000+`
- `GlobalTexturePool::kMaxBindlessSlots` 上限是 65535（见 `UserInterface::RequestImTextureIdRaw` 相关说明）。

建议把 remote encode slot base 放在 `60000..61999` 区间，或改成由 `GlobalTexturePool` 提供统一 transient slot allocator，避免未来继续撞号。

### 4.3 每 session 的视频流

当前 `FVideoPipeline` 语义是“一个全局视频 pipeline + 多 sink fan-out”。多视口 Cloud Play 需要改成：

- `FRemoteVideoStream`：一个 session 一个 encoder，输出只给该 session。
- `FRemoteVideoStreamManager`：管理 session -> stream，统一限流、swapchain destroy、device idle cleanup。

建议分两步重构：

1. **先把现有 `FVideoPipeline` 拆出可复用的 encoder/capture slot 逻辑**：
   - 保留 legacy path：source = swapchain，sink = 多 session fan-out。
   - 新增 view path：source = render view RT / resolved color，sink = 单 session。
2. **再做 per-session stream**：
   - 每个 stream 独立 `desiredBitrateKbps_`、`keyframeRequested_`、`encodeThread_`、capture slot ring。
   - H.264 profile negotiation 从全局 profile map 改为 session-local；不同浏览器可以各自选择 profile，互不影响。

每 session encoder 会消耗硬件编码资源，默认上限不要太激进：

```text
--remote-max-clients 默认 2
硬上限 = min(user value, RenderView bank 可用数, encoder session policy)
```

如果创建第 N 个 encoder 失败，要拒绝该 session 并通过 signaling 返回 `{type:"error", message:"server full or encoder unavailable"}`。

### 4.4 从 RenderView 输出到 NV12

现有 shader `assets/shaders/Remote.BgraToNv12.comp.slang` 读的是 `RT_SWAPCHAIN0 + imageIndex`（`assets/shaders/Remote.BgraToNv12.comp.slang:68`），而每个 remote view 的画面在 `view.RtBankBase() + RT_DENOISED`（`assets/shaders/common/BindlessTexture.slang:15`）。

不能直接复用现有 shader。建议新增一条 view source 转换路径：

```text
Remote.ViewToNv12.comp.slang
输入：
  uint viewBankBase
  uint srcRtSlot = RT_DENOISED
  uint outYIndex / outUvIndex
  uint srcWidth / srcHeight
  uint dstWidth / dstHeight
  uint paperWhiteNit / outputMode or simplified SDR params
输出：
  per-stream Y plane / UV plane
```

颜色空间有两种实现路径：

1. **推荐首期：复用 `Process.UpScaleFSR` / resolve 逻辑的 SDR 输出语义**  
   新增 `ResolveViewToEncodeColorImage()`，先把 `RT_DENOISED` resolve 到 per-stream `VK_FORMAT_B8G8R8A8_UNORM` image，再用现有 BGRA->NV12 逻辑。这条路更接近主 swapchain 结果，风险低。
2. **后续优化：直接 `RT_DENOISED` -> NV12**  
   在转换 shader 里读 `Bindless.ViewRT(viewBankBase, RT_DENOISED)`，做 SDR tonemap / clamp / BT.709 limited range，再写 NV12。少一次 BGRA 中间图，但要小心 HDR/linear 编码差异。

因为 `--remote` 已强制 `ForceSDR` / hidden window（`src/Engine/Options.cpp:124`），首期可以只支持 SDR；遇到 HDR swapchain 保持当前拒绝编码逻辑（`src/Modules/NextRemote/VideoPipeline.cpp:373` 附近已有 warning）。

### 4.5 session-aware 输入

Legacy `FInputRouter` 继续保留：它把浏览器输入注入全局 SDL window，服务旧 `--remote`。

Cloud Play 新增 session-aware 输入路径：

```cpp
struct FRemoteInputEvent
{
    enum class EType { Key, MouseMove, MouseButton, Wheel, Gamepad };
    std::string sessionId;
    ...
};

class FCloudInputRouter
{
public:
    void HandleBinaryMessage(std::span<const std::byte> data);
    void DrainTo(FRemoteClientView& view); // main thread only
};
```

DataChannel callback 只做两件事：

- 解析 message。
- 推入该 session 的 lock-protected queue。

主线程 `RemoteServer::Tick()` 消费 queue：

- 更新该 session 的 `FRemoteViewController`。
- 处理离散 action，例如 `Space` 发射 box。
- 更新 `lastInputTime`，用于空闲 view 降帧 / 暂停。

浏览器 DataChannel 是 session 私有的，所以消息体不需要额外携带 viewId；server 侧用 `FRemoteSession::Id()` 找到对应 `FRemoteClientView` 即可。

### 4.6 每客户端相机控制器

`gkNextRenderer` 现在的 `ModelViewController` 是单实例，并且接口主要吃 SDL event（`src/Engine/Runtime/Camera/ModelViewController.hpp:30`）。多客户端首期建议用最小侵入方案：

- 为每个 remote client 创建一个 `Runtime::Camera::ModelViewController`。
- 初始化时复制当前 scene render camera 或主 `modelViewController_` 生成的 camera。
- 给 `ModelViewController` 增加少量 session-friendly 方法，避免构造假的全局 SDL window event：

```cpp
void SetKeyHeld(SDL_Keycode key, bool held);
void ApplyMouseMove(float x, float y, bool relative);
void ApplyMouseButton(uint8_t button, bool down, float x, float y);
void ApplyWheel(float y);
```

如果为了改动更小，也可以在 `FCloudInputRouter` 内构造 `SDL_Event`，但直接调用该 client 的 controller，而不是 `SDL_PushEvent`。不要让 remote multiview 的输入进入 `NextEngine::HandleEvent()`，否则仍会污染主窗口相机 / ImGui capture 状态。

### 4.7 gkNextRenderer 发射 box

把当前 `CreateBoxAndPush()` 拆成两层：

```cpp
struct FLaunchView
{
    glm::vec3 position;
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
};

void CreateBoxAndPushFromView(const FLaunchView& view);
void CreateBoxAndPush(); // 继续用主 modelViewController_，调用 FromView
```

Cloud Play 的 `Space` action 调用 `CreateBoxAndPushFromView(remoteControllerSnapshot)`。

注意：

- `GetEngine().GetScene().AddNode()`、`MarkDirty()`、`GetPhysicsEngine()->CreateBoxBody()`、`AddForceToBody()` 都必须在主线程执行。
- 多客户端同时按 `Space` 时，按主线程 queue 顺序处理即可；`instanceId` 继续从 `Scene().Nodes().size()` 取，但最好在同一个主线程函数内取和 Add，避免未来并发引入重复 id。
- 可以给 remote box 命名带 session 短 id，例如 `remoteBox-<id>`，便于调试。

### 4.8 Browser client

`assets/remote/index.html` 已有：

- `clientId`（`assets/remote/index.html:334`）
- key/mouse/gamepad binary protocol（`assets/remote/index.html:475`、`assets/remote/index.html:697`、`assets/remote/index.html:712`、`assets/remote/index.html:782`）
- `request` signaling 携带 id（`assets/remote/index.html:946`）

首期只需要很小改动：

- `/config` 增加 `mode: "legacy" | "multiview"`、`maxClients`、`viewWidth`、`viewHeight`。
- 页面标题 / status 显示 `Cloud Play view <short id>`。
- 可选：URL 支持 `?name=alice`，server 记录 displayName，便于日志。
- 输入协议保持兼容，不新增 viewId。

---

## 5. 开发计划

### Phase 0：开关、边界和文档对齐

目标：不改变行为，先把模式入口和限制条件立住。

- `Options` 增加：
  - `--remote-multiview`
  - `--remote-max-clients <N>`，默认 2
  - 可选 `--remote-view-res WIDTHxHEIGHT`，未设置时复用 `--remote-res`
- `RemoteServer::FConfig` 增加 `multiView`、`maxClients`、`viewWidth/viewHeight`。
- `/config` 返回 mode/maxClients。
- 启动时日志明确：
  - legacy remote: `RemotePlay: mode=legacy source=swapchain`
  - multiview: `RemotePlay: mode=multiview maxClients=N view=WxH`

验证：

- `./gnb.bat build gkNextRenderer`
- `gnb run gkNextRenderer -- --remote` 行为不变。
- `gnb run gkNextRenderer -- --remote --remote-multiview` 能启动并返回 `/config`。

### Phase 1：Renderer 外部 RenderView 调度接缝

目标：Remote module 可以请求渲染一个外部 RenderView，但先不接 WebRTC。

- 新增 renderer public facade 或 `IRenderFrameConsumer::ScheduleRenderViews()` hook。
- 把“根据 camera + extent 调度 RenderView”的通用逻辑从 `OffscreenRenderViewController` 抽出或复用。
- 新增一个临时 debug path：创建一个 hidden remote view，按固定 orbit camera 渲染，并在 GPU timer / log 中看到该 view 被执行。
- 不要复制 scene，不要创建第二个 `VulkanBaseRenderer`。

验证：

- `./gnb.bat build gkNextRenderer gkNextUnitTests`
- `gnb shot --scene assets/models/playground.glb`，主画面无回归。
- 开启 debug remote view 时，日志显示 view bank 分配，GPU 不报 validation error。

### Phase 2：每 session view 生命周期

目标：浏览器 session 建立时分配 view，断开时释放 view。

- `RemoteServer` 新增 `FRemoteViewHub`：
  - `CreateClientView(sessionId)`
  - `DestroyClientView(sessionId)`
  - `FindClientView(sessionId)`
  - `Tick(deltaSeconds)`
  - `ScheduleViews(commandBuffer, imageIndex, renderer)`
- `FSignalingServer` 在收到 `request` 时先检查 `maxClients`；满员时回 error。
- `FRemoteSession` 创建 / 销毁时通知 `RemoteServer`，不要只在 `SignalingServer` 内部维护 session map。
- RenderView 释放要在 render thread / device idle 安全点做；swapchain destroy 时清空 view 资源。

验证：

- 连两个浏览器，日志分别显示 `session <id> view bank=<base>`。
- 断开一个浏览器，bank 被释放；再打开新浏览器可复用。
- 超过 `--remote-max-clients` 时浏览器收到 error。

### Phase 3：per-session 视频流

目标：每个 session 收到自己的 RenderView 视频，而不是主 swapchain fan-out。

- 把现有 `FVideoPipeline` 拆成 legacy pipeline + reusable stream pieces。
- 新增 `FRemoteVideoStream`：
  - per-session capture slots
  - per-session bindless encode slots
  - per-session `FVulkanVideoEncoder`
  - per-session packet callback
- 新增 remote encode slot allocator，替换固定 `RT_REMOTE_ENCODE0_Y + slotIndex*2`。
- 新增 `Remote.ViewToNv12` 或 `ResolveViewToEncodeColorImage + BgraToNv12`。
- `FRemoteSession` 的 video track 只订阅自己的 stream，不再订阅全局 `FVideoPipeline::AddSink()`。
- Legacy `--remote` 保持原有 `FVideoPipeline` fan-out。

验证：

- 两个浏览器能同时播放。
- 给两个 session 临时设置不同 debug camera，画面不同。
- `IDR` / bitrate slider 只影响当前 session，不能全局影响其它 session。
- 断开一个 session 不影响另一个 session 的视频。

### Phase 4：session-aware 输入和相机

目标：两个浏览器能操纵不同视口。

- 新增 `FCloudInputRouter` 或给 `FInputRouter` 增加 multiview mode，但不要把 multiview 输入推入 `SDL_PushEvent`。
- 每 session 保存：
  - key held state
  - pointer position
  - mouse buttons
  - gamepad state
  - `ModelViewController`
- `RemoteServer::Tick()` 消费输入队列并更新 controller。
- `ScheduleViews()` 用每 session controller 生成 `Assets::Camera` 和 UBO。
- 鼠标绝对坐标按该 session video frame 归一化映射；相对 pointer-lock 只影响该 session controller。

验证：

- 两个浏览器右键拖动，只改变各自画面。
- WASDQE / scroll / gamepad 不影响主窗口相机，也不影响其它浏览器。
- legacy `--remote` 仍可控制主窗口。

### Phase 5：gkNextRenderer Space 发射 box

目标：实现用户需求的可见玩法闭环。

- `NextRendererGameInstance` 拆 `CreateBoxAndPushFromView()`。
- 增加 remote multiview action path：
  - session 输入 `Space` down
  - main thread 找到 session camera snapshot
  - 调 `CreateBoxAndPushFromView(snapshot)`
- 可选：每 session 分配不同材质色或日志标记，方便肉眼确认 box 来源。

验证：

- 浏览器 A 看向左侧按 `Space`，box 从 A 视角飞出。
- 浏览器 B 看向右侧按 `Space`，box 从 B 视角飞出。
- 两边都能看到彼此发射的 box。
- 主窗口按 `Space` 仍使用主 controller 行为。

### Phase 6：稳定性与性能

目标：让模式可长期运行。

- 空闲 session 降帧：无输入且画面静止时可降到 10-15 fps；有输入恢复目标 fps。
- 每 session backpressure 独立：某个 encoder backlog 只丢该 session 帧，不阻塞 renderer。
- 资源统计：日志或 debug counter 显示 active sessions、view banks、encode slots、dropped frames。
- swapchain recreate / scene reload：
  - 所有 remote views invalidate temporal history。
  - 重新创建 visibility framebuffer / encode color intermediates。
  - session 保持连接，下一帧请求 IDR。
- 硬件能力不足时优雅失败。

验证：

- `./gnb.bat build gkNextRenderer gkNextUnitTests`
- `./out/build/windows/bin/gkNextUnitTests`（如果本次 touching core systems）
- `gnb shot --scene assets/models/playground.glb` 验证非 remote 主路径。
- 手动打开 2-4 个浏览器，运行 5 分钟，观察无 validation error、无 encoder deadlock、断连重连正常。

---

## 6. 推荐文件改动清单

### Engine / Rendering

- `src/Engine/Runtime/RenderFrameConsumer.hpp`  
  增加可选 `ScheduleRenderViews(...)` 或新增独立 `IRenderViewFrameConsumer`。

- `src/Engine/Runtime/Engine.cpp`  
  在主线程 `Tick()` 调用 remote view hub 的 input drain；在 renderer delegate 中暴露 remote view scheduling。

- `src/Engine/Rendering/VulkanBaseRenderer.{hpp,cpp}`  
  暴露受控外部 RenderView facade；保持 private 资源所有权不泄漏。

- `src/Engine/Rendering/RenderViewResourceFactory.{hpp,cpp}`  
  增加 remote/video 用 framebuffer 或中间 color image 创建 helper。

- `assets/shaders/Remote.ViewToNv12.comp.slang` 或扩展 `Remote.BgraToNv12.comp.slang`  
  支持 per-view source。

### Modules / NextRemote

- `src/Modules/NextRemote/RemoteServer.{hpp,cpp}`  
  新增 multiview hub、session lifecycle、view scheduling。

- `src/Modules/NextRemote/RemoteSession.{hpp,cpp}`  
  session 创建时注册 view/stream；DataChannel input 在 multiview mode 下进入 session queue。

- `src/Modules/NextRemote/SignalingServer.{hpp,cpp}`  
  request 满员处理；`/config` 返回 multiview 信息。

- `src/Modules/NextRemote/VideoPipeline.*`  
  保留 legacy pipeline；抽出可复用 stream/encoder/capture slot 代码。

- 新增 `RemoteVideoStream.{hpp,cpp}`、`RemoteViewHub.{hpp,cpp}`、`CloudInputRouter.{hpp,cpp}`。

- `src/Engine/Runtime/RemoteProtocol.hpp`  
  如果新增协议消息（例如 client display name / view stats），在这里扩展。首期输入消息可保持兼容。

### gkNextRenderer

- `src/Application/Render/gkNextRenderer/gkNextRenderer.hpp`
- `src/Application/Render/gkNextRenderer/gkNextRenderer.cpp`

改动：

- 拆出 `CreateBoxAndPushFromView(...)`。
- 为 remote multiview 暴露 per-session camera controller 创建 / tick / action 处理入口。
- 保持主窗口 `CreateBoxAndPush()` 行为不变。

### Browser

- `assets/remote/index.html`

改动：

- 显示 multiview mode / view id。
- `/config` 新字段展示。
- 输入协议保持兼容。

---

## 7. 风险与处理

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| Vulkan Video encoder session 数有限 | 第 3/4 个浏览器可能创建失败 | 默认 `--remote-max-clients=2`；创建 encoder 失败时拒绝 session，不影响已连接 session |
| bindless encode slot 冲突 | 多 stream 互相覆盖 NV12，画面错乱 | 必做 `FRemoteEncodeSlotAllocator`，不要复用固定 `RT_REMOTE_ENCODE0_*` |
| WebRTC 回调线程直接改 scene | 崩溃 / 数据竞争 | DataChannel 只入队；`RemoteServer::Tick()` 主线程执行 gameplay action |
| Remote view scheduling 时机太晚 | 编码上一帧或黑帧 | 在 renderer render phase 增加 schedule hook，不只依赖 postRender consumer |
| `RT_DENOISED` 色彩与 swapchain 不一致 | 浏览器偏暗/过曝 | 首期先 resolve 到 SDR BGRA8 中间图；后续再优化 direct ViewToNv12 |
| 多客户端频繁 `Space` 导致 scene dirty/TLAS rebuild 过重 | 掉帧 | 首期接受；后续给 gkNextRenderer remote box 加数量上限 / TTL / object pool |
| 输入仍走 SDL/ImGui capture | 多客户端互相抢主相机 | multiview mode 禁止 `SDL_PushEvent` 控制主输入；legacy mode 保持原逻辑 |
| swapchain recreate / scene reload | view 资源悬空 | 在 `OnRendererDeleteSwapChain()` 和 scene reload hook 中释放或 invalidate remote view resources |

---

## 8. 验收标准

基础：

- `--remote` legacy 行为不变。
- `--remote --remote-multiview --remote-max-clients 2` 下两个浏览器能同时连接。
- 两个浏览器画面可以不同，且操作互不影响。

gkNextRenderer 玩法：

- 浏览器 A / B 分别调整到不同朝向。
- A 按 `Space`，box 从 A 的相机附近沿 A 的方向飞出。
- B 按 `Space`，box 从 B 的相机附近沿 B 的方向飞出。
- A 和 B 都能看到两个 box 进入同一个共享 scene。

稳定性：

- 任一浏览器刷新 / 关闭，不影响其它浏览器。
- 超过最大连接数时有明确错误。
- 运行 5 分钟无 Vulkan validation error、无 encoder worker 死锁。

构建验证：

```bash
./gnb.bat build gkNextRenderer gkNextUnitTests
gnb shot --scene assets/models/playground.glb
```

如果改动只在 `NextRemote` + `gkNextRenderer`，可先跑 `./gnb.bat build gkNextRenderer`；触碰 `RenderView` / renderer facade / `RenderFrameConsumer` 后再加 `gkNextUnitTests`。

---

## 9. 接手建议

优先顺序不要从 browser UI 做起。先把 renderer / session / stream 的生命周期打通：

1. 让一个 session 绑定一个 RenderView 并在日志中稳定渲染。
2. 让一个 session 从该 RenderView 编码，而不是从 swapchain 编码。
3. 再扩展到两个 session。
4. 最后接 per-session 输入和 `Space` gameplay action。

这样每一步都有明确的可验证输出，也能避免在协议、输入、渲染、编码四条线同时出问题时难以定位。
