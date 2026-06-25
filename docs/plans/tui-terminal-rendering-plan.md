---
title: "TUI 终端渲染运行模式（隐藏窗口 + 终端逐帧刷新）设计与开发计划"
category: plan
status: 规划已定稿（待实现）
owner: engine
created: 2026-06-25
last_updated: 2026-06-25
---

# TUI 终端渲染运行模式（隐藏窗口 + 终端逐帧刷新）设计与开发计划

> **状态**：规划已定稿，待实现（原 §10 开放问题已于 2026-06-25 拍板，见 §10 决策记录）
> **目标读者**：负责实现本特性的后续 AI agent / 开发者
> **本文写作前已核实的真实代码**：`src/DesktopMain.cpp`、`src/Engine/Options.{hpp,cpp}`、`src/Engine/Runtime/Engine.{hpp,cpp}`、`src/Engine/Runtime/ScreenShot.cpp`、`src/Engine/Runtime/FrameStreamer.hpp`、`src/Engine/Rendering/VulkanBaseRenderer.{hpp,cpp}`、`src/Engine/Vulkan/WindowSurface.{hpp,cpp}`、`src/Modules/NextRemote/RemoteServer.cpp`、`tools/gnb/cmd/gnb/main.go`。下文所有 API、行号引用均为仓库当前真实符号（行号以写作时为准，实现时以最新代码为准）。
> **结论先行**：引擎已经具备「隐藏窗口真实渲染 + swapchain 回读到 CPU」的全部底层能力（agent-validation / screenshot 路径）。新增 TUI 模式**不需要改渲染管线**，本质是三件事：(1) 复用隐藏窗口让引擎照常渲染到 swapchain；(2) 每帧把 swapchain 回读为 CPU 像素，缩放到「终端列 × 行×2」的网格；(3) 用 **truecolor + 上半块字符 `▀`(U+2580)** 把网格写到终端 —— 每个字符格上下两个子像素，行间无缝、像素接近正方形，正是 opencode / chafa 那种观感。Windows Terminal 通过开启 VT 处理 + UTF-8 代码页即可支持。

---

## 1. 背景与目标

### 1.1 需求一句话

希望从 terminal 启动引擎，**不弹出独立窗口**，而是像 `--agent-validation` 那样用隐藏窗口真实渲染，再把画面以 TUI 的方式**持续刷新到终端**上：行与行之间没有缝隙、能输出方形「像素」、分辨率尽量高，支持 Windows Terminal 这类现代终端即可。

### 1.2 为什么现在能做、且代价很小

引擎当前的关键事实（详见 §3）：

- **隐藏窗口已经能真实渲染**：`--hidden-window` 用 `SDL_WINDOW_HIDDEN` 建窗，不抢焦点、不弹窗，swapchain 照常 present，详见 `src/Engine/Vulkan/WindowSurface.hpp:26-30`、`src/Engine/Runtime/Engine.cpp:424-425`。
- **swapchain → CPU 回读已经实现**：`VulkanBaseRenderer::CaptureScreenShot()`（`src/Engine/Rendering/VulkanBaseRenderer.cpp:1004-1032`）把当前 swapchain image `vkCmdCopyImage` 到一张 host-visible 线性 image，随后 `GetScreenShotMemory()->Map()` 即可逐像素读出（见 `src/Engine/Runtime/ScreenShot.cpp` 的 SDR 分支，BGRA8 解包）。
- **有现成的「逐帧旁路消费画面」注入点**：`IFrameStreamer`（`src/Engine/Runtime/FrameStreamer.hpp`）被 WebRTC Remote Play 用来每帧把画面录进命令缓冲并编码外发，`NextEngine` 在 `postRender` 钩子里回调它（`src/Engine/Runtime/Engine.cpp:1292-1296`）。TUI 是同一类「旁路消费者」。
- **主循环是 SDL3 回调式**：`SDL_AppIterate / SDL_AppEvent / SDL_AppInit`（`src/DesktopMain.cpp`），每个 iterate 调一次 `NextEngine::Tick()`。TUI 的输出/输入可以挂在这个节奏上。

因此 TUI 模式 = **隐藏窗口 + 强制 SDR + immediate present + 每帧回读 + 终端 blit + 终端输入桥接**，绝大多数是「新增一个子系统 + 一个平台 IO 抽象 + 几个 Options + 一个 gnb 子命令」，不触碰渲染管线。

### 1.3 本次目标

1. 新增运行模式 `--tui`：从终端启动，引擎隐藏窗口渲染，画面以半块 truecolor 方式持续刷新到当前终端，按下退出键/`Ctrl+C` 干净退出。
2. 输出质量达到「行间无缝、方形像素、随终端尺寸自适应分辨率」，在 Windows Terminal、以及 Linux 现代终端（kitty/wezterm/gnome-terminal 等）可用。
3. 终端输入桥接：把终端按键映射为引擎输入（至少支持相机移动 / 退出 / 截图），使 TUI 可交互而非纯只读预览。
4. 提供 `gnb tui` 子命令，和 `gnb shot` 同构，开箱即用。
5. 分阶段交付，每阶段可独立验证、可回退。

### 1.4 非目标（本期不做）

