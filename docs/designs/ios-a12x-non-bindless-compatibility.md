---
title: "iOS A12X Non-Bindless 兼容路径：适配复盘与重构设计"
category: design
status: 根因复盘；目标已由 ios-a12x-compatibility-minimal-render-mvp.md 取代（其 M0 已实施）
owner: engine
created: 2026-08-22
last_updated: 2026-08-22
---

# iOS A12X Non-Bindless 兼容路径：适配复盘与重构设计

## 1. 结论先行

这次 iPad Pro（Apple A12X GPU）适配暴露的不是一个单独的 shader bug，而是当前引擎把三件不同的事情混在了一起：

1. Vulkan descriptor indexing 是否可用；
2. MoltenVK 是否能把当前 Slang 的 typed bindless arrays 翻译成有效的 Metal argument buffer；
3. gkNextEngine 是否真的拥有一个不依赖全局 bindless 资源池的兼容 renderer。

这台机器可以运行一部分 descriptor-indexing 和 Metal argument-buffer 能力，但不能满足当前完整 bindless renderer 的资源契约。临时增加 12 个 sampled、8 个 storage、16 个 sampler 的“constrained bindless”只能证明某些简化 shader 可以启动，不能把它当成 non-bindless 架构。

如果要把这次改动整体回滚，推荐重新设计一个明确的 `NonBindless`/`Compatibility` backend：由它拥有固定 descriptor set、显式的 per-pass 资源绑定和有限的 SoftwareModernNoAmbient 功能面；完整 bindless renderer 与兼容 renderer 在资源创建、shader、材质绑定和 render-target 生命周期上分开，而不是在 `VulkanBaseRenderer` 里动态缩小同一套全局数组。

## 2. 目标设备的已知能力

本次测试设备和驱动日志如下：

- GPU：`Apple A12X GPU`；
- Vulkan：`1.2.334`；
- MoltenVK：`1.4.1`；
- Metal texture slots：`96`；
- Metal storage texture slots：`96`；
- Vulkan per-stage descriptor limits：sampled/storage/sampler = `96/96/16`；
- Vulkan descriptor-set limits：sampled/storage/sampler = `480/480/80`；
- 当前可用 profile：`constrained`，不是 full bindless。

> **更正（2026-08-22）**：上面两行是**基础**上限，不是本引擎的判据。bindless set 的每个 binding 都带
> `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`，规范把这类 binding 排除在 `maxPerStageDescriptor*`
> 之外，改为按 `VkPhysicalDeviceDescriptorIndexingProperties` 的 update-after-bind 上限校验。按基础
> 上限判断会误降级每一台 Apple GPU——M3 Max 同样只报 `maxPerStageDescriptorSamplers = 16`，却能创建
> 完整的 17,506 descriptor layout（其 update-after-bind 上限是 1000000/500000/1000000）。
> A12X 的真实约束需要重新按 update-after-bind 上限测一次。

这里最容易误判的是：Metal 侧有 96 个 texture slot，并不表示一个使用大量不同 typed `StorageTextureArray` 的 Slang shader 就能安全运行。MoltenVK 还要把 Vulkan/Slang 的资源类型、访问权限和数组布局翻译成 Metal 函数参数表。

Apple 的官方能力表把 A12 系列归到 Apple GPU Family 5，并列出 `R32Uint` 的 Color Write 能力；因此“这台机器完全不支持 u32 格式”不是正确结论。[Apple Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)

但是，Color Attachment、Storage Image、Sampled Image 和 Read/Write Texture 是不同的能力维度。以后必须对每一种实际用法分别通过 Vulkan format probing 验证，不能因为 `R32_UINT` 可以作为 color attachment，就推断它一定可以作为任意类型的 storage image 或 sampled image。

## 3. 故障链

### 3.1 完整 bindless 资源契约超过了设备的可观测下限

当前引擎的完整资源模型包含：

- 大型全局 sampled texture array；
- 大型全局 storage image array；
- sampler array；
- RenderView 的 RT bank；
- visibility、G-buffer、temporal、tone-map、shadow 和其他模块资源共用的 bindless 地址空间；
- shader 中大量通过 `Bindless.GetStorageTexture<T>()` / `GetSampleTexture()` 动态索引的资源。

A12X 报告的最小 per-stage 资源上限是 sampler `16`，最小 descriptor-set 上限是 `80`。这不足以满足完整 renderer 的固定 descriptor layout。因此设备必须在资源创建和 renderer 选择之前被分到兼容 backend，而不是先按完整 renderer 创建，再在后面强制切换逻辑 renderer。

### 3.2 “constrained bindless”仍然是 bindless

