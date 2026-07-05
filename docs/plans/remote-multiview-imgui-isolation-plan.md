---
title: "Remote MultiView 独立 ImGui 界面开发计划"
category: plan
status: 草案
owner: engine
created: 2026-07-03
last_updated: 2026-07-03
---

# Remote MultiView 独立 ImGui 界面开发计划

## 1. 背景与目标

最近一次提交已经把 `--remote-multiview` 做到“多个浏览器连接同一个 `gkNextRenderer`，每个浏览器拥有独立 RenderView、相机和操作”。当前缺口是 UI：

- `--remote` 走主 swapchain，浏览器看到的是主窗口最终画面，所以天然包含 `gkNextRenderer` 的 ImGui。
- `--remote-multiview` 走每 session 的 offscreen `RenderView`，现在编码源是 `view.RtBankBase() + RT_DENOISED`，只包含场景，不包含 ImGui。
- ImGui 当前只有一个全局 active context 和一套 `NextUI::UserInterface` 状态。即使把同一份 draw data 贴到所有 remote view，也会导致窗口开关、hover/focus、输入文本、滚动位置互相抢。

本计划目标：

1. `gnb run gkNextRenderer -- --remote --remote-multiview` 时，每个浏览器画面包含和 `--remote` 基本一致的 `gkNextRenderer` ImGui 界面。
2. 每个浏览器拥有独立 `ImGuiContext`，窗口位置、展开状态、输入焦点、hover/capture、滚动等 ImGui 内部状态互不影响。
3. `gkNextRenderer` 的现有 ImGui 绘制逻辑尽量共用，不复制一套 remote-only UI。
4. 每个浏览器的 UI 输入先进入自己的 ImGui context；只有该 context 不捕获输入时，才继续驱动该 session 的相机和 gameplay action。
5. 旧 `--remote`、本地窗口、`gnb shot` 行为不变。

## 2. 现状分析

### 2.1 Remote MultiView 视频路径

- `RemoteServer::Start()` 在 `config_.multiView` 下为每个 view slot 创建独立 `FVideoPipeline`，并注册 `OffscreenRenderViewController::SetViewRenderedCallback()`，见 `src/Modules/NextRemote/RemoteServer.cpp:37`、`src/Modules/NextRemote/RemoteServer.cpp:69`。
- `RemoteServer::TickCloudViews()` 为每个 session 更新独立 `ModelViewController`，再调用 `OffscreenViews().SetEnabled()`、`SetRenderExtent()`、`SetCameraOverride()`，见 `src/Modules/NextRemote/RemoteServer.cpp:339`。
- `OffscreenRenderViewController::ScheduleViews()` 在 renderer frame 内调度每个 offscreen `RenderView`，post callback 里先 `CopyViewOutput()`，再触发 remote callback，见 `src/Engine/Rendering/Preview/OffscreenRenderViewController.cpp:264`。
- `RemoteServer::RecordCloudViewFrame()` 当前直接编码 `view.RtBankBase() + RT_DENOISED`，见 `src/Modules/NextRemote/RemoteServer.cpp:484`。
- `FVideoPipeline::RecordFrameFromStorage()` 要求 source bindless slot 是 storage texture，见 `src/Modules/NextRemote/VideoPipeline.cpp:378`。因此“把 UI 画到 sampled offscreenImage 上”不会被当前编码管线看到。

### 2.2 主 ImGui 路径

- `NextEngine` 只持有一个 `NextUI::UserInterface`，见 `src/Engine/Runtime/Engine.hpp:117`、`src/Engine/Runtime/Engine.hpp:382`。
- `UserInterface` 构造时调用 `ImGui::CreateContext()`，初始化 SDL platform backend、renderer backend、字体 atlas 和 swapchain UI render pass，见 `src/Engine/Runtime/Editor/UserInterface.cpp:228`。
- UI frame 生命周期是：
  - `NextEngine::OnRendererAfterSubmit()` 中 `userInterface_->PreRender()`、`gameInstance_->OnRenderUI()`、debug panels、`PrepareDrawData()`，见 `src/Engine/Runtime/Engine.cpp:1386`。
  - 下一帧 command buffer 的 `NextEngine::OnRendererPostRender()` 中 `RenderPreparedDrawData()` 画到主 swapchain，见 `src/Engine/Runtime/Engine.cpp:1322`。
- `UserInterface::RenderDrawData()` 已经是可复用的 Vulkan draw-data 提交函数，支持传入任意 framebuffer extent、render buffer 和 pipeline，见 `src/Engine/Runtime/Editor/UserInterface.cpp:633`。

