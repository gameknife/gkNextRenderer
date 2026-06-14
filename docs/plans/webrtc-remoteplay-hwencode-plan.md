---
title: "Remote Play 硬件编码改造计划（HW Texture → Vulkan Video → WebRTC）"
category: plan
status: 进行中
owner: engine
created: 2026-06-10
last_updated: 2026-06-10
---

# Remote Play 硬件编码改造计划（HW Texture → Vulkan Video → WebRTC）

> 状态：设计完成，待实现（供后续 agent 接手开发，建议按 Phase 拆 `.spec/TODO.md` 任务逐个推进）。
> 前置：`docs/designs/webrtc-remoteplay-design.md`（总体设计）已落地到 Phase 3 附近——openh264 软编 + WebRTC 推流 + 输入转发**已可用但很慢**。本文是其 Phase 4（硬件编码）的细化执行计划，并先修掉当前软件路径的结构性瓶颈。
> 目标：`--remote` 推流从"主线程阻塞式 CPU 全软路径"改为"渲染帧命令缓冲内零拷贝 NV12 转换 + Vulkan Video 硬件 H.264 编码 + 异步码流回读"，达到 1080p@60 推流、渲染线程每帧额外开销 < 0.5ms、远程相关 CPU 占用 < 5%。
> 日期：2026-06-10
> 关联代码：`src/Modules/NextRemote/*`、`src/Engine/Rendering/VulkanBaseRenderer.{hpp,cpp}`、`src/Engine/Vulkan/Device.{hpp,cpp}`、`src/Engine/Vulkan/CommandExecution.cpp`、`src/Engine/Runtime/Engine.cpp`。

---

## 0. 结论（先读这一段）

当前慢的根因**不是 openh264 本身**，而是整条取帧链路在主线程上每帧做了三次重活 + 一次 GPU 硬等待。即使换上硬件编码器，如果不先把"阻塞式取帧 + 每会话独立管线"这两个结构问题修掉，依然快不起来。因此计划分两步走：

1. **P0/P1（结构修复，软编仍在）**：取帧并入渲染帧命令缓冲（不再 `SingleTimeCommands` 阻塞等待）、颜色转换上 GPU compute、编码下放 worker 线程、N 个会话共享一条编码管线。这一步完成后软编路径本身就能达到 720p@30 流畅，并成为硬编不可用时的回退。
2. **P2~P4（硬编主线）**：`VK_KHR_video_encode_h264` 编码会话，swapchain → compute BGRA→NV12 →（视频编码队列）`vkCmdEncodeVideoKHR` → 码流 buffer，全程零 CPU 像素拷贝；只有最终的 H.264 码流（每帧几十 KB）回到 CPU 交给 libdatachannel 打包发送。

---

## 1. 现状瓶颈分析（基于 2026-06-10 master，行号供参考）

数据流现状：`Engine::Tick()` → `remoteServer_->Tick()`（`Engine.cpp:797-801`，主线程）→ 每个 `FRemoteSession::Tick()` → `SendVideoFrame()`（`RemoteSession.cpp:322-372`）在**主线程**串行完成取帧 + 转换 + 编码 + 发送。

| # | 瓶颈 | 位置 | 代价 |
|---|---|---|---|
| B1 | **每帧阻塞式 GPU 拷贝**：`CaptureScreenShot()` 走 `SingleTimeCommands::Submit`，单独提交命令缓冲并 `vkWaitForFences` 硬等（`VulkanBaseRenderer.cpp:968-997`、`CommandExecution.cpp:81-128`）。这是一次主线程↔GPU 的完整同步往返，且与正在渲染的帧争用 graphics queue | `FrameSource.cpp:107` | 主线程每帧被 GPU 卡住数 ms；帧率越高占比越大 |
| B2 | **标量逐像素 BGRA→I420 + 缩放**：双重循环逐像素 `BgraToYuv` + 最近邻缩放 + chroma 累加，纯标量无 SIMD，在主线程跑 | `FrameSource.cpp:129-147` | 720p 约 92 万像素/帧，毫秒级；1080p 不可用 |
| B3 | **线性 tiling 全量回读**：截图 image 是线性 host-visible BGRA（4B/px），按 rowPitch 全分辨率 Map 读取 | `FrameSource.cpp:116-122` | 回读带宽是必要量（NV12 1.5B/px）的 2.7 倍，且走的是慢速线性内存 |
| B4 | **openh264 编码在主线程**：`encoder_.Encode()` 同步调用 | `RemoteSession.cpp:358` | 720p@30 数 ms/帧直接计入主循环 |
| B5 | **每会话独立管线**：每个 `FRemoteSession` 自带 `FFrameSource` + `FOpenH264Encoder`，N 个浏览器 = N 次 B1~B4 | `RemoteSession.cpp:47-49` | 多客户端线性恶化 |
| B6 | 截图 image 与渲染帧无同步保证：`CaptureScreenShot` 在 `Tick` 期随机时机拷 `frame_.currentImageIndex`，与在途帧存在竞态（目前靠 B1 的硬等待掩盖） | `FrameSource.cpp:107` | 修掉 B1 后必须用正确的帧同步替代 |