- 不做真正的 headless（无 surface、纯 offscreen render target）。本期沿用「隐藏窗口 + 真实 swapchain」这条已验证路径；offscreen-only 留作后续可选优化（见 §10）。
- 不在 TUI 里渲染 ImGui 面板（ImGui 仍可截图，但 TUI 不解析 UI 控件）。HUD 信息用终端文本叠加实现。
- 不追求和桌面窗口逐帧同步的高帧率；TUI 目标是「可用、流畅观感」，受终端带宽与回读开销约束（见 §7 性能）。
- 不做 sixel / kitty graphics protocol / iTerm2 inline image 等「真·图像协议」路径（兼容性碎片化）。本期只做**字符块**路径，覆盖面最广；图像协议作为可选增强列入 §10。

---

## 2. 主流 TUI 图像渲染方法调研与选型

终端里「显示一张图」的主流做法，按兼容性与密度排序：

| 方法 | 字符 | 每格像素 | 纵向密度 | 颜色 | 兼容性 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| ASCII 灰度 | `.:-=+*#%@` | 1 | 1× | 单色/16 色 | 极广 | 老式，画质差，**不选** |
| **半块 ▀** | `U+2580` 上半块 | 2（上下） | **2×** | 24-bit truecolor | **极广**（仅需 truecolor + UTF-8） | 上像素=前景色，下像素=背景色；行间无缝、像素近正方形。**首选基线** |
| 四分块 ▘▝▖▗▀▄▌▐█… | `U+2580` 区段 | 4（2×2） | 2× 横向×2 | 仅 2 色/格（前景+背景） | 广 | 横向也翻倍，但一格只有两种颜色，彩色图易串色，更适合线稿；可作可选高密度档 |
| 六分块（sextant） | `U+1FB00`–`U+1FB3B` | 6（2×3） | 3× | 2 色/格 | 中（需较新字体/终端，Windows Terminal 支持） | 比四分块更高密度，串色问题相同 |
| 八分块（octant，Unicode 16） | `U+1CD00`+ | 8（2×4） | 4× | 2 色/格 | 较新（2024 起，字体覆盖有限） | 最高密度的字符路径，但字体支持不普遍，列为远期可选 |
| 图像协议 | sixel / kitty / iTerm2 | 真像素 | 高 | 真彩 | 碎片化 | 画质最好但需终端专项支持，本期不做 |

**参考实现**：`chafa`、`viu`、`timg`、`notcurses` 都以半块 truecolor 为主力路径并辅以四/六分块；opencode 等现代 TUI 的图像/画面块也走同类字符块 + truecolor。这些工具的共识是：**半块 truecolor 是「画质/兼容性/实现复杂度」的最佳折中**。

### 2.1 选型结论（已拍板：唯一字形 = 半块）

- **唯一渲染字形**：**半块 `▀` + 24-bit truecolor**（已拍板，不引入四/六/八分块档）。每个终端字符格表示**上下两个像素**：
  - 前景色 `ESC[38;2;R;G;Bm` = 该格**上**像素颜色；
  - 背景色 `ESC[48;2;R;G;Bm` = 该格**下**像素颜色；
  - 字形固定为 `▀`（上半实心）。
  - 于是 `cols × rows` 个字符 = `cols × (rows×2)` 个像素。终端字符格本身宽高约 1:2，半块把每个子像素压成约 1:1 —— **天然方形像素**，正中需求。
- **不做高密度字形档**：四/六/八分块每格仅两色，彩色 3D 画面易串色、字体覆盖也更窄；本特性彩色画面唯一目标，故只保留半块。相应地**不引入 `--tui-glyph` 开关**（编码器内部固定半块）。表格中四/六/八分块仅作背景对照，非实现项。
- 颜色一律 truecolor；不做 256/16 色量化（现代终端均支持 truecolor；老终端非本期目标）。

### 2.2 终端能力与平台前置

- **Windows**：需 (a) `SetConsoleOutputCP(CP_UTF8)` 让 `▀` 正确输出；(b) `SetConsoleMode(hOut, … | ENABLE_VIRTUAL_TERMINAL_PROCESSING)` 开启 VT 序列解析；(c) 输入侧 `ENABLE_VIRTUAL_TERMINAL_INPUT` 并关掉行模式/回显做 raw 输入。Windows Terminal、Win10 1809+ conhost 均支持。
- **POSIX（Linux/macOS）**：truecolor + UTF-8 默认可用；raw 输入用 `termios`（关 `ICANON`/`ECHO`），尺寸用 `ioctl(TIOCGWINSZ)`，尺寸变化用 `SIGWINCH`。
- **终端尺寸**：Windows 用 `GetConsoleScreenBufferInfo`，POSIX 用 `ioctl(TIOCGWINSZ)`，每帧或在 resize 事件时刷新目标网格。

---

## 3. 现状盘点（数据流与文件地图）

### 3.1 隐藏窗口与运行模式开关

