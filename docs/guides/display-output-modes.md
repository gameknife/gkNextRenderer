---
title: SDR / HDR10 / EDR 输出模式
category: guides
status: current
owner: gkNextRenderer
created: 2026-06-21
last_updated: 2026-06-21
---

# SDR / HDR10 / EDR 输出模式

gkNextRenderer 的最终 backbuffer 输出不再只有 `SDR` / `HDR` 两种语义。当前实现把 swapchain 显示输出拆成三个明确模式：

| 模式 | 值 | 典型平台 | Swapchain 语义 | Shader 输出 |
| --- | ---: | --- | --- | --- |
| SDR | `0` | 普通显示器、`--forcesdr` | `*_UNORM + SRGB_NONLINEAR` | tone mapped SDR |
| HDR10 ST2084 | `1` | Windows HDR10 电视/显示器 | 10-bit packed + `HDR10_ST2084` | linear sRGB -> BT.2020 -> PQ |
| Extended sRGB Linear | `2` | macOS / Apple EDR | `R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR` | linear extended sRGB |

这个区分很重要：macOS/MoltenVK 通常同时暴露 HDR10/PQ 和 Apple EDR/scRGB 类格式。对当前 renderer 来说，EDR linear float 是更自然的路径；如果把 macOS EDR 当成 HDR10/PQ，会出现颜色和亮度映射不匹配。

## Swapchain 选择

`SwapChain::ChooseSwapSurfaceFormat` 的优先级：

1. 若没有 `--forcesdr`，优先选择 `VK_FORMAT_R16G16B16A16_SFLOAT + VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT`，输出模式为 `ExtendedSrgbLinear`。
2. 若没有 EDR linear 格式，再选择 HDR10 10-bit packed：`A2B10G10R10_UNORM_PACK32` 或 `A2R10G10B10_UNORM_PACK32` + `VK_COLOR_SPACE_HDR10_ST2084_EXT`，输出模式为 `HDR10_ST2084`。
3. Android 保留 Display P3 fallback。
4. 最后回退到 SDR `B8G8R8A8_UNORM` / `R8G8B8A8_UNORM + SRGB_NONLINEAR`。

启动日志会打印实际选择，例如 macOS 预期为：

```text
Swap Chain format: R16G16B16A16_SFLOAT (97) colorSpace: EXTENDED_SRGB_LINEAR (1000104002) outputMode: 2
```

这条日志是定位平台 HDR 问题的第一入口。

## Shader 编码

Camera UBO 同时传递：

- `HDR`：当前 swapchain 是否为 HDR/EDR 输出。
- `HDROutputMode`：具体输出编码模式。

最终 compose 统一走 `EncodeHdrOutput(color, paperWhiteNit, outputMode)`：

- `HDR10_ST2084`：先做 scene tone mapping，再把 linear sRGB 转 BT.2020，最后编码为 ST2084/PQ。
- `EXTENDED_SRGB_LINEAR`：先做 scene tone mapping，再输出以 203 nit 为 reference white 的 linear extended sRGB 值。

ImGui UI 也使用同一个模式语义：

- HDR10 下 UI 从 sRGB texture 转 linear BT.709，再转 BT.2020 + PQ。
- EDR linear 下 UI 从 sRGB texture 转 linear sRGB 后直接输出。
- SDR 下保持原来的 sRGB UI 输出。

多视口平台窗口目前仍按 SDR UI 输出，因为它们走独立平台 window swapchain，未接入主 swapchain 的 HDR 输出模式。

## 截图与验证

截图路径也必须按输出模式分支读取：

- SDR：按 8-bit UNORM backbuffer 读取。
- HDR10：按 packed 10-bit backbuffer 读取，并做 SDR 预览转码。
- EDR linear：按 `R16G16B16A16_SFLOAT` 读取 float16，再转 sRGB 8-bit JPG。

如果 EDR linear 被误按 HDR10 10-bit 读取，`gnb shot` 的 JPG 会出现明显蓝紫错色。这通常表示截图读回路径和 swapchain output mode 不一致。

快速验证命令：

```bash
./gnb build gkNextRenderer gkNextUnitTests
./out/build/macos-arm64/bin/gkNextUnitTests
./gnb shot --scene assets/models/playground.glb --frames 30
```

`gnb shot` 日志里应出现 `uploaded scene [...] to gpu`，并打印实际 swapchain format/colorSpace/outputMode。macOS 上截图应正常显示低多边形 playground 颜色，而不是蓝紫色 debug-like 图像。

## 与 DLSS-G 的关系

Streamline DLSS-G 仍要求 HDR swapchain format 是受支持的 10-bit HDR format。EDR linear float 不是 DLSS-G HDR 路径的目标格式。若未来在 Windows 上使用 DLSS-G + HDR，应确认 swapchain 选择落在 `HDR10_ST2084` 模式，而不是 float EDR 模式。