可用的现成基建：

- `postRender` delegate 在**渲染帧命令缓冲内部**、`Render()` 之后、`vkQueueSubmit` 之前被调（`VulkanBaseRenderer.cpp:1691-1695`）——硬编/GPU 转换命令的天然录制点，零额外提交。
- swapchain 创建时已带 `VK_IMAGE_USAGE_STORAGE_BIT | TRANSFER_SRC`（`SwapChain.cpp:63`）——compute 可直接读 swapchain image。
- 帧提交结构清晰：单 `vkQueueSubmit`，wait `imageAvailable`、signal `renderFinished`、fence `inFlightFences[currentFrame]`（`VulkanBaseRenderer.cpp:1707-1730`）——便于追加 timeline semaphore 与编码队列对接。
- 引擎大量使用 slang compute（`PipelineCommon::ZeroBind*Pipeline` + `assets/shaders/*.comp.slang`），BGRA→NV12 转换 shader 有完整先例可抄。
- `Tasks::TaskCoordinator` 后台线程池已存在（`ScreenShot.cpp` 在用）。
- Device 已按 family 建多队列（graphics/present/transfer，`Device.cpp:115-136`），加视频编码队列是同构扩展。

注意：Instance 以 `VK_API_VERSION_1_2` 创建（`Engine.cpp:208`）。`VK_KHR_video_queue` 依赖 `VK_KHR_synchronization2`（1.2 中非 core），需作为设备扩展显式启用并开 feature；timeline semaphore 是 1.2 core 可直接用。

---

## 2. 目标架构

### 2.1 数据流（硬编路径）

```text
[渲染线程，每帧，全部录进既有帧命令缓冲，不新增提交]
 OnRendererPostRender(cmd, imageIndex)
   └─ VideoPipeline::RecordFrame(cmd)              // 按 --remote-fps 节流，不该出帧则直接返回
        ├─ barrier: swapchain image → GENERAL (storage read)
        ├─ compute: Remote.BgraToNv12.comp.slang   // 读 swapchain BGRA，缩放+BT.709 limited，
        │           写 slot[i].nv12 (Y plane / UV plane 两个 storage view)
        ├─ barrier: swapchain image → 还原；nv12 → release 给 video encode queue (QFOT)
        └─ (主提交追加 signal: timelineSem_gfx = frameId)

[主线程，postRender 返回后、Submit 之后，仅一次轻量 vkQueueSubmit]
 VideoPipeline::SubmitEncode(slot i)
   └─ video encode queue 提交：
        wait  timelineSem_gfx >= frameId
        cmd:  acquire nv12 (QFOT) → vkCmdBeginVideoCodingKHR
              → vkCmdEncodeVideoKHR(nv12 → slot[i].bitstreamBuf, query i)
              → vkCmdEndVideoCodingKHR
        signal timelineSem_enc = frameId

[编码输出线程（专用，常驻）]
 loop: vkWaitSemaphores(timelineSem_enc >= frameId)        // host 等待，不占主线程
       vkGetQueryPoolResults(query i) → {offset, size}     // VIDEO_ENCODE_FEEDBACK
       map slot[i].bitstreamBuf → 组装 Annex-B（IDR 前插 SPS/PPS）
       RemoteServer::BroadcastFrame(packet)                // fan-out 到所有 session
       slot i 归还 ring

[libdatachannel 线程] track->sendFrame(...) → H264RtpPacketizer → SRTP → 浏览器
```

