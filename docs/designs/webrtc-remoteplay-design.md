---
title: "WebRTC 远程游玩（Remote Play）设计与开发计划"
category: design
status: 进行中
owner: engine
created: 2026-06-08
last_updated: 2026-06-26
---

# WebRTC 远程游玩（Remote Play）设计与开发计划

> 状态：调研完成，持续实现中。
> 2026-06-26 更新：`openh264` 软编回退已移除；remote 现仅支持 **Vulkan Video H.264**。设备若不支持 Vulkan Video 编码，则直接不支持 remote。
> 目标：给 gkNextEngine 增加 `--remote` 启动模式，在本机 host 一个自托管服务器；浏览器连上后通过 **WebRTC** 实时看到游戏画面，并用**键盘 / 鼠标 / 手柄**操控游戏。
> 典型用途：连接远程开发机，配合远程 agent 工作流，做"随时随地"的开发 / 调试环境；顺带天然支持"云游戏"。
> 非目标（v1）：游戏音频回传、公网穿透（TURN）、鉴权 / TLS、移动端触屏、多人同屏协作。这些在第 12 节列为扩展路径。
> 日期：2026-06-08
> 关联代码：`src/DesktopMain.cpp`、`src/Engine/Runtime/Engine.cpp`、`src/Engine/Runtime/ScreenShot.cpp`、`src/Engine/Rendering/VulkanBaseRenderer.{hpp,cpp}`、`src/Engine/Vulkan/WindowSurface.cpp`、`src/Engine/Options.{hpp,cpp}`、`vcpkg.json`、`src/CMakeLists.txt`。

---

## 0. 结论（先读这一段）

可行性**高**。gkNextEngine 现有的三块基础设施刚好覆盖了远程游玩的三大难点，几乎不需要动核心架构：

| 难点 | 引擎现成能力 | 落点 |
|---|---|---|
| **拿到每帧画面** | 截图系统已把 swapchain 拷到**线性可映射** image（`CaptureScreenShot()` / `GetScreenShotImage()` / `GetScreenShotMemory()`），并有 `OnRendererPostRender(cmd, imageIndex)` 每帧钩子 | 复用截图拷回路径喂编码器；HW 编码走 `postRender` 命令录制 |
| **转发输入** | 所有输入统一走 `SDL_Event` → `NextEngine::HandleEvent()`；`SDL_PushEvent` 注入合成事件**已是仓库内既有写法**（`WindowSurface.cpp:207`） | 键鼠 / 滚轮合成 `SDL_Event` 注入；手柄用 **SDL3 虚拟手柄** |
| **无头运行** | `--hidden-window`（`Options::HiddenWindow`）已支持隐藏窗口真实渲染 | `--remote` 默认隐藏窗口即可纯后台 host |

技术选型（已与用户确认）：

