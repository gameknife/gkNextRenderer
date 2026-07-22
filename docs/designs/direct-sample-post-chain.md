---
title: "直接样本后处理与 Upscaler 输入链"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-20
last_updated: 2026-07-22
---

# 直接样本后处理与 Upscaler 输入链

PathTracing、SoftwareTracing 和 SoftwareModern 共用
`PipelineCommon::SamplePostChain`。实时路径不做颜色 history reprojection，也不运行引擎内
spatial denoiser；renderer 生成的当前帧 diffuse/specular 样本直接 compose 成 scene color，
随后交给 DLSS、FSR 或 native resolve。

## 当前链路

1. renderer 写 `RT_SINGLE_DIFFUSE`、`RT_SINGLE_SPECULAR`、`RT_ALBEDO` 和 upscaler 所需
   G-buffer/motion/hit-distance。
2. PathTracing 或 SoftwareTracing 开启 ReSTIR 时，分别由 `Core.RestirSpatialShade` 或
   `Core.SwRestirSpatialShade` 先把面光直接项加入 `RT_SINGLE_DIFFUSE`。两者共享 reservoir
   和算法主体；ReSTIR 时域复用是光照采样器自身状态，不是颜色历史。
3. `Process.Compose.comp.slang` 直接读取 single diffuse/specular 与当前 albedo，叠加编辑器
   outline，输出 HDR 编码或 SDR tonemap 后的 `RT_SCENE_COLOR`。
4. DLSS、FidelityFX FSR 3.1、Native TAAU、SGSR2 或 native presentation 统一消费
   `RT_SCENE_COLOR`。DLSS Ray Reconstruction 的
   noisy diffuse/specular resource 直接绑定 `RT_SINGLE_DIFFUSE/SPECULAR`，不经过引擎滤波。

Tracing renderer 的 direct-lighting 通道语义是强契约：`RT_SINGLE_DIFFUSE` 保存
albedo-demodulated diffuse lighting，compose 时才乘 `RT_ALBEDO`；`RT_SINGLE_SPECULAR` 保存已经
包含 Fresnel、metal tint、BSDF 与 emitter radiance 的完整 specular radiance，compose 不再乘
base color。ReSTIR 第二阶段只能累加前者，不能覆盖主 pass 已写入的 glossy/delta direct。

FidelityFX temporal FSR 与 Native TAAU 可在 evaluate 后运行同一层显示分辨率的可选后处理：upscaler 先写入
每个 swapchain image 独立的 ping/pong intermediate，随后 firefly neighborhood clamp 与
以当前帧 albedo/normal 引导的多 pass a-trous 滤波写回 swapchain。该阶段只清理 upscaler
输出中的孤立亮点和高频残余噪声，不参与 renderer
样本生成、历史重投影或 DLSS 路径。

`SoftwareModernNoAmbient` 和 `VoxelTracing` 有自己的 compose shader，但输出契约同样是
`RT_SCENE_COLOR`，因此后续 presentation 不需要知道 renderer 类型。

PathTracing、SoftwareTracing、SoftwareModern 和 SoftwareModernNoAmbient 都声明通用 upscale
能力。只有 PathTracing 声明 Ray Reconstruction contract；在其他 renderer 选择 type 2 时，
该类型不可用并回到 native rendering，不会隐式改成另一种 upscaler。

## 唯一保留的颜色累积

显式 offline progressive rendering 仍需要多帧收敛。该模式只运行
`Process.ProgressiveAccumulate.comp.slang`，在 `RT_PROGRESSIVE_DIFFUSE/SPECULAR` 中做原位
running average，然后由同一个 compose pass 读取。它不读取 motion、ObjectId 或上一帧
投影，不是 reprojection，也不参与实时路径。

## 已删除资源与接口

每个 RenderView 不再拥有 `TemporalResolve` 或 `AtrousDenoiser`。下列全分辨率 image slot
及其生命周期已经删除：三张 diffuse/specular/albedo color history、六张 a-trous
ping/pong/output，以及 accumulate albedo。相关 denoiser/reprojection CVar 和 UI 也不存在。

`RenderView::historyGeneration` 仍保留，服务 camera cut、renderer/scene/extent 切换时的
previous UBO、ObjectId/depth 与 ReSTIR reservoir 失效；它不代表存在颜色 history。

## 修改护栏

- 新 renderer 应输出 single lighting + current G-buffer，再复用 `SamplePostChain`；不要在
  renderer 内重新建立一条颜色历史链。
- upscaler 的 reset、jitter、motion-vector 和 exposure 契约由 upscaler integration 管理；
  不要为了“加强 DLSS”再叠一层引擎 TAA/reprojection。
- FSR 输出滤波必须保持无历史、只读本帧 FSR intermediate，并在显示分辨率执行；不要将它
  扩展成另一条 temporal accumulation 链。
- realtime scene color 必须来自当前样本。只有用户明确进入 offline progressive 模式时才
  允许读取 `RT_PROGRESSIVE_*`。
- temporal upscaler provider 必须实现现有 `IUpscaler`/scene-color 契约，不应要求 renderer
  恢复旧 storage。当前 `NextStreamline` 与 `NextFidelityFX` 互斥注册同一个 provider factory。
- `RT_MOTIONVECTOR` 的全引擎契约是 render-pixel 单位；Streamline 与 FidelityFX 都提交
  1:1 motion-vector scale。renderer 不得单独写 normalized UV motion。