### 2.2 所有权重构（修 B5）

```text
RemoteServer
 ├─ VideoPipeline（唯一，全局一条）          ← 新增，持有编码器/转换/slot ring
 │    ├─ IVideoEncoder*：VulkanVideoEncoder（主）/ OpenH264Encoder（回退）
 │    └─ FrameFanout：把 FEncodedPacket 广播给订阅的 session
 ├─ FSignalingServer（不变）
 └─ FRemoteSession × N：只剩 pc/track/datachannel/输入，订阅 VideoPipeline
      新 session 接入 / 收到 RequestKeyframe → VideoPipeline::RequestKeyframe()
```

`FEncodedPacket = { shared_ptr<const vector<byte>> annexb; bool keyframe; uint64 timestampUs; }`，各 session 共享同一份内存，`track->sendFrame` 可直接在输出线程调用（libdatachannel 线程安全）。

### 2.3 slot ring（避免任何停顿）

每个 slot：`nv12 image + bitstream VkBuffer(host-visible, ~512KB) + query index + frameId`。slot 数取 `max(swapchain image count, 3)`。渲染线程取不到空闲 slot（编码积压）就**跳过本帧**——丢帧优于反压渲染。

---

## 3. 关键设计决策与细节

### 3.1 设备与队列（`Device.{hpp,cpp}`、`VulkanBaseRenderer.cpp`）

- 设备扩展（仅在 `GOption->RemoteMode` 且探测可用时启用，沿用 `enableDeviceExtensionIfAvailable` 风格）：`VK_KHR_synchronization2`（+feature）、`VK_KHR_video_queue`、`VK_KHR_video_encode_queue`、`VK_KHR_video_encode_h264`、`VK_KHR_video_maintenance1`（可选）。
- 队列：用 `VkQueueFamilyProperties2` + `VkQueueFamilyVideoPropertiesKHR` 找 `VK_QUEUE_VIDEO_ENCODE_BIT_KHR` 且 `videoCodecOperations & VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR` 的 family（NVIDIA 上是独立 family），加进 `uniqueQueueFamilies`，暴露 `Device::VideoEncodeQueue()` / `VideoEncodeFamilyIndex()`（不存在则置 `~0u`）。
- `DeviceProcedures` 增补 video encode 系列函数指针（`vkCreateVideoSessionKHR`、`vkCmdEncodeVideoKHR`、`vkGetEncodedVideoSessionParametersKHR` 等，`vkGetDeviceProcAddr` 加载）。
- 能力探测封装成 `VulkanVideoCaps Probe(physicalDevice)`：`vkGetPhysicalDeviceVideoCapabilitiesKHR`（profile：H.264 encode、4:2:0、8-bit）+ `vkGetPhysicalDeviceVideoFormatPropertiesKHR`（ENCODE_SRC 是否支持 `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`、是否允许 STORAGE 用法）。结果决定 `--remote-encoder auto` 落到哪条路径，并打一行清晰日志。

### 3.2 H.264 profile 与 SDP 兼容

- profile 按能力探测从 `ConstrainedBaseline → Main → High` 择优（NVIDIA VK 驱动通常全有）。无 B 帧、IPPP、单 slice。
- 现在 `RemoteSession.cpp:126` 的 `video.addH264Codec(102)` 默认 fmtp 是 `42e01f`（CBP）。若实际编出 Main/High，**必须验证 Chrome/Firefox 在该 SDP 下仍正常解码**（通常宽容，但要实测）；不行就给 `addH264Codec` 传匹配的 profile-level-id。验收项里有这一条。

### 3.3 NV12 转换（`assets/shaders/remote/Remote.BgraToNv12.comp.slang`）

- 输入：swapchain image（storage read 或 sampled，注意非 sRGB view）；输出：NV12 image 的两个 plane storage view（plane0 = R8 Y，plane1 = R8G8 UV）。image 需 `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT`，usage `VIDEO_ENCODE_SRC_KHR | STORAGE`。
- 若驱动不允许 ENCODE_SRC + STORAGE 共存（探测 3.1 给出）：回退为 compute 写两张独立 R8/R8G8 image，再 `vkCmdCopyImage` 按 `VK_IMAGE_ASPECT_PLANE_{0,1}_BIT` 拷入 NV12。NVIDIA 实测大概率直写可行，回退路径留好即可。
- 缩放在同一 pass 内做（swapchain 分辨率 → `--remote-res`），BT.709 limited range，系数与 SPS 的 VUI 标注一致。
- 编码分辨率对齐 `VkVideoCapabilitiesKHR::pictureAccessGranularity`/16，非整除部分用 SPS frame cropping 表达。
- 一次 dispatch 写 2x2 像素块（一个 UV 对应 4 个 Y），避免 chroma 竞写。
- 同一 shader 加分支或双入口输出 I420 到 buffer（P1 软编用，回读 1.5B/px），两条路径共享色彩矩阵代码。