### 2.3 输入路径

- Legacy `--remote` 的 `FInputRouter` 把 DataChannel 输入转成 SDL event 并 `SDL_PushEvent()` 到主窗口，见 `src/Modules/NextRemote/InputRouter.cpp:92`。
- MultiView 的 `FCloudInputRouter` 不进 SDL 全局队列，而是按 session 缓存 `FCloudInputEvent`，由 `RemoteServer::TickCloudViews()` 消费，见 `src/Modules/NextRemote/CloudInputRouter.cpp:28`、`src/Modules/NextRemote/RemoteServer.cpp:372`。
- 浏览器端已经发送 key、mouse move/button、wheel、gamepad、bitrate/keyframe，但没有专门的 UTF-8 text input 消息，见 `assets/remote/index.html:480`、`assets/remote/index.html:700`。

### 2.4 gkNextRenderer UI 状态耦合

`NextRendererGameInstance::OnRenderUI()` 现在直接使用：

- `workMode_`、`lastWorkMode_`、`memoryStatisticsPanelOpen_` 等 game instance 成员，见 `src/Application/Render/gkNextRenderer/gkNextRenderer.hpp:75`。
- `GetEngine().GetUserSettings().ShowSettings / ShowOverlay`，见 `src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:520`。
- `gizmoController_`、`titleBarFont_` 等 context 相关对象，见 `src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:571`、`src/Application/Render/gkNextRenderer/gkNextRenderer.cpp:1470`。

所以独立 ImGuiContext 只是必要条件。要真正做到“界面状态独立”，还要把 `gkNextRenderer` 的 UI 外部状态拆成 per-view state。

## 3. 目标架构

### 3.1 总体数据流

```text
Browser input
  -> FRemoteSession DataChannel
  -> FCloudInputRouter(sessionId)
  -> RemoteServer::TickCloudViews()
       -> RemoteImGuiSession(sessionId).ApplyInput()
       -> if !io.WantCaptureMouse/Keyboard:
             update per-session ModelViewController / gameplay action

Renderer frame
  -> primary view as before
  -> OffscreenRenderViewController schedules active session RenderViews
  -> per session post callback:
       copy scene RT_DENOISED -> remote final video image
       set current ImGuiContext(sessionId)
       build gkNextRenderer UI draw data using per-session UI state
       render draw data into remote final video image
       FVideoPipeline::RecordFrameFromStorage(finalVideoBindlessSlot)
```

### 3.2 新增核心概念

#### `NextUI::FImGuiContextScope`

一个小 RAII 工具，进入时 `ImGui::SetCurrentContext(context)`，退出时恢复旧 context。所有多 context 代码必须通过它切换，避免 remote UI 渲染污染主窗口 context。

#### `NextUI::FRemoteImGuiSession`

每个 remote browser session 一个实例，建议放在 `src/Engine/Runtime/Editor/`，由 `RemoteServer` 持有或通过 engine service 管理。

职责：

- 拥有一个独立 `ImGuiContext*`。
- 使用 headless platform adapter，不复用 `ImGui_ImplSDL3_NewFrame()` 的 OS 鼠标查询。
- 维护 `ImGuiIO.DisplaySize`、`DisplayFramebufferScale`、`DeltaTime`。
- 把 `FCloudInputEvent` 转成 `io.AddMousePosEvent()`、`io.AddMouseButtonEvent()`、`io.AddMouseWheelEvent()`、`io.AddKeyEvent()`、`io.AddInputCharactersUTF8()`。
- 执行 `ImGui::NewFrame()`、调用 game UI 绘制、`ImGui::Render()`。
- 暴露该 session 的 `ImDrawData*`、`WantCaptureMouse`、`WantCaptureKeyboard`。
- 持有 per-swapchain-image 的 `NextUI::UiRenderBuffer`。
- 持有 per-context font 指针或 font handle cache。首期允许每个 context 各自构建 font atlas，后续再考虑共享 `ImFontAtlas`。

不要直接用现有 SDL backend 处理 remote context。`imgui_impl_sdl3_custom.cpp` 虽然把 backend data 放在 `io.BackendPlatformUserData`，但文件自身注释说明多 context 支持未充分验证，且 mouse cursor / gamepad 等共享资源处理不可靠，见 `src/ThirdParty/imgui-custom/imgui_impl_sdl3_custom.cpp:131`。

#### `NextUI::FUiRenderBackend`