本次临时路径把全局容量缩小到 sampled `12`、storage `8`、sampler `16`，并继续使用 `GlobalTexturePool`、全局 storage array 和 shader 内的动态索引。这解决了容量的一部分问题，但仍然继承了 bindless 的关键风险：

- shader resource type 由 `.as<RWTexture2D<T>>()` 决定；
- 同一套 descriptor binding 可能被不同 `T` 解释；
- pipeline 是否能被 Metal 编译取决于最终生成的 MSL 参数数组；
- 材质纹理仍会因为全局容量耗尽而回落到固定槽位；
- renderer contract 仍然假定许多 G-buffer/storage 资源存在。

因此它是“紧缩版 bindless”，不是 non-bindless 兼容路径。未来设计不能只继续调小数组。

### 3.3 `texture(12)` 冲突的真正原因

最初的 constrained visual-debug shader 同时访问了不同 typed storage image，例如 float、float4、uint 或不同访问权限的 storage texture。Slang/MoltenVK 将它们翻译成多个 Metal texture arrays，但这些数组都尝试从同一个 argument-buffer texture location 开始，例如：

```text
array<texture2d<float, access::write>, 8> StorageTextureArray [[texture(12)]]
array<texture2d<float>, access::read>, 8> StorageTextureArray_1 [[texture(12)]]
```

Metal 不允许这些资源数组在同一函数参数表中重叠，所以 pipeline 创建失败并报告：

```text
cannot reserve 'texture' resource locations at index 12
```

这不是 `R32_UINT` 本身导致的格式拒绝，也不是 shader 中某个 ID 超出范围。根因是“一个全局 typed storage array 被多个互不兼容的资源类型解释”，再叠加 MoltenVK 对 argument-buffer resource location 的约束。

后来把所有访问统一为 `RWTexture2D<float4>` 后，SPIR-V/Metal 只剩一个 storage array，pipeline 可以创建。这证明了冲突来源，但也只是在语法和资源类型层面绕开了问题。

### 3.4 `float16` 不能作为 visibility ID 存储

临时版本把 visibility attachment 改为 `R16G16B16A16_SFLOAT`，目的是让 debug shader 中所有 storage texture 都成为 float4。这不满足 visibility buffer 的数据契约：

- binary16 只有 11 位有效精度（含隐藏位）；
- `0` 到 `2048` 的整数可以逐个精确表示；
- 更大的整数会出现舍入，间隔逐渐变成 2、4、8……；
- 不能把它当作可逆的 32-bit instance/triangle ID。

因此 float16 只能用于临时显示颜色，不能用于保存 visibility ID，也不能让后续 Surface Build、material lookup 或 debug tooling 依赖这种编码。

### 3.5 `R32_UINT` 实际上可以走 color attachment 路径

最后一次实机验证使用了：

- instance visibility：`R32_UINT` color attachment；
- triangle visibility：`R32_UINT` color attachment；
- 第三个 attachment：`R16G16B16A16_SFLOAT` debug color；
- debug color 在 visibility fragment shader 中直接生成，不再由 compute shader 同时读取 uint、写 float storage image。

该版本在 A12X 上成功创建 render pass、framebuffer 和 graphics pipeline，场景提交后稳定运行约 30 秒，没有出现：

- `VK_ERROR_FORMAT_NOT_SUPPORTED`；
- `cannot reserve texture resource locations at index 12`；
- `ERROR_DEVICE_LOST`。

这说明 A12X 的 `R32_UINT` color-attachment 路径可用。它尚不能证明所有 `R32_UINT` storage read/write 组合都可用；这个结论必须由独立的 format/usage probe 得出。

## 4. Visibility Buffer 数据契约

当前普通 visibility layout 是：

- instance plane：`R32_UINT`；
- triangle plane：`R16_UINT`；
- 背景值：`0`；
- instance ID 通常是 one-based render-proxy index；
- shader 侧统一解码为 `uint2`。

对 compatibility backend，建议先统一为两个 `R32_UINT` plane。这样可以：

- 让 instance 和 triangle 都有清晰的 32-bit 上限；
- 避免兼容 shader 需要在同一 pass 中处理不同整数位宽；
- 让 Visual Debug、readback、验证脚本和后续显式 descriptor pass 使用同一个数据格式；
- 在 A12X 上以 color attachment 写入时，避开对 typed storage-image read/write 的额外依赖。

如果未来确认 triangle index 始终严格小于 `65536`，普通 full bindless 路径仍可以保留 `R16_UINT` 以节省带宽；但 compatibility backend 不应为了节省一半 attachment 大小而牺牲可诊断性。

