---
title: "SOG 高斯溅射与普通场景深度融合开发计划"
category: plan
status: 草案
owner: engine
created: 2026-07-04
last_updated: 2026-07-04
---

# SOG 高斯溅射与普通场景深度融合开发计划

> 范围：在现有 SOG 加载、ref 场景混合、深度遮挡已经可用的基础上，让高斯溅射资产进一步参与普通场景的阴影、光追遮挡和光照着色。本文是开发方案，不直接实现代码。

## 0. 结论

当前 SOG 是一个后处理式叠加 pass：`GaussianSplatPass` 在 logic renderer 写完 `RT_DENOISED` 后执行，GPU 排序、billboard 光栅到 `RT_SPLAT_ACCUM`，再 compute compose 回主 HDR 图（`src/Engine/Rendering/GaussianSplat/GaussianSplatPass.cpp:491`）。它使用主场景 depth attachment 做深度测试，但深度写入关闭（`src/Engine/Rendering/GaussianSplat/GaussianSplatPass.cpp:315`），所以普通 mesh 可以挡住 SOG 的可见性，SOG 却不会进入阴影图、TLAS、AmbientCube 或任何 ray tracer。

要解决这个隔离问题，首选方案应当更贴近现有引擎：**SOG 体素化后生成一个普通三角形 proxy mesh**。这个 proxy mesh 不进入主视口 visibility buffer，不直接显示；但它像普通物件一样进入 shadow map、GPUAS 和 GI bake。这样 CSM 与 ray tracing 不需要引入 procedural AABB / 体积 raymarch 新路径，只需要补齐“主视口可见 / 阴影可见 / GPUAS 可见 / GI 可见”的渲染用途 mask。

```
SOG 高斯集合
  -> 局部密度体素（density / albedo / gradient）
  -> iso-surface / voxel surface proxy mesh
      -> main visibility: 关闭，不写 RT_MINIGBUFFER
      -> shadow visibility: 开启，复用现有 ShadowMapPass
      -> ray tracing visibility: 开启，复用 triangle BLAS/TLAS；ray query mask 不需要改
      -> SOG shading: 采样 CSM + AmbientCube + proxy probe，mesh 光照影响 SOG
```

第一阶段只要求“稳定、可控、便宜”的近似：proxy mesh 能投影、能挡 RT shadow/occlusion ray、能被 CSM/AmbientCube 影响。半透明透射、体积软阴影、procedural volume raymarch 作为后续质量增强或备选，不进入首版主线。

## 1. 目标与非目标

### 1.1 目标

1. **SOG 投影到普通场景**：太阳 CSM 中能看到 SOG proxy mesh，普通 mesh 采样 CSM 时获得 SOG 阴影。
2. **SOG 挡住光追射线**：硬件 ray query、软件 tracing、AmbientCube 硬件 bake 的遮挡查询能把 SOG proxy mesh 当成 occluder。
3. **普通场景影响 SOG 光照**：mesh 的 CSM 阴影、AmbientCube/IBL 光照能调制高斯溅射的显示颜色，让 SOG 能被场景挡光、受光。
4. **ref 场景继续正确工作**：SOG 通过 scene reference 加入其他场景时，proxy mesh 跟随 `GaussianSplatComponent` 节点世界变换、显隐、透明度等状态。
5. **可调试、可回退**：所有新路径有 CVar 和 debug view，可单独关闭 ray、shadow、lighting，避免影响现有 SOG 视觉回归。

### 1.2 非目标

- 不做每个高斯的精确 ray intersection。30 万级 splat 逐元求交不适合当前架构。
- 不用 proxy mesh 替代 SOG 的可见渲染。最终画面仍由 `GaussianSplatPass` 负责，proxy mesh 只负责场景交互。
- 不让 proxy mesh 进入主视口 visibility buffer / object id / outline。它是“影响场景”的隐藏代理，不是可见 mesh。
- 首版不追求物理严格的半透明体积多重散射。proxy mesh 是阈值表面近似，后续再做半透明透射。
- 首版不处理 SOG 内部编辑、训练、动态重建；SOG 内容静态，节点 transform 可以动态。