### 3.4 编码会话（`VulkanVideoEncoder`）

- `VkVideoSessionKHR` + `VkVideoSessionParametersKHR`；SPS/PPS 由 `vkGetEncodedVideoSessionParametersKHR` 取出缓存，每个 IDR 帧前拼进码流（浏览器中途加入依赖此）。
- DPB：2 slots（current + last reference），`VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR` image 数组；StdVideoEncodeH264 引用结构按 vk_video_samples 的最简 IPPP 写法。
- 码率控制：起步用 **CBR**（`VkVideoEncodeRateControlInfoKHR` + H264 layer，averageBitrate = `--remote-bitrate`）；`SetBitrate()` 通过 `vkCmdControlVideoCodingKHR` 在下一帧前更新。bring-up 阶段可先 constant-QP 出图再切 RC。
- 反馈：`VkQueryPool`（`VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR`，bitstream offset + size），输出线程 `vkGetQueryPoolResults` 带 WAIT 读取。
- 码流格式：确认驱动输出的 NALU 是否带 Annex-B start code（NVIDIA 实测带；以目标驱动实测为准），缺则自行补 `00 00 00 01`。打包器现用 `rtc::NalUnit::Separator::StartSequence`（`RemoteSession.cpp:134`），保持 Annex-B 即可。
- swapchain 重建（`rendererDelegates.createSwapChain`）只需重建转换输入 view/descriptor；编码分辨率固定为 remote-res，session 不重建。`--remote-res` 动态切换才重建 session（P5 范畴）。

### 3.5 帧同步（修 B6）

- 渲染主提交追加 signal 一个 **timeline semaphore**（值 = `frame_.frameCount`）。改动 `VulkanBaseRenderer.cpp:1707-1730`：给 renderer 加一个可选的 `extraSignalSemaphore`（timeline，由 VideoPipeline 注册），用 `VkTimelineSemaphoreSubmitInfo` 挂进既有 submit。**不要**动 `renderFinished`/present 链。
- 编码提交 wait 该值；输出线程 `vkWaitSemaphores` host 等编码 timeline。全链路无任何 `vkQueueWaitIdle`/`vkWaitForFences` 在主线程出现。
- NV12 image 用 EXCLUSIVE + 显式 QFOT（graphics release / encode acquire barrier 成对）；嫌繁可先 CONCURRENT 调通再收紧。

### 3.6 软编回退路径同样受益（P0/P1 后的形态）

- `CaptureScreenShot()` 阻塞调用从 remote 路径**删除**；改在 `postRender` 录制 swapchain→I420 compute（写 host-visible buffer，per-slot）。
- 主线程在下一帧（该 slot 的 frameId 已被 inFlightFence 保证完成）把 buffer 指针丢给编码 worker；openh264 + `sendFrame` 全在 worker 跑。
- 截图功能（`SaveSwapChainToFileFast` 等）不受影响，继续用原 `CaptureScreenShot`。

---

## 4. 分阶段执行计划（给接手 agent）

> 每 Phase 落一条 `.spec/TODO.md` 任务 + journal。构建：`./gnb build gkNextRenderer gkNextUnitTests`。验证可用 `--remote --agent-validation --agent-validation-frames N --hidden-window` 冒烟 + 浏览器实连。
> P0→P1 先做（立刻解决"跑起来很慢"，且是硬编的结构前提）；P2→P4 是硬编主线；P5 收尾。

