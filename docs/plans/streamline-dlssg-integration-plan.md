---
title: "Streamline 接入现状分析 + DLSS-G(帧生成) 重构与扩展计划"
category: plan
status: 规划草案
owner: engine
created: 2026-06-17
last_updated: 2026-06-17
---

# Streamline 接入现状分析 + DLSS-G（帧生成）重构与扩展计划

> **状态**：规划草案（待评审拍板，见 §9 开放问题）
> **目标读者**：负责实现本次重构与 DLSS-G 接入的后续 AI agent / 开发者
> **本文写作前已核实的真实代码**：`src/Engine/Rendering/Upscaler/StreamlineIntegration.{hpp,cpp}`、`src/Engine/Rendering/VulkanBaseRenderer.{cpp,hpp}`、`src/Engine/Runtime/Engine.cpp`、`src/Engine/Runtime/Engine.CameraUbo.cpp`、`src/Engine/Runtime/Config/{UserSettings.hpp,EngineCVars.cpp}`、`src/Engine/Vulkan/SwapChain.{hpp,cpp}`、`src/CMakeLists.txt`、`external/streamline-2.10.0/`（SDK 2.10.0，含 `sl_dlss_g.h`/`sl_reflex.h`/`sl_pcl.h`/`sl_core_types.h`）。下文所有 API、行号引用均为仓库当前真实符号。
> **结论先行**：当前 Streamline 以 **manual hooking（手动挂钩）** 模式工作，可支撑 DLSS-SR / DLSS-RR 这类"显式 evaluate 的 compute pass"，但 **无法直接支撑 DLSS-G**。DLSS-G 必须让 Streamline 接管 swapchain 与 present，因此本计划的核心是：先做一轮接入模式与生命周期的重构，再叠加 Reflex + DLSS-G。

---

## 1. 背景与目标

### 1.1 现状一句话

引擎已经把 NVIDIA Streamline 2.10.0 SDK 放进 `external/`，并在 `VulkanBaseRenderer::UpdateStreamline()` 里实现了一套 DLSS-SR / DLSS-RR 的逐帧 evaluate 流程，由 `r.dlss` / `r.dlssrr` 两个 CVar 控制开关。整体能跑通"超分/光线重建"这条路径，但代码集中、模式选择存在隐患，且**完全没有为帧生成（DLSS-G / DLSS-FG）做任何准备**。

### 1.2 本次目标

1. **梳理并修复现状问题**：把 Streamline 从 `VulkanBaseRenderer` 这个 1200+ 行的 god-class 里抽出来，形成一个可维护、可扩展的 Upscaler 抽象，并修正初始化时序、能力（caps）单一真相源、扩展申请等问题。
2. **接入 DLSS-G（帧生成）**：在重构基础上引入 interposer + Reflex + PCL，完成 DLSS-G 的 options/tags/present marker/state 全链路，并提供 UI 与 CVar 开关。
3. **保持现有渲染路径不回归**：DLSS off、TAA、Reference 模式、各软/硬件渲染器路径行为不变。

### 1.3 非目标（本期不做）

- 不在 Linux / macOS / 移动端启用 DLSS（`HasNvidiaAdapter()` 走的是 DXGI，仅 Windows）。本期仅 Windows x86_64。
- 不引入 DirectSR / NIS / DeepDVC（SDK 里有头文件，但不在本期范围）。
- 不做动态分辨率（DRS）联动 DLSS-G 的 `eDynamicResolutionEnabled`（留作后续）。

---

## 2. 现状盘点（数据流与文件地图）

### 2.1 文件地图