## 2. 当前实现诊断

### 2.1 SOG 数据与渲染

- SOG 解码输出 `FGaussianSplatData`，包含 GPU splat、SH palette、局部 AABB、所属节点 id（`src/Engine/Assets/Core/GaussianSplat.hpp:13`）。
- loader 已计算 3σ AABB，并把节点挂上 `GaussianSplatComponent`（`src/Modules/SplatLoader/FSogLoader.cpp:261`, `src/Modules/SplatLoader/FSogLoader.cpp:326`）。
- scene reference 已能合并 SOG，并修正 `splatModelId` 偏移（`src/Engine/Runtime/Scene/SceneList.cpp:471`）。
- pass 每帧把节点 world matrix 写入 `SplatModelState`（`src/Engine/Rendering/GaussianSplat/GaussianSplatPass.cpp:356`），排序后画 billboard。
- billboard 着色只评估 SOG 原始 SH，颜色与场景灯光无关（`assets/shaders/Splat.Billboard.vert.slang:55`）。

### 2.2 阴影与光追隔离点

- 太阳阴影 pass 是 depth-only CSM，只画 soft mesh shader 的间接绘制结果（`src/Engine/Rendering/Shadow/ShadowMapPass.cpp:212`）。SOG 不参与，所以不会投影。
- TLAS 构建只为 scene models 生成 BLAS（`src/Engine/Rendering/VulkanBaseRenderer.RayTracingAS.cpp:143`），TLAS 实例来自 `NodeProxy` 的 mesh model（`src/Engine/Rendering/VulkanBaseRenderer.cpp:2356`）。
- 主视口 GPU cull 和 shadow GPU cull 都用 `node.visible > 0` 作为候选条件（`assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang:55`, `assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact.comp.slang:53`）。所以不能简单把 proxy mesh 的 `RenderComponent::Visible=false`，否则它也不会投影阴影。
- `RenderComponent` 已有 `CastShadows / ReceiveGI / LayerMask`（`src/Engine/Runtime/Components/RenderComponent.h:37`），但当前 `NodeProxy` 和 GPU cull 没有完整消费这些用途标志。SOG proxy mesh 需要推动这里收敛为明确的 render participation mask。
- AmbientCube 硬件 bake 使用 `FHardwareRayTracer`（`assets/shaders/Bake.HwAmbientCube.comp.slang:8`）。只要 proxy mesh 进入 triangle TLAS，现有后续 ray query 就能开始被 SOG 影响；当前混合 path tracing 的 primary surface 来自 visibility buffer，不依赖 TLAS primary ray。

### 2.3 AmbientCube 可复用点

现有 GI 已经是“probe at voxel positions”结构：

- GPU bake 遍历 active brick probe，得到 probe world position 后调用 `FGpuProbeGenerator::RenderAmbientCubeOnly`（`assets/shaders/Bake.HwAmbientCube.comp.slang:18`）。
- 运行时 shading 通过 `interpolateAmbientCubes` 做 8-tap 插值，并用 `VoxelData` 距离场防漏光（`assets/shaders/common/AmbientCube.slang:566`）。

这和用户提到的“体素位置也可以作为探针计算场景光照，附加到高斯溅射 SH 项上”方向一致。SOG 不需要另起一套世界 GI；它可以先采样现有 AmbientCube，再在生成 proxy mesh 时保留的局部密度/表面采样点上缓存一层更贴合 SOG 表面的 lighting correction。

## 3. 总体设计

### 3.1 新增资源：Splat Proxy Mesh

每个 `FGaussianSplatData` 派生一个隐藏的普通 `Model`：