从 `UserInterface` 中抽出的 Vulkan ImGui renderer backend，主窗口和 remote session 共用：

- pipeline layout / graphics pipeline 创建。
- font texture 上传和 bindless `TexID` 编码。
- `RenderDrawData()`。
- `RequestImTextureId()` / `RequestImTextureIdRaw()`。

现有 `UserInterface` 可以保留为“主窗口 UI session + SDL platform adapter + swapchain render pass”的封装；remote 只复用 renderer backend，不复用 SDL platform adapter。

#### `FRemoteViewCompositeTarget`

每个 active remote view 需要一个“最终视频源”image，而不是直接编码 `RT_DENOISED`：

- format: `VK_FORMAT_R16G16B16A16_SFLOAT`，和现有 render view 输出一致。
- usage: `VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`。
- bindless: 绑定到 storage texture slot，供 `Remote.BgraToNv12.comp.slang` 读取；必要时也绑定 sample slot，方便 debug ImGui 预览。
- frame flow:
  1. copy `view.RtBankBase() + RT_DENOISED` 到 composite image。
  2. transition composite image 到 color attachment，load op = load。
  3. 用 ImGui pipeline 把 remote session draw data 叠加进去。
  4. transition 回 `GENERAL` 或 compute shader 可读 layout。
  5. `FVideoPipeline::RecordFrameFromStorage(compositeBindlessSlot, extent)`。

这样不需要给所有 render target bank 的 `RT_DENOISED` 增加 color attachment usage，也不会影响非 remote render path。

## 4. Game UI 共用方案

### 4.1 扩展 `NextGameInstanceBase`

新增带上下文的 UI hook，保留旧 hook 兼容现有 app：

```cpp
struct FGameUiFrameContext
{
    enum class ESurfaceKind { MainWindow, RemoteView };

    ESurfaceKind surfaceKind = ESurfaceKind::MainWindow;
    std::string_view sessionId;
    VkExtent2D framebufferExtent{};
    Assets::Camera viewCamera{};
    Runtime::Config::UserSettings* userSettings = nullptr;
    Runtime::Config::ShowFlags* showFlags = nullptr;
    bool allowWindowCommands = true;
};

virtual bool OnRenderUI(const FGameUiFrameContext& context)
{
    (void)context;
    return OnRenderUI();
}

virtual void OnRemoteUiSessionClosed(std::string_view sessionId) {}
```

主窗口路径用 `surfaceKind = MainWindow` 调用；remote path 用 `surfaceKind = RemoteView` 调用。

### 4.2 gkNextRenderer 的 UI state 参数化

把 `NextRendererGameInstance` 的 UI 状态拆成一个结构：

```cpp
struct FRendererUiState
{
    EWorkMode workMode = EWorkMode::Renderer;
    EWorkMode lastWorkMode = EWorkMode::Count;
    bool memoryStatisticsPanelOpen = false;
    bool showSettings = true;
    bool showOverlay = false;
    NextUI::GizmoController gizmoController;
    ImFont* bigFont = nullptr;
    ImFont* titleBarFont = nullptr;
};
```

主窗口持有 `mainUiState_`；remote 按 `sessionId` 持有 `remoteUiStates_`。绘制函数改成：

```cpp
bool DrawRendererUi(const FGameUiFrameContext& context, FRendererUiState& state);
```

`OnRenderUI()` 变成主窗口 wrapper；新的 `OnRenderUI(context)` 在 remote view 下查找对应 `FRendererUiState` 并调用同一套 `DrawTitleBar()`、`DrawModeRail()`、`DrawSettings()`、`DrawViewportTopBar()` 等函数。

需要同步改造这些函数的参数：

- `DrawSettings()`、`DrawModeRail()`、`DrawMemoryStatisticsPanel()` 不再直接读写成员 `workMode_` / `memoryStatisticsPanelOpen_`，改读写 `FRendererUiState&`。
- `DrawTitleBar()` 接收 `FGameUiFrameContext` 和 `FRendererUiState&`。remote view 下保留视觉 chrome，但窗口 minimize/maximize/close 这类 OS 命令设为 no-op 或隐藏 disabled 状态。
- `DrawViewportTopBar()`、`DrawViewportBottomBar()` 使用 `context.framebufferExtent` 作为 remote 分辨率来源；主窗口仍可读 swapchain output extent。
- `GizmoController` 首期只在主窗口启用。remote view 若要完整编辑交互，单独做 object picking 和 gizmo 操作；本阶段先保证 UI 独立显示和面板交互。