| 位置 | 职责 | 备注 |
| --- | --- | --- |
| `src/Engine/Rendering/Upscaler/StreamlineIntegration.hpp` | 对外暴露 `ShouldInitialize / Initialize / LazyInit / Shutdown` | 仅 18 行，只覆盖 init/shutdown |
| `src/Engine/Rendering/Upscaler/StreamlineIntegration.cpp` | `StreamlineWrapper` 命名空间 + **`VulkanBaseRenderer::UpdateStreamline()` 的实现** | 422 行，evaluate/tag/constants 逻辑全在这里，但函数挂在 `Vulkan::VulkanBaseRenderer` 上 |
| `src/Engine/Rendering/VulkanBaseRenderer.cpp` | 设备/扩展创建（L505–527）、`Start()` 设置 caps（L295–296）、帧循环与 present（L860–1000）、resolve/DLSS 分支（L1117–1166）、render-scale（L636–655） | DLSS 与渲染主循环强耦合 |
| `src/Engine/Rendering/VulkanBaseRenderer.hpp` | `DeviceCaps`（L124–132）含 `supportDLSS/supportDLSSRR/streamlineExtsEnabled`；`UpdateStreamline` 声明（L285） | |
| `src/Engine/Runtime/Engine.cpp` | L188–195：`vkCreateInstance` 之前调用 `StreamlineWrapper::Initialize()` | slInit 必须先于 instance，时序正确 |
| `src/Engine/Runtime/Engine.CameraUbo.cpp` | jitter（L81–94）、`ProjectionUnJit` 等矩阵、render-extent 投影 | DLSS constants 的矩阵来源 |
| `src/Engine/Runtime/Config/UserSettings.hpp` | `SuperResolution`(L27) / `DLSS`(L28) / `DLSSRR`(L29) | |
| `src/Engine/Runtime/Config/EngineCVars.cpp` | `r.dlss`(L87) / `r.dlssrr`(L89)，回调 `RequestSwapChainIfPossible` | 改开关会重建 swapchain |
| `src/CMakeLists.txt` | L478–510：`WITH_STREAMLINE` 链接 `sl.interposer` + delayload | **见 §3.A 关键问题** |
| `external/streamline-2.10.0/CMakeLists.txt` | `STREAMLINE_FEATURE_*` 开关，DLL 拷贝（含 `sl.dlss_g.dll`/`nvngx_dlssg.dll`/`sl.reflex.dll`/`NvLowLatencyVk.dll`） | DLSS_FG/REFLEX 默认 `OFF` |

### 2.2 当前 DLSS 数据流（运行时）

1. `NextEngine::CreateRenderer`（Engine.cpp L188）：若检测到 NVIDIA 适配器，`slInit()`（manual hooking，`featuresToLoad = {DLSS, DLSS_RR}`）。
2. `VulkanBaseRenderer::SetPhysicalDeviceImpl`（L505–527）：`WITH_STREAMLINE` 下尝试加入 NVX/buffer-device-address 等扩展，成功则 `caps_.streamlineExtsEnabled = true`，并把 `VkPhysicalDeviceVulkan12Features{ timelineSemaphore = true }` 挂进 pNext 链。
3. `Start()`（L295–296）：`caps_.supportDLSS = caps_.supportDLSSRR = caps_.streamlineExtsEnabled`（**注意：此时还没问过 SL 真实支持情况**）。
4. `CreateSwapChain()`（L636–655）：按 `SuperResolution` **硬编码** render scale（Quality 1.5 / Balanced 1.7 / Performance 2.0 / UltraPerf 3.0 / Native 1.0），`RenderExtent = Extent / scale`，`OutputExtent = Extent`。
5. 帧循环 `Render()`（L1117–1166）：若 `SupportDLSS() && settings.DLSS` → `UpdateStreamline()`；否则把 `RT_DENOISED` blit 到 swapchain。
6. `UpdateStreamline()`（StreamlineIntegration.cpp L233）：首帧 `LazyInit()` 调 `slSetVulkanInfo` + `slIsFeatureSupported` 回填 caps → `slGetNewFrameToken` → `slDLSS(D)SetOptions` → `slSetConstants` → 一组 `slSetTagForFrame`（depth/mvec/input/output，RR 时再加 albedo/normal/specAlbedo/diffHitDist）→ `slEvaluateFeature`。
7. present（L984–996）：直接 `vkQueuePresentKHR`。

---

## 3. 现状问题分析

> 严重度：🔴 阻塞 DLSS-G / 影响正确性；🟡 健壮性 / 可维护性；🟢 改进项。

### A. 接入模式与链接（DLSS-G 的根本障碍）