- `WindowConfig::HiddenWindow`（`src/Engine/Vulkan/WindowSurface.hpp:26-30`）：用 `SDL_WINDOW_HIDDEN` 建窗，不抢焦点；swapchain 仍真实渲染、可被回读。
- `Options`（`src/Engine/Options.hpp:30-58`）已有 `AgentValidation / AgentValidationFrames / HiddenWindow / RemoteMode / ForceSDR / FastExit / PresentMode` 等开关；解析在 `src/Engine/Options.cpp`（如 `--hidden-window` 见 :44，`--remote` 隐含 `--hidden-window` 见 :109-111）。
- `NextEngine` 启动时 `windowConfig.HiddenWindow = options.AgentValidation || options.HiddenWindow`（`src/Engine/Runtime/Engine.cpp:424-425`）；agent-validation 还会把 present 切到 `VK_PRESENT_MODE_IMMEDIATE_KHR` 以摆脱 vsync（`Engine.cpp:514-518`）。

### 3.2 主循环与逐帧钩子

- 入口 `src/DesktopMain.cpp`：`SDL_AppInit` 建 `NextEngine` 并 `Start()`；`SDL_AppIterate` 每次调 `GApplication->Tick()`；`SDL_AppEvent` 转 `HandleEvent`。
- `NextEngine::Tick()`（`src/Engine/Runtime/Engine.cpp:690-886`）：scene/physics/script/game tick → `renderer_->DrawFrame()`（:811-813）→ 处理 pending screenshot（:817-829）→ `TickAgentValidation()`（:875-878）。
- `renderer_->GetDelegates().postRender`（在 `Engine::Start()` 注册，`Engine.cpp:531-533`）→ `OnRendererPostRender(commandBuffer, imageIndex)`，内部会回调 `frameStreamer_->RecordVideoFrame(...)`（`Engine.cpp:1292-1296`）。**这是每帧、拿得到 command buffer 和 imageIndex 的旁路注入点**。

### 3.3 swapchain → CPU 回读（TUI 复用核心）

- `VulkanBaseRenderer::CaptureScreenShot()`（`src/Engine/Rendering/VulkanBaseRenderer.cpp:1004-1032`）：`SingleTimeCommands::Submit` 内做 barrier + `vkCmdCopyImage`，把当前 swapchain image 拷到 `screenshot_.image`（host-visible 线性，格式同 swapchain，见创建处 :716-722）。
- 读出：`GetScreenShotMemory()`/`GetScreenShotImage()`（`VulkanBaseRenderer.hpp:99-100`）+ `vkGetImageSubresourceLayout` 拿 `rowPitch`，`Map()` 后逐像素解包。SDR 路径见 `src/Engine/Runtime/ScreenShot.cpp` 的 `else` 分支：内存里是 **BGRA8**，`R=(px>>16)&0xff, G=(px>>8)&0xff, B=px&0xff`。
- HDR swapchain（10-bit / extended-linear）有单独分支（`ScreenShot.cpp`）。**TUI 一律强制 SDR**（见 §5.4）以走最简单的 8-bit 解包，和 Remote 强制 forcesdr 同思路。

### 3.4 既有「逐帧消费者」范式：FrameStreamer / Remote

- `IFrameStreamer`（`src/Engine/Runtime/FrameStreamer.hpp`）：`Start()` / `RecordVideoFrame(cmd, imageIndex, renderer)` / `OnRendererDeleteSwapChain()`。
- `RemoteServer`（`src/Modules/NextRemote/RemoteServer.cpp:69-75`）实现 `RecordVideoFrame` → 视频管线在 GPU 上 RecordFrame 后编码外发；`SetFrameStreamer` 在 `DesktopMain.cpp` 中按 `--remote` 装配。
- **TUI 与 Remote 是「同一形态、不同后端」**：Remote 把帧编码成 H.264 走 WebRTC；TUI 把帧回读、缩放、编码成 ANSI 走 stdout。可以共用「隐含 hidden-window + forcesdr」这套装配习惯。

### 3.5 截图自动化范式：agent-validation 状态机

- `TickAgentValidation()`（`src/Engine/Runtime/Engine.cpp:1299-1340`）：等 N 帧 → `RequestScreenShot` → 等几帧落盘 → `RequestClose()`。说明引擎里「等渲染稳定 → 回读 → 收尾退出」是成熟范式，TUI 的退出/清屏可参照。

### 3.6 CLI：gnb shot 范式

- `newShotCommand`（`tools/gnb/cmd/gnb/main.go:473-510`）：包装 `--agent-validation [--agent-validation-frames=N] [--agent-validation-ui]` 调 `gnb run`，结束打印截图路径。`gnb tui` 与之同构，只是改成长驻前台进程，把 stdin/stdout 直通到子进程（继承终端）。

---

## 4. 总体架构设计

### 4.1 数据流（Phase 1 基线，同步回读）

```
SDL_AppIterate
  └─ NextEngine::Tick()
       ├─ ... scene/script/game tick ...
       ├─ renderer_->DrawFrame()            // 照常渲染到隐藏窗口 swapchain
       └─ if (tui active) TuiPresenter::Tick():
            1. renderer_->CaptureScreenShot()        // 复用：swapchain → host image（同步）
            2. Map screenshot memory → 取 BGRA8 + rowPitch
            3. Downscale(srcW×srcH → cols×(rows*2))  // 盒式/最近邻
            4. EncodeHalfBlock(grid → ANSI string)   // diff 上一帧，只发变化
            5. TerminalIO::Write(ansi)               // 一次 write 到 stdout

SDL_AppEvent (桌面事件，隐藏窗口基本无输入)
TuiPresenter::PollInput()  ← 每 Tick 从 TerminalIO 读 stdin（raw, 非阻塞）
   └─ 映射按键 → 合成引擎输入 / 退出 / 截图
```