```cpp
struct FGaussianSplatProxy
{
    uint32_t modelId = invalid;       // scene.Models() 中的普通三角形模型
    uint32_t materialId = invalid;    // depth/AS 用代理材质，主视口不消费
    glm::vec3 localAabbMin;
    glm::vec3 localAabbMax;
    uint32_t sourceSplatModelId;
    uint32_t sourceNodeInstanceId;
    uint32_t gridDimX;
    uint32_t gridDimY;
    uint32_t gridDimZ;
};
```

proxy mesh 可以挂在同一个 SOG node 上，也可以挂一个内部 child node。推荐首版挂 **内部 child node**：

- child node 继承 SOG node transform，ref 场景移动/缩放时自动同步。
- child node 带 `RenderComponent(proxyModelId)`，但设置 `MainVisibility=false`、`ShadowVisibility=true`、`RayTracingVisibility=true`、`RayCastVisible=false`。
- editor/selection/focus 仍以 `GaussianSplatComponent` 原节点和 SOG AABB 为准，不让 proxy mesh 进入用户可选对象。

proxy mesh 是普通 `Model`，所以 CSM、TLAS、CPU BVH、soft mesh shader 都可以沿用现有三角形路径。

### 3.2 从高斯生成 proxy mesh

生成发生在 SOG load 后或 scene `PostLoad/Reload` 阶段，首版走 CPU async，后续可迁到 GPU compute。流程分两步：先生成局部密度体素，再抽取三角形表面。

对每个 splat：

1. 取局部位置 `mu`、协方差 `Sigma`、opacity、SH0。
2. 计算 `kProxySigma` 范围内的 voxel bbox，默认 2.0-3.0 σ。
3. 对 bbox 内 voxel 中心 `x` 估算：

```text
g = exp(-0.5 * (x - mu)^T * inverse(Sigma) * (x - mu))
tau += opacity * g * densityScale
color += sh0Color * opacity * g
```

4. 最终：

```text
voxelOpacity = 1 - exp(-tau)
voxelAlbedo = color / max(weight, eps)
```

5. 对 `voxelOpacity >= isoThreshold` 的区域抽取 surface。

mesh 抽取决策：

- **首版固定使用 Marching Cubes**。三角面承载没有问题，且实现资料多、法线自然、与普通三角形渲染/BLAS/CSM 路径完全匹配。
- 不走 Surface Nets / Dual Contouring 作为首版主线，避免引入额外拓扑/quad 处理分叉。
- 不走 voxel block mesh fallback，除非只做 debug；它的台阶阴影会放大 proxy 近似误差。

proxy mesh 顶点需要：

```cpp
Position = local surface point
Normal   = density gradient or face normal
Material = proxy material slot 0
```

材质只用于 RT hit fallback / debug，不进入主 visibility。

性能注意：

- 不能按全体积扫每个 splat。必须按 splat bbox 写有限 voxel。
- 对极大协方差或异常 splat 限制最大 voxel bbox，避免单个 splat 填满全 volume。
- 默认 grid 不应超过 `128³`；常规资产从 AABB 和 splat count 推导 `32³ / 64³ / 96³`，再由 CVar 强制上限。
- 代理是近似几何，不要把很低 opacity 的毛边全部变成实体。`densityScale`、`isoThreshold` 是质量关键旋钮。
- Marching Cubes 后做基本顶点焊接和退化三角形清理；mesh simplification / LOD 作为性能旋钮保留，但不是首版正确性的前置条件。

### 3.3 Render participation mask

当前 `NodeProxy.visible` 是 bool 语义。proxy mesh 方案需要把“是否存在”和“进入哪个渲染用途”拆开，建议引入 bitmask：

```cpp
namespace RenderParticipation
{
    constexpr uint32_t MainVisibility = 1u << 0; // visibility buffer / object id / main raster
    constexpr uint32_t ShadowCaster   = 1u << 1; // CSM shadow pass
    constexpr uint32_t GpuAs          = 1u << 2; // GPUAS / RT occlusion inclusion
    constexpr uint32_t GIBake         = 1u << 3; // CPU/GPU GI voxelization / bake
};
```