- **🔴 A1 — 实际运行在 manual hooking 模式，interposer 没有真正拦截 Vulkan。**
  `src/CMakeLists.txt` L471 链接了真实 Vulkan loader `${Vulkan_LIBRARIES}`，L482/485 又链接 `sl.interposer` 并 `/DELAYLOAD:sl.interposer.dll`。由于 delayload 只在首次引用 `sl*` 符号时才加载 interposer DLL，而 `vkCreateInstance/vkCreateDevice/vkCreateSwapchainKHR/vkAcquireNextImageKHR/vkQueuePresentKHR` 全部解析到真实 `vulkan-1`，**Streamline 的 swapchain/present 钩子从未生效**。这对 DLSS-SR/RR 没问题（它们靠 `slEvaluateFeature` 在你给的 command buffer 里跑 compute），但 **DLSS-G 必须由 Streamline 接管 present 才能在真实帧之间插入生成帧**，当前链接方式下无法工作。
- **🔴 A2 — 没有 Reflex / PCL。**
  `slInit` 的 `featuresToLoad` 只有 `{DLSS, DLSS_RR}`。而 `sl_dlss_g.h` 明确：`DLSSGStatus::eFailReflexNotDetectedAtRuntime`（L124–125）——**DLSS-G 运行时必须开启 Reflex**。当前没有 `kFeatureReflex`/`kFeaturePCL`，没有 `slReflexSetOptions`，没有在 simulation/render/present 三处打 `slPCLSetMarker`，也没有 `slReflexSleep`。
- **🔴 A3 — 设备/实例扩展是硬编码而非按特性查询。**
  L509–517 手写了 `VK_NVX_BINARY_IMPORT` / `VK_NVX_IMAGE_VIEW_HANDLE` / buffer-device-address 等。正确做法是用 `slGetFeatureRequirements(kFeatureXXX, FeatureRequirements&)`（`sl_core_types.h` L615 的 `FeatureRequirements` 暴露 `vkDeviceExtensions` / `vkInstanceExtensions` / `vkFeatures12` / `vkNumOpticalFlowQueuesRequired`）。DLSS-G 还需要光流队列（optical flow queue）与额外扩展，硬编码列表必然缺项。

### B. 初始化与生命周期

- **🟡 B1 — `slSetVulkanInfo` 在首帧的渲染热路径里懒加载。**
  `UpdateStreamline()`（L238）才调用 `LazyInit()`。Streamline 期望设备/队列信息在设备创建后立即设置；放在渲染循环里既脆弱又难调试（首帧抖动、错误难定位）。应在 `OnDeviceSet()` 后立即完成。
- **🟡 B2 — caps 没有单一真相源，且会在运行时翻转。**
  `Start()` 把 `supportDLSS/RR` 置为 `streamlineExtsEnabled`（"我加了扩展"），而真实支持要等首帧 `LazyInit` 里的 `slIsFeatureSupported` 才知道（StreamlineIntegration.cpp L200–204）。导致 UI/分支在前若干帧用的是乐观假值；在不支持 DLSS 的 NV 卡上会出现先 true 后 false 的翻转。
- **🟡 B3 — `Initialize()` 双入口。**
  Engine.cpp L190 调一次，`LazyInit()` 内再调一次（StreamlineIntegration.cpp L165）。靠 `GStreamLineInitAttempted` 幂等，但双路径让人困惑。
- **🟢 B4 — `slInit` 用占位 `applicationId = 12345678` 与示例 projectId GUID。**（StreamlineIntegration.cpp L139–142）开发期可用；正式发布应使用 NVIDIA 分配的 app id。
- **🟢 B5 — `Shutdown()` 只 `slShutdown()`，未先把各 feature options 置 off / 释放每特性资源**（DLSS-G 尤其需要显式 `mode = eOff` 再关闭）。

### C. 正确性（UpdateStreamline 内部）

- **🟡 C1 — 资源生命周期标记不适配 DLSS-G。**
  所有 tag 用 `sl::eOnlyValidNow`（"只在本次命令有效"）。这对 SR/RR（同一 command buffer 内消费）成立；但 **DLSS-G 在 present 时刻消费 depth/mvec/HUD-less/UI**，必须用持久生命周期（`sl::ePresentFrame` 语义）并配合 `eUseFrameBasedResourceTagging`（pref 里已设，L143）。直接照搬会导致 FG 读到已被覆盖的资源。