> Phase 1 直接复用 `CaptureScreenShot()`（`SingleTimeCommands` 同步 submit），实现最快、风险最低；性能瓶颈在 §7 量化，Phase 3 用异步 readback ring 优化。

### 4.2 新增组件与落点

建议新增一个模块 `src/Modules/NextTui/`（与 `NextRemote` 平级，模块化、可独立编译开关），包含：

| 组件 | 文件（建议） | 职责 |
| --- | --- | --- |
| `TuiPresenter` | `src/Modules/NextTui/TuiPresenter.{hpp,cpp}` | 主驱动：每 Tick 触发回读→缩放→编码→输出；管理输入轮询与退出 |
| `TerminalBlitter` | `src/Modules/NextTui/TerminalBlitter.{hpp,cpp}` | 纯算法：RGBA 帧 → 目标网格缩放 + 半块编码 + 帧间 diff，产出 ANSI 字节串（**可单测、无平台依赖**） |
| `TerminalIO` | `src/Modules/NextTui/TerminalIO.{hpp,cpp}` + 平台分支 | 终端原生 IO：开启 VT/UTF-8、raw 输入、查询尺寸、写 stdout、进/出 alt-screen、恢复现场、`Ctrl+C`/`SIGWINCH` 处理 |
| `NextTuiModule` | `src/Modules/NextTui/NextTuiModule.{hpp,cpp}` | 装配入口：`Install(NextEngine&)`，按 `--tui` 创建 `TuiPresenter` 并接入引擎（参照 `Modules::NextRemote::CreateRemoteServer`） |

**接入方式 —— 已拍板：方案 A**：

- **方案 A（采纳）**：`TuiPresenter` 不走 `IFrameStreamer`，而是由 `NextEngine::Tick()` 末尾调用（类似 `TickAgentValidation()` 的位置，`Engine.cpp:875-878`）。因为 TUI 的回读是 **CPU 路径**，放在 `Tick` 末尾、`DrawFrame` 之后最直观，且能直接拿 `renderer_`。在 `Engine` 里加一个可选 `std::unique_ptr<Runtime::ITuiPresenter> tuiPresenter_`（接口放 Runtime 层，实现放 Modules，和 FrameStreamer 同构），`Tick` 末尾 `if (tuiPresenter_) tuiPresenter_->Tick();`。
  - Phase 1 在 `TuiPresenter::Tick()` 内同步 `CaptureScreenShot()`（最快、最低风险）。
  - Phase 3 异步化时**仍走方案 A 的驱动位置**，只把回读换成 ring + fence：`TuiPresenter::Tick()` 每帧请求一次 copy（可借由 `postRender` 钩子把 copy 插进当帧 cmd，参考 `RemoteServer::RecordVideoFrame` 的做法）并消费 N-1 帧已完成的回读，避免每帧 `SingleTimeCommands` 同步 submit。接入点不变，改的只是回读时序。
- **方案 B（不采纳）**：让 `TuiPresenter` 整体挂到 `IFrameStreamer` 上由渲染线程驱动 —— CPU 取回时机更绕、与输入/退出节奏耦合更紧，已否决。

> 采用「Runtime 定义接口 `ITuiPresenter` + Modules 实现」可保持 Engine 不依赖具体 TUI 实现，与现有 `IFrameStreamer` 完全同构，便于按 CMake 开关裁剪。

### 4.3 Options / 装配

新增 Options（`src/Engine/Options.hpp` + 解析 `Options.cpp`，参照现有 `--remote` 隐含项写法 `Options.cpp:109-111`）：

| flag | 字段 | 默认 | 说明 |
| --- | --- | --- | --- |
| `--tui` | `Tui` | false | 启用 TUI 模式。**隐含** `HiddenWindow=true`、`ForceSDR=true`，并把 present 切 immediate（同 agent-validation）。 |
| `--tui-fps` | `TuiFps` | 30 | 终端刷新上限（节流，避免刷爆 stdout） |
| `--tui-max-cols` / `--tui-max-rows` | `TuiMaxCols/Rows` | 0=自适应 | 上限，0 表示跟随终端尺寸 |
| `--tui-no-input` | `TuiNoInput` | false | 只读预览，不接管 stdin（用于管道/日志场景） |

> 不设 `--tui-glyph`：字形固定为半块（见 §2.1 拍板）。

装配在 `src/DesktopMain.cpp::SDL_AppInit`，紧挨 `if (GOption->RemoteMode) … SetFrameStreamer` 之后加：`if (GOption->Tui) Modules::NextTui::Install(*GApplication);`。

### 4.4 退出与现场恢复