### P0 — 管线所有权重构 + 去阻塞 + 编码下线程（软编，结构修复）
- 任务：
  1. 新建 `Remote/VideoPipeline.{hpp,cpp}`：RemoteServer 唯一持有；session 只订阅（2.2 的结构），删除 `FRemoteSession` 内的 `frameSource_`/`encoder_`，`RequestKeyframe` 改走 pipeline 聚合。
  2. `Engine::OnRendererPostRender` 增加 remote tap：`VideoPipeline::RecordFrame(cmd, imageIndex)`（节流逻辑从 `SendVideoFrame` 挪入）。本阶段 RecordFrame 先录 swapchain→线性 image 拷贝（复用截图的拷贝姿势，但**录进帧命令缓冲**，删除 `SingleTimeCommands` 路径），per-slot 多缓冲（slot 数 = swapchain image count）。
  3. 读取时机：slot 的 frameId 对应帧的 `inFlightFence` 已在下一轮 `DrawFrame` 等过 → 主线程零等待取走，交给专用编码线程（`std::jthread`，内部 SPSC 队列）做 BGRA→I420（暂留标量）+ openh264 + 广播 `sendFrame`。
  4. `FrameFanout`：多 session 共享码流；新 session open → 强制 IDR。
- 改动：`Remote/VideoPipeline.*`（新）、`Remote/RemoteServer.*`、`Remote/RemoteSession.*`、`Remote/FrameSource.*`（瘦身为 CPU 转换工具）、`Engine.{hpp,cpp}`。
- 验收：浏览器画面与之前一致；`SCOPED_CPU_TIMER("remote")` 主线程开销 < 0.5ms（原先含 capture+convert+encode 的全部时间消失）；两个浏览器同时观看 CPU 不翻倍；单元测试绿。

### P1 — GPU compute 颜色转换（消灭标量循环）
- 任务：`assets/shaders/remote/Remote.BgraToYuv.comp.slang`（I420→buffer 与 NV12→image 双输出，3.3）；P0 的 RecordFrame 改为录制 compute（swapchain storage read → host-visible I420 buffer），编码线程直接拿 planar 指针喂 openh264，删除 `BgraToYuv` 标量路径（测试图样保留）。
- 改动：shader（新）、`Remote/VideoPipeline.*`、`assets/CMakeLists.txt`（shader 编译清单）。
- 验收：色彩正确（BT.709 limited，与此前画面肉眼一致，无偏绿/偏紫）；编码线程单帧转换耗时降为 0（GPU 完成）；720p@60 软编可跑满 `--remote-fps 60`；渲染线程帧时间无回归。

### P2 — Vulkan Video 基建（扩展/队列/探测，不出码流）
- 任务：3.1 全部内容——设备扩展与 synchronization2 feature、视频编码队列 family 探测与创建、`DeviceProcedures` 函数指针、`VulkanVideoCaps Probe()`、`--remote-encoder {auto,vulkan,openh264}` flag（`Options.{hpp,cpp}`）。启动日志打印探测结果（profile/格式/STORAGE 支持/队列 family）。
- 改动：`Vulkan/Device.{hpp,cpp}`、`Vulkan/DeviceProcedures.*`、`Rendering/VulkanBaseRenderer.cpp`（扩展启用）、`Engine/Options.*`、`Remote/VulkanVideoCaps.{hpp,cpp}`（新）。
- 验收：目标 N 卡上日志列出 encode 队列与 H.264 能力；无 video 支持的设备（或 `--remote-encoder openh264`）行为与 P1 完全一致；validation layer 干净。

### P3 — VulkanVideoEncoder 核心（离线出码流，先不接 WebRTC）
- 任务：`Remote/VulkanVideoEncoder.{hpp,cpp}` 实现 3.4（session/SPS-PPS/DPB/RC/feedback query/Annex-B 组装）；输入侧先用单元测试合成 NV12（填充图样），独立 encode 队列提交 + 输出线程回读；加 `gkNextUnitTests` 用例：编码 60 帧 → 落盘 `.h264` → 断言 NAL 类型序列（SPS/PPS/IDR/P）与尺寸合理。
- 改动：`Remote/VulkanVideoEncoder.*`（新）、`test/`（新用例）、`Remote/VideoEncoder.hpp`（统一 `IVideoEncoder` 接口：openh264/vulkan 双实现，输入抽象为 "CPU planar 或 GPU NV12 slot"）。
- 验收：落盘码流 `ffprobe` 可识别、`ffplay`/浏览器 MSE 可播；constant-QP 与 CBR 两模式均出流；`RequestKeyframe` 生效（下一帧是带 SPS/PPS 的 IDR）。

