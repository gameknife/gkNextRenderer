---
title: "DLSS 超分无抗锯齿 / scale 无收益 —— 根因定位与修复开发计划"
category: plan
status: 草案
owner: engine
created: 2026-06-20
last_updated: 2026-06-20
---

# DLSS 超分（DLSS-SR）无抗锯齿 / scale 无收益 —— 根因定位与修复开发计划

> **状态**：📝 草案（已核实源码，待后续 agent 实现）
> **目标读者**：负责修复 DLSS-SR 抗锯齿问题的后续 AI agent / 开发者
> **前置必读**：[`docs/plans/streamline-dlssg-integration-plan.md`](streamline-dlssg-integration-plan.md)（Streamline 接入现状 + DLSS-G 计划，本文是它的兄弟篇，专攻"超分质量"而非"帧生成接入"）、[`docs/plans/noambient-deferred-taa-fix.md`](noambient-deferred-taa-fix.md)（引擎自身 TAA 的抖动/过曝修复，与本文 jitter 链路强相关）、[`AGENTS.md`](../../AGENTS.md)
> **本文写作前已核实的真实代码**：`src/Engine/Runtime/Engine.CameraUbo.cpp`、`src/Engine/Rendering/VulkanBaseRenderer.cpp`、`src/Engine/Rendering/Upscaler/{StreamlineIntegration.cpp,UpscalerTypes.hpp,IUpscaler.hpp}`、`src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.cpp`、`assets/shaders/{common/Shading.slang,Core.SwModernNoAmbient.comp.slang,Process.ReProjectSimple.comp.slang}`、`src/Engine/Runtime/Config/UserSettings.hpp`、`src/Engine/Runtime/Engine.cpp`、`src/Engine/Vulkan/SwapChain.{hpp,cpp}`。下文所有 `文件:行号` 均为仓库当前真实位置。

---

## 结论先行（TL;DR）

现象：开启 DLSS 后 **DLSS-G（帧生成）生效**，但 **DLSS-SR（超分）画质≈不开 DLSS**，整体有明显锯齿；不开 DLSS 时切 TAA 能看到锯齿变化，开 DLSS 时切 TAA 无变化。

经源码核实，问题不在 Streamline 接入层（tag / evaluate / constants 流程完整），而在**喂给 DLSS 的输入**有三处关键缺陷，按影响排序：

| 编号 | 根因 | 影响 | 置信度 |
| --- | --- | --- | --- |
| **R1** | **相机 jitter（亚像素抖动）按"输出分辨率"计算，但画面在"渲染分辨率"下光栅化**，同时传给 Streamline 的 `jitterOffset` 又是按渲染像素声明的 → 实际抖动与声明值差了一个"超分倍率"，DLSS 拿到错位的 jitter，无法做时域重建，退化成空间放大 | **超分不抗锯齿的主因**；越是高倍率（Performance/UltraPerf）越糊越花 | 高 |
| **R2** | **DLSS 的 color 输入取自 `RT_DENOISED`（compose 之后，且 TAA 开启时已含引擎自身的时域累积）**，而 DLSS 期望的是"未经时域处理、带抖动的当前帧颜色"；引擎 ReProject(TAA) pass 在开 DLSS 时未被旁路 | DLSS 输入被预先平滑/二次时域处理，重建质量进一步劣化；也解释了"开 DLSS 后切 TAA 看不出差别" | 高 |
| **R3** | **DLSS 关闭时仍按 SuperResolution 倍率降分辨率渲染 + 线性放大**（`GetOptimalRenderSettings` 无条件套用 fallbackScale），所谓"Null DLSS"基线本身就是低分辨率线性放大 | 开/不开 DLSS 都是低分辨率，A/B 对比看不出超分收益，且基线本身就有锯齿 | 高 |
| **R4** | **超分时未对纹理采样施加负 mip LOD bias**（采样器 `MipLodBias` 恒为 0） | 纹理细节偏糊（非边缘锯齿主因，但加重"无清晰度提升"观感） | 中 |
| **R5** | mvec scale/符号、`clipToPrevClip` 等矩阵未用静止/平移/旋转三组对照固化 | 运动时可能拖影/抖动（非静态锯齿主因，需验证） | 待验证 |

**最小修复**：先做 **R1**（把 jitter 除数从输出分辨率改成渲染分辨率，并校准 Streamline jitterOffset 的符号/尺度），多半能立刻让 DLSS-SR 出抗锯齿。随后做 **R2/R3** 让超分真正"既清晰又抗锯齿且可对比"，**R4/R5** 收尾打磨。