- 进入：保存终端原始模式 → 切 UTF-8/VT/raw → 进 alt-screen（`ESC[?1049h`）+ 隐藏光标（`ESC[?25l`）+ 清屏。
- 退出（正常退出 / 退出键 / `Ctrl+C` / 异常）：**务必**恢复光标、退 alt-screen（`ESC[?1049l`）、还原 console mode/termios。用 RAII（`TerminalIO` 析构）+ 信号处理双保险。`FastExit` 路径（`DesktopMain.cpp::SDL_AppQuit` 里 `std::quick_exit`）会跳过析构，因此**信号/atexit 钩子是恢复现场的关键**，需显式注册。

---

## 5. 关键技术细节

### 5.1 半块编码（核心算法）

目标网格 `cols × rows`，对应像素 `cols × (rows*2)`。对每个字符格 `(cx, cy)`：

- 上像素 = `pixels[2*cy][cx]`，下像素 = `pixels[2*cy+1][cx]`。
- 输出：`ESC[38;2;{Rt};{Gt};{Bt}m` `ESC[48;2;{Rb};{Gb};{Bb}m` `▀`（UTF-8: `E2 96 80`）。
- 行末 `ESC[0m\r\n` 或在帧首一次性 home（`ESC[H`）后逐行写。
- **行优先合并同色**：连续同前景/背景的格可省略重复 SGR，显著减小字节量。

边界：`rows*2` 为奇数时最后一行下像素用黑色或复制上像素（建议复制，避免底部出现黑条）。

### 5.2 缩放

- 源是 swapchain 分辨率（隐藏窗口尺寸，默认见 `Options` Width/Height，可经 `--width/--height` 或 TUI 按终端比例设定）。目标是 `cols × rows*2`。
- Phase 1：**盒式降采样（box filter）** 求每个目标像素覆盖区域的平均色，画质明显优于最近邻，成本可接受（目标像素总数很小，常 < 200×120）。
- **让隐藏窗口分辨率贴近终端长宽比**：开 TUI 时按 `cols : rows*2`（≈终端可视像素比）设定渲染分辨率，减少缩放形变，也省 GPU。终端 resize 时触发 swapchain recreate（引擎已有 `RecreateSwapChain` 流程，`VulkanBaseRenderer.cpp:1051-1056`）。

### 5.3 帧间 diff（带宽优化）

- 维护「上一帧网格颜色缓存」。本帧编码时，**仅对发生变化的格**发定位 `ESC[{row};{col}H` + 颜色 + `▀`；未变化的格跳过。
- 静态画面几乎零输出；运动画面只发变化区域。这是 TUI 流畅的关键（终端瓶颈是 stdout 带宽，不是 CPU）。
- 首帧 / resize 后全量重绘。

### 5.4 颜色与格式

- 强制 SDR：TUI 隐含 `ForceSDR=true`，使 swapchain 走 8-bit、`ScreenShot.cpp` 的 SDR 分支（BGRA8）。避免 HDR 10-bit 解包与色彩映射复杂度。
- 注意内存布局是 **BGRA**（解包顺序见 §3.3）；编码时按 R/G/B 提取，别写反。
- 可选 `--tui-srgb` 处理：swapchain 通常已是 sRGB 编码字节，直接当显示色发给终端即可，**不要**再做一次 gamma。若发现偏暗/偏亮再按显示模式（`SwapChain::OutputMode`）校正。

### 5.5 终端输入桥接

- `TerminalIO` raw 非阻塞读 stdin，解析：
  - 普通键（`w/a/s/d/q/空格` 等）、方向键（`ESC[A/B/C/D`）、`Ctrl+C`（0x03）。
- **桥接方式 —— 已拍板：合成 `SDL_Event`（原方案 B / 路径 2）**：`TerminalIO` 把解析出的按键封装成 SDL 键盘事件（`SDL_PushEvent`，含 `SDL_EVENT_KEY_DOWN/UP`、对应 `SDL_Scancode`/`SDL_Keycode` 与修饰位），统一喂给 `NextEngine::HandleEvent`，从而**复用现有全部输入绑定**（`GameInstance::OnKey`，如 `gkNextRenderer.cpp:677`；`OnMouseButton` :742）。好处：相机/游戏/编辑器各 program 的键位无需在 TUI 侧重写，行为与桌面窗口一致。
  - 与 `docs/designs/agent-validation-input-driver.md`（输入驱动设计）方向一致：两者都在「合成 SDL 输入注入引擎」这一层，应**共用同一套合成输入工具**（按键名→scancode 映射、事件构造、注入入口），避免两份实现。实现时优先把该映射抽成可复用 helper。
  - 退出/截图这类**全局动作**（`q`、`Ctrl+C`、`r`）由 `TuiPresenter` 在合成事件之前**先行拦截**直接调 `RequestClose()` / `RequestScreenShot()`，不依赖具体 program 的键位绑定，保证任何场景都能退出。
  - 按键释放：终端 stdin 只给「按下」字符、没有原生 KeyUp。需为合成的 KeyDown 排一个**短延时自动 KeyUp**（或下一次该键再次到达前补发 Up），让持续移动（WASD 长按）手感可用；具体延时在 Phase 2 调参。
- `--tui-no-input` 时完全不接管 stdin（纯预览 / 可被重定向）。