- **🟡 C2 — swapchain 输出图像 layout 假定为 `VK_IMAGE_LAYOUT_GENERAL`。**
  `slOutput`（L356）与各 tag 都填 `VK_IMAGE_LAYOUT_GENERAL`，但 DLSS evaluate 后还要走 `InsertBarrierToPresent`（L1164）。需确保 evaluate 前 swapchain image 真为 GENERAL、evaluate 后正确转 PRESENT_SRC；当前依赖隐式假设，未见显式 barrier 配套，存在 layout 不一致风险（validation layer 易报错）。
- **🟡 C3 — DLSS-RR 的 guide buffer 集合不完整。**
  仅 tag 了 albedo / normal-roughness / specular-albedo / diffuse-hit-dist，而 specular-motion / diffuse-noisy / specular-noisy / specular-hit-dist 全被注释掉（StreamlineIntegration.cpp L383–410）。RR 在不完整输入下质量不可控。要么补全，要么本期明确"RR 暂不保证"。
- **🟡 C4 — 运动矢量缩放与矩阵约定需复核。**
  `mvecScale = 1/RenderExtent`（L311）假定 mvec 以渲染目标像素为单位；`clipToPrevClip`/`prevClipToClip`（L307–308）由 `PrevViewProjectionUnJit * ModelViewInverse * ProjectionInverseUnJit` 拼出。**这是 DLSS 出现拖影/抖动最常见的根因**，必须用静止/平移/旋转三组对照单测验证（见 §8）。
- **🟢 C5 — frame token 局部化。**
  `slGetNewFrameToken`（L245）拿到的 token 只在 `UpdateStreamline` 内用。DLSS-G 需要同一 token 贯穿 Reflex sleep → 三个 PCL marker → evaluate → present，必须提升到帧级作用域。

### D. 可维护性

- **🟡 D1 — `SuperResolution` → 模式映射重复 3 处**：CreateSwapChain 的 render scale（L642–650）、DLSS options（L257–265）、DLSS-RR options（L286–292）。任何新增档位都要改 3 处。
- **🟡 D2 — render scale 硬编码、未取 SL 最优值。**
  应由 `slDLSSGetOptimalSettings()`（`sl_dlss.h` L177，返回 `optimalRenderWidth/Height`、`renderWidthMin` 等）驱动 RenderExtent，而非硬编码 1.5/1.7/2.0/3.0。
- **🟡 D3 — 职责错位**：`UpdateStreamline` 是 `VulkanBaseRenderer` 的成员函数，却定义在 `StreamlineIntegration.cpp`，直接读 `bindless_`/`frame_`/`caps_` 私有成员。Streamline 逻辑既没封装、又散落在渲染主类里，新增 DLSS-G 只会让 god-class 继续膨胀。

### E. 可移植性

- **🟢 E1 — `HasNvidiaAdapter()` 仅 Windows（DXGI）**（StreamlineIntegration.cpp L46–104）。这是有意的范围决定，文档化即可。

---

## 4. DLSS-G 的硬性前置条件（实现前必须满足）

来自 `sl_dlss_g.h` / `sl_reflex.h` / `sl_pcl.h` / SL 2.10 编程指南，逐条对应：

1. **Streamline 接管 swapchain/present（interposer 或 SL 代理 swapchain）。** 解决 A1。
2. **Reflex 必须在运行时处于开启状态**（`eFailReflexNotDetectedAtRuntime`）。需 `kFeatureReflex` + `kFeaturePCL`，每帧 `slReflexSetOptions(mode = eLowLatency/eLowLatencyWithBoost)`、`slReflexSleep(frameToken)`，并在三个时刻打 PCL marker：`eSimulationStart/End`、`eRenderSubmitStart/End`、`ePresentStart/End`（`sl_pcl.h` L58–64）。
3. **按特性查询扩展**：`slGetFeatureRequirements(kFeatureDLSS_G, req)` → 把 `req.vkDeviceExtensions/vkInstanceExtensions` 合并进设备/实例创建；按 `req.vkNumOpticalFlowQueuesRequired` 额外创建光流队列；`timelineSemaphore` 已开（L507）需保留。解决 A3。
4. **持久资源 tag**：depth / motion-vectors / HUD-less color / UI color&alpha 用持久生命周期 tag，确保 present 时刻仍有效。解决 C1。
5. **HUD-less / UI 分离**：DLSS-G 需要"无 UI 的颜色"作为插帧输入，UI（ImGui）应单独 tag 为 `kBufferTypeUIColorAndAlpha`，避免 UI 被插帧拉花。引擎当前 ImGui 通过 `delegates_.postRender`（VulkanBaseRenderer.cpp L942–946）直接画到 swapchain，需要调整为可分离的 UI 层。
6. **HDR 格式校验**：`eFailHDRFormatNotSupported`——HDR swapchain 下需符合 DLSS-G 对格式的要求。引擎有 `forceSDR_` / `IsHDR()`，需在开启 FG 时校验。