---

## 1. 现象与复现

### 1.1 用户报告

- DLSS 开启后 DLSS-G 确实生效（有插帧）。
- DLSS 自身的 scale（超分）相比不开 DLSS "没感觉提升"。
- 不开 DLSS 时，切 TAA 开/关能看到锯齿变化；开 DLSS 时，切 TAA 无变化。
- 整体画面能感受到锯齿。

### 1.2 默认配置（影响复现）

- `SuperResolution = 1`（Balanced，fallbackScale 1.7）—— `src/Engine/Runtime/Config/UserSettings.hpp:24`
- `DLSS = false` 默认关闭 —— `UserSettings.hpp:25`
- `TemporalFrames = 16` —— `src/Engine/Runtime/Engine.cpp:272`

即默认就处于"渲染分辨率 = 输出/1.7"的状态，无论是否开 DLSS（见 R3）。

---

## 2. 数据流梳理（渲染 → resolve → DLSS）

以 SoftwareModern（NoAmbient）路径为例，单帧链路：

1. **决定渲染分辨率** —— `src/Engine/Rendering/VulkanBaseRenderer.cpp:787-806`
   - `dlssEnabled = supportDLSS && settings.DLSS && upscaler_`（L790）。
   - `upscaler_->GetOptimalRenderSettings(SuperResolution, Extent, dlssEnabled)`（L793）→ `renderExtent`。
   - `UpdateRenderViewport(0,0,renderExtent.w,renderExtent.h)`（L806）；`OutputViewport = 全分辨率`（L807）。
2. **组装相机 UBO** —— `VulkanBaseRenderer.cpp:1450`
   - `GetUniformBufferObject(RenderOffset(), OutputExtent())` —— **注意传入的是 `OutputExtent`（全分辨率）**。
3. **jitter / 投影** —— `src/Engine/Runtime/Engine.CameraUbo.cpp:80-94`
   - 当 `TAA || DLSS` 时取 Halton 序列，`jitter ∈ [-0.5,0.5]`（L82-84）。
   - `ubo.Projection[2][0] = jitter.x / extent.width * 2.0`（L86），`[2][1]` 同理（L87）—— **`extent` 此处 = OutputExtent**。
   - `ubo.Jitter = vec4(jitter.x, jitter.y,0,0)`（L89）—— 原始 [-0.5,0.5] 值。
4. **着色 + 运动矢量** —— `assets/shaders/Core.SwModernNoAmbient.comp.slang:162,170`
   - `motion = CalculateMotionVector(...) * float2(size)`，`size = 渲染分辨率`，写入 `RT_MOTIONVECTOR`（渲染像素单位）。
   - `CalculateMotionVector` 用 **UnJit** 矩阵、返回 `prev - curr`（半 NDC）—— `assets/shaders/common/Shading.slang:78-99`。
5. **ReProject(引擎 TAA) → compose** —— `src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.cpp:79-103`
   - ReProjectSimple 累积到 `RT_ACCUMULATE_DIFFUSE`，由 push const `TAAEnabled = settings.TAA?1:0` 控制（L85）。
   - ComposeSimple 写出最终低分辨率颜色 `RT_DENOISED`（compose 在 ReProject 之后）。
6. **resolve（关键分叉）** —— `VulkanBaseRenderer.cpp:1379-1410`
   - 若 `upscaler_ && SupportDLSS() && settings.DLSS`：`upscaler_->Evaluate(BuildUpscalerFrameInputs(...))`（L1382）。
   - 否则：`vkCmdBlitImage(RT_DENOISED@renderExtent → swapchain@outputExtent, VK_FILTER_LINEAR)`（L1396-1409）—— 纯线性放大。
7. **BuildUpscalerFrameInputs** —— `VulkanBaseRenderer.cpp:1511-1595`
   - `scalingInputColor = RT_DENOISED @ renderExtent`（L1554-1557）。
   - `scalingOutputColor = swapchain @ outputExtent`（L1558-1562）。
   - `depth = DepthBuffer @ renderExtent`、`motionVectors = RT_MOTIONVECTOR @ renderExtent`。
8. **Streamline constants** —— `src/Engine/Rendering/Upscaler/StreamlineIntegration.cpp:1465-1529`
   - `constants.jitterOffset = (ubo.Jitter.x, ubo.Jitter.y)`（L1489）。
   - `constants.mvecScale = 1/renderExtent`（L1490；`UpscalerTypes.hpp:75-81`）。
   - 矩阵用 `ProjectionUnJit / ProjectionInverseUnJit / PrevViewProjectionUnJit`（L1485-1488）。