### 5.6 终端尺寸与 resize（已拍板：必须随终端比例 recreate swapchain）

本特性**要求隐藏窗口/swapchain 分辨率随终端比例自适应**（而非固定分辨率再拉伸），以获得方形像素、最小形变与更省的 GPU 负载。具体：

- **尺寸检测**：低频（如 5Hz）查询终端尺寸，避免每帧 syscall。POSIX 用 `SIGWINCH` 触发 + `ioctl(TIOCGWINSZ)`；Windows 无对应信号，轮询 `GetConsoleScreenBufferInfo`。
- **目标网格**：`cols × (rows*2)` 即目标像素分辨率（底部 HUD 行从 `rows` 扣除）。
- **swapchain 贴合比例**：终端尺寸变化时，按 `targetW = cols`、`targetH = rows*2`（或其整数倍上采样系数 `k`，见下）请求把隐藏窗口/swapchain resize 到对应分辨率，复用引擎既有 `requestRecreateSwapChain_` / `RecreateSwapChain()` 流程（`VulkanBaseRenderer.cpp:987`、:1051）。recreate 完成（`OnRendererDeleteSwapChain` → 重建）后由 `TuiPresenter` 标记**全量重绘**并重置 diff 缓存与回读 ring。
  - **上采样系数 `k`**：直接渲染到 `cols×rows*2` 往往偏低清、3D 画面锯齿明显。允许以 `k`（默认 1，可 `--tui-ssaa` 配 2）渲染到 `k·cols × k·rows*2`，CPU 回读后再 box-downscale 到目标网格，等效超采样抗锯齿。`k` 越大越清晰但 GPU/回读越贵。
- **节流 / 防抖**：连续 resize（拖拽终端边框）期间对 recreate 做去抖（停止变化 ~150ms 再 recreate），避免反复重建 swapchain 抖动。
- **最小尺寸保护**：cols/rows 过小（如 cols<10）时暂停渲染与回读，居中打印「terminal too small」，恢复后再继续。
- **生命周期顺序**：recreate 涉及 GPU 资源重建，须在引擎渲染线程的既有 recreate 时机内完成，不可在 `TuiPresenter::Tick()` 里直接重建——只「请求」，由渲染器在下一帧落地（与现有 `requestRecreateSwapChain_` 语义一致）。

### 5.7 HUD / 状态行（可选）

- 复用引擎已有统计（`frameState_.frameRate`、`GetTotalFrames()`，`Engine.hpp:120-126`），在底部预留 1 行打印 `fps / frame / scene / 分辨率 / 按键提示`，不占用图像网格。

---

## 6. 分阶段开发计划

> 每个 Phase 都能独立编译、独立验证、可回退。建议严格按序推进。

### Phase 0 —— 可行性打样（0.5 天，纯验证，可丢弃代码）

- 写一个最小 C++ 片段或独立小程序：读一张已有截图 `out/build/windows/screenshots/agent_validation.jpg`（用 `gnb shot` 生成），缩放后用半块 truecolor 打印到终端。
- 在 **Windows Terminal** 验证 `▀` + truecolor + UTF-8/VT 是否正确显示、像素是否方正、行间是否无缝。
- 产出：确认 §5.1/§5.2 编码与 §2.2 平台前置正确。**不改引擎**。

### Phase 1 —— MVP：只读 TUI 预览（2–3 天）

- 新增 `src/Modules/NextTui/`：`TerminalBlitter`（缩放+半块+diff）、`TerminalIO`（VT/UTF-8/尺寸/写出/现场恢复，先只做输出与退出键）、`TuiPresenter`、`NextTuiModule`。
- Runtime 加 `ITuiPresenter` 接口（同构 `IFrameStreamer`）+ `NextEngine` 持有 `tuiPresenter_`，`Tick()` 末尾驱动。
- Options 加 `--tui / --tui-fps / --tui-no-input`，隐含 `HiddenWindow + ForceSDR + immediate present`。
- `DesktopMain.cpp` 装配 `Modules::NextTui::Install`。
- CMake：`src/CMakeLists.txt` 收录新模块，加 `ENABLE_TUI`（默认 ON 桌面端）开关。
- 验收：`gnb run gkNextRenderer -- --tui --load-scene=assets/models/playground.glb`（或经 `gnb tui`，见 Phase 1.5）能在终端看到实时画面，`q`/`Ctrl+C` 干净退出、终端现场完好。

### Phase 1.5 —— gnb tui 子命令（0.5 天）

- `tools/gnb/cmd/gnb/main.go` 仿 `newShotCommand`（:473）加 `newTuiCommand`：包装 `--tui`，把子进程 stdin/stdout 直通继承当前终端（前台长驻，非一次性）。
- 用法：`gnb tui --scene assets/models/playground.glb`、`gnb tui --target ScadStudio --scene …`。
- 更新 `docs/guides/gnb-cli.md`。

### Phase 2 —— 输入交互 + 随终端比例 resize（2–3 天）