### 4.3 UserSettings / ShowFlags 的隔离原则

不是所有设置都应该 per-session：

- per-session UI 状态：窗口位置、tab、mode rail 当前页、settings panel 是否显示、profiler panel 是否展开。
- engine-global 状态：renderer 类型、sample count、denoiser、show flags、scene env settings、CVar。这些会影响共享 renderer / shared scene，任一 session 修改后所有视图都会看到效果。
- remote-local 镜像：`ShowSettings`、`ShowOverlay` 这类只是 UI chrome 开关，应从 `UserSettings` 中拆出到 `FRendererUiState`，避免 A 浏览器切到 Profiler 时 B 浏览器的 settings 面板被关掉。

首期可以让面板里的真实渲染参数仍写 `Engine::UserSettings`，但控制面板显示/折叠状态必须 per-session。

## 5. 输入设计

### 5.1 扩展协议

新增 input message：

```cpp
enum class ERemoteInputMessage : uint8_t
{
    ...
    TextUtf8 = 8,
};
```

payload: `uint16_t byteLength + UTF-8 bytes`，或沿用 JSON `{ "type": "text", "text": "..." }`。建议二进制，和现有 key/mouse 路径一致。

浏览器端：

- `keydown` 继续发 key down/up。
- 对可打印字符，且没有 Ctrl/Meta shortcut 时，额外发 `TextUtf8`。
- 远期支持 IME 时，引入隐藏 textarea / `beforeinput`，首期先覆盖英文、数字、常用符号。

### 5.2 remote session 输入分发

`RemoteServer::TickCloudViews()` 当前直接把所有事件喂给 `ModelViewController`。改成：

1. Drain session events。
2. `RemoteImGuiSession::ApplyInput(events)`。
3. 如果是 absolute mouse move/button/wheel/key/text，先更新 ImGui。
4. 查询该 session 的 `io.WantCaptureMouse` / `WantCaptureKeyboard`。
5. 未被捕获的事件再驱动 `clientView.controller` 和 `OnRemoteViewAction()`。
6. relative pointer-lock mouse move 默认继续给相机；右键状态仍可同时送 ImGui，便于 UI 感知按钮释放。

这会让每个浏览器里的 ImGui hover、active item、keyboard focus 都只影响自己的 session。

## 6. 实施阶段

### P0 - 验证与拆分边界

- 新增 `FImGuiContextScope`。
- 从 `UserInterface` 中抽 `FUiRenderBackend`，让 `RenderDrawData()`、pipeline、font texture 逻辑脱离“主 swapchain context”。
- `UserInterface` 主路径行为保持不变。
- 构建：`./gnb.bat build gkNextRenderer gkNextUnitTests`。
- 验证：`gnb shot --target gkNextRenderer --ui`，确认本地 UI 无回归。

### P1 - Headless Remote ImGui Session

- 新增 `FRemoteImGuiSession`，创建独立 `ImGuiContext`。
- 实现 headless input adapter 和 `BeginFrame/EndFrame`。
- 给每个 session 独立 ini 文件名，例如 `imgui_remote_<sessionShortId>.ini`，或首期 `IniFilename = nullptr` 避免 remote session 持久化污染。
- 支持 TextUtf8 输入。
- 不接视频，只做单元级 smoke：构造 session、喂 mouse/key/text、确认 `WantCapture*` 和 draw data 能生成。

### P2 - Remote Composite Target + UI 叠加

- 为 `OffscreenRenderViewController` 或 remote 专用资源增加 `FRemoteViewCompositeTarget`。
- 在 per-view post callback 中：
  - copy scene output 到 composite target。
  - 调用 `FRemoteImGuiSession` 生成 draw data。
  - 用共用 ImGui renderer backend render 到 composite target。
  - 编码 composite storage slot。
- 旧 `--remote` 仍编码主 swapchain；`--remote-multiview` 改编码 composite target。
- 验证：两个浏览器都能看到 titlebar、mode rail、settings panel、bottom bar。

### P3 - gkNextRenderer UI State 参数化

- 引入 `FRendererUiState`。
- 把 `workMode_`、`lastWorkMode_`、`memoryStatisticsPanelOpen_`、`gizmoController_`、UI font pointer 移入 state。
- 将 `DrawTitleBar/DrawModeRail/DrawSettings/DrawViewport*Bar/DrawMemoryStatisticsPanel` 改为接收 `FGameUiFrameContext` + `FRendererUiState&`。
- 主窗口继续使用 `mainUiState_`。
- remote session 使用 `remoteUiStates_[sessionId]`，session 关闭时清理。
- 验证：两个浏览器分别切换 Renderer / Profiler / Settings，不互相改变面板显示状态。