---

## 3. 根因分析

### R1 —— jitter 按输出分辨率算，与渲染分辨率 / Streamline 声明值不一致【主因】

**事实**：
- 投影 jitter 的除数是 `extent.width/height`，而 `extent` = `OutputExtent`（`VulkanBaseRenderer.cpp:1450` 传入），见 `Engine.CameraUbo.cpp:86-87`。
- 画面实际在 `RenderExtent`（低分辨率）下光栅化（`VulkanBaseRenderer.cpp:806`，各 dispatch 用 `SwapChain().RenderExtent()`）。
- 传给 Streamline 的 `jitterOffset = ubo.Jitter`（原始 [-0.5,0.5]，`StreamlineIntegration.cpp:1489`），Streamline 约定该值以**渲染目标像素**为单位。

**推导**：投影对 clip 施加 `ndc.x += 2·jitter.x / outputW`。在宽为 `renderW` 的渲染目标里，该 NDC 偏移对应的像素位移 = `ndc.x/2 · renderW = jitter.x · renderW/outputW`。

于是：
- **画面里真实的亚像素抖动（渲染像素）= `jitter · renderW/outputW`**（被超分倍率缩小）。
- **告诉 DLSS 的 jitterOffset = `jitter`**。

二者仅在 `renderW == outputW`（Native/DLAA）时相等；任何真实超分倍率下都**差了一个 `scale = outputW/renderW` 因子**。DLSS 依据 `jitterOffset` 去对齐/反抖动每帧样本，输入的 jitter 又比声明值小，导致样本错位、时域重建失败 → DLSS 退化成"空间放大器"，输出≈线性放大，残留锯齿。倍率越高（Performance 2.0 / UltraPerf 3.0）偏差越大，越糊越花。

**这也解释了"开 DLSS 切 TAA 无变化"**：引擎自身 TAA 在低分辨率下用的也是这个被缩小的 jitter，时域收益本就很弱；经过 DLSS（近似空间放大）后差异被进一步抹平，肉眼几乎看不出 TAA 开关。

**置信度**：高。修复方式明确（除数改成渲染分辨率），且与 Streamline 官方对 jitterOffset 的定义一致。

---

### R2 —— DLSS 的颜色输入是"已 compose / 已时域处理"的图，而非原始抖动帧【高】

**事实**：
- `scalingInputColor = RT_DENOISED`（`VulkanBaseRenderer.cpp:1554`）。
- `RT_DENOISED` 是 compose pass 输出，位于 ReProject(引擎 TAA) **之后**（`SoftwareModernNoAmbientRenderer.cpp:79-103`：shading → reproject → compose）。
- 开 DLSS 时引擎 ReProject(TAA) pass **没有被旁路**，仍照常执行。

**问题**：DLSS-SR 的神经网络期望输入是"带抖动、未做时域累积的当前帧颜色"（含锯齿信息以便重建）。当 `TAA=on` 时，DLSS 拿到的是已被引擎时域累积过的图（被预先平滑），等于在 DLSS 之前先做了一次"弱 TAA"，再让 DLSS 二次时域处理 → 细节糊、时域信息冲突、重建质量下降。

**结论**：开 DLSS-SR 时，应旁路引擎 ReProject/TAA，把"compose（不含时域累积）的当前帧颜色"作为 DLSS 输入。

**置信度**：高（pipeline 顺序已核实）。

---

### R3 —— DLSS 关闭时仍降分辨率渲染，基线不是"原生"【高】

**事实**：`GetOptimalRenderSettings`（`StreamlineIntegration.cpp:1082-1122`）：
- L1089 无条件 `result.renderExtent = ScaleExtent(outputExtent, modeInfo.fallbackScale)`。
- L1094-1097：当 `!dlssEnabled || !supportDLSS || ...` 时**直接返回这个已降分辨率的 fallback**。

调用方 `VulkanBaseRenderer.cpp:790-798`：只要 `upscaler_` 存在（Windows 恒为真），无论 DLSS 是否开都会套用返回的 `renderExtent`。

**后果**：DLSS 关闭时，画面也是 `输出/fallbackScale`（默认 Balanced=1.7）+ 线性 blit 放大。所以"Null DLSS"基线本身就是低分辨率线性放大、本身就有锯齿；与"开 DLSS"对比，两者都是低分辨率，自然"看不出超分提升"。`dlssEnabled` 标志被传入却没有用于"关闭时回到原生分辨率"。

