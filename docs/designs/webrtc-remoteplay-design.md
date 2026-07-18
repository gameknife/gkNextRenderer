---
title: "WebRTC Remote Play 当前架构"
category: design
status: 现行
owner: NextRemote
created: 2026-05-27
last_updated: 2026-07-17
---

# WebRTC Remote Play 当前架构

Remote Play 由 `src/Modules/NextRemote/` 实现，通过 HTTP 提供内嵌浏览器客户端、WebSocket 交换信令、WebRTC 传输 H.264 视频与输入数据。当前唯一编码后端是 Vulkan Video H.264；`auto` 只是自动选择该后端，不代表存在软件编码 fallback。

## 启动

```bash
./gnb.sh build gkNextRenderer
./gnb.sh remote --scene assets/models/playground.glb --res 1280x720
./gnb.sh remote --target gkNextEditor --show-window

# 每个客户端独立 RenderView；gnb remote 的尾随参数原样传给应用
./gnb.sh remote -- --remote-multiview --remote-max-clients=2
```

默认 HTTP 端口为 `8088`，信令端口为 `8089`，流目标为 30 fps；启动时会打印 loopback 和可用 LAN URL。默认绑定 `0.0.0.0`，且当前没有认证、TLS 或公网防护，**只允许在可信局域网使用，不得直接暴露到互联网**。

设备必须同时支持 Vulkan Video encode queue、H.264 encode 扩展和可用 profile。缺失时 Remote 模式会明确报错并停用，不应在文档或 UI 中声称会回退 CPU 编码。

## 两种视图模式

### Legacy（默认）

- 视频源是主 swapchain。
- 输入经 `InputRouter` 注入 SDL，影响本地全局状态。
- 多个观看者共享同一画面和控制状态。

### MultiView（`--remote-multiview`）

- 每个 session 获得独立 `RenderView` slot、`ModelViewController`、ImGui context、合成目标和 `VideoPipeline`。
- `CloudInputRouter` 按 session 路由输入；应用可通过 `OnRemoteViewAction` 与 `FGameUiFrameContext::RemoteView` 提供每客户端逻辑/UI 状态。
- 引擎当前保留 3 个 secondary-camera view slot，因此 `--remote-max-clients` 的有效上限是 3；初始化失败还会进一步收紧容量。

RenderView 的资源所有权和调度见 [多视图架构](multi-viewport-renderview-design.md)。不要恢复已删除的“共享 renderer 全局参数快照/回滚”方案。

### Session 生命周期与线程边界

信令、WebRTC 和 data channel 回调不直接创建或销毁 Scene、RenderView、ImGui、Vulkan 资源。它们只注册 session、写入输入队列或登记待移除项；`RemoteServer::TickCloudViews()` 在引擎主线程消费这些状态，完成 view 初始化、相机更新、资源释放和 `OnRemoteUiSessionClosed()` 通知。

每个 session 的输入先送入自己的 `RemoteImGuiSession`。ImGui 的 `WantCaptureKeyboard` / `WantCaptureMouse` 会阻断相应的游戏相机与 action 路径；相对鼠标移动和 gamepad 按当前代码的独立规则处理。新增输入类型时必须同时明确 UI capture 语义，不能直接旁路到全局 SDL 状态。

每个 session 具有独立 ImGui context、IO、渲染缓存、窗口状态和 `VideoPipeline`。产品 UI 若持有可变状态，必须以 `FGameUiFrameContext::sessionId` 分区；实际 renderer 选择和 `UserSettings` 仍是进程级全局状态，当前没有为远端 view 做 snapshot/rollback。不得把“独立 UI”误写成“每客户端拥有完整独立引擎配置”。

session 建立、场景变更或 swapchain 失效后，相应 pipeline 会重新请求关键帧。不要把多个 session 合并到一个带共享编码状态的 pipeline。

### MultiView 帧合成顺序

Remote MultiView 的 post-render callback 在主渲染线程录制 command buffer 时执行，顺序固定为：

1. 从该 RenderView 的 `RT_DENOISED` 拷贝到 session composite image（`CopyViewToComposite`）。
2. 在 composite image 上绘制该 session 的 ImGui draw data。
3. 通过 `RecordFrameFromStorageImage` 交给该 session 的 `VideoPipeline` 做颜色转换和编码。

回调不是异步视频消费线程；编码线程只处理已经录制并完成同步的帧资源。改变合成顺序会直接影响 UI 是否进入远端视频以及 image layout/barrier 契约。

## 关键组件

- `NextRemoteModule`：把 Options 转换为 server 配置，并在 Vulkan device 创建前注册视频能力增强器。
- `RemoteServer`：生命周期、legacy/multiview 分流、客户端容量和每帧采集。
- `SignalingServer` / `RemoteSession`：HTTP/WS、PeerConnection、H.264 track、data channel。
- `VideoPipeline` / `VulkanVideoEncoder`：GPU 色彩转换、NV12 staging、编码、码率更新与关键帧。
- `InputRouter` / `CloudInputRouter` / `VirtualGamepad`：legacy 与 session 输入路径。
- `RemoteImGuiSession`：每客户端 ImGui context，不复用主窗口的可变 UI state。

## 当前边界

- 无音频传输、认证、TLS、TURN 管理或公网部署方案。
- 分辨率在启动时确定；没有完整的浏览器 resize/动态重建产品流程。
- 编码能力和并发 session 数受 GPU/driver 限制；日志中的实际 profile、extent 和 capacity 才是运行时事实。
- Remote Play 仍是实验性 LAN 能力。后续工作应先写清本轮验收目标（互操作、性能、动态分辨率、安全或音频），不要把旧计划中的未完成勾选框直接视为当前需求。
- Legacy 与 MultiView 是两条有意分离的路径：前者采集主 swapchain、注入全局输入，后者拥有 session 级 view/UI/encoder。修改一条路径时不要假定另一条自动继承相同行为。