落地方式：

- 短期可复用 `NodeProxy.visible` 存 bitmask，保持 bit0 兼容旧判断；长期改名为 `renderMask`。
- `RenderComponent` 增 `MainVisible` 或 `VisibleInMainPass` 时，要与现有 `Visible` 语义兼容。普通 mesh 默认 `Main|Shadow|GpuAs|GIBake`，SOG proxy 默认 `Shadow|GpuAs|GIBake`。
- 主视口 cull 改为检查 `MainVisibility`。
- shadow cull 改为检查 `ShadowCaster`，并同时尊重 `RenderComponent::CastShadows`。
- TLAS/GPUAS inclusion 由 `GpuAs` 位决定，不引入新的 ray tracing mask 分叉。
- CPU/GI voxelization 由 `GIBake` 位决定。

这比新增 SOG 专用 shadow/ray pass 更小，也顺手修正了现有 `CastShadows` 没被 shadow GPU cull 完整消费的问题。

## 4. SOG 投影：写入 CSM

首版不新增 `SplatShadowPass`。proxy mesh 作为普通 mesh 参加现有 shadow GPU cull 和 `ShadowMapPass::DrawCascade`：

1. `Scene.Update` 为 proxy child node 生成 `NodeProxy`，其 render mask 含 `ShadowCaster`，不含 `MainVisibility`。
2. `Task.SoftMeshShaderGpuCullCompact` 主视口 pass 跳过 proxy。
3. `Task.SoftMeshShaderShadowGpuCullCompact` shadow pass 保留 proxy。
4. `Rast.ShadowMapSoftMeshShader.vert.slang` / `Rast.ShadowMap.frag.slang` 不需要知道这是 SOG。
5. 普通 mesh shading 继续调用 `Common.SampleSunShadowCSM`，自然收到 SOG proxy 的深度遮挡。

这条路径的质量由 proxy mesh 的 iso-surface 决定。想要更透明/更软的 SOG 阴影，后续再叠加 transmittance shadow map；不要在首版把 CSM 改成体积 raymarch。

## 5. SOG 挡住光追射线

### 5.1 硬件 TLAS：普通 triangle BLAS

proxy mesh 被追加到 `scene.Models()` 后，`CreateBottomLevelStructures` 可以像普通 model 一样给它建 triangle BLAS。TLAS 更新只需要按 `GpuAs` participation 位决定是否加入实例，不需要给 SOG proxy 新增独立 ray tracing mask。

当前混合渲染路径的 primary surface 由 visibility buffer 决定：`Core.PathTracing.comp.slang` 使用 `FVisibilityBufferRayCaster` 做 `renderer.PrimaryHit(rayCaster)`，proxy mesh 不进入主视口 visibility buffer，因此天然不会成为 primary hit。`FHardwareRayTracer` 继续使用现有 ray query 参数即可，direct shadow、`TraceOcclusion`、`TraceSegment` 等后续查询会像命中普通三角形一样命中 proxy mesh。

如果未来新增纯硬件 primary ray 模式并启用 `FHardwarePrimaryRayCaster`，那条新模式需要单独决定是否排除 proxy mesh；这不是当前方案的前置改动。

这样不需要恢复 `AddGeometryAabb`，也不需要 procedural candidate / `CommitProceduralPrimitiveHit`。

### 5.2 软件 tracing / 非 RT fallback

软件路径有两种低复杂度接法：

1. **通过 GI voxelization 接入**：proxy mesh 带 `GIBake` 位，CPUAccelerationStructure / Ambient voxelization 把它当普通静态几何写入 `VoxelData`。`FSoftwareRayTracer` 的 DDA 路径自然能看到它。
2. **通过 CPU BVH 接入**：如果某些软件查询直接走 tinybvh，则 proxy mesh 因为是普通 `Model + RenderComponent`，也可按 `GIBake/GpuAs` participation 纳入 BVH。