### P4 — 整合：零拷贝直通 + 回退链（硬编上线）
- 任务：
  1. P1 的 compute 增加 NV12 直写路径；`VideoPipeline::RecordFrame` 按探测结果选择 NV12(硬编)/I420 buffer(软编)。
  2. 3.5 同步链：渲染 submit 追加 timeline signal；encode 队列提交（主线程，轻量）；QFOT barriers；输出线程 host 等待 + query 回读 + fan-out。
  3. 回退链生效：`auto` → Vulkan Video 不可用/初始化失败/连续 N 帧编码错误 → 自动降级 openh264 并打日志。
  4. SDP/profile 兼容验证（3.2），必要时调整 `addH264Codec`。
- 改动：`Remote/VideoPipeline.*`、`Remote/VulkanVideoEncoder.*`、`Rendering/VulkanBaseRenderer.{hpp,cpp}`（extraSignalSemaphore，≤30 行）、shader。
- 验收（核心性能门槛，N 卡实测）：
  - `--remote-res 1920x1080 --remote-fps 60` 浏览器流畅观看，丢帧 < 5%；
  - 渲染线程 `remote` CPU timer < 0.5ms/帧；进程整体 CPU 相比 P1 软编下降 ≥ 70%；
  - postRender 录制 → 码流就绪（输出线程时间戳差）p50 < 8ms；
  - LAN 端到端（输入→画面响应）肉眼 < 100ms，`pc.getStats()` 无持续 PLI/NACK 风暴；
  - 杀浏览器重连、二客户端并发、swapchain resize（拖窗口）均不崩不花屏；
  - `--remote-encoder openh264` 强制回退仍正常。

### P5 — 打磨（沿用原设计 Phase 5）
- 基于 `getStats`/RTCP 的简易码率自适应（接到 `SetBitrate`，硬编 RC 更新即时生效）；动态 `--remote-res` 切换（重建 session）；统计 overlay；NVENC 后备仅在 Vulkan Video 实测受阻时立项（决策记录 #3）。

---

## 5. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 驱动不允许 NV12 ENCODE_SRC + STORAGE 共存 | 3.3 的 copy 回退路径（compute→独立 plane image→vkCmdCopyImage），P2 探测时即确定走哪条 |
| Vulkan Video 驱动 bug / 初始化失败 | P4 回退链自动降级 openh264（P0/P1 已保证软编路径可用且不卡主线程）；NVENC 作最后后备 |
| Main/High profile 与 SDP `42e01f` 不符导致浏览器拒解 | P4 验收项强制实测；必要时改 `addH264Codec` fmtp 或探测时优选 ConstrainedBaseline |
| timeline semaphore 接入主 submit 引入回归 | 改动收敛为可选 extraSignalSemaphore，非 remote 模式完全不挂；unit tests + agent-validation 回归 |
| 编码积压反压渲染 | slot ring 取不到即丢帧；输出线程统计丢帧率暴露到日志/overlay |
| QFOT/layout 错误花屏（验证层难全覆盖） | bring-up 先 CONCURRENT sharing + GENERAL layout 调通，再逐步收紧为 EXCLUSIVE+QFOT；开 validation layer 的 video 检查 |
| `--agent-validation` 无头 CI 机器无 N 卡 | 所有 Vulkan Video 代码路径运行期探测守卫；CI 只验软编回退 + 编译 |

## 6. 参考资料

- Vulkan Video encode 规范：`VK_KHR_video_encode_h264` <https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_video_encode_h264.html>
- Khronos 官方示例（编码会话/DPB/RC 的权威参考实现）：vk_video_samples <https://github.com/KhronosGroup/Vulkan-Video-Samples>（`vk_video_encoder` 目录）
- NVIDIA Vulkan Video encode 博文（队列/NV12/反馈 query 实务）：<https://developer.nvidia.com/blog/encoding-video-with-vulkan-video/>
- vulkan-headers 自带 `vk_video/vulkan_video_codec_h264std_encode.h`（StdVideoEncodeH264* 结构，无需新依赖）
- 既有总体设计：`docs/designs/webrtc-remoteplay-design.md` §4/§5/§11 Phase 4
- 开工前在目标机跑：`vulkaninfo | grep -i -E "video_(queue|encode)"` 确认扩展三件套在列