- **WebRTC 库：[libdatachannel](https://github.com/paullouisageneau/libdatachannel)**（C++17，vcpkg 直接可用，静态库，Release 仅约 20MB vs libWebRTC 约 600MB）。提供 PeerConnection、Media Track（H.264/H.265/AV1 RTP 打包）、DataChannel、以及**内置 `rtc::WebSocketServer`**——信令可在引擎进程内自托管。**不自带编解码器**，正合我们用引擎自己的 GPU 编码。
- **视频编码：** 仅支持 **Vulkan Video（`VK_KHR_video_encode_h264`）**。它厂商中立、复用现有 Vulkan 后端、可对 swapchain 零拷贝编码；若设备不支持该能力，则 remote 功能不可用。
- **信令 / 自托管：** `rtc::WebSocketServer` 跑信令（offer/answer 交换），外加一个极小 HTTP 服务（推荐 header-only 的 cpp-httplib）把 Web 客户端单页发出去。LAN / 可直连优先，只配 STUN。
- **输入转发：** 浏览器经 **DataChannel**（无序、低延迟）把键鼠 / 手柄事件发回，服务端 `InputRouter` 译成 `SDL_Event` 用 `SDL_PushEvent` 注入；手柄注入到 `SDL_AttachVirtualJoystick` 建的虚拟手柄，`TickGamepadInput()` 的轮询会透明读到。

整体落成一个新模块 `src/Modules/NextRemote/`，用 `GK_WITH_REMOTE` CMake 开关守卫，`--remote` 时实例化。对引擎其余部分**零侵入**（只在 `Engine.cpp` 加一个成员 + 一处 `postRender` tap + 一处 tick）。

---

## 1. 背景与目标

### 1.1 用户故事

```text
开发机（有 GPU，可能在机房 / 云）          任意终端（笔记本 / 平板 / 手机浏览器）
┌─────────────────────────────┐         ┌──────────────────────────┐
│ gkNextRenderer --remote      │  WebRTC │ Chrome / Edge / Firefox   │
│   ├─ Vulkan 真实渲染（隐藏窗口）│ ◀─────▶ │   <video> 实时画面          │
│   ├─ H.264 编码 → RTP        │  视频    │   键鼠（Pointer Lock）       │
│   ├─ 自托管信令 + HTTP        │ ─────▶  │   手柄（Gamepad API）        │
│   └─ InputRouter 注入 SDL    │  输入    │                          │
└─────────────────────────────┘ DataChan└──────────────────────────┘
```

1. 在开发机上 `gnb run gkNextRenderer -- --remote`（或任意 program 加 `--remote`）。
2. 进程启动 Vulkan 正常渲染（默认隐藏窗口），并在本机 host `http://<dev-ip>:8088`。
3. 用户浏览器打开该地址 → 自动建立 WebRTC → 看到画面、能操控。
4. 配合远程 agent：agent 在开发机上改代码 / 跑 `gnb shot`，人在任意地方用浏览器实时验证手感与画面。

### 1.2 为什么是 WebRTC 而不是 RTSP / 自定义 TCP

WebRTC 的 ICE/DTLS/SRTP + 浏览器原生 `RTCPeerConnection` 让"浏览器端零安装、低延迟、能穿 NAT"三者一次满足；H.264 baseline 是浏览器解码的最大公约数。DataChannel 的**部分可靠 / 无序**模式正适合输入（丢一帧旧输入比卡顿好）。

---

## 2. 现状调研：引擎集成点（给接手 agent 的地图）

> 以下行号基于 2026-06-08 的 `master`，接手时以实际代码为准。

### 2.1 入口与主循环（SDL3 callbacks）

`src/DesktopMain.cpp` 用 `SDL_MAIN_USE_CALLBACKS`：

- `SDL_AppInit` 解析 `Options` → `new NextEngine(*GOption)` → `Start()`。
- `SDL_AppIterate` → `GApplication->Tick()`（持续 game loop，不阻塞等事件）。
- `SDL_AppEvent` → `GApplication->HandleEvent(*event)`。

**含义**：因为是持续 iterate，`SDL_PushEvent` 注入的合成事件会在下一轮被 `SDL_AppEvent` 正常分发，无需额外唤醒。

### 2.2 帧捕获：截图系统已经做完一半

`src/Engine/Rendering/VulkanBaseRenderer.hpp`：

```cpp
DeviceMemory* GetScreenShotMemory() const { return screenshot_.imageMemory.get(); } // line 77
const Image*  GetScreenShotImage()  const { return screenshot_.image.get(); }       // line 78
std::function<void(VkCommandBuffer, uint32_t)> postRender;                           // line 114 (delegate)
void CaptureScreenShot();                                                            // line 120
void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);                 // line 287
```

`screenshot_.image` 用 `frame_.swapChain->Format()` 创建（`VulkanBaseRenderer.cpp:747`），**线性 tiling、host 可映射**。`Runtime::ScreenShot::SaveSwapChainToFileFast()`（`ScreenShot.cpp:43`）展示了完整拷回姿势：

```cpp
renderer->CaptureScreenShot();                 // 把当前 swapchain copy 进线性 image
VkSubresourceLayout layout = ...;              // rowPitch 可能 > width*4，要按 pitch 取
uint8_t* gpu = (uint8_t*)renderer->GetScreenShotMemory()->Map(0, VK_WHOLE_SIZE);
memcpy(cpuBuf, gpu + layout.offset, layout.rowPitch * height);
renderer->GetScreenShotMemory()->Unmap();
// 然后丢给 TaskCoordinator 后台线程做像素处理（这里是写 jpg，我们改成喂编码器）
Tasks::TaskCoordinator::GetInstance()->AddTask(...);
```

像素是 **BGRA8**（代码里 `(uInPixel & (0xFF<<16))>>16` 取 R，说明内存顺序 B,G,R,A，即 `VK_FORMAT_B8G8R8A8_UNORM`）。HDR swapchain 走 10-bit 打包，`Fast` 路径直接 bail——**远程模式应强制 SDR**（见 2.6）。

**每帧钩子**：`Engine.cpp:504-511` 把引擎方法注册成 renderer delegate：

```cpp
rendererDelegates.createSwapChain = [this]{ OnRendererCreateSwapChain(); };
rendererDelegates.beforeNextTick  = [this]{ OnRendererBeforeNextFrame(); };
rendererDelegates.postRender      = [this](VkCommandBuffer cmd, uint32_t imageIndex){ OnRendererPostRender(cmd, imageIndex); };
```

`OnRendererPostRender()`（`Engine.cpp:1387`）是**录制 GPU 命令的天然 tap 点**——软件编码在这里触发拷回；Vulkan Video 硬件编码在这里录制 encode 命令（零拷贝）。

### 2.3 输入：全部走 SDL_Event，且已有 PushEvent 先例

`NextEngine::HandleEvent()`（`Engine.cpp:554`）是唯一分发中枢：UI → RmlUi → QuickJS → `OnKey/OnMouseButton/OnCursorPosition/OnScroll`。**任何来源的 `SDL_Event` 都会流过完整输入栈（编辑器 UI、脚本、相机、玩法）**。鼠标移动还区分相对模式：

```cpp
case SDL_EVENT_MOUSE_MOTION:
    if (SDL_GetWindowRelativeMouseMode(window_)) OnCursorPosition(event.motion.xrel, event.motion.yrel); // 相对
    else                                          OnCursorPosition(event.motion.x,   event.motion.y);    // 绝对
```

→ 浏览器 **Pointer Lock** 给的 `movementX/Y` 正好映射到 `xrel/yrel`。

注入合成事件**仓库已有先例**——`WindowSurface.cpp:207` 的 `Window::Close()` 就是构造 `SDL_Event` 调 `SDL_PushEvent`。`SDL_PushEvent` 线程安全，可从 WebRTC 线程直接调。

键盘是**事件驱动**（`ModelViewController::OnKey` 读 `event.key.key`），全仓库**没有** `SDL_GetKeyboardState` 轮询——所以合成 `SDL_EVENT_KEY_DOWN/UP` 完全够用，无需维护影子键盘态。

### 2.4 手柄：轮询 + 事件双路，用虚拟手柄覆盖

`TickGamepadInput()`（`Engine.cpp:1810`）**轮询**：`SDL_GetGamepads()` → `SDL_GetGamepadFromID()` → `SDL_GetGamepadAxis()` 喂 `gameInstance_->OnGamepadInput(...)`。同时 `HandleEvent` 也处理 `SDL_EVENT_GAMEPAD_BUTTON_DOWN/UP`。

→ 合成事件覆盖不了轮询路径，但 **SDL3 虚拟手柄**（`SDL_AttachVirtualJoystick` + `SDL_SetJoystickVirtualAxis/Button/Hat`）建出来的是**真**SDL 手柄，`SDL_GetGamepads/SDL_GetGamepadAxis` 与事件路径都会透明看到。这是手柄注入的正解。

### 2.5 配置系统

`src/Engine/Options.hpp` 是 `Runtime::Config::Options`（cxxopts 解析，`Options.cpp`），全局 `GOption`。已有 `HiddenWindow`、`ForceSDR`、`Width/Height`、`PresentMode` 等。`--remote` 系列开关加在这里。

### 2.6 已知约束

- **HDR**：远程编码强制 SDR——`--remote` 时置 `Options::ForceSDR = true`（swapchain 走 8-bit BGRA，匹配编码器 / 截图 Fast 路径）。
- **线程**：重活（颜色转换 + 编码）必须离开渲染线程。仓库有 `Tasks::TaskCoordinator`（`ScreenShot.cpp` 已用它后台存图）；也可起专用编码线程。GPU 内存 `Map/memcpy/Unmap` 这步留在主线程，拷出后立刻交给 worker。
- **vcpkg 现状**（`vcpkg.json`）：已有 sdl3(vulkan)、curl、imgui、nlohmann-json、vulkan-headers/loader、vma 等。**需新增** libdatachannel（开 srtp）、cpp-httplib（颜色转换走 GPU compute，不用 libyuv）。

---

## 3. WebRTC 方案选型

### 3.1 候选对比

| 方案 | 语言 / 体量 | vcpkg | 媒体 | 信令 | 适配度 |
|---|---|---|---|---|---|
| **libdatachannel** ✅ | C++17，Release ~20MB | ✅ 直接 | H.264/H.265/AV1 **打包**（不含编解码器） | **内置** WebSocket(Server) | **最佳**：轻、静态、自带信令服务、BYO 编码器正合我们用 GPU 编码 |
| Google libWebRTC | C++，~600MB，GN 构建 | ❌（无好 port） | 全，含软编解码 + BWE | 需自接 | 过重、构建地狱、和 vcpkg/CMake 不友好 |
| GStreamer webrtcbin | C，插件式 | 部分 | 全 | 需自接 | 运行期插件依赖重，部署复杂 |
| Pion / aiortc | Go / Python | — | — | — | 非 C++，进程外，不合适 |

libdatachannel 依赖链小：libjuice（ICE/NAT）、usrsctp（SCTP/DataChannel）、OpenSSL 或 GnuTLS/MbedTLS（DTLS）、可选 libsrtp（媒体）。vcpkg 会处理传递依赖。

**已知局限**（写进风险）：libdatachannel **无带宽估计（BWE）**、不响应接收端码率请求——码率要我们自己定 / 自适应；只对 H.264 做**收包解包**（我们是发送端，不影响）。

### 3.2 关键 API（取自官方 `examples/streamer/main.cpp`，确认可用）

发送视频轨：

```cpp
auto video = rtc::Description::Video("video-stream");
video.addH264Codec(102);                       // payload type
video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
auto track = pc->addTrack(video);

auto rtpConfig  = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "video", 102, rtc::H264RtpPacketizer::ClockRate);
auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::Length, rtpConfig);
packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtpConfig)); // RTCP SR
packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());        // NACK 重传
track->setMediaHandler(packetizer);

// 每帧编码后：track->sendFrame(annexBOrLengthPrefixedNalus, timestampMicros);
```

DataChannel（输入回传）：

```cpp
auto dc = pc->createDataChannel("input");      // 见第 7 节配置 unordered/低延迟
dc->onMessage([](rtc::message_variant msg){ /* 解析输入，注入 SDL */ });
```

信令（**进程内自托管**，`rtc::WebSocketServer` 已确认存在）：

```cpp
rtc::WebSocketServer::Configuration cfg;       // 端口、可选 TLS/证书
cfg.port = 8089;
auto wsServer = std::make_shared<rtc::WebSocketServer>(cfg);
wsServer->onClient([](std::shared_ptr<rtc::WebSocket> ws){
    ws->onMessage([ws](auto data){ /* offer/answer/candidate 交换 */ });
});
```

---

## 4. 视频编码选型

### 4.1 路线（已确认）

| 阶段 | 编码器 | vcpkg | 优点 | 取舍 |
|---|---|---|---|---|
| **Phase 1（打通）** | **openh264**（Cisco） | ✅ `openh264` | Constrained Baseline ≤L5.2，浏览器全兼容（Chrome/Firefox 本身就用它），纯软、可移植 | CPU 占用高，1080p60 吃力；先用 720p / 中码率验证链路 |
| **Phase 2（主线 HW）** | **Vulkan Video** `VK_KHR_video_encode_h264` | 引擎自带 Vulkan，无需 vcpkg | **厂商中立**、复用现有 device/queue、**对 swapchain 零拷贝**、不锁 N 卡 | 实现复杂，驱动支持需探测；需 NV12 + encode session 管理 |
| 可选加速 | NVENC（NVIDIA Video Codec SDK） | 手动 SDK | 延迟最低、成熟 | 锁 N 卡；仅在 Vulkan Video 不达标时作为后备 |

> **已确认远程主机为 NVIDIA**：Phase 4 的 Vulkan Video 优先做（N 卡驱动对 `VK_KHR_video_encode_h264` 支持成熟）。万一目标驱动的 Vulkan Video 路径遇阻，NVENC 是同卡天然后备（N 卡必有），优先级高于回退软编。

抽象成接口，三者可热插拔：

```cpp
// src/Modules/NextRemote/VideoEncoder.hpp
class IVideoEncoder {
public:
    struct Config { uint32_t width, height, fps, bitrateKbps; };
    virtual bool   Init(const Config&) = 0;
    virtual void   RequestKeyframe() = 0;                 // 新客户端 / 丢包恢复 → 强制 IDR
    virtual void   SetBitrate(uint32_t kbps) = 0;         // 手动码率自适应
    // 输入一帧（CPU：BGRA/I420 指针；或 GPU：VkImage），输出 Annex-B NAL 列表
    virtual bool   Encode(const FrameView& in, std::vector<rtc::byte>& outNalus, bool& isKeyframe) = 0;
    virtual ~IVideoEncoder() = default;
};
```

### 4.2 颜色转换 BGRA → I420 / NV12（决策：GPU compute）

openh264 吃 **I420（YUV420p）**，Vulkan Video 吃 **NV12**。swapchain 是 BGRA8。**决策：走 GPU compute 转换，不引入 libyuv。**

原因：libyuv 的 vcpkg 端口在 **MSVC 下不编译任何 SIMD 加速码**（port 自带告警 + microsoft/vcpkg#28446），逐帧 BGRA→I420 会很慢；而引擎本就是 Vulkan，写个 compute shader 转换更快、回读量更小，且与 Phase 4 的 GPU 硬编路径同源。

- **转换 shader**（`assets/shaders/remote/bgra_to_yuv.comp.slang`）：在 `OnRendererPostRender` 以 swapchain（或截图 image）为输入，输出 I420（软编回读）/ NV12（硬编直喂）。回读量从 BGRA 4B/px 砍到 1.5B/px。
- **Phase 1 例外**：最小打通只推固定一帧 / 测试图样时，可用十几行**标量 BGRA→I420** 先验证链路（零依赖），Phase 3 起换上 compute shader 做连续流。
- BT.601/709 系数与 limited/full range 要和编码器、浏览器解码约定一致（默认 BT.709 limited range，SDR）。

### 4.3 编码参数（低延迟基线）

- Profile：Constrained Baseline（无 B 帧、单 slice / 按需多 slice）。
- GOP：长 GOP + **按需 IDR**（新客户端接入、收到 PLI/NACK 风暴时 `RequestKeyframe()`）。openh264 设 `iUsageType = CAMERA_VIDEO_REAL_TIME`、`bEnableFrameSkip`、`iRCMode = RC_BITRATE_MODE`。
- 起始：720p@30、~4Mbps，跑通后再调到 1080p / 60。
- 时间戳：90kHz 时钟（`H264RtpPacketizer::ClockRate`），`sendFrame` 传微秒。

---

## 5. 架构设计

### 5.1 新模块布局

```text
src/Modules/NextRemote/            # 新增，GK_WITH_REMOTE 守卫
├── RemoteServer.{hpp,cpp}            # 编排器：持有信令 + 会话表 + 编码器 + 帧源；--remote 时由 Engine 创建
├── SignalingServer.{hpp,cpp}         # rtc::WebSocketServer（信令） + cpp-httplib（发 Web 客户端单页）
├── RemoteSession.{hpp,cpp}           # 每客户端：PeerConnection + video track + input DataChannel + 状态机
├── FrameSource.{hpp,cpp}             # tap 截图拷回 / GPU 颜色转换，产出编码器输入帧（含帧率节流）
├── VideoEncoder.hpp                  # IVideoEncoder 接口
├── VulkanVideoEncoder.{hpp,cpp}      # Phase 2 硬编（VK_KHR_video_encode_h264）
├── InputRouter.{hpp,cpp}            # DataChannel 消息 → SDL_Event（SDL_PushEvent）
├── VirtualGamepad.{hpp,cpp}          # SDL3 虚拟手柄（SDL_AttachVirtualJoystick）
└── RemoteProtocol.hpp                # 输入 / 控制消息 schema（与 Web 客户端共享约定）

assets/remote/
└── index.html                        # 单文件 Web 客户端（HTML+JS+CSS，无构建步骤）

assets/shaders/remote/
└── bgra_to_yuv.comp.slang            # GPU 颜色转换 BGRA→I420/NV12（Phase 3 起，替代 libyuv）
```

引擎侧改动（**最小侵入**）：

```text
src/Engine/Options.{hpp,cpp}          # +RemoteMode/RemotePort/RemoteHttpPort/RemoteBitrate/RemoteFps/RemoteWidth...
src/Engine/Runtime/Engine.{hpp,cpp}   # +std::unique_ptr<RemoteServer> remote_; Start()里按需建; Tick()里泵; OnRendererPostRender里tap
src/DesktopMain.cpp                    # --remote 时默认 HiddenWindow=true、ForceSDR=true（也可保留窗口）
vcpkg.json                             # +libdatachannel(ws,srtp) +cpp-httplib
src/CMakeLists.txt                     # find_package + GK_WITH_REMOTE + 链接
assets/CMakeLists.txt                  # 拷贝 assets/remote/
```

### 5.2 数据流

```text
[渲染线程 / 主线程]                          [编码线程(TaskCoordinator/专用)]        [libdatachannel 内部线程]
 OnRendererPostRender(cmd,imageIndex)
   └─ FrameSource: 节流到目标fps
        ├─(SW) CaptureScreenShot()+Map+memcpy ──► BGRA→I420(GPU compute) ─► IVideoEncoder.Encode ─► NALUs
        └─(HW) 录制 BGRA→NV12 compute + VkVideoEncode 命令                                   │
                                                                                            ▼
                                                          track->sendFrame(NALUs, ts)  ─► H264RtpPacketizer ─► SRTP ─► 浏览器 <video>

[浏览器]  KeyboardEvent/PointerLock/Gamepad  ─DataChannel(unordered)─►  InputRouter
                                                                          ├─ 键鼠/滚轮 → 合成 SDL_Event → SDL_PushEvent ─► HandleEvent
                                                                          └─ 手柄 → SDL_SetJoystickVirtualAxis/Button ─► TickGamepadInput 轮询
```

### 5.3 线程与生命周期

- `RemoteServer` 在 `NextEngine::Start()` 末尾、`gameInstance_->OnInit()` 之后创建（仅当 `GOption->RemoteMode`）。
- 信令 / WebRTC 跑各自线程（libdatachannel 内部线程池 + 我们的信令回调）。会话表加锁。
- `FrameSource::OnPostRender()` 在渲染线程被调；只做"判断该不该出帧 + 触发拷回/录制"，重活转 worker。
- `InputRouter` 在 DataChannel 回调线程把事件 `SDL_PushEvent` 进 SDL 队列（线程安全），主线程下一轮消费。
- `NextEngine::End()` 里 `remote_.reset()`：停信令、关所有 PeerConnection、`SDL_DetachVirtualJoystick`。

---

## 6. 信令与自托管服务器

### 6.1 端口与服务

- **HTTP（cpp-httplib，默认 8088）**：`GET /` 返回 `assets/remote/index.html`；`GET /config` 返回 STUN 列表、信令 WS 地址、会话参数（分辨率/fps）。
- **信令 WS（rtc::WebSocketServer，默认 8089）**：每个浏览器一条 WS，交换 offer/answer/ICE candidate。

> 也可只开一个端口：cpp-httplib 发页面、libdatachannel WS 单列端口；二者分端口实现最简。若要单端口可后续用反代或 httplib 的 upgrade 钩子，非 v1 必需。

### 6.2 会话建立（服务端 offer 模式，与官方 streamer 一致）

```text
浏览器                          信令WS                     RemoteServer
  │ open ws://dev:8089 ─────────────►│
  │ {type:"request", id} ───────────►│ onClient/onMessage
  │                                  │ 建 RemoteSession: pc + addTrack(video) + createDataChannel(input)
  │                                  │ pc.setLocalDescription()  → 收集 ICE
  │ ◄──── {type:"offer", sdp} ───────│ onGatheringStateChange(Complete) 时回发
  │ pc.setRemoteDescription(offer)   │
  │ pc.createAnswer()/setLocal       │
  │ {type:"answer", sdp} ───────────►│ pc.setRemoteDescription(answer)
  │ ◄═══════ DTLS/SRTP/SCTP 直连 ════►│  视频流 + DataChannel 建立
```

`disableAutoNegotiation = true`（streamer 同款）已纳入当前实现。STUN 是否需要引入，取决于部署拓扑：纯 LAN / 内网穿透由外层网络解决时，可继续不配；若要支持跨子网 / NAT 直连，再单独补 `iceServers` 配置。

### 6.3 LAN 优先的取舍

- 默认绑 `0.0.0.0`，靠局域网 / 直连可达；公网穿透（TURN）、token 鉴权、WSS/TLS 见第 12 节，**v1 不做**。
- 安全提示：v1 假设可信网络。文档与启动日志要打印"未鉴权，勿暴露到公网"。

---

## 7. 输入转发协议（RemoteProtocol）

### 7.1 DataChannel 配置

`createDataChannel("input", {reliability})`：输入用 **unordered + 部分可靠（maxRetransmits=0）**——丢个旧输入无所谓，绝不为重传卡顿。控制类消息（resize、keyframe 请求）可走第二条**可靠有序** channel 或同一 channel 标志位区分。

### 7.2 消息格式

为低延迟用紧凑二进制（首字节 type），调试期可先 JSON。Schema：

| type | 字段 | → 引擎动作 |
|---|---|---|
| `key` | down:bool, code:u32(SDL_Scancode), mod:u16 | 合成 `SDL_EVENT_KEY_DOWN/UP`（填 `key.scancode/key.key/key.mod`）→ `SDL_PushEvent` |
| `mousemove` | dx:i32, dy:i32 (Pointer Lock 相对) / 或 x,y 绝对 | `SDL_EVENT_MOUSE_MOTION`（填 `motion.xrel/yrel` 或 `x/y`）|
| `mousebtn` | down:bool, button:u8 | `SDL_EVENT_MOUSE_BUTTON_DOWN/UP` |
| `wheel` | dx:f32, dy:f32 | `SDL_EVENT_MOUSE_WHEEL` |
| `gamepad` | axes[6]:i16, buttons:u16 bitmask | `SDL_SetJoystickVirtualAxis/Button`（不是 PushEvent）|
| `ctrl` | resize{w,h} / requestKeyframe / ping | 改编码分辨率 / `encoder->RequestKeyframe()` / RTT 测量 |

### 7.3 键码映射

浏览器 `KeyboardEvent.code`（如 `"KeyW"`、`"ArrowUp"`、`"ShiftLeft"`）→ **SDL_Scancode**（`SDL_SCANCODE_W`...）的静态表，放 Web 客户端做（发 scancode 数值），服务端再 `SDL_GetKeyFromScancode` 补 `key.key`。用 `code`（物理键位）而非 `key`，避免输入法 / 布局干扰，FPS 操作更稳。

### 7.4 鼠标相对模式

Web 客户端点击画面 → `requestPointerLock()`；锁定后用 `movementX/movementY` 发相对位移。服务端注入相对位移。注意 `HandleEvent` 的相对分支依赖 `SDL_GetWindowRelativeMouseMode(window_)`——隐藏窗口下可能为 false，两条路任选：

1. `--remote` 时对（隐藏）窗口开 `SDL_SetWindowRelativeMouseMode(true)`；或
2. `InputRouter` 维护"远程会话已 pointer-lock"标志，直接调 `OnCursorPosition(dx,dy)`（需把它提成可访问入口，或加 `NextEngine::InjectRelativeMouse(dx,dy)` 薄封装）。

推荐 2（不依赖窗口状态，语义清晰）。

### 7.5 虚拟手柄

`RemoteServer` 启动时 `SDL_AttachVirtualJoystick(&desc)` 建一个标准布局（6 轴 + 15 键，`SDL_GAMEPAD_TYPE_STANDARD`），拿到 `SDL_JoystickID`。`gamepad` 消息到达 → `SDL_SetJoystickVirtualAxis/Button`。`TickGamepadInput()` 与手柄事件路径透明读到。关闭时 `SDL_DetachVirtualJoystick`。

---

## 8. Web 客户端（assets/remote/index.html）

单文件、无构建步骤、原生 API：

- `RTCPeerConnection` 收 video track → `videoEl.srcObject = stream`；`<video autoplay playsinline muted>` 全屏铺满。
- 信令：`new WebSocket("ws://"+location.hostname+":8089/<id>")`，收 offer → `setRemoteDescription` → `createAnswer` → 回发。
- 输入：`document.addEventListener('keydown/keyup')`（`e.code`→scancode 表）；点击 `requestPointerLock`，`mousemove` 取 `movementX/Y`；`mousedown/up`、`wheel`；`navigator.getGamepads()` 每帧轮询打包发送。
- 控制条（可隐藏）：连接状态、码率 / RTT / 手动 IDR / 原始像素显示。
- 统计面板：基于 `pc.getStats()` 展示视频分辨率、解码 FPS、接收码率、累计字节、抖动、丢包、候选链路与可用带宽，作为当前阶段的浏览器端调试入口。
- 统一通过 `inputChannel.send(pack(msg))` 发二进制。

> 客户端尽量自包含，方便直接被 cpp-httplib 当静态资源发出去，也方便嵌进二进制（`xxd -i` 或资源打包）做成"零外部文件"部署。

---

## 9. 命令行与配置

`Options.hpp` 新增（`Options.cpp` 注册 cxxopts）：

| flag | 默认 | 说明 |
|---|---|---|
| `--remote` | false | 开启远程游玩；隐含 `HiddenWindow=true`、`ForceSDR=true`（可被显式覆盖）|
| `--remote-http-port` | 8088 | Web 客户端 HTTP 端口 |
| `--remote-port` | 8089 | 信令 WS 端口 |
| `--remote-bitrate` | 4000 | 起始码率 kbps |
| `--remote-fps` | 30 | 目标推流帧率 |
| `--remote-res` | 源分辨率 | 如 `1280x720`，编码缩放目标 |
| `--remote-bind` | 0.0.0.0 | 监听地址 |
| `--remote-show-window` | false | 调试用：保留可见窗口 |

`DesktopMain.cpp` 在 `--remote` 且用户没显式给窗口选项时设隐藏窗口 + SDR。

`gnb` 侧补一个 `gnb remote` 快捷命令：包装 `gnb run <target> -- --remote ...`，负责打印浏览器访问地址，减少手写 remote 参数的成本。

---

## 10. vcpkg / CMake 集成

> 以下已按 vcpkg 上游 portfile 核对。当前 remote 依赖的关键点是：libdatachannel 的**媒体传输是 feature-gated**，必须显式开 `srtp` 才有 H.264 轨（`ws` 默认开，给 WebSocketServer）。

`vcpkg.json` dependencies 增（桌面 Win+Linux；按决策排除 macOS / android / ios）：

```json
{ "name": "libdatachannel", "features": ["ws", "srtp"], "platform": "!(android | ios | osx)" },
{ "name": "cpp-httplib",                                "platform": "!(android | ios | osx)" }
```

> 注：libdatachannel 会传递引入 **libjuice / usrsctp / openssl / plog / libsrtp**（srtp feature），以及 **nlohmann-json**（引擎已用，共享）。首次 `vcpkg install` 会编译 openssl，构建时间会涨。**不再依赖 libyuv**（颜色转换走 GPU compute，见 §4.2）。

`src/CMakeLists.txt`：

```cmake
option(GK_WITH_REMOTE "Enable WebRTC remote play" ON)
# 决策：桌面 Win + Linux 才开；macOS(MoltenVK 无 VK video encode) / 移动端不构建 remote
if(GK_WITH_REMOTE AND NOT (ANDROID OR IOS OR APPLE))
    find_package(LibDataChannel CONFIG REQUIRED)         # → LibDataChannel::LibDataChannel
    target_compile_definitions(gkNextEngine PUBLIC GK_WITH_REMOTE=1)
    target_link_libraries(gkNextEngine PRIVATE LibDataChannel::LibDataChannel httplib::httplib)
endif()
```

> 接手 agent 注意：
> - 静态 triplet（本仓 `x64-windows-static`）下 `LibDataChannel::LibDataChannel` 即静态目标；若 config 只导出 `LibDataChannel::LibDataChannelStatic`，改用该名。
> - cpp-httplib 是 header-only：`find_package(httplib CONFIG REQUIRED)` → `httplib::httplib`（仅头文件，无需链接库，但要 include）。
> - Vulkan Video（Phase 4）走引擎已有的 vulkan-headers，无需新 port，但要在 device 创建处启用 `VK_KHR_video_queue` / `VK_KHR_video_encode_queue` / `VK_KHR_video_encode_h264` 扩展并运行期探测可用性。

---

## 11. 开发计划（分阶段，给接手 agent）

> 每个 Phase 给"任务 / 改动文件 / 验收"。建议每 Phase 落一条 `.spec/TODO.md` 任务并写 journal。除非大重构，按 `AGENTS.md` 用 targeted build：`./gnb build gkNextRenderer gkNextUnitTests`。
> **平台范围（已决策）**：仅桌面 **Windows + Linux** 构建 remote；macOS / Android / iOS 关 `GK_WITH_REMOTE`。

### Phase 0 — 依赖与脚手架（不出画面，先把架子搭起来）
- 任务：加 vcpkg 依赖 + `GK_WITH_REMOTE` + 空 `Remote/` 模块；`--remote` 系列 flag；`RemoteServer` 起 cpp-httplib 发一个静态 `index.html`（占位文字）+ `rtc::WebSocketServer` 能 onClient 打日志。
- 改动：`vcpkg.json`、`src/CMakeLists.txt`、`Options.{hpp,cpp}`、`DesktopMain.cpp`、`Remote/RemoteServer.*`、`Remote/SignalingServer.*`、`assets/remote/index.html`、`assets/CMakeLists.txt`。
- 验收：`gnb run gkNextRenderer -- --remote` 后浏览器开 `http://127.0.0.1:8088` 看到占位页；WS 连上服务端打印 client 日志；`gkNextRenderer gkNextUnitTests` 构建绿。

### Phase 1 — 视频端到端最小可用（能看到画面，无输入）
- 任务：`OpenH264Encoder` + `FrameSource`（先用**纯色 / 测试图样或固定一帧 CaptureScreenShot**）→ `H264RtpPacketizer` → track；走完 offer/answer；Web 客户端 `RTCPeerConnection` 显示 `<video>`。新客户端接入发初始 NALU（参考 streamer `sendInitialNalus`）。
- 改动：`Remote/OpenH264Encoder.*`、`Remote/FrameSource.*`、`Remote/RemoteSession.*`、信令补全、`index.html` 接收端、`Engine.cpp`(建 RemoteServer + tick)。
- 验收：浏览器实时看到引擎画面（哪怕固定一帧/低帧率），`pc.getStats()` 有视频流入、`bytesReceived` 增长。

### Phase 2 — 输入转发（能操控）
- 任务：`InputRouter`：键 / 鼠 / 滚轮 → `SDL_PushEvent`；Pointer Lock 相对鼠标（用 `InjectRelativeMouse` 薄封装）；`VirtualGamepad` + 手柄消息。`index.html` 采集并经 DataChannel 发送；`RemoteProtocol.hpp` 定 schema。
- 改动：`Remote/InputRouter.*`、`Remote/VirtualGamepad.*`、`Remote/RemoteProtocol.hpp`、`Engine.{hpp,cpp}`(+`InjectRelativeMouse`)、`index.html`。
- 验收：浏览器 WASD/鼠标能转视角、点击有反应；插手柄（或浏览器手柄）能驱动 `OnGamepadInput`；在 MagicaLego / CharacterDemo 实测手感。

### Phase 3 — 真实连续推流 + 健壮性
- 任务：`FrameSource` 接**连续**取帧（按 `--remote-fps` 节流）+ **GPU compute BGRA→I420**（`bgra_to_yuv.comp`，在 `OnRendererPostRender` 录制，回读 planar），编码转 worker 线程（TaskCoordinator）；按需 IDR（新客户端 / NACK）；手动码率（`SetBitrate`）；多客户端会话表；断连重连。
- 改动：`FrameSource.*`、`OpenH264Encoder.*`、`RemoteServer.*`（会话管理）、`assets/shaders/remote/bgra_to_yuv.comp.slang`。
- 验收：移动相机时画面流畅跟随；720p@30 稳定；二客户端可同时观看；杀掉浏览器再连可恢复；渲染线程帧时间无明显抖动（worker 卸载生效）。

### Phase 4 — Vulkan Video 硬件编码（厂商中立主线）
- 任务：device 创建启用 video encode 扩展 + 能力探测；`VulkanVideoEncoder` 用 `VK_KHR_video_encode_h264`；`OnRendererPostRender` 录制 BGRA→NV12 compute + encode 命令，**零回读**直接拿码流；若设备不支持则 remote 不启动。
- 改动：`Vulkan/Device.*`（扩展）、`Remote/VulkanVideoEncoder.*`、`FrameSource.*`（GPU 路径）、`assets/shaders/`（BGRA→NV12 compute）。
- 验收：目标 **N 卡**实测 Vulkan Video 硬编出流，CPU 占用相比 Phase 3 明显下降、延迟下降；不支持 Vulkan Video 的设备启动时明确提示 remote 不可用。

### Phase 5 — 质量与体验打磨（范围调整）
- 本阶段已完成：
  - 客户端统计 overlay；
  - `gnb remote` 快捷起服务与地址打印。
- 以下事项暂不纳入当前开发范围，后续按需求单独立项：
  - 基于 `getStats` / RTCP 的简易码率自适应；
  - 分辨率协商与动态 resize；
  - 二维码。
- 当前范围维持：手动码率、启动时固定 remote 分辨率、基础浏览器 stats 展示。

### Phase 6 —（可选 / later）扩展
- 音频回传（Opus，接 `NextAudio` 混音输出 → `OpusRtpPacketizer`）。
- 公网：内置 / 对接 TURN（coturn）+ token 鉴权 + WSS/TLS。
- 移动端触屏（`OnTouch/OnTouchMove` 已有）→ 虚拟摇杆叠加层。

---

## 12. 风险与未决问题

| 项 | 风险 | 缓解 |
|---|---|---|
| libdatachannel 无 BWE | 弱网无法自动降码率 | 当前阶段接受手动码率；若后续有明确弱网诉求，再单独立项做自适应 |
| Vulkan Video 驱动支持参差 | 部分 GPU/驱动无 encode 队列 | 运行期能力探测；不支持时直接禁用 remote，并在启动日志明确说明 |
| 颜色转换 / 回读开销 | 软件路径 1080p60 CPU 高 | 默认 720p；worker 线程；Phase 4 GPU NV12 零回读 |
| 隐藏窗口相对鼠标 | `SDL_GetWindowRelativeMouseMode` 为 false | 用 `InjectRelativeMouse` 直注入，不依赖窗口态（7.4）|
| SDL 事件注入时序 | 跨线程 PushEvent 顺序 / 焦点 | PushEvent 线程安全；输入 channel 用无序低延迟；必要时主线程批量 drain |
| HDR swapchain | 10-bit 打包不匹配编码器 | `--remote` 强制 `ForceSDR` |
| 安全（v1 LAN 假设） | 暴露公网无鉴权风险 | 启动告警 + 文档；公网走 Phase 6 鉴权/TLS |
| 多客户端扩展 | 每客户端独立编码开销大 | v1 单 / 少客户端；多观看者可后续共享同一码流多路 RTP |
| 时间戳 / A-V 同步 | v1 无音频，纯视频时间基 | v1 只需视频 90kHz 单调时间戳；音频在 Phase 6 再对齐 |

**决策记录（2026-06-08 已敲定）**：

| # | 议题 | 决策 |
|---|---|---|
| 1 | vcpkg 目标名 | `find_package(LibDataChannel CONFIG)` → `LibDataChannel::LibDataChannel`（静态 triplet 同名；若只导出则用 `LibDataChannel::LibDataChannelStatic`）。**libdatachannel 必须开 `srtp` feature**（`ws` 默认开）否则无媒体轨。详见 §10。|
| 2 | HTTP 静态服务 | **cpp-httplib**（Phase 0 起用，header-only，`httplib::httplib`）；嵌入二进制留作后续可选（Phase 5）。|
| 3 | 主机 GPU / Vulkan Video | 远程主机 **NVIDIA** → Phase 4 Vulkan Video 优先做；当前阶段**不实现任何软件/厂商私有后备**，设备不支持则 remote 不可用。|
| 4 | macOS remote | **不做**，桌面仅 **Win + Linux**（`GK_WITH_REMOTE` 在 APPLE/android/ios 关）。后续若需 mac 再评估 VideoToolbox 等独立路径。|
| 5 | 颜色转换 | **GPU compute**（`bgra_to_yuv.comp.slang`），**不引入 libyuv**（其 vcpkg/MSVC 端口无 SIMD、很慢）。详见 §4.2。|

仍需开工时跑一次的**运行期探测**（非决策）：在目标 N 卡上 `vulkaninfo | grep -i video_encode` 确认列出 `VK_KHR_video_encode_h264` / `VK_KHR_video_encode_queue` —— 决定 Phase 4 是直接走 Vulkan Video 还是先落 NVENC。

---

## 13. 参考资料

- libdatachannel（C/C++ WebRTC，DataChannels / Media / WebSockets）：<https://github.com/paullouisageneau/libdatachannel> ，官网 <https://libdatachannel.org/>
- libdatachannel vs libWebRTC 体量 / 能力对比（20MB vs 600MB、无 BWE、H.264/H.265/AV1 打包、BYO 编解码器）：<https://tensorworks.com.au/blog/a-brief-comparison-of-libdatachannel-and-libwebrtc/>
- libdatachannel streamer 示例（H264/Opus over WebRTC，本设计 API 取样）：<https://github.com/paullouisageneau/libdatachannel/blob/master/examples/streamer/main.cpp>
- `rtc::WebSocketServer`（进程内信令服务）头文件：<https://github.com/paullouisageneau/libdatachannel/blob/master/include/rtc/websocketserver.hpp>
- OpenH264（Constrained Baseline 实时编码，Chrome/Firefox 采用），vcpkg：<https://vcpkg.io/en/package/openh264.html> ，<https://github.com/cisco/openh264>
- SDL3 虚拟手柄 `SDL_AttachVirtualJoystick` / `SDL_SetJoystickVirtualAxis`：<https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick>
- libyuv（BGRA/ARGB ↔ I420 颜色转换）：<https://chromium.googlesource.com/libyuv/libyuv>
- Vulkan Video encode 扩展 `VK_KHR_video_encode_h264`：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_video_encode_h264.html>
