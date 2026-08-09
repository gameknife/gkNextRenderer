---
title: "AMD FidelityFX FSR 3.1 / Frame Generation"
category: guide
status: 现行
owner: NextFidelityFX
created: 2026-07-21
last_updated: 2026-07-21
---

# AMD FidelityFX FSR 3.1 / Frame Generation 指南

`NextFidelityFX` 通过引擎已有的 `IUpscaler`、可组合 swapchain interposer 和 device creation
augmenter 接入 FidelityFX SDK v1.1.4（FSR 3.1.4）。当前实现只在 Windows Vulkan 构建
启用；其他平台保留可编译的空模块。版本固定在 1.1.4，是因为新版 Redstone SDK 当前
不提供 Vulkan backend。

## 获取与构建

普通 `gnb setup` 会获取依赖，也可单独执行：

```bash
gnb deps fetch fidelityfx
gnb build gkNextRenderer gkNextUnitTests
```

归档解压到 `external/fidelityfx-sdk-1.1.4/`。构建链接官方
`PrebuiltSignedDLL/amd_fidelityfx_vk.lib`，并把对应 DLL 复制到可执行文件目录；SDK 源码和
二进制不提交到仓库。

## Provider 选择与 CVar

Windows 桌面会同时安装可用的 `NextStreamline` 和 `NextFidelityFX` provider：

- NVIDIA adapter 可以在 DLSS、DLSS Ray Reconstruction 与 FidelityFX FSR 之间运行时切换。
- Streamline 保持 process-wide Vulkan loader/interposer；FidelityFX 只在 FSR Frame Generation
  激活时通过独立 swapchain layer 接管对应 swapchain，两者不再争用同一个 provider slot。
- 非 NVIDIA adapter 仍可使用 FidelityFX。
- `--disable-fidelityfx` 禁用 FidelityFX；`--agent-validation` 会同时禁用两个外部 provider。

相关 CVar：

- `r.upscaler.type 3`：provider 可用时选择 FSR 3.1 temporal upscale；不可用时使用 native
  rendering，不再提供内建 spatial FSR fallback。
- `r.upscaler.postFilter 1`：在支持该后处理的 temporal upscaler evaluate 后启用
  display-resolution a-trous cleanup；默认开启，与 Native TAAU、SGSR2 共用同一 bindless pass。
- `r.upscaler.postFilterPasses 1..4`：a-trous pass 数，步长依次为 1、2、4、8，默认 `3`。
- `r.upscaler.postFilterStrength 0..1`：滤波结果混合强度，默认 `0.65`。
- `r.upscaler.postFilterLumaSigma 0.01..0.5`：颜色/亮度边缘阈值，越小越保边，默认 `0.10`。
- `r.upscaler.fireflySigma 1..8`：孤立高亮相对邻域标准差的拒绝阈值，越小抑制越强，默认 `2.5`。
- `r.frameGeneration 1`：为当前选择的 upscaler type 启用 Frame Generation。
- `r.upscaler.qualityMode 0..5`：Quality、Balanced、Performance、Ultra Performance、Native
  AA、Auto。

在 NVIDIA 机器上验证 FSR upscale：

```bash
gnb run gkNextRenderer --cvar "r.upscaler.type 3" --cvar "r.upscaler.qualityMode 0"
```

加入 `--cvar "r.frameGeneration 1"` 可验证 frame generation。真实 FG 验证应使用普通可见窗口；
`gnb shot`/`gnb validate` 的 agent-validation 模式会禁用 FidelityFX，不能证明 SDK dispatch
或 proxy present 已生效。

## Vulkan 与帧数据契约

FSR 3.1 upscale 通过当前 frame command buffer dispatch，输入 scene color、depth、motion
vector。启用 post filter 时，FSR 先写每个 swapchain image 对应的 display-resolution storage
image；后续与 Native TAAU 共用的 bindless pass 对孤立高亮做邻域方差裁剪，再以当前帧
albedo/normal 作为稳定边缘引导，
执行 B3-spline a-trous 滤波并写入 swapchain。关闭
post filter 时 FSR 仍直接写 swapchain storage image。输入 `RT_SCENE_COLOR` 已按目标显示空间编码，
集成按 SDR sRGB 或 HDR10 PQ 设置 non-linear color flags。projection jitter phase count 来自
SDK query，camera cut、scene/extent 切换通过 `reset` 清空 history。

全引擎 `RT_MOTIONVECTOR` 契约为 render-pixel 单位，因此 FSR 和 Streamline 都使用 1:1
motion-vector scale。depth 使用普通（非 reversed、有限 far plane）语义。

Frame Generation 使用官方 Vulkan proxy swapchain；只有 `r.frameGeneration` 实际请求 FG 时才替换
swapchain，单独使用 upscale 保持普通 Vulkan swapchain。设备创建前，模块会选择并请求四条互不
相同的 Vulkan queue：game、async compute、present、image acquire。adapter 无法提供四条
queue 时只关闭 FG，FSR upscale 仍然可用。启用 FG 后 renderer 强制 immediate present，
在 ImGui 之前保存无 HUD scene image，并向 frame-generation context 提交 depth、motion、
camera vectors 与 frame timing。

FSR upscale/frame-generation context、proxy swapchain context、无 HUD image 和共享 temporal
post-filter image/pipeline 都只在 FidelityFX FSR 或对应 Frame Generation 实际激活时存在；切换到
其他 upscaler 或 None 时随 swapchain teardown 立即释放。

SDK 1.1.4 的 backend 会在 Vulkan 1.1+ 上调用 KHR 后缀的 memory-requirements entry point，
因此模块仍显式启用 `VK_KHR_get_memory_requirements2` 与
`VK_KHR_dedicated_allocation`，并为 proxy swapchain 启用 timeline semaphore feature。不要把
这些当作已被 core promotion 完全替代的冗余请求删除。

## 预期日志与排障

正常启动应出现：

```text
FidelityFX FSR 3.1 Vulkan provider installed alongside other upscalers
FidelityFX FG queues: game=..., async=..., present=..., acquire=...
FidelityFX Vulkan device ready. FSR=true, Frame Generation=true
FidelityFX FSR 3.1 active for ...
FidelityFX FSR upscale dispatch is active
FidelityFX proxy present is active
FidelityFX Frame Generation prepare dispatch is active
```

- 只有 `Frame Generation=false`：检查是否能暴露四条独立 queue；upscale 不受影响。
- 没有 provider installed：检查 `--disable-fidelityfx`/`--agent-validation` 和 SDK DLL。
- SDK context/dispatch 失败：确认 executable 旁存在 `amd_fidelityfx_vk.dll`，并检查输入
  image usage/layout、swapchain STORAGE 支持和输出 format。
- Vulkan Validation 报 `rw_luma_history` 的 `rgba8`/`R16G16B16A16_SFLOAT` mismatch：这是
  FidelityFX SDK 1.1.4 Vulkan shader 的上游已知问题（GPUOpen FidelityFX-SDK #161）；与引擎
  导入的 scene color、depth、motion vector 状态错误要分开判断。
- HDR 颜色异常：Windows HDR10 路径应使用 10-bit + ST2084/PQ；不要把 FP16 EDR 当作 PQ。