首版优先第 1 条，因为它和现有 AmbientCube/DDA 体系一致。

### 5.3 AmbientCube bake

硬件 bake 使用 `FHardwareRayTracer`，因此 proxy mesh 进入 TLAS 后，probe 射线会自然被 SOG proxy 遮挡。

软件 bake 则依赖 proxy mesh 是否进入 GI voxelization。需要避免 SOG 自己的 proxy 把 SOG 接收光照探针全部遮黑：SOG 自身的局部 probe 修正可以选择采样世界 AmbientCube，而不是重新 ray trace 并命中自己；如果后续要对 SOG proxy 单独烤 probe，再加 self-mask。

## 6. 普通场景影响 SOG 光照

### 6.1 首版：在 billboard shader 采样场景光照

`Splat.Billboard.vert.slang` 目前只输出 `EvaluateColor(splat, shDirection)`。改为输出或在 fragment 中计算：

- `worldPosition`：至少用 splat center。高质量版本可按 billboard local offset 估算椭球表面点。
- `proxyNormal`：优先从 proxy mesh 最近点/局部密度 gradient 采样；没有 proxy 或 gradient 过小时 fallback 到 `-viewDir`。
- `sceneLighting`：
  - CSM：`Common.SampleSunShadowCSM(worldPosition, proxyNormal, viewDist)`。
  - AmbientCube：`interpolateAmbientCubes<FullAmbientCubeSampler>(worldPosition, proxyNormal)`，无 AmbientCube 时回退 sky/IBL。
  - Sun：`SunColor * shadow * max(dot(proxyNormal, SunDirection), 0)`。

因为 SOG SH 本身是拍摄/训练得到的 radiance，不是纯 albedo，不能直接当 PBR 材质重打光。首版建议保守混合：

```text
sogRadiance = EvaluateColor(SOG_SH, viewDir)
albedoHint  = saturate(SH0_or_proxyAlbedo)
litRadiance = albedoHint * (ambient + sun)
finalColor  = lerp(sogRadiance, sogRadiance * ambientVisibility + litRadiance, lightingStrength)
```

默认 `lightingStrength` 取 0.25-0.5，避免把原始 SOG 颜色完全洗掉。组件和 CVar 都可调。

### 6.2 中期：SOG 代理探针附加到 SH

为每个 SOG proxy surface sample 烤一份局部 lighting correction：

```cpp
struct FSplatLightingVoxel
{
    uint32_t irradianceRgb9e5;  // L0 ambient/indirect
    uint32_t sunRgb9e5;         // direct sun after scene occlusion
    uint32_t normalOctOrDir;    // optional dominant direction
    uint32_t flags;
};
```

烘焙方式：

1. 复用 proxy voxel 中心作为 probe position。
2. 对 active surface voxels 采样现有 AmbientCube，或用 `FHardwareRayTracer` 低样本数 trace scene。
3. 写入 `FSplatLightingVoxel`，随时间累积。
4. SOG billboard 根据 splat local position trilinear 采样 lighting voxel，把结果作为 **SH DC/L0 修正**：

```text
correctedShColor = EvaluateColor(originalSH, viewDir)
                 + albedoHint * splatLightingL0 * r.splat.lightingProbeStrength
```

若后续需要方向性，可把 `FSplatLightingVoxel` 扩展为 6-face AmbientCube 或 2-band SH，但首版 L0 足够验证“场景光照附加到 SH 项上”这条链路。

## 7. 组件与 CVar

### 7.1 GaussianSplatComponent 新属性

```cpp
bool CastShadow = true;
bool RayTraceOccluder = true;
bool ReceiveLighting = true;
float LightingStrength = 0.35f;
float ProxyDensityScale = 1.0f;
float ProxyAlphaThreshold = 0.35f;
```

