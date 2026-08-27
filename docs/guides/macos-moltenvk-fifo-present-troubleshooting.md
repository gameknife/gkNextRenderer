# macOS / MoltenVK FIFO 黑屏与闪烁排障

## 1. 适用范围与当前结论

本文记录 gkNextRenderer 在 macOS/MoltenVK 下使用 `VK_PRESENT_MODE_FIFO_KHR` 时，主场景随机闪烁或全黑、但 ImGui 仍可正常显示的问题。结论基于 2026-08-13 对当前代码、运行日志、逐 swapchain image 抓帧以及 MoltenVK 1.4.1 源码的交叉审计。

当前结论按优先级排列如下：

1. **主根因**：引擎把 onscreen swapchain image 长期绑定为 bindless storage image，并由 compute shader 直接写入；MoltenVK 1.4.1 没有正确失效 swapchain `VkImageView` 背后动态变化的 `MTLTexture` 缓存。FIFO 更容易令 descriptor 指向旧 drawable，当前 acquired image 因而只保留清屏后的黑色。
2. **独立同步缺陷**：场景的 compute/transfer 写入与 ImGui `LOAD` render pass 之间缺少覆盖真实生产者的 memory dependency。这可能放大闪烁，但不足以解释逐 imageIndex 稳定出现的全零帧。
3. **独立 CPU/GPU race**：`Scene::StartUpdateNodes()` 在等待上一帧 fence 之前写入若干单份 GPU 共享 buffer。该问题可能造成材质、光照或环境数据异常，但不是当前整屏黑的首要解释。
4. **已排除项**：主 UBO 的 imageIndex、UBO 数量、双缓冲改三缓冲都不是根因。三缓冲改善 FIFO pacing，但不能修复 drawable/descriptor 的物理资源失配。

当前代码在 Apple 平台强制使用 Immediate present mode。这是规避措施，不是最终资源模型：`Engine.cpp` 的 Apple display workaround 注释同时记录了 FIFO 三缓冲只改善 pacing、黑帧仍存在。

## 2. 主故障链

### 2.1 swapchain 被当作长期 storage image

可见窗口在 surface 支持时为 swapchain 请求 `VK_IMAGE_USAGE_STORAGE_BIT`：

- `src/Engine/Vulkan/SwapChain.cpp`：swapchain usage 选择；
- `src/Engine/Rendering/VulkanBaseRenderer.cpp`：`CreateRenderImages()` 创建资源时，将每张 swapchain `VkImageView` 一次性绑定到 `RT_SWAPCHAIN0 + imageIndex`；
- `src/Engine/Assets/GPU/Texture.cpp`：`BindStorageTexture()` 最终调用 `vkUpdateDescriptorSets()`；
- `src/Engine/Assets/GPU/DescriptorSystem.cpp`：该 binding 是全局 bindless storage-image 数组，使用 `PARTIALLY_BOUND` 和 `UPDATE_AFTER_BIND`。

这些 descriptor 在 swapchain 创建时写入。每帧 `WaitAndAcquire()` 只取得 `currentImageIndex`，不会在 acquire 后刷新相应的 swapchain storage descriptor。

### 2.2 场景输出与 ImGui 访问 swapchain 的方式不同

主场景最终输出的关键路径是：

1. 通过直接 image command 清除当前 acquired swapchain image；
2. 当 swapchain 支持 storage usage 时，tone-map compute shader 通过 `RT_SWAPCHAIN0 + imageIndex` 写入场景颜色；
3. ImGui 随后把当前 swapchain image 作为 framebuffer color attachment，以 `LOAD + STORE` 绘制 UI；
4. image 转为 present layout 后提交显示。

因此两类绘制虽然使用同一逻辑 `VkImage`，实际 Metal 资源解析时机不同：

- 场景 compute 写入使用早先写入 descriptor 的具体 Metal texture；
- 清屏、render-pass attachment 和 present 使用当前 acquired drawable。

当 descriptor 指向旧 drawable 时，场景被写到错误的 Metal texture；当前 drawable 仍是清屏后的黑色。ImGui 对当前 framebuffer attachment 的新写入仍然可见，于是形成“背景全黑但 UI 正常”的特征现象。

相关代码入口：

- `src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp`：当前 swapchain image 清屏；
- `src/Engine/Rendering/VulkanBaseRenderer.cpp`：最终输出选择 storage-image 或 intermediate+blit 路径；
- `assets/shaders/Process.TonemapAfterUpscaler.comp.slang`：tone-map storage image 写入；
- `src/Engine/Runtime/Editor/UserInterface.cpp`：ImGui framebuffer 与 `LOAD + STORE` render pass。

### 2.3 MoltenVK 1.4.1 的对应实现缺陷