---

## 5. 重构方案（Phase 0–2 的设计）

### 5.1 目标架构

引入一个 Upscaler 抽象，把 Streamline 从 `VulkanBaseRenderer` 解耦：

```
src/Engine/Rendering/Upscaler/
  IUpscaler.hpp            // 抽象接口：能力查询 / 每帧 evaluate / present 钩子
  StreamlineContext.hpp/.cpp   // slInit/slShutdown/slSetVulkanInfo/扩展查询/feature 支持缓存（单一真相源）
  StreamlineUpscaler.hpp/.cpp  // DLSS-SR / DLSS-RR / DLSS-G 的 options+tags+constants+evaluate
  StreamlineReflex.hpp/.cpp    // Reflex + PCL marker/sleep 封装
  UpscalerTypes.hpp        // EUpscaleMode 枚举 + 模式↔SL 参数的"唯一映射表"（消除 D1）
```

`VulkanBaseRenderer` 只持有 `std::unique_ptr<IUpscaler>`，并在三个 hook 点回调：`OnDeviceCreated`、`Evaluate(cmd, frameCtx)`、`OnPresent(frameToken)`。

### 5.2 关键改造点（按文件）

1. **能力单一真相源（解决 B2）**：新增 `StreamlineContext`，在 `OnDeviceSet()` 后立即 `slSetVulkanInfo` + 针对每个 feature 调 `slIsFeatureSupported`，把结果写入 `caps_`。删除 `Start()` L295–296 的乐观赋值与 `UpdateStreamline` 里的 `LazyInit`。
2. **扩展按需查询（解决 A3）**：在 `SetPhysicalDeviceImpl` 之前/之中，对启用的 feature 集合调用 `slGetFeatureRequirements`，把 `vkDeviceExtensions` 去重后合并进 `requiredExtensions`；实例扩展在 `Engine.cpp` 创建 `Vulkan::Instance` 前合并。
3. **模式映射统一（解决 D1/D2）**：`UpscalerTypes.hpp` 提供 `EUpscaleMode → {sl::DLSSMode, 推荐 preset}`；render scale 改由 `slDLSSGetOptimalSettings` 取值后写入 `SwapChain::UpdateRenderViewport`（替换 VulkanBaseRenderer.cpp L642–654 的硬编码 switch）。
4. **constants/tags 迁移**：把 StreamlineIntegration.cpp L304–411 的逻辑搬进 `StreamlineUpscaler::Evaluate()`，通过一个轻量 `FUpscaleFrameInputs` 结构体（depth/mvec/color in/out + RR guide buffers + UBO 矩阵 + camera）传入，不再直接抓 `bindless_`/`frame_`。
5. **frame token 提升（解决 C5）**：token 在帧开始处 `slGetNewFrameToken` 一次，存入帧上下文，贯穿 Reflex/eval/present。

### 5.3 接入模式切换（解决 A1，DLSS-G 前提）

**推荐：interposer 模式。** 步骤：
- `src/CMakeLists.txt`：`WITH_STREAMLINE` 时**移除对 `${Vulkan_LIBRARIES}` 的直接链接**（或确保 interposer 在前并去除 delayload），改为让全部 Vulkan 入口经 `sl.interposer` 解析；或采用 SL 推荐的 `slGetVulkanProcAddr` 自定义 loader。
- 验证：在 `vkCreateSwapchainKHR`/`vkQueuePresentKHR` 处打日志或断点，确认调用进入 interposer。
- **备选**（风险更低、改动更小）：保留真实 loader，使用 Streamline 的"manual hooking + 代理 swapchain"路径——但需确认 SL 2.10 Vulkan 下 DLSS-G 是否支持该路径（见 §9 开放问题，须查 2.10 编程指南验证）。

> ⚠️ 这是全计划风险最高的一步，建议单独成里程碑并保留可回退开关 `WITH_STREAMLINE_INTERPOSER`。

