---
title: "NVIDIA Streamline / DLSS / DLSS-G"
category: guide
status: 现行
owner: NextStreamline
created: 2026-06-18
last_updated: 2026-07-21
---

# NVIDIA Streamline / DLSS / DLSS-G 指南

gkNextEngine 只在 Windows x86_64 上集成 NVIDIA Streamline。非 Windows 构建会编译相同的 renderer 路径，但 Streamline 处于禁用状态。

## 运行时 CVar

- `r.upscalerType 1`：选择 DLSS Super Resolution。
- `r.upscalerType 2`：选择 DLSS Ray Reconstruction；这是独立的 upscaler type。
- `r.frameGeneration 1`：在当前 type 支持 Frame Generation 时启用。
- `r.frameGeneration.multiplier 2`：帧倍增系数，目前限制在 `2..4`。
- `r.frameGeneration.frameLimitFps 0`：Frame Generation 开启时的 base-frame 限帧；0 表示不限制。
- `r.upscaler.jitterFrames 16`：provider 未给出 phase count 时的 projection jitter 序列长度。
- `r.upscaler.jitterInvertY 0`：只用于验证 temporal upscaler jitter 符号的诊断开关。
- `r.superResolution 0..5`：Quality、Balanced、Performance、Ultra Performance、Native/DLAA、Auto。Auto 在输出不超过 1920×1080 时关闭超分并使用原生分辨率，更高分辨率使用 Quality。

修改 DLSS、DLSS-RR、DLSS-G、帧倍增系数或超分辨率模式会重建 swapchain。

无需编辑 `cvar_user.json`，也可以通过启动参数覆盖：

```bash
./gnb.sh run gkNextRenderer --load-scene=assets/models/playground.glb --cvar "r.upscalerType 1" --cvar "r.frameGeneration 1"
```

对于会反复创建和销毁 engine instance 的测试进程，使用 `--disable-streamline`。Windows
桌面应用会同时注册 Streamline 与 FidelityFX；二者通过 composite `IUpscaler` 和可叠加的
swapchain interposer layer 共存，选择 FSR 不再要求 `--disable-streamline`。

## 预期启动日志

在受支持的 NVIDIA 硬件上，启动时应报告 Streamline device 就绪状态和各 feature 支持情况：

```text
Streamline Vulkan proxy device ready. DLSS=true, RR=true, DLSS-G=true, Reflex=true, PCL=true
DLSS-G enabled
DLSS-G state OK: presented=2, maxGenerate=3, vram=0 MB
```

`presented=2` 是默认 2x 模式下的预期 active 值。预热期间，或驱动临时抑制生成时（例如窗口非活动/被遮挡，或存在不兼容 overlay），运行时可能报告 `1`。

启用 DLSS-G 后，renderer 会在 UI 渲染前标记 depth、motion vector、专用的无 HUD 快照以及 backbuffer。它还会用 PCL 标记 simulation、submit、present，并且每帧调用一次 Reflex sleep。

## DLSS-G 要求

DLSS-G 需要：

- Windows，且已启用硬件加速 GPU 调度。
- 可执行文件旁存在 Streamline DLSS-G plugin 和 Reflex plugin。
- `slIsFeatureSupported` 报告 NVIDIA 硬件和驱动支持该功能。
- swapchain format 被 DLSS-G 接受。HDR 输出只允许 10-bit HDR format；DLSS-G 会禁用 FP16/scRGB HDR。
- VSync 关闭。启用 DLSS-G 时，Vulkan renderer 会强制使用 immediate present mode。

Streamline 初始化时，engine 会要求 SDL 将 `sl.interposer.dll` 作为 Vulkan loader 加载；如果失败，则回退到系统 Vulkan loader。主 renderer swapchain 会在可用时通过 Streamline proxy entry points 路由 create、image query、acquire、present 和 wait-idle 调用。

已禁用下载/OTA Streamline plugin，因此运行时始终使用仓库随附的 plugin 版本。

## UI/HUD 处理

对于每个 proxy swapchain image，renderer 都持有一张输出分辨率的无 HUD image。ImGui 渲染前，它会把 resolved scene color 复制到这张 image，并将其标记为带 `eValidUntilPresent` 的 `kBufferTypeHUDLessColor`。ImGui 继续原地渲染到最终 backbuffer，因此生成帧使用的是冻结的场景 image，而不是后续被 UI 渲染修改过的 backbuffer。

当前 ImGui backend 不会生成独立的 premultiplied `kBufferTypeUIColorAndAlpha` target。无 HUD 集成是 SDK 支持的 fallback，可避免 UI 像素进入 optical-flow 输入；专用 UI target 仍是复杂 world-space HUD 的可选质量增强项。

swapchain 重建或切换到非 Streamline upscaler 时，DLSS-SR、RR、DLSS-G 都会关闭并释放 feature
resources 与 persistent tag；重新激活后由首帧 evaluate 按需创建。

engine 状态为 `Loading` 时不会启用 DLSS-G。这可以避免 `slDLSSGSetOptions` 与场景加载期间的 swapchain 替换发生竞争。

## 故障排查

- 如果 `r.dlssg 1` 没有增加 presented frames，检查 renderer settings 面板中的 DLSS-G state 行。非零 status mask 表示 Streamline runtime 拒绝了当前状态。
- 如果 status 为 `0` 但 presented count 仍为 `1`，确认游戏窗口处于活动状态，并在重新测试前禁用 RivaTuner Statistics Server 等 present-hook overlay。
- 不要用 `--agent-validation` 或 `--hidden-window` 验证 Streamline。Agent validation 为确定性
  会禁用 Streamline；`gnb shot` 只能验证进入 DLSS 前的 scene color/depth/motion 资源链。
  DLSS-SR/RR/DLSS-G 必须使用正常可见窗口验证。
- 如果启动日志显示请求的 Vulkan extension 不可用，该 adapter 的 DLSS 功能会被禁用，renderer 会回退到普通 resolve。
- 如果启用了 HDR 但 DLSS-G 拒绝运行，强制使用 SDR，或切换到 10-bit HDR swapchain format。
- 如果 validation 报告 swapchain layout 问题，确认 resolve 路径在 DLSS/blit 前调用 `SwapChain::InsertBarrierToWrite`，并在 present 前调用 `InsertBarrierToPresent`。