MoltenVK 1.4.1 的 presentable image 会在 acquire 时释放旧 drawable，并通过 `CAMetalLayer.nextDrawable` 获取新的 `CAMetalDrawable`。新的 drawable 可以包含不同的 `MTLTexture`。另一方面，descriptor 更新会取得当时的 `MTLTexture`，并把 texture 或 `gpuResourceID` 编入 Metal argument buffer。

这与上游 [MoltenVK PR #2747](https://github.com/KhronosGroup/MoltenVK/pull/2747) 的描述完全一致：swapchain image 的底层 source 是动态的，`nextDrawable` 可能返回新的 texture，因此永久缓存 texture 不正确。该修复于 2026-07-19 合入。

可复核的 1.4.1 源码位置：

- [`MVKImage.mm`：acquire、drawable 与 texture 获取](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/MoltenVK/MoltenVK/GPUObjects/MVKImage.mm#L1537-L1608)；
- [`MVKDescriptorSet.mm`：image descriptor 编码 Metal texture](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/MoltenVK/MoltenVK/GPUObjects/MVKDescriptorSet.mm#L853-L974)；
- [`MVKImage.mm`：render-pass attachment 解析 texture](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/MoltenVK/MoltenVK/GPUObjects/MVKImage.mm#L2127-L2131)。

[MoltenVK 1.4.2 更新记录](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/Whats_New.md)同时列出：

- `Fix invalidation of cached metal textures`；
- `MVKRenderPass: encode barriers for subpass dependencies`。

这两项分别对应本文的主根因和次级 render-pass 同步风险。升级 MoltenVK 是必要的验证项，但引擎仍不应依赖 swapchain image 背后的 Metal texture 永久稳定。

## 3. 本地证据

### 3.1 黑帧在 present 前已经存在

FIFO、三张 swapchain image 的一次连续抓取中，共获得 15 帧：

- imageIndex 按 `1, 0, 2, 1, 0, 2...` 循环；
- 其中 13 帧像素严格全零；
- 仅两张正常帧都对应 imageIndex 2。

抓图 command 位于 ImGui delegate 之后、present 之前，直接从当前 swapchain image 复制。因此黑帧不是 CoreAnimation 丢弃 present，也不是显示器没有刷新；图像在提交 present 前已经是黑色。黑帧与固定 imageIndex 的关联也比“随机 UBO 内容错误”更符合 descriptor/drawable 失配。

### 3.2 三缓冲只改善 pacing

当前 Apple 路径将 FIFO swapchain 目标数量加深到三张。实测从两张改为三张后，帧率和 frame-time 波动明显改善，但黑帧仍存在。

MoltenVK 的 runtime guide 也建议在 Metal 上使用三张 concurrent swapchain image，以降低等待下一张 drawable 的延迟。该建议解决的是 availability 和 pacing，不建立逻辑 `VkImage` 与物理 `MTLTexture` 的永久一一映射。

## 4. 为什么不是 UniformBuffer

当前 frame/resource 模型为：

- `kFramesInFlight = 1`；
- frame fence、image-available semaphore 和 command buffer 按 frame slot 管理；
- render-finished semaphore 和主 UBO 按实际 swapchain image 数量创建；
- 等待上一帧 fence 后才 acquire image；
- UBO 更新发生在 acquire 之后，使用取得的 `imageIndex`；
- UBO 内存为 `HOST_VISIBLE | HOST_COHERENT`。

审计未发现 `currentFrame` 与 `currentImageIndex` 混用，也没有发现 CPU 在上一份主 UBO 仍被 GPU 读取时覆盖它。FIFO 当前为三张图，相关 vector 均按实际 `Images().size()` 创建，bindless slot 也覆盖第三张图。

所以：

- 增加 UBO 数量不会解决；
- 增加 frames-in-flight 不会解决；
- 双缓冲改三缓冲只能改善等待和 pacing；
- 增加并行帧反而会扩大 descriptor、共享 buffer 和 drawable 生命周期的复杂度。

## 5. 需要独立修复的同步问题

### 5.1 scene output 到 ImGui LOAD

主场景通过 compute storage write，或 fallback transfer blit，写入 swapchain。随后 ImGui render pass 对同一 color attachment 使用 `VK_ATTACHMENT_LOAD_OP_LOAD`。

当前 UI render pass 的 external-to-subpass dependency 使用：

- source stage：`COLOR_ATTACHMENT_OUTPUT`；
- source access：0；
- destination stage：`COLOR_ATTACHMENT_OUTPUT`；
- destination access：color attachment read/write。

它没有覆盖实际生产者 `COMPUTE_SHADER / SHADER_WRITE` 或 `TRANSFER / TRANSFER_WRITE`。而场景结束时转到 present 的 barrier 使用 `BOTTOM_OF_PIPE`、access 0，也不能独立建立生产者写入到 UI attachment read 的 memory visibility。

Vulkan 对 [`VK_ATTACHMENT_LOAD_OP_LOAD`](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentLoadOp.html) 的定义明确要求保留先前内容；对于 color attachment，它使用 `VK_ACCESS_COLOR_ATTACHMENT_READ_BIT`。

修复时应建立完整依赖：

```text
COMPUTE_SHADER / SHADER_WRITE
或 TRANSFER / TRANSFER_WRITE
    -> COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_READ | COLOR_ATTACHMENT_WRITE
```

UI 完成后，再建立 color attachment write 到 present 的布局和执行依赖。不要把 `PRESENT_SRC_KHR` 或 `BOTTOM_OF_PIPE` 当作自动完成内存可见性的同步原语。

### 5.2 fence 之前写单份 GPU 共享 buffer

主循环先调用 `Scene::StartUpdateNodes()`，之后才进入 `DrawFrame()`；上一帧 submit fence 的等待位于 `FrameSubmission::WaitAndAcquire()` 内。因此 `StartUpdateNodes()` 中以下行为可能和上一帧 GPU 读取重叠：

- 更新单份 light buffer；
- 读取并清零 mapped GPU shared dynamic stats；
- dirty 时覆盖 material dynamic buffer；
- 更新部分 ambient arena/page-table 数据；
- 某些资源替换或销毁路径。

这是一处真实 CPU/GPU race。修复原则是：`StartUpdateNodes()` 只能启动纯 CPU 工作；所有 GPU-visible host write、GPU resource replacement 和 destruction 必须移动到本 frame fence 已完成之后。

## 6. 一锤定音的验证顺序

### A. 强制 intermediate + blit

在 Apple onscreen swapchain 上临时移除 `VK_IMAGE_USAGE_STORAGE_BIT`，复用现有 `RT_TONEMAP_INPUT + vkCmdBlitImage` fallback：

```text
tone-map compute -> per-image intermediate texture
                 -> vkCmdBlitImage(current acquired VkImage)
                 -> ImGui
                 -> present
```

保持 FIFO、三张 swapchain image 和其他设置不变。若场景与 ImGui 都稳定，便可确认 direct-storage descriptor/drawable 是决定性变量。

### B. acquire 后刷新当前 descriptor

作为诊断探针，在 `WaitAndAcquire()` 成功、上一帧 fence 已完成之后，重新写入 `RT_SWAPCHAIN0 + currentImageIndex` descriptor，再录制 command buffer。

若 FIFO 立即稳定，同样可以确认旧 descriptor 捕获了错误 Metal texture。该方法适合诊断；正式实现仍推荐 intermediate+blit，以避免依赖 MoltenVK drawable 内部行为。

### C. 版本与路径矩阵

至少执行以下矩阵，SDR 和 EDR 各一轮：

| MoltenVK | 最终输出 | Present mode | 预期 |
| --- | --- | --- | --- |
| 1.4.1 | direct swapchain storage | FIFO | 可复现黑帧 |
| 1.4.1 | intermediate + blit | FIFO | 应稳定 |
| 1.4.2 | direct swapchain storage | FIFO | 用于判断上游修复覆盖程度 |
| 1.4.2 | intermediate + blit | FIFO | 应稳定，推荐基线 |

如需 Metal capture，可比较同一帧中 tone-map compute 写入的 `MTLTexture` 与随后 ImGui framebuffer attachment 的 `MTLTexture` 或 IOSurface identity。坏帧中两者预期不一致。

## 7. 推荐修复顺序与长期约束

1. Apple onscreen swapchain 禁用 direct storage 输出，固定走 per-image intermediate + blit。
2. 将运行时升级到 MoltenVK 1.4.2 或更新版本并完成上述矩阵复测。
3. 补齐 scene compute/transfer 到 ImGui attachment `LOAD` 的显式同步。
4. 将 GPU-visible shared-buffer 写入和相关资源生命周期操作移动到 frame fence 之后。
5. 保持 Apple FIFO 三缓冲，用于 pacing；不要把 image count 当作 correctness 修复。
6. 在回归测试中记录 `presentMode`、实际 swapchain image count、`imageIndex` 和最终输出路径，避免 Immediate 模式再次掩盖错误。

长期不变量是：**swapchain image 是 WSI 拥有的动态呈现资源，不应假设其平台底层 texture identity 在多次 acquire 之间保持不变。** 对平台后端最稳健的做法，是让 compute/post-process 写入引擎拥有的 per-image 中间图，再通过 Vulkan image command 写入当前 acquired swapchain image。