- `TerminalIO` 完成 raw 输入解析（普通键 + 方向键 + Ctrl 组合）。
- **输入桥接走合成 `SDL_Event`（已拍板，§5.5）**：把按键封装成 SDL 键盘事件喂 `NextEngine::HandleEvent`，复用各 program 既有键位绑定；全局动作（`q`/`Ctrl+C`/`r`）在 `TuiPresenter` 先行拦截；为 KeyDown 排短延时自动 KeyUp 支撑长按移动。优先把「按键名→scancode→SDL_Event」抽成可与 `agent-validation-input-driver` 共用的 helper。
- **随终端比例 resize（已拍板，§5.6）**：`SIGWINCH`/轮询检测 → 请求 swapchain recreate 贴合 `cols×rows*2`（可选 `--tui-ssaa` 上采样系数 `k`）→ recreate 落地后全量重绘并重置 diff/ring；拖拽去抖；最小尺寸保护。
- 验收：终端内可用既有键位操控相机/游戏、可触发 `gnb shot` 同款截图；改变终端大小时画面按比例自适应、不花屏、无残帧。

### Phase 3 —— 画质与性能（2–3 天）

- **异步回读**：用 ring（2–3 张 host image + fence），把 copy 插进帧 cmd（参考 `RemoteServer` 在 `RecordVideoFrame` 内 record 的做法），`TuiPresenter` 消费上一帧已完成的回读，去掉每帧 `SingleTimeCommands` 同步 submit 与 `WaitIdle`（消除卡顿，见 §7）。驱动位置仍是方案 A 的 `Tick` 末尾，只改回读时序。
- box filter / 简单 sharpen 调优；`--tui-ssaa` 超采样系数 `k` 调参；diff 编码进一步压缩（行游程）。
- 验收：1080p 等效隐藏窗口、120×60 终端下稳定 30fps，CPU/GPU 开销可接受。
- （**不含**高密度字形档：已拍板仅半块，见 §2.1。）

### Phase 4 —— 跨平台打磨与文档（1 天）

- Linux（kitty/wezterm/gnome-terminal）+ Windows Terminal 全跑通；macOS 尽力而为。
- 异常路径现场恢复（panic/信号/`FastExit quick_exit`）全覆盖。
- 写 `docs/guides/tui-mode.md`（用户向）+ 更新 `AGENTS.md` Run 段 + `docs/README.md` 索引（见 §9）。

---

## 7. 性能分析与预算

- **回读成本**：`CaptureScreenShot()` 走 `SingleTimeCommands`（一次 submit + 隐式等完成）+ `Engine::Tick` 里 screenshot 落盘前还有 `Device().WaitIdle()`（`Engine.cpp:819`）。**TUI 每帧同步回读 = 每帧一次 GPU 往返**，Phase 1 在低分辨率/30fps 下可接受，但会拉低帧率、引入卡顿。
- **Phase 3 异步化必要性**：改成「帧 cmd 内插 copy + fence + 消费 N-1 帧」后，CPU 不再每帧等 GPU，回读与渲染重叠，帧率回升。这是性能关键项，**不要长期停留在 Phase 1 同步路径**。
- **终端带宽**：truecolor 半块每格满编约 ~20–40 字节。120×60 全量 ≈ 200KB/帧；30fps 全量 ≈ 6MB/s —— 现代终端能扛但偏高。**帧间 diff（§5.3）是把它压到运动区域**的关键，静态画面接近零输出。
- **缩放成本**：目标像素数小（常 <3 万），box filter 在 CPU 上微秒级，可忽略；如成瓶颈可放 compute shader 预缩小后再回读（远期）。
- **节流**：`--tui-fps` 限制终端刷新率（独立于引擎渲染帧率），避免 stdout 成为瓶颈。

---

## 8. 文件改动清单

**新增**

- `src/Modules/NextTui/TerminalBlitter.{hpp,cpp}` —— 缩放 + 半块编码 + diff（纯算法，**可单测**）
- `src/Modules/NextTui/TerminalIO.hpp` + `TerminalIO_Windows.cpp` / `TerminalIO_Posix.cpp` —— 平台终端 IO
- `src/Modules/NextTui/TuiPresenter.{hpp,cpp}` —— 主驱动
- `src/Modules/NextTui/NextTuiModule.{hpp,cpp}` —— `Install(NextEngine&)`
- `src/Engine/Runtime/ITuiPresenter.hpp` —— 接口（同构 `FrameStreamer.hpp`）
- `docs/guides/tui-mode.md` —— 用户文档（Phase 4）
- 单测：`src/Tests/…`（`TerminalBlitter` 的缩放/半块/diff 用例）

**修改**

- `src/Engine/Options.hpp` / `src/Engine/Options.cpp` —— 新增 `--tui*` flags 与隐含项
- `src/Engine/Runtime/Engine.hpp` / `Engine.cpp` —— 持有 `tuiPresenter_`；`Tick()` 末尾驱动；`Start()` 里 TUI 同 agent-validation 切 immediate present；`windowConfig.HiddenWindow |= options.Tui`；resize 时按 §5.6 请求 swapchain recreate（贴合终端比例，复用 `requestRecreateSwapChain_`）
- 合成输入 helper（建议放 `src/Engine/Runtime/Platform/` 或与 `agent-validation-input-driver` 共用处）—— 「按键名→`SDL_Scancode`/`SDL_Keycode`→`SDL_Event`」映射与注入，供 TUI 与 agent 输入驱动共用
- `src/DesktopMain.cpp` —— `if (GOption->Tui) Modules::NextTui::Install(*GApplication);`
- `src/CMakeLists.txt`（及相关 `cmake/`）—— 收录 `NextTui`，加 `ENABLE_TUI` 开关
- `tools/gnb/cmd/gnb/main.go` —— `newTuiCommand`（仿 `newShotCommand` :473）
- `docs/README.md` —— 索引新增本计划与 `tui-mode.md`
- `AGENTS.md` —— Run Commands 段补 `gnb tui`
- `docs/guides/gnb-cli.md` —— 补 `gnb tui`