**置信度**：高。

---

### R4 —— 超分时缺少纹理 mip LOD bias【中，质量项】

**事实**：采样器 `MipLodBias` 字段恒为 0（`src/Engine/Vulkan/MemoryAndShader.cpp:301` 取 `config.MipLodBias`，默认 `MemoryAndShader.hpp:77 = 0.0f`；纹理创建处 `TextureImage.cpp` 同样置 0），全仓未见任何按渲染/输出比设置 LOD bias 的代码。

**问题**：DLSS/任意时域超分都要求采样 mip 时施加负 bias `≈ log2(renderRes/outputRes)`（NVIDIA 推荐 `log2(renderW/outputW) - 1.0`，-1.0 为可选锐化），否则在低分辨率渲染时按低分辨率选 mip，纹理细节丢失、整体偏糊。这不会直接造成边缘锯齿，但会加重"开 DLSS 也不清晰"的观感。

**置信度**：中（影响清晰度，不影响边缘锯齿）。

---

### R5 —— mvec 尺度/符号与 clip↔prevClip 矩阵待固化【待验证】

**事实**：mvec 以渲染像素存储、`mvecScale = 1/renderExtent`（与 shader 一致），矩阵用 UnJit 系列（`StreamlineIntegration.cpp:1485-1490`、`Shading.slang:78-99`）。方向 `prev - curr`、Vulkan Y 翻转下的符号是否与 Streamline 期望完全一致，**未经静止/平移/旋转三组对照固化**。

**说明**：DLSS-G 能"生效"不代表 mvec 完全正确（FG 容忍度更高）。mvec 错误主要表现为**运动时拖影/抖动**，不是静态锯齿，因此非本次主因；但属于必须验证项（与既有 DLSS-G 计划 §8 共用验证方法）。

**置信度**：待验证。

---

## 4. 修复方案

### Fix-1（对应 R1）：jitter 改为渲染分辨率相对，并校准 Streamline jitterOffset

**改动点**：`src/Engine/Runtime/Engine.CameraUbo.cpp:80-94`。jitter 除数改用渲染分辨率（`renderer_->SwapChain().RenderExtent()`，本文件 L112-113 已在用同一接口）：

```cpp
if (config_.userSettings.TAA || config_.userSettings.DLSS)
{
    const VkExtent2D renderExtent = renderer_->SwapChain().RenderExtent(); // 低分辨率渲染目标
    std::vector<glm::vec2> haltonSeq = GenerateHaltonSequence(config_.userSettings.TemporalFrames);
    glm::vec2 jitter =
        haltonSeq[frameState_.totalFrames % config_.userSettings.TemporalFrames] - glm::vec2(0.5f, 0.5f);

    // 除数用渲染分辨率：让真实亚像素抖动 == 渲染像素单位的 jitter，与 Streamline jitterOffset 一致
    ubo.Projection[2][0] = jitter.x / static_cast<float>(renderExtent.width)  * 2.0f;
    ubo.Projection[2][1] = jitter.y / static_cast<float>(renderExtent.height) * 2.0f;

    ubo.Jitter = glm::vec4(jitter.x, jitter.y, 0, 0); // 现在确为"渲染像素"单位
}
```

要点：
- 投影的透视部分（FOV/aspect）不受影响——渲染/输出宽高比相同，改动只触及 jitter 行 `[2][0]/[2][1]`。
- `ubo.Jitter` 不变（仍是 [-0.5,0.5]），修完除数后它天然就是渲染像素单位，`StreamlineIntegration.cpp:1489` 无需改。
- **符号校准（必须）**：Streamline `sl::Constants::jitterOffset` 约定为"投影所施加的像素偏移"。本引擎在 `[1][1] *= -1`（`Engine.CameraUbo.cpp:63`）之后才加 jitter，Y 方向符号需用静态相机实测确认；若 DLSS 仍在 Y 方向抖/糊，对 `jitterOffset.y` 取反（或对 `Projection[2][1]` 取反，二者保持一致）。
- 确认 `RenderExtent` 在 UBO 组装时已是本帧值（`CreateSwapChain` 中 `UpdateRenderViewport` 早于渲染循环，满足）。

**验证**：静态场景、DLSS Quality，正确后应在数帧内收敛为干净无锯齿的画面（见 §6）。