## 5. 重新设计的目标架构

### 5.1 在设备选择阶段确定 backend

建议引入明确的 renderer resource mode，例如：

```text
FullBindless
NonBindlessCompatibility
Unsupported
```

选择顺序必须是：

1. 枚举物理设备；
2. 查询 descriptor indexing features 和 descriptor limits；
3. 对 renderer 实际需要的 image format/usage 做 probe；
4. 确定 resource mode；
5. 创建与该 mode 对应的 descriptor managers、resource pools、pipelines 和 renderer；
6. 之后不再在同一个设备生命周期内把 FullBindless renderer 强行替换成 compatibility renderer。

FullBindless 的判定不能只看 `runtimeDescriptorArray` 或 `descriptorSetArgumentBuffers`；必须同时满足：

- descriptor indexing feature 完整；
- sampled/storage/sampler 的 descriptor 数量满足完整 layout；
- 关键 image format 对实际 usage 可用；
- 代表性 bindless shader 能在目标后端完成 pipeline 编译。

### 5.2 NonBindless backend 不再拥有全局 typed texture array

兼容 backend 应该有独立的固定 descriptor set 设计，例如：

```text
set 0: Frame UBO / camera / per-frame constants
set 1: Scene SSBO / node / mesh / indirect draw data
set 2: Current pass input attachments and output images
set 3: Current material constants + a small fixed texture set
```

关键原则：

- 不依赖 `GlobalTexturePool` 的大数组；
- 不在 shader 中通过任意整数索引全局纹理；
- 不使用 `Bindless.GetStorageTexture<T>()` 作为兼容 backend 的主要资源接口；
- 每个 pass 只声明它确实需要的资源；
- 材质纹理按 material/pass 显式绑定，超出固定数量时使用 material fallback 或 CPU 分批，而不是静默覆盖 bindless 槽位；
- storage image 的 typed 资源在一个 pipeline 内保持单一、可审计的类型。

这才是 non-bindless，而不是把 bindless 数组从 16384 改成 12。

### 5.3 兼容 renderer 的第一阶段功能面

第一阶段不应试图让 A12X 运行完整 PathTracing、SoftwareTracing、AmbientCube、Surface Build、temporal upscaling 和所有 debug G-buffer。推荐只支持：

- `SoftwareModernNoAmbient` 的固定 compatibility variant；
- CPU/GPU scene buffers；
- 基础 raster visibility/depth；
- 简化的 Lambert + IBL/CSM；
- 一个显式绑定的 scene color output；
- Visual Debug 和基础截图/退出控制。

先保证“能启动、能提交场景、能画出颜色、能显示 ID debug、能正常 present”，再逐项增加材质、阴影和后处理。

### 5.4 Visibility 和 Visual Debug 的推荐实现

兼容 backend 有两种安全方案，优先级如下。

#### 方案 A：visibility pass 直接写 debug color

visibility graphics pass 使用三个 color attachment：

1. `R32_UINT` instance ID；
2. `R32_UINT` triangle ID；
3. float scene/debug color。

fragment shader 从当前 primitive 的 uint ID 计算稳定颜色，ID 与颜色在同一个 draw 中写出。这样：

- ID 不经过 float 转换；
- 不需要 uint storage-image read；
- 不需要 float/uint typed bindless array；
- Visual Debug 不增加 descriptor 数量；
- 兼容设备只需支持 R32Uint color write 和普通 float color write。

这是最适合第一阶段 bring-up 的方案。

#### 方案 B：显式 descriptor 的独立 debug pass

如果需要像桌面 Visual Debug 一样对 visibility plane 做缩放、分屏和采样，则：

- visibility plane 以明确的 `Texture2D<uint>` descriptor 传给 debug pass；
- debug output 以明确的 float storage image 或 color attachment 传出；
- 不使用同一个全局 bindless binding 同时承载 float 和 uint typed arrays；
- 若 A12X 的 `R32_UINT` storage read/write probe 失败，就改用 graphics input/sampled path，不强行使用 storage image。

方案 B 应在方案 A 已稳定后再做，不应作为兼容 backend 的启动前置条件。

## 6. 需要从当前临时改动中保留和删除的内容

### 可以保留为知识或测试的内容

- A12X 的能力日志和 descriptor limit 记录；
- full bindless / compatibility / unsupported 的能力分类思路；
- `R32_UINT` color attachment 的实机验证；
- Visual Debug 菜单和 F2 绑定到同一个 Debug Layer 开关；
- iOS build/install/run 的设备编号流程；
- 对 MoltenVK typed array 冲突的启动回归测试；
- “场景提交后继续运行一段时间且无 `DEVICE_LOST`”的设备 smoke test。

