---
title: "AMD FidelityFX FSR 3.1 / Frame Generation"
category: guide
status: 现行
owner: NextFidelityFX
created: 2026-07-21
last_updated: 2026-07-21
---

# AMD FidelityFX FSR 3.1 / Frame Generation 指南

`NextFidelityFX` 通过引擎已有的 `IUpscaler`、Vulkan interposer 和 device creation
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

Windows 桌面启动时先安装 `NextStreamline`，只有 Streamline 未初始化时才安装
`NextFidelityFX`，因此一个进程只有一个 `IUpscaler` provider：

- NVIDIA 默认使用 Streamline；传 `--disable-streamline` 可强制走 FidelityFX。
- 非 NVIDIA adapter 自动使用 FidelityFX。
- `--disable-fidelityfx` 禁用 FidelityFX；`--agent-validation` 会同时禁用两个外部 provider。

相关 CVar：

- `r.fsr 1`：provider 可用时启用 FSR 3.1 temporal upscale；不可用时保留原来的内建
  spatial FSR fallback。
- `r.fsrg 1`：同时启用 FSR Frame Generation；要求 `r.fsr 1`。
- `r.superResolution 0..5`：Quality、Balanced、Performance、Ultra Performance、Native
  AA、Auto。

在 NVIDIA 机器上强制验证 FSR upscale：

```bash
gnb run gkNextRenderer --disable-streamline --cvar "r.fsr 1" --cvar "r.superResolution 0"
```

加入 `--cvar "r.fsrg 1"` 可验证 frame generation。真实 FG 验证应使用普通可见窗口；
`gnb shot`/`gnb validate` 的 agent-validation 模式会禁用 FidelityFX，不能证明 SDK dispatch
或 proxy present 已生效。

## Vulkan 与帧数据契约

FSR 3.1 upscale 通过当前 frame command buffer dispatch，输入 scene color、depth、motion
vector，直接写 swapchain storage image。输入 `RT_SCENE_COLOR` 已按目标显示空间编码，
集成按 SDR sRGB 或 HDR10 PQ 设置 non-linear color flags。projection jitter phase count 来自
SDK query，camera cut、scene/extent 切换通过 `reset` 清空 history。

全引擎 `RT_MOTIONVECTOR` 契约为 render-pixel 单位，因此 FSR 和 Streamline 都使用 1:1
motion-vector scale。depth 使用普通（非 reversed、有限 far plane）语义。

Frame Generation 使用官方 Vulkan proxy swapchain；只有 `r.fsrg` 实际请求 FG 时才替换
swapchain，单独使用 upscale 保持普通 Vulkan swapchain。设备创建前，模块会选择并请求四条互不
相同的 Vulkan queue：game、async compute、present、image acquire。adapter 无法提供四条
queue 时只关闭 FG，FSR upscale 仍然可用。启用 FG 后 renderer 强制 immediate present，
在 ImGui 之前保存无 HUD scene image，并向 frame-generation context 提交 depth、motion、
camera vectors 与 frame timing。

SDK 1.1.4 的 backend 会在 Vulkan 1.1+ 上调用 KHR 后缀的 memory-requirements entry point，
因此模块仍显式启用 `VK_KHR_get_memory_requirements2` 与
`VK_KHR_dedicated_allocation`，并为 proxy swapchain 启用 timeline semaphore feature。不要把
这些当作已被 core promotion 完全替代的冗余请求删除。

## 预期日志与排障

正常启动应出现：

```text
FidelityFX FSR 3.1 Vulkan provider installed
FidelityFX FG queues: game=..., async=..., present=..., acquire=...
FidelityFX Vulkan device ready. FSR=true, Frame Generation=true
FidelityFX FSR 3.1 active for ...
FidelityFX FSR upscale dispatch is active
FidelityFX proxy present is active
FidelityFX Frame Generation prepare dispatch is active
```

- 只有 `Frame Generation=false`：检查是否能暴露四条独立 queue；upscale 不受影响。
- 没有 provider installed：确认未由 Streamline 占用 provider，或检查
  `--disable-fidelityfx`/`--agent-validation`。
- SDK context/dispatch 失败：确认 executable 旁存在 `amd_fidelityfx_vk.dll`，并检查输入
  image usage/layout、swapchain STORAGE 支持和输出 format。
- Vulkan Validation 报 `rw_luma_history` 的 `rgba8`/`R16G16B16A16_SFLOAT` mismatch：这是
  FidelityFX SDK 1.1.4 Vulkan shader 的上游已知问题（GPUOpen FidelityFX-SDK #161）；与引擎
  导入的 scene color、depth、motion vector 状态错误要分开判断。
- HDR 颜色异常：Windows HDR10 路径应使用 10-bit + ST2084/PQ；不要把 FP16 EDR 当作 PQ。
