---
title: "Gaussian Splat / SOG v2 格式与集成"
category: design
status: 现行
owner: SplatLoader
created: 2026-06-18
last_updated: 2026-07-17
---

# Gaussian Splat / SOG v2 格式与集成

本文保留旧 SOG 开发计划中仍是当前兼容性契约的部分。当前实现位于可选模块 `src/Modules/SplatLoader/`；Engine 核心只提供 component/render-pass 接缝，不拥有 SOG 解析器。

## 模块边界

`Modules::Splat::Install()` 安装 CVar、注册 `GaussianSplatComponent` 反射，并调用 `Register()`。后者注册：

- `.sog` scene loader；
- priority 0 的 `GaussianSplatPass` external content pass，位于 primary view 后、debug overlay 前。

注册在 application/test 装配阶段发生。不要把 `.sog` 特判加回通用 Scene loader，也不要让 Engine 链接 `SplatLoader`。

## SOG v2 输入契约

正常入口是包含 `meta.json` 与 WebP 条目的 ZIP `.sog`。ZIP reader 当前只支持 stored（method 0）和 deflate（method 8）；不支持的压缩方式会失败。底层 file-set 也能从输入旁边读取 loose members，但 registry 只公开 `.sog` 扩展名。

`meta.json` 必须满足 `version == 2`，且 `count` 在 `1..UINT32_MAX`。必需数据：

- `means.files[0..1]`：low/high WebP 的 RGB channel 拼成 16-bit 值，经 `mins/maxs` 反量化后使用 signed exponential `sign(x) * (exp(abs(x)) - 1)` 解码位置。
- `scales`：RGB index 查 256-entry log-scale codebook，再 `exp` 为三个轴的 scale。
- `quats`：RGB 保存三个分量，alpha tag 必须在 252..255，tag 决定省略的 `w/x/y/z` 分量；缺失分量由单位四元数约束恢复。非法 tag 当前退回 identity。
- `sh0`：RGB index 查 256-entry codebook，alpha 为 opacity。scale 和 sh0 codebook 都必须恰好 256 项。

可选 `shN` 将 bands clamp 到 0..3，对应每 splat 0/3/8/15 个系数。centroid palette 的宽度必须是 `64 * coefficientCount`，label 使用两字节 palette index，越界 label 回退到 0。

所有 WebP 至少要覆盖 `count` 个像素。解析失败会记录 `failed to load SOG` 并返回 false，不允许用部分 splat 静默成功。

## 坐标与 covariance

SOG 到引擎的 basis 是 `diag(-1,-1,1)`。位置直接乘该 basis；椭球不能只转换 quaternion，而是先由 rotation + exp(logScale) 构造 covariance，再做：

```text
C_engine = B * C_sog * transpose(B)
```

这样可避免 handedness/basis 变化导致椭球方向错误。加载后 `shBasisFlipXY=true`，shader 在评估 SH 时使用同一 basis 约定。

每个 splat 在 GPU 侧是一个 80-byte AoS `FGaussianSplatGpu`：position/opacity、两段对称 covariance、SH0 和 metadata。旧计划中的 SoA/projected-splat 中间布局不是当前事实。

## Scene 产物

loader 根据 `3σ` covariance extent 建 AABB，生成一个默认 orbit-friendly camera，关闭默认 sun/sky，并创建带 `GaussianSplatComponent` 的 scene node。组件持有共享、不可变的 `FGaussianSplatData`，每 node 的 visible、opacity、raycast、shadow、ray occlusion、receive lighting 和 proxy threshold 是独立可反射状态。

`SplatProxyBuilder` 额外生成隐藏的 triangle proxy。它不是 billboard 的视觉替代，而是让普通引擎路径能参与：

- CSM cast shadow；
- hardware/software ray occlusion；
- ray picking 与 bounds；
- 需要 mesh/voxel 表示的 GI 路径。

修改 proxy 参数时必须同时验证可见 billboard 和隐藏 proxy 的一致性；只看正面颜色无法发现 shadow/occlusion 回归。

## 渲染数据流

`GaussianSplatPass` 要求当前 renderer contract 提供 Color + Depth，产生更新后的 Color。缺少输出时外部 pass 会被 renderer 跳过。

1. 收集当前 scene 中有数据的 splat components，组合 immutable splat/palette buffer 和 per-model state。
2. compute histogram → group scan/prefix → scatter，得到远到近的 sorted index 与 indirect draw；相机和 model state 未变化时可复用 sort cache。
3. instanced billboard 使用 scene depth test、不写 scene depth，累积到专用 splat RT。
4. compute compose 回 `RT_SCENE_COLOR`，然后继续正常的 DLSS/spatial upscale/blit resolve。

当前是 bucket 近似排序，不是逐 splat CPU sort；`r.splat.bucketCount` 是请求下限，pass 还会根据 splat count 自动提高到受限的 2 的幂。

## 明确边界

- SOG 不会被 SceneExport 写回 glTF/GLB；隐藏 proxy 也不是可逆的 SOG 表示。
- external pass 当前只在 primary view 执行，不能据此声称 camera preview/Remote secondary view 已渲染 splat。
- 没有通用 `.ply` loader、训练/编辑 pipeline 或 SOG 写出器。
- 示例资产可能来自可选 pak；资产缺失不等于平台不支持 loader。

使用方式、CVar 和截图命令见 `AGENT_GUIDE/GaussianSplat.md`。改格式解码时优先补 `FSplatQuant`/loader 单测；改渲染时还要验证普通 mesh 前后遮挡、SH、多个 splat node、proxy shadow 和 renderer contract 不兼容时的安全跳过。