保留 `Visible / RayCastVisible / OpacityScale` 原语义。`OpacityScale` 应同时影响可见 splat 和 proxy density，避免用户把 SOG 变透明后阴影仍过重。

### 7.2 全局 CVar

- `r.splat.proxy.enable`：总开关。
- `r.splat.proxy.gridMax`：单轴最大体素数，默认 96 或 128。
- `r.splat.proxy.brickSize`：默认 8。
- `r.splat.proxy.sigma`：生成体素代理的 splat 影响半径。
- `r.splat.proxy.isoThreshold`：从密度场抽取 proxy mesh 的 iso-surface 阈值。
- `r.splat.proxy.simplifyRatio`：proxy mesh 抽取后的可选简化强度，默认不破坏 Marching Cubes 输出。
- `r.splat.shadow.enable`：CSM 投影开关。
- `r.splat.rayOcclusion.enable`：ray tracing 遮挡开关。
- `r.splat.proxy.debugVisible`：调试用，默认关闭；开启时允许主视口看到 proxy mesh。
- `r.splat.receiveLighting`：SOG 着色接收光照开关。
- `r.splat.lightingStrength`：全局光照混合强度。
- `r.splat.proxy.debug`：0 off / 1 density / 2 mesh wire / 3 GPUAS inclusion。

## 8. 开发阶段

### Phase 0 - 渲染用途 mask 与资源骨架

- 新增 `RenderParticipation` bitmask，并让 `NodeProxy.visible` 短期承载 mask 或新增 `renderMask` 字段。
- 主视口 GPU cull 检查 `MainVisibility`，shadow GPU cull 检查 `ShadowCaster`，TLAS/GPUAS 检查 `GpuAs`，GI/CPU BVH 检查 `GIBake`。
- `FGaussianSplatData` 增 `proxyModelId/proxyNodeInstanceId` 或 scene 侧并行数组。
- `GaussianSplatComponent` 反射新增 shadow/ray/lighting 属性。
- 新增 CVar 和 AGENT_GUIDE 更新点。
- 验收：普通 mesh 默认行为不变；不生成代理时现有 `assets/sog/Grape.sog` 截图与当前一致。

### Phase 1 - SOG -> proxy mesh

- CPU 生成 density/albedo/gradient grid。
- 用 Marching Cubes 抽取 proxy mesh，写入普通 `Model`。
- 生成内部 proxy node / RenderComponent，mask = `Shadow|GpuAs|GIBake`，不含 `MainVisibility`。
- debug draw proxy mesh wireframe / density heatmap。
- 单测：小型人工 splat 集生成稳定 mesh AABB、density 单调、ref transform 不破坏 proxy model id。
- 验收：加载 `Grape.sog` 日志打印 proxy grid、triangle count、显存；开启 `r.splat.proxy.debugVisible` 能看见代理网格，默认画面不出现代理网格。

### Phase 2 - CSM 投影

- 不新增 SOG 专用 shadow pass。
- shadow GPU cull 纳入 proxy mesh，主视口 GPU cull 排除 proxy mesh。
- `SampleSunShadowCSM` 不改或只加 debug 分支。
- 验收：SOG 放在地面和太阳之间，`gnb shot --scene <mixed-scene> --frames 120` 可见 SOG 阴影；关闭 `r.splat.shadow.enable` 阴影消失。

### Phase 3 - 硬件 ray occlusion

- proxy mesh 作为普通 triangle BLAS 进入 TLAS。
- 不修改 ray tracing mask；当前 primary 由 visibility buffer 产生，proxy mesh 不进主 visibility buffer，所以不会在 path tracing 主画面直接显示。
- AmbientCube 硬件 bake 自动看到 SOG occluder。
- 验收：ray traced direct shadow / path occlusion 中，SOG 能挡住太阳射线；primary path tracing 画面不直接显示 proxy mesh。