---

### Fix-2（对应 R2）：DLSS-SR 激活时旁路引擎 TAA，喂原始抖动帧

两种实现，二选一（推荐 A）：

- **A：开 DLSS-SR 时跳过 ReProject(TAA) pass**，让 compose 直接用当前帧着色结果，`RT_DENOISED` 即"未时域处理的当前帧颜色"。
  - 在 `SoftwareModernNoAmbientRenderer.cpp:79-95` 用条件包住 reproject pass：`const bool dlssSR = engine.GetUserSettings().DLSS && caps.supportDLSS; if(!dlssSR){ /* reproject */ }`，并保证 compose 在 dlssSR 时读取正确来源。
  - 同步：UBO 里 `ubo.TAA` / denoiser 路由（`Engine.CameraUbo.cpp:182,214-221`）在 dlssSR 下应使其等效于"无额外时域累积"。
- **B：新增一张"compose-pre-TAA"的颜色目标**作为 `scalingInputColor`，引擎 TAA 仅用于非 DLSS 路径。改动更大但对其它渲染路径影响最小。

**注意**：PathTracing 路径的时域累积/降噪与 SoftwareModern 不同，本 Fix 先聚焦 SoftwareModern（用户复现路径）；PathTracing 路径单独评估（见 §7）。

**验证**：开 DLSS 后切 TAA 应"无明显差异"属预期（DLSS 接管时域）；重点看 DLSS 输出本身是否清晰且抗锯齿。

---

### Fix-3（对应 R3）：DLSS 关闭时回到原生分辨率

- 在 `GetOptimalRenderSettings`（`StreamlineIntegration.cpp:1082-1097`）中，当 `!dlssEnabled` 时返回 `renderExtent = outputExtent`（scale 1.0），而非 fallbackScale 降分辨率；
- 或在调用方 `VulkanBaseRenderer.cpp:790-798`：仅当 `dlssEnabled`（或其它显式开启的空间超分）时才采用降分辨率，否则 `renderExtent = Extent()`。
- 若仍想保留"非 DLSS 的渲染缩放"选项，应作为独立设置项，**不要与 DLSS 的 SuperResolution 倍率隐式耦合**。

**验证**：DLSS 关闭时应为原生分辨率（截图边缘清晰、与开 DLSS Quality 做有意义的 A/B）。

---

### Fix-4（对应 R4）：超分时施加纹理 mip LOD bias

- 计算 `lodBias = log2(float(renderW)/float(outputW))`（约 -0.77 @1.7x、-1.0 @2.0x），DLSS 激活时对场景纹理采样器生效；可选再 `-1.0` 锐化（按 NVIDIA 指南）。
- 实现选项：
  - 资源侧：分辨率/模式变化时用新 `MipLodBias` 重建受影响采样器（`MemoryAndShader.cpp:301`）。
  - 着色侧：把 `lodBias` 放入 UBO，shader 中改用 `SampleBias`/`SampleLevel` 显式带 bias。
- 分辨率或 SuperResolution 模式变化时需重算。

---

### Fix-5（对应 R5）：固化 mvec 尺度/符号与重投影矩阵

- 复用既有 DLSS-G 计划 §8 的"静止 / 纯平移 / 纯旋转"三组对照：mvec 在静止时应≈0、平移/旋转时方向与量级正确。
- 用单测/可视化固化 `mvecScale`、`clipToPrevClip`、`jitterOffset` 符号，避免回归。

---

## 5. 开发计划（分阶段）

### Phase A —— 让 DLSS-SR 出抗锯齿（最小修复）
- [ ] A1：实现 Fix-1（jitter 除数改渲染分辨率）。
- [ ] A2：用静态相机实测校准 Streamline `jitterOffset` 符号（X/Y）。
- [ ] A3：`gnb shot` 截图验证 DLSS Quality 静态收敛为无锯齿；DLSS off 与 on 对比锯齿明显改善。
- **验收**：开 DLSS-SR（Quality/Balanced）静态画面无明显锯齿，边缘明显优于线性放大基线。

### Phase B —— 让超分"既清晰又可对比"
- [ ] B1：实现 Fix-3（DLSS off 回到原生分辨率），建立真实 A/B 基线。
- [ ] B2：实现 Fix-2（DLSS-SR 旁路引擎 TAA，喂原始抖动帧）。
- [ ] B3：截图回归（Quality/Balanced/Performance 三档）确认无过度模糊/重影。
- **验收**：原生(off) vs DLSS Quality A/B 中，DLSS 在相近清晰度下帧率提升、无新增锯齿/重影。