---

## 9. 验收标准与测试

**功能验收**

1. `gnb tui --scene assets/models/playground.glb` 在 Windows Terminal 实时显示画面：行间无缝、像素方正、随终端尺寸自适应。
2. `q` / `Ctrl+C` 干净退出，终端光标/模式/屏幕完全恢复（无残留 alt-screen、无隐藏光标、无 raw 模式）。
3. Phase 2 后：终端内能操控相机、`r` 截图（落点与 `gnb shot` 一致）、resize 不花屏。
4. `--tui-no-input` 下输出可被重定向到文件/管道而不卡死。

**自动化测试**

- `TerminalBlitter` 单测（Catch2，入 `gkNextUnitTests`）：给定固定 RGBA 输入，断言缩放结果、半块 ANSI 字节串、diff 仅输出变化格。**纯算法、无 GPU/终端依赖**，CI 友好。
- 集成冒烟：`--tui --tui-no-input --tui-fps=5` 跑固定帧数后退出，校验 stdout 含预期 SGR/`▀` 序列且进程 0 退出（可加进 `gnb` 自检或 CI 脚本）。
- 复用 `gnb shot` 作为画面正确性的旁证（同一隐藏窗口 + 回读路径）。

**跨平台**

- Windows Terminal、Linux（kitty/wezterm/gnome-terminal）必过；macOS Terminal/iTerm2 尽力而为。

---

## 10. 决策记录与风险

**决策记录（2026-06-25 拍板，原开放问题已闭合）**

1. **接入方式 = 方案 A**：`TuiPresenter` 由 `NextEngine::Tick()` 末尾驱动（`ITuiPresenter` 接口，同构 `IFrameStreamer`）。Phase 1 同步回读，Phase 3 在同一驱动位置换成 ring+fence 异步化。详见 §4.2。
2. **输入桥接 = 合成 `SDL_Event`（原方案 B / 路径 2）**：终端按键封装成 SDL 键盘事件喂 `NextEngine::HandleEvent`，复用各 program 既有键位绑定；与 `agent-validation-input-driver` 共用合成输入 helper；全局动作先行拦截；KeyDown 配短延时自动 KeyUp。详见 §5.5。
3. **字形 = 仅半块 `▀`**：不引入四/六/八分块档，不设 `--tui-glyph`。详见 §2.1。
4. **随终端比例 resize = 做**：swapchain 随 `cols×rows*2` recreate，配可选 `--tui-ssaa` 上采样、拖拽去抖、最小尺寸保护。详见 §5.6。

**风险**

- **每帧同步回读拖帧**：Phase 1 已知短板，必须在 Phase 3 异步化；不要把同步路径当成最终形态。
- **`FastExit`/`quick_exit` 跳过析构**导致终端现场未恢复：必须用信号/atexit 钩子兜底（§4.4）。
- **终端兼容性**：truecolor/UTF-8/VT 在老终端缺失会乱码；本期明确只保现代终端，启动时可探测并对不支持的终端给出降级提示。
- **BGRA/RGB 顺序写反**：解包按 §3.3，加单测固定。
- **HDR 误入**：必须强制 `ForceSDR`，否则走 10-bit 分支解包异常。

**远期可选（非本期）**

- 真 headless（无 surface offscreen render target），彻底摆脱隐藏窗口/swapchain。
- 图像协议路径（kitty graphics / sixel / iTerm2 inline）作为「能力探测到就走更高画质」的可选后端。
- 八分块（Unicode 16 octant）超高密度档，待字体覆盖成熟。

---

## 11. 参考

- 引擎内：`docs/designs/webrtc-remoteplay-design.md`（同形态的逐帧旁路消费者）、`docs/designs/agent-validation-input-driver.md`（输入合成方向）、`AGENTS.md` 的「Agent Visual Validation」段（`gnb shot` 机制）。
- 终端图像渲染参考实现：`chafa`、`viu`、`timg`、`notcurses`（半块 truecolor 为主力，辅以四/六分块）。
- Unicode 块元素：`▀` U+2580（上半块）、四分块 U+2596–U+259F、六分块 U+1FB00–U+1FB3B、八分块 U+1CD00+（Unicode 16）。
- VT/控制台：Windows `ENABLE_VIRTUAL_TERMINAL_PROCESSING` / `ENABLE_VIRTUAL_TERMINAL_INPUT` / `CP_UTF8`；ANSI 真彩 `ESC[38;2;r;g;bm` / `ESC[48;2;r;g;bm`；alt-screen `ESC[?1049h/l`。