### Phase 4 - SOG 接收 CSM + AmbientCube 光照

- `Splat.Billboard.vert/frag` 引入 proxy normal、CSM shadow、AmbientCube lighting。
- 保守混合原始 SH 和 scene lighting。
- 验收：把 mesh 放在 SOG 与太阳之间，SOG 受阴影变暗；改变 sun direction/sky intensity，SOG 有可控响应；`lightingStrength=0` 回到原始外观。

### Phase 5 - SOG 局部 lighting probe / SH DC 修正

- 给 proxy surface samples 烤 `FSplatLightingVoxel`。
- 使用现有 AmbientCube 或低样本 ray query 生成 L0 correction。
- billboard 根据 splat local position 采样 lighting correction 并附加到 SH DC。
- 验收：SOG 靠近彩色墙/发光体时出现局部环境色影响；代理 probe debug 能显示 lighting field。

### Phase 6 - 软件 tracing、移动端降级与性能整理

- GI voxelization / CPU BVH 按 `GIBake` mask 纳入 proxy mesh，让 `FSoftwareRayTracer` 的 DDA 能看到 SOG proxy。
- `Bake.SwAmbientCube` 能通过 `VoxelData` 看到 SOG proxy。
- 非 RT 设备至少支持 CSM 投影和 billboard 接收 CSM/AmbientCube；若无 AmbientCube 则回退 sky/CSM。
- profile proxy triangle count、shadow pass cost、TLAS build/update cost，补充 mesh simplification / LOD 策略。

## 9. 验证计划

### 9.1 构建

本计划触及 Engine 层、shader、测试。按仓库规则使用 targeted build：

```powershell
.\gnb.bat build gkNextRenderer gkNextUnitTests
```

### 9.2 单测

- `Test_SogLoader` 增加 proxy mesh 生成测试。
- 新增 `Test_SplatProxyMesh`：
  - 单个 isotropic Gaussian 的中心 density 最大。
  - opacity scale 影响 proxy density。
  - iso threshold 提高时 triangle count 不增加。
  - scene reference 合并后 splat model id 和 proxy model id 一致。

### 9.3 视觉/交互验证场景

新增一个混合场景，例如：

```text
assets/scenes/sog_integration_test.gltf
  - 地面
  - 太阳方向光
  - 一个普通 box/墙体
  - ref assets/sog/Grape.sog
```

验证命令：

```powershell
.\gnb.bat shot --scene assets/scenes/sog_integration_test.gltf --frames 180
.\gnb.bat shot --scene assets/scenes/sog_integration_test.gltf --frames 3000
```

第二条用于 AmbientCube 收敛，参考现有 AmbientCube 文档里 `gnb shot` 90 帧不足以验证 GI 的注意事项。

### 9.4 必测矩阵

| 项 | 期望 |
|---|---|
| mesh in front of SOG camera | 已有深度遮挡不回归 |
| SOG above plane, sun angled | plane 出现 SOG 阴影 |
| mesh between sun and SOG | SOG 局部变暗 |
| SOG between mesh and sun | mesh ray/CSM 认为被遮挡 |
| PathTracing / SoftwareModern / SwModernNoAmbient | 都能显示 SOG；至少 CSM 投影路径一致 |
| ref 场景多实例 | 每个实例 proxy transform、显隐独立 |
| `OpacityScale=0` | 可见 splat 和 proxy shadow/ray 都消失 |

## 10. 风险与取舍