### P4 - 输入捕获与 UI 操作闭环

- `FCloudInputRouter` 支持 `TextUtf8`。
- `RemoteServer::TickCloudViews()` 按 ImGui capture 结果分流输入。
- 浏览器端发 text input。
- 验证：
  - 鼠标点击 settings panel 控件时，相机不被拖动。
  - 鼠标离开 UI 或右键 pointer-lock 时，相机仍可独立控制。
  - 两个浏览器分别滚动 / 展开同名 ImGui tree，不互相影响。

### P5 - 稳定性与性能收尾

- session 断开时释放 remote ImGui context、composite target、UI render buffers、font atlas texture。
- swapchain recreate / scene reload 时 remote UI session 保留，composite target 重建。
- encoder backlog 时 UI 不阻塞主 renderer。
- stats log 增加 remote UI draw data vertex/index 数、composite target resize 次数。
- 可选：remote font atlas 共享，降低多 session 显存。

## 7. 验证计划

### 构建

Engine 层和 gkNextRenderer 都会改，默认验证：

```bash
./gnb.bat build gkNextRenderer gkNextUnitTests
```

若改到 CMake 或新增文件未被 glob 收录，加 `--reconfigure`。

### 本地 UI 回归

```bash
gnb shot --target gkNextRenderer --ui --frames 90
```

检查主窗口 UI、settings panel、titlebar、mode rail 仍正常。

### Remote legacy 回归

```bash
gnb run gkNextRenderer -- --remote --remote-res 1280x720 --remote-fps 30
```

浏览器应继续看到主 swapchain UI，输入走 legacy SDL path。

### Remote multiview 验证

```bash
gnb run gkNextRenderer -- --remote --remote-multiview --remote-max-clients 2 --remote-res 960x540 --remote-fps 30
```

打开两个浏览器页面：

- 两边都显示 gkNextRenderer UI。
- A 切到 Profiler，不影响 B 的 Settings / Renderer mode。
- A 拖 settings 面板滑块时，B 画面不中断。
- A/B 分别右键 pointer-lock 操作相机，视口独立。
- A/B 分别按 Space，box 从各自视口方向发射。

### 视觉截图

首期可以增加一个 agent script 或 debug flag，让 remote composite target 同步绑定 sample slot，并在主窗口 ImGui 里显示两个 remote view thumbnail，方便 `gnb shot --ui` 截图验证两个 remote UI 状态不同。

## 8. 风险与处理

| 风险 | 处理 |
| --- | --- |
| SDL ImGui backend 多 context 不稳定 | remote 使用 headless ImGui input adapter，不调用 `ImGui_ImplSDL3_NewFrame()` / `ProcessEvent()` |
| ImGui font pointer 属于 context，复用 `titleBarFont_` 会失效 | font pointer 移入 `FRendererUiState`，每 context 初始化一次 |
| UI 画在 sampled image 上但编码源仍是 storage image | 引入 `FRemoteViewCompositeTarget`，最终视频源同时支持 color attachment 和 storage bindless |
| remote UI 修改全局 `UserSettings` 导致所有 session 互相影响 | UI chrome 状态 per-session；真实 renderer 参数仍明确作为 shared global |
| InputText 无法输入字符 | 协议新增 `TextUtf8`，浏览器发送 printable / beforeinput 文本 |
| remote UI 绘制太晚导致本帧编码拿不到 UI | UI draw 和 `RecordFrameFromStorage()` 放在同一个 RenderView post callback 内，先 draw 后 record |
| session 断开后 context/font/descriptor 泄漏 | `OnRemoteUiSessionClosed()` + `RemoteServer::UnregisterCloudSession()` 清理 state 和 GPU resource |

## 9. 建议落地顺序

先做 P0/P1/P2，把“独立 ImGuiContext 能画进 remote 视频源”打通；再做 P3/P4，把 gkNextRenderer UI 状态和输入捕获做完整。这样每个阶段都有可见结果，且不会一次性重构所有 app 的 UI hook。

首个 PR 的最小验收可以定为：

- `--remote` 无回归。
- `--remote-multiview` 两个浏览器都能看到 gkNextRenderer titlebar / mode rail / settings。
- 两个浏览器切换 mode rail 后视觉状态独立。
- 相机和 Space 发射仍按 session 独立。