---

## 6. 扩展方案：DLSS-G（Phase 3）

在 §5 重构落地、§4 前置满足后：

1. **feature 注册**：`slInit` 的 `featuresToLoad` 增加 `kFeatureDLSS_G`、`kFeatureReflex`、`kFeaturePCL`；`external/streamline-2.10.0/CMakeLists.txt` 打开 `STREAMLINE_FEATURE_DLSS_FG=ON`、`STREAMLINE_FEATURE_REFLEX=ON`（确保拷贝 `sl.dlss_g.dll` / `nvngx_dlssg.dll` / `sl.reflex.dll` / `NvLowLatencyVk.dll`）。
2. **Reflex 全帧**：每帧 `slReflexSetOptions` + `slReflexSleep` + 三段 PCL marker（封装在 `StreamlineReflex`，在 Engine 帧循环的 simulation/submit/present 三处调用）。
3. **DLSS-G options**：`slDLSSGSetOptions(viewport, DLSSGOptions{ mode=eOn, numFramesToGenerate=1, colorWidth/Height=OutputExtent, mvecDepthWidth/Height=RenderExtent, *BufferFormat=... })`。
4. **持久 tags**：depth、motion-vectors、`kBufferTypeHUDLessColor`、`kBufferTypeUIColorAndAlpha`、back buffer，用持久生命周期 tag（见 C1/§4.4–4.5）。需要先实现 **UI 分离层**（把 ImGui 从直接画 swapchain 改为画到独立 UI target，再合成），这是本阶段最大的工程量。
5. **state 查询与 UI**：`slDLSSGGetState` 读 `numFramesActuallyPresented` / `estimatedVRAMUsageInBytes` / `status`，在 ImGui 调试面板展示；`status` 非 `eOk` 时给出可读提示（分辨率过低 / Reflex 未开 / HDR 不支持）。
6. **开关与设置**：`UserSettings` 增 `bool DLSSG`（+ 可选 `uint32_t FrameGenMultiplier`）；`EngineCVars` 增 `r.dlssg`（回调同样触发必要的 swapchain/feature 重建）。注意 DLSS-G 与 `r.dlssrr` 的兼容关系需确认（SR+G 常见，RR+G 需查指南）。

---

## 7. 分阶段实施计划（里程碑 + 验收）

> 每个里程碑应可独立编译、独立验收，并写 `.spec/journal/<id>.md`（见 AGENTS.md 工作流）。

### Phase 0 — 抽象骨架与零回归重构（不改行为）
- 建 `IUpscaler` / `StreamlineContext` / `UpscalerTypes`；把 `UpdateStreamline` 逻辑平移进 `StreamlineUpscaler::Evaluate`，`VulkanBaseRenderer` 仅持接口指针。
- 统一 `SuperResolution` 映射到单一表；`slSetVulkanInfo` 移到 `OnDeviceSet` 后；caps 单一真相源。
- **验收**：`r.dlss 1`（SR）与 `r.dlss 1 + r.dlssrr 1`（RR）在 NV 卡上画面与重构前一致；非 NV / DLSS off 路径零回归；CI（Windows/Linux/macOS/Android/iOS）全绿。

### Phase 1 — 正确性加固
- 用 `slDLSSGetOptimalSettings` 驱动 RenderExtent；复核并用单测固化 mvec scale 与 clip↔prevClip 矩阵（C4）；补全或显式降级 RR guide buffers（C3）；显式管理 swapchain image layout（C2）。
- **验收**：静止画面无残影/抖动；平移、旋转相机 mvec 单测通过；validation layer 无 layout 报错。

### Phase 2 — 接入模式切换（interposer）+ Reflex/PCL
- 切 interposer（带 `WITH_STREAMLINE_INTERPOSER` 回退开关）；接 `kFeatureReflex`+`kFeaturePCL`，全帧 marker/sleep。
- **验收**：确认 present 经 interposer；`slReflexGetState` 有有效数据；开 Reflex 后延迟下降、画面无异常。

### Phase 3 — DLSS-G 主体
- UI 分离层（HUD-less + UIColorAndAlpha）；`slDLSSGSetOptions`；持久 tags；`slDLSSGGetState` + UI；`r.dlssg` 开关。
- **验收**：`r.dlssg 1` 时 `numFramesActuallyPresented` ≈ 2×（2x 模式），`status == eOk`，帧率提升而 UI 不拉花；关闭后干净恢复；VRAM 占用展示合理。