### Phase C —— 打磨
- [ ] C1：实现 Fix-4（mip LOD bias）。
- [ ] C2：实现 Fix-5（mvec/矩阵对照固化 + 单测）。
- [ ] C3：评估 PathTracing 路径是否需要同样的 TAA 旁路 / 输入调整（§7）。
- [ ] C4：跑 `gkNextVisualTest` 全量回归 + baseline diff。
- **验收**：运动场景无拖影；纹理清晰度达预期；视觉回归通过。

---

## 6. 验证方法

- **快速肉眼**：`gnb shot --scene assets/models/playground.glb`（截固定帧、不弹窗、自动退出），改 jitter 后对比开/关 DLSS 的边缘锯齿。需要含 UI 时 `--ui`。
- **静态收敛判据**：相机静止时，正确的 DLSS-SR 应在数帧内输出干净的高分辨率边缘；若静止仍花/抖，说明 jitter 符号或 mvec 仍错。
- **三组对照（mvec/jitter）**：静止 / 纯平移 / 纯旋转，分别看是否拖影、抖动、错位（复用 DLSS-G 计划 §8）。
- **A/B 基线**：完成 Fix-3 后，DLSS off=原生分辨率，再与 DLSS Quality 对比帧率/清晰度。
- **回归**：`gkNextVisualTest` 生成 report + baseline diff（构建：`./gnb build gkNextRenderer gkNextUnitTests`）。

---

## 7. 风险与开放问题

- **多渲染路径差异**：本文聚焦 SoftwareModern（用户复现路径）。PathTracing 路径有自己的时域累积/降噪（`Engine.CameraUbo.cpp:214-221` denoiser 路由），Fix-2 的"旁路 TAA / 喂原始帧"需在该路径单独评估，勿一刀切。
- **jitter 与引擎 TAA 的共用**：jitter 同时服务引擎 TAA 与 DLSS（`Engine.CameraUbo.cpp:80` 条件 `TAA || DLSS`）。Fix-1 改大了渲染像素抖动幅度，对引擎自身 TAA 是更正确的（此前同样偏小），但需回归非 DLSS 的 TAA 画质，确认无新增抖动。
- **符号约定**：Streamline jitterOffset / mvec 的 Y 符号在 Vulkan（Y 翻转）下需实测，不要仅凭推导。
- **fallbackScale 语义**：Fix-3 后需确认 UI/CVar 对 SuperResolution 的展示语义（它现在只在 DLSS 开启时影响渲染分辨率）。
- **DLSS-G 依赖**：DLSS-G 的 mvec/depth 与 DLSS-SR 同源，Fix-1/Fix-5 改善后应一并复测帧生成质量（不应回退）。

---

## 8. 关联文档与代码索引

- 兄弟篇：[`docs/plans/streamline-dlssg-integration-plan.md`](streamline-dlssg-integration-plan.md)（接入模式、生命周期、DLSS-G、§8 验证方法）。
- 引擎 TAA：[`docs/plans/noambient-deferred-taa-fix.md`](noambient-deferred-taa-fix.md)、[`docs/plans/reproject-history-clamp-blackdot-fix.md`](reproject-history-clamp-blackdot-fix.md)。
- 核心代码：
  - jitter / 投影：`src/Engine/Runtime/Engine.CameraUbo.cpp:80-102`
  - UBO 调用（传 OutputExtent）：`src/Engine/Rendering/VulkanBaseRenderer.cpp:1450`
  - 渲染分辨率决策：`src/Engine/Rendering/VulkanBaseRenderer.cpp:787-807`
  - resolve 分叉（Evaluate vs blit）：`src/Engine/Rendering/VulkanBaseRenderer.cpp:1379-1410`
  - 上行输入装配：`src/Engine/Rendering/VulkanBaseRenderer.cpp:1511-1595`
  - Streamline constants / tag / evaluate：`src/Engine/Rendering/Upscaler/StreamlineIntegration.cpp:1202-1237, 1465-1593`
  - GetOptimalRenderSettings：`src/Engine/Rendering/Upscaler/StreamlineIntegration.cpp:1082-1122`
  - 运动矢量：`assets/shaders/common/Shading.slang:78-99`、`assets/shaders/Core.SwModernNoAmbient.comp.slang:162,170`
  - ReProject(TAA) / compose：`src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.cpp:79-103`