### 不建议作为最终架构保留的内容

- 在 `VulkanBaseRenderer` 中把完整 bindless 资源池动态缩小成 12/8/16；
- 用 fallback slot 掩盖 sampled texture 容量耗尽；
- 为 constrained mode 重复定义一套与普通 RT slot 数字重叠的全局地址空间；
- 用 float16 保存 instance/triangle ID；
- 在同一全局 storage binding 中混合 `RWTexture2D<float>`、`RWTexture2D<float4>`、`RWTexture2D<uint>`；
- 在设备资源已经创建后再强制切换逻辑 renderer；
- 为了让 debug 画面出现而让正常兼容路径每帧执行 visibility/cull/debug 全链路；
- 把“能创建 pipeline”当成“完整 renderer 资源契约成立”。

如果要回滚当前试验性实现，建议整体回滚这次 compatibility 代码和 shader variant，而不是只删除某个 `R16G16B16A16_SFLOAT`。否则可能留下不匹配的 slot constants、descriptor pool 容量、renderer contract 或 shader asset。

## 7. 分阶段重构建议

### M0：能力探针和格式契约

- 增加只读的 device capability report；
- 分别 probe `R32_UINT` 的 color attachment、storage image、sampled image usage；
- probe `R16G16B16A16_SFLOAT` color/storage usage；
- 把 probe 结果写入启动日志和一个可选 JSON report；
- 不改变当前 renderer 行为。

### M1：NonBindless resource/context 骨架

- 新增 compatibility resource context；
- 固定 descriptor set layout；
- 显式绑定 frame/scene/material/pass 资源；
- compatibility backend 不创建完整 `GlobalTexturePool` bindless arrays；
- 用一个纯色 triangle/mesh pass 验证 descriptor、render pass 和 present。

### M2：SoftwareModernNoAmbient compatibility variant

- CPU scene upload 与 indirect draw；
- depth/color pass；
- 固定材质纹理组；
- 基础 Lambert/IBL/CSM；
- 不接入 upscaler、Surface Build、AmbientCube 和完整 Visual Debug G-buffer。

### M3：Lossless visibility 和 debug

- 两个 `R32_UINT` visibility attachments；
- 首选方案 A 直接输出 debug color；
- 再实现方案 B 的显式 uint debug sampling；
- 运行 ID round-trip/readback 测试，验证高于 `2048`、`65535` 的边界值不会被转换或截断。

### M4：逐项扩大兼容功能面

- 按 capability matrix 增加更多材质纹理；
- 加入明确的 material batching/fallback；
- 再评估 temporal/upscaler、G-buffer debug 和 RenderView；
- 每新增一个功能都必须保持 compatibility backend 不依赖全局 bindless array。

## 8. 验收矩阵

### Desktop full bindless

- 当前完整 renderer 行为不变；
- 普通 visibility contract 和 Surface Build 回归通过；
- full bindless shader 编译和 visual test 通过。

### A12X non-bindless

- 不创建大型 sampled/storage/sampler arrays；
- 不依赖 descriptor indexing 才能启动；
- `conf_room.glb` 能提交；
- 默认 compatibility color 能 present；
- Visual Debug 能看到稳定颜色；
- visibility instance/triangle 高位 ID round-trip 正确；
- 运行至少 60 秒无 pipeline creation error、validation error、`DEVICE_LOST`；
- `gnb ios run --device 2` 可重复部署。

### 明确失败的设备

如果设备连 compatibility backend 所需的固定 descriptor、R32Uint color attachment 或基础 storage/transfer usage 都不满足，应明确报 unsupported GPU，而不是再继续缩小 bindless 数组或静默降低 ID 精度。

## 9. 复盘后的核心原则

1. **Non-bindless 是独立资源模型，不是小容量 bindless。**
2. **格式能力必须按 usage 分别探测。** `R32_UINT` color write 可用，不等于所有 storage read/write 组合可用。
3. **Visibility ID 永远保持整数。** 颜色可用 float，ID 不可用 float16 代替。
4. **一个 Metal pipeline 中的 typed resource array 必须可预测且同质。** 不要让多个 `T` 通过同一个 global storage binding 自由转换。
5. **兼容 backend 先收窄功能契约，再逐步扩展。** 先保证一条可验证的 SoftwareModernNoAmbient 路径，而不是让所有 renderer 共享一套不断增加条件分支的基础设施。
6. **设备模式必须在资源创建前决定。** renderer、descriptor manager、shader 和 render target 必须从同一个 capability decision 派生。