### Phase 4 — 打磨与文档
- 多分辨率/多 DLSS 档位组合矩阵测试；异常态（窗口最小化、Alt-Tab、swapchain 重建）稳定；更新 `docs/guides/` 增加一篇 DLSS/DLSS-G 使用与排错指南；`README` 技术方向补一行。

---

## 8. 验证与回归

1. **矩阵/mvec 单测**：构造已知相机运动（纯平移、纯旋转、静止），离线对比 DLSS constants 推导出的重投影与 ground truth；纳入 `src/Tests`。
2. **Validation layer**：开启 Khronos validation 跑 DLSS/RR/G 三路径，layout/同步零报错。
3. **视觉回归**：用引擎现有 HDR 截图能力，对固定场景固定相机出图，跨 commit 做像素对比（DLSS off 必须逐帧一致；DLSS on 做容差对比）。
4. **DLSS-G 运行态断言**：`slDLSSGGetState` 的 `status` 必须 `eOk`，否则按位解读 `DLSSGStatus` 打可读日志。
5. **子 agent 复核（高风险项）**：interposer 切换与矩阵约定建议用独立 review agent 二次核对。

---

## 9. 风险与开放问题（需评审拍板）

1. **interposer 切换的链接风险（最高）**：移除直接链接 `vulkan-1` 可能影响其他依赖 Vulkan 符号的第三方（VMA、imgui vulkan backend）。需评估是否全程序统一走 interposer，还是仅渲染模块。**建议先做一个最小验证 PoC。**
2. **DLSS-G 在 SL 2.10 Vulkan 下是否强制 interposer / 能否走 manual 代理**：须以 `external/streamline-2.10.0` 的编程指南与 sample 为准，本文按"需要 interposer"规划。
3. **UI 分离改造范围**：ImGui 当前直绘 swapchain（delegates_.postRender L942），分离为独立 UI target 影响 Editor/各 Game 的 overlay，需要统一过一遍调用点。
4. **DLSS-RR + DLSS-G 是否可同时开**：需查指南确认；若不兼容，UI 上做互斥。
5. **applicationId**：是否申请正式 NVIDIA app id（B4），还是开发期沿用占位。
6. **是否本期就支持 3x/4x 帧生成**（`numFramesToGenerate`），还是先只做 2x。

---

## 10. 关键代码位置索引（给实现 agent）

| 主题 | 位置 |
| --- | --- |
| slInit / slSetVulkanInfo / Shutdown | `src/Engine/Rendering/Upscaler/StreamlineIntegration.cpp` L107–228 |
| evaluate / tags / constants（待迁移） | 同上 L233–420 |
| 设备扩展 + streamlineExtsEnabled | `src/Engine/Rendering/VulkanBaseRenderer.cpp` L505–527 |
| caps 乐观赋值（待删） | 同上 L295–296；`VulkanBaseRenderer.hpp` L124–132 |
| render scale 硬编码（待改 optimal settings） | `VulkanBaseRenderer.cpp` L636–655 |
| DLSS 分支 / resolve / blit | 同上 L1117–1166 |
| acquire / submit / present（interposer 影响点） | 同上 L860–996 |
| ImGui 直绘 swapchain（UI 分离影响点） | 同上 L942–946 |
| slInit 时序（先于 instance） | `src/Engine/Runtime/Engine.cpp` L188–195 |
| jitter / ProjectionUnJit / 矩阵来源 | `src/Engine/Runtime/Engine.CameraUbo.cpp` L63–120 |
| 设置项 / CVar | `Config/UserSettings.hpp` L27–29；`Config/EngineCVars.cpp` L87–90 |
| swapchain extent 语义（Render/Output） | `src/Engine/Vulkan/SwapChain.{hpp,cpp}` |
| 链接 / delayload | `src/CMakeLists.txt` L478–510；`external/streamline-2.10.0/CMakeLists.txt` |
| DLSS-G / Reflex / PCL / 需求 API | `external/streamline-2.10.0/include/sl_dlss_g.h`、`sl_reflex.h`、`sl_pcl.h`、`sl_core_types.h`(FeatureRequirements L615) |