1. **proxy mesh 不是精确 SOG**：薄结构、半透明毛边会有过重或过轻阴影。通过 density scale、iso threshold、grid resolution 和 debug view 调参；simplification/LOD 只做性能控制。
2. **主视口隐藏必须靠 mask，不可靠 `Visible=false`**：现有主 cull 和 shadow cull 都看 `node.visible`。如果不先拆 render participation，proxy mesh 要么被画出来，要么也不能投影。
3. **不要引入不必要的 RT mask 分叉**：当前混合 path tracing 的 primary 来自 visibility buffer，proxy mesh 不进主 visibility buffer 就已经被排除。proxy 进入 TLAS 后应先复用现有 ray query 行为；只有未来纯硬件 primary ray 模式才需要额外策略。
4. **CSM depth 是硬表面近似**：首版无法表达漂亮的半透明体积阴影。需要时再加 transmittance map 或 procedural volume shadow 作为质量增强。
5. **SOG radiance 与重打光会双重计光**：默认 `LightingStrength` 保守，且允许 per-component 关闭。不要把原始 SH 当纯 albedo。
6. **AmbientCube 反馈**：SOG proxy 参与 bake 后，SOG 自己的 lighting probe 可能自遮挡过强。首版优先采样世界 AmbientCube；单独 probe bake 再考虑 self-mask。
7. **性能**：proxy mesh 会增加 shadow triangle、BLAS、TLAS instance 和 CPU/GI voxelization 成本。首版必须统计 triangle count、shadow GPU time、AS build time，并提供 `gridMax/simplifyRatio`。

## 11. 主要改动文件预估

- `src/Engine/Assets/Core/GaussianSplat.hpp`：proxy model id / proxy node id 数据描述。
- `src/Modules/SplatLoader/FSogLoader.cpp` 或新 `src/Engine/Assets/Acceleration/SplatProxyMeshBuilder.*`：density grid + proxy mesh 生成。
- `src/Engine/Assets/Core/Scene.*` / `Node.*`：内部 proxy node、render participation mask、scene reference 同步。
- `src/Engine/Runtime/Components/GaussianSplatComponent.*`：属性与反射。
- `src/Engine/Runtime/Components/RenderComponent.*`：主视口/阴影/RT/GI 用途 mask 或等价属性。
- `src/Engine/Runtime/Config/UserSettings.hpp` / `EngineCVars.cpp`：CVar。
- `assets/shaders/Task.SoftMeshShaderGpuCullCompact*.slang`：主视口 cull 只看 `MainVisibility`。
- `assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact*.slang`：shadow cull 看 `ShadowCaster`。
- `src/Engine/Rendering/VulkanBaseRenderer.RayTracingAS.cpp`：按 `GpuAs` participation 把 proxy mesh 作为普通 triangle instance 纳入 TLAS。
- `assets/shaders/common/RayTracers.slang`：通常不需要修改 ray mask；只确认现有 occlusion/segment 查询能命中 proxy triangle。
- `src/Engine/Assets/Acceleration/CPUAccelerationStructure.*`：GI/软件 tracing 按 `GIBake` mask 纳入 proxy mesh。
- `src/Engine/Rendering/GaussianSplat/GaussianSplatPass.*`：接收光照、proxy normal/lighting sample。
- `assets/shaders/Splat.*.slang`：接收光照与 lighting probe 采样。
- `assets/shaders/common/AmbientCubeBaker.slang` / `Bake.*AmbientCube.comp.slang`：确认 bake occlusion 和 self-mask。
- `src/Tests/Test_SogLoader.cpp` / 新测试：proxy 单测。

## 12. 推荐优先级

优先做 Phase 0 + Phase 1 + Phase 2。这样能最小代价验证 proxy mesh 主线：SOG proxy 不进入主视口，但能进入现有 shadow map 并投影。

Phase 3 的硬件 ray occlusion 价值高，但现在只需要把 proxy mesh 纳入 triangle BLAS/TLAS，不需要 procedural RayQuery，也不需要 ray tracing mask 分叉。它适合紧跟 Phase 2 做，因为实现复杂度已明显降低。

Phase 4/5 是质量增强：先让 SOG 接收已有 CSM/AmbientCube，再把“体素/表面采样点作为探针，附加到 SH 项”做完整。不要把它塞进 proxy mesh 首版，否则会同时调 geometry、shadow、AS、lighting 四套系统，定位困难。
