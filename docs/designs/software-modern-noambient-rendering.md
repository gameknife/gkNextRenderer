---
title: "SoftwareModernNoAmbient 渲染与 GTAO"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-17
last_updated: 2026-07-17
---

# SoftwareModernNoAmbient 渲染与 GTAO

`SoftwareModernNoAmbient` 是不依赖 VoxelData、AmbientCube 或 TLAS 的轻量 deferred 路径。它保留 visibility、PBR 直接光、IBL、CSM、屏幕空间 GTAO 与 HDR/SDR 输出，但当前没有引擎时序累积。旧文档曾同时记录已实现的 GTAO、后来移除的 `ReProjectSimple`，以及未落地的体素天光遮蔽；本文只描述当前代码。

## Contract 与使用边界

精确 contract 定义在 `VulkanBaseRenderer.cpp` 的 `RendererDescriptors`：

- scene resources 为 `None`，prepass 为 Cull、Clear、Visibility、CSM；
- 输出 Color、Depth、Motion、ObjectId、Normal；
- 允许 spatial upscale 与 debug G-buffer，不允许 temporal post-process；
- history channel 为 `None`；
- `supportsSceneOverrideWithoutPrepare=true`，所以材质预览等独立小 Scene 可以安全复用它。

“无 Ambient”指不使用 AmbientCube GI，不表示没有环境光。该路径仍采样 sky IBL，也不表示没有 AO；近中景天光遮蔽由 GTAO 提供。

## 当前 pass 顺序

该 renderer 只有 [Primary Surface 路径](visibility-surface-gbuffer-shading-scheduler.md)一条路线
（`r.surface.build` / `r.surface.scheduler` / `r.gtao.applyInCore` 开关与迁移前的单体入口
`Core.SwModernNoAmbient` 都已删除）：

1. `Core.SurfaceBuild.comp.slang` 从 visibility buffer 全率解析出 dense G-buffer。
2. `Task.ShadingClassify` + `Task.ShadingClassifyFinalize` 把屏幕分成 8×8 tile 的 Background /
   Emissive / Standard 三个桶。
3. 若 `r.gtao.enable`，`Core.GTAO.comp.slang` 在 half-resolution `R16_SFLOAT` 的 `RT_GTAO` 上计算
   horizon visibility。它排在着色**之前**——读的 depth/normal 来自 Build 而不是着色输出。
4. 每个桶一次 indirect dispatch（`Core.SwModernNoAmbient{Standard,Background,Emissive}`），光照
   逻辑都在 `common/NoAmbientShading.slang`。Standard kernel 用 3×3 depth/normal joint bilateral
   把 AO 上采样到全分辨率，并把它乘进自己写出的 `RT_AMBIENT`。
5. `Process.GTAOCompose.comp.slang` 把两个光照通道相加（AO 已经在里面了），画
   selection/hover/lock/danger outline，并编码 HDR 或做 SDR tonemap 到 `RT_SCENE_COLOR`。

着色阶段有意把光照拆开：

- `RT_SINGLE_DIFFUSE` 保存太阳直接光、specular 与 emissive；
- `RT_AMBIENT` 只保存 sky diffuse；
- Standard shading kernel 只对 `RT_AMBIENT` 乘 `pow(saturate(ao), strength)`，compose 只做相加。

不要在 GTAO pass 后对总颜色整体相乘；那会把已有 CSM 的太阳光再次压暗，也会错误遮蔽 specular 和 emissive。若增加新的环境光项，必须先判断它属于可遮蔽的 sky diffuse，还是应留在不受 GTAO 影响的直接/自发光通道。

## GTAO 采样语义

GTAO 从 `RT_PREV_DEPTHBUFFER` 的 NDC depth 和 `RT_NORMAL` 的 world normal 重建 view-space position/normal。每个 half-resolution 像素在 2×2 full-resolution footprint 中轮转代表样本，并用 frame/pixel noise 旋转切片；世界半径投影到屏幕后钳制到 2..64 pixels。

`r.gtao.quality` 对应的 slice × step × two-side 样本数是：

| Quality | Slices | Steps | Taps |
| --- | ---: | ---: | ---: |
| 0 | 2 | 4 | 16 |
| 1 | 3 | 6 | 36 |
| 2 | 4 | 8 | 64 |
| 3 | 6 | 10 | 120 |

distance falloff 限制世界半径，thickness heuristic 降低深度断层造成的 halo。shader 输出原始 visibility；用户 strength 只在 `GTAOSkyOcclusion`（Core Shading 调用）应用一次。不要在采样 shader 和着色/compose 重复加权。

## CVar 与调试

Engine 注册默认值是：`r.gtao.enable=true`、`quality=1`、`radius=1.0`、`strength=5.0`、`thickness=0.5`、`debugMode=0`。用户配置和 application override 可以改变实际值，因此排障时应读取当前 CVar，不要只看 `UserSettings` 成员初始化器。

`debugMode=1` 显示原始 AO visibility，`debugMode=2` 显示未遮蔽的 sky diffuse。前者用于查采样/上采样，后者用于查光照拆分；二者都不是最终 tonemapped 画质基准。

## 明确没有时序 history

当前 renderer 不创建或复制 color/AO history。`NextEngine::GetUniformBufferObject()` 对该 renderer 禁用普通 TAA jitter，并把 `TemporalFrames` 设为 1。Motion output 仍是 upscaler contract 的有效输出，但不能据此宣称 NoAmbient 正在执行引擎 TAA。

若未来加入新的时序 AA/AO，必须由 upscaler contract 或独立、明确的 feature design 定义资源与生命周期，不能恢复已删除的通用颜色 history。通用约束见 [直接样本后处理与 Upscaler 输入链](direct-sample-post-chain.md)。

## 修改与验证

修改这条路径时至少对比 GTAO 开/关、四档 quality、两个 debug mode、sky/sun/emissive 分离、CSM、HDR/SDR、材质 preview SceneOverride 和两个不同相机。屏幕空间 AO 不能表示屏外或大尺度封闭关系；不要用增加 radius/strength 掩盖这一边界，也不要把未实现的 voxel sky visibility 写成现有能力。

按 Engine 范围构建 `gkNextRenderer gkNextUnitTests`，用 `gnb shot` 检查静态画面；代表样本会随帧轮转，画质修改还应观察连续帧和相机运动，而不是只看一张稳定帧。
