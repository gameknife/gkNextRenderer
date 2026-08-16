---
title: "Visibility Surface、G-buffer 与 Shading Scheduler 设计"
category: design
status: 现行（M0–M4 + M5a/b/c 已实现）
owner: engine/rendering
created: 2026-08-16
last_updated: 2026-08-16
related_plan: ../plans/visibility-surface-gbuffer-plan.md
---

# Visibility Surface、G-buffer 与 Shading Scheduler

本文描述**当前实现**的职责链、数据契约与调度抽象。执行记录、实测数据与决策门结论见
[开发计划](../plans/visibility-surface-gbuffer-plan.md)。

cvar 一览（后三者都隐含依赖 `r.surface.build`）：

| cvar | 默认 | 作用 |
| --- | --- | --- |
| `r.surface.build` | off | Primary Surface 路径。关闭时每个 renderer 走迁移前的 inline 解码路径，逐像素一致 |
| `r.surface.scheduler` | off | tile 分类 + per-bucket indirect dispatch（见「调度器实测结论」） |
| `r.taau.sparseCheckerboard` | off | checkerboard lighting 不做 resolve，直接交给 Native TAAU |
| `r.gtao.applyInCore` | off | GTAO 上采样与遮蔽由 Core Shading 完成，而不是 compose |

**checkerboard 现在是 Primary Surface 路径独有的能力**：`r.surface.build` 关闭时
`IsCheckerboardRenderingActive()` 恒为 false。因此 PathTracing、PathTracingLite、VoxelTracing
（没有、也无法有 Build pass）在 upscaler 下一律全率着色。

## 职责链

```text
Visibility Buffer（Rast.VisibilityPass，不变）
        ↓
Surface Build（Core.SurfaceBuild.comp.slang，永远 full-rate）
        ├── Shading Classify（Task.ShadingClassify，8x8 tile 分桶；r.surface.scheduler）
        ├── GTAO（半分辨率，读 dense depth/normal，已前移到 Core 之前）
        ├── 屏幕空间 shadow / tracing（读 dense surface，不再逐步重解几何）
        └── Core Shading（读 surface；解析式全屏 或 per-bucket indirect dispatch）
                        ↓
            Lighting-only checkerboard resolve（不再复制 surface 资源；sparse 模式下整个不跑）
                        ↓
                    Compose / Upscaler（外部 upscaler 契约不变）
```

核心结论：

1. Visibility Buffer 只表示命中定位；几何、材质、motion 由 Surface Build 解析一次，
   经统一 codec（`assets/shaders/Shader/SurfaceBuffer.slang`）读写。
2. Primary Surface **复用现有 RT bank**，没有并行资源集合。契约是「由谁写、写什么语义」的收口。
3. 第一阶段不建 pixel queue。单 bucket + checkerboard rate 用解析映射即可（零内存、零 atomic）；
   tile 分类 + indirect dispatch 是 M4 的可选路径。
4. 首批真实 bucket 是 **Background(sky) 与 Emissive**，不是材质特征桶：它们本来就是每个 Core
   shader 里独立的 early-out 写出集，不引入任何材质系统改动就能验证调度机制。
5. Checkerboard 下沉为 per-bucket 采样率；Surface Build 永远 full-rate，resolve 收缩为 lighting-only。
6. GTAO 前移到 Build 之后、Core 之前；遮蔽默认仍在 compose 应用，`r.gtao.applyInCore` 可把它下沉到
   Core Shading。无论在哪一端，它都只作用于 sky diffuse。
7. Renderer 迁移：SoftwareModernNoAmbient、SoftwareModern、SoftwareTracing 已迁移；
   PathTracing / PathTracingLite（HW primary 含 aperture/DOF，raster VB 无法重放）与
   VoxelTracing（数据来源不同）不迁移。

## 它解决的问题

### 循环依赖：Core 的输出曾是 Core 想要的输入

dense depth（`RT_PREV_DEPTHBUFFER`）与 normal（`RT_NORMAL`）过去是 Core Shading 的**输出**，因此：

- `Common.ScreenSpaceShadowVisibility` 在 Core 内部被调用时无 dense depth 可用，只能对每个 march
  step 重新读 Visibility、取 3 个顶点、做矩阵变换和平面求交（`LoadVisibilitySurfacePlane`）。
- `TraceInScreenSpace` 每个 march step 调一次完整的 `Common.get_material_data`。
- GTAO 被迫排在 Core 之后，因为它读的 depth/normal 由 Core 写出。

Surface Build 前置打破了这个环：屏幕空间消费者现在读两张纹理而不是重放一次几何解码，
实测 screen-space shadow 的 march 成本下降约 45%（数据见开发计划）。

### Visibility 不是 surface

每个消费者过去都要重复：读 `NodeProxy`/`ModelData`/索引/三个顶点 → skinning/transform →
barycentric → `NodeProxy.matId[]` 材质解析 → 纹理采样。现在这套逻辑只在 Build 里跑一次。

### Checkerboard resolve 复制 surface 是语义错误

邻居复制的 depth/normal/motion/objectId 在物体边缘、深度与运动不连续处是**语义错误的 surface
数据**，会污染 temporal upscaler。现状与主机 CBR 实践一致：几何数据保持全率，只有 lighting 被重建。

## Primary Surface 契约

### 字段与所有权（Core.SurfaceBuild 写入，全率）

| RT 槽位 | 格式 | 语义 | 备注 |
| --- | --- | --- | --- |
| `RT_PREV_DEPTHBUFFER` | R32_SFLOAT | NDC depth（**jittered** `ViewProjection`） | 背景 sentinel `Surface.kBackgroundDepth` = 1000 |
| `RT_NORMAL` | RGBA16F | xyz=world shading normal（含 normal map），w=roughness | 背景写 float4(0)，零长度法线即「非表面」 |
| `RT_ALBEDO` | RGBA16F | base color（diffuse 纹理已乘入） | 见下文「RT_ALBEDO 的双重身份」 |
| `RT_OBJECTID_0` | R32_UINT | `Common.EncodeObjectId`（含 selected/hovered/locked/danger） | 背景写 0 |
| `RT_MOTIONVECTOR` | RG32F | render-pixel 单位 motion；背景写 sky motion | 不变 |
| `RT_MOTIONMOMENT` | R16_UINT | motion 突变计数 | `Common.CalculateMotionVector` 的副作用，随 motion 归 Build |
| `RT_BSDF_DATA` | RG32_UINT | x=resolved material ID（背景/退化=`kInvalidMaterialId`），y=`f16 metalness \| 16bit flags` | 见下文 |
| `RT_SPECULAR_ALBEDO` | RGBA16F | specular albedo（metallic 用 base color） | 仅在消费者需要时写（tracing 系），退出 resolve |

不写 world position：由 depth + `Camera.ProjectionInverse` 重建
（`Surface.ReconstructPosition`）。

Feature flags：`EMISSIVE`、`METALLIC`、`DIELECTRIC`、`HAS_MRA`、`HAS_NORMALMAP`。
Material ID 负责身份，flags 负责策略，二者不合并成「每材质一条 shader」的接口。所有写入方
统一经 `Common.MakeSurfaceFlags(material)` 派生。

### 三个必须记住的契约细节

**depth 用 jittered 矩阵**。Build 用 `Camera.ViewProjection`（含 jitter）投影，因此重建必须用
`Camera.ProjectionInverse`（同样含 jitter）。迁移前 tracing 系用 `ViewProjectionUnJit` 写 depth，
而 `RestirSpatialShade` 用 `ProjectionInverse` 重建——两者不自洽；统一到 jittered 对消除了这个
潜在偏差。`Process.AtmosphereComposite` 仍用 `ProjectionInverseUnJit` 重建，属于遗留分歧。

**`RT_ALBEDO` 的双重身份**。`Process.Compose` 的场景色是 `diffuse * albedo + specular`，所以
albedo 同时是**解调引导**：tracing 系在 sky 与 emissive 像素上必须把它覆盖成白色，否则 compose
会把自发光再乘一次自身颜色。因此 Build 写材质 base color，tracing 的 Core 在这两种像素上覆写，
而 `RT_ALBEDO` 是唯一留在 lighting resolve 集合里的 surface 类 RT。SoftwareModernNoAmbient 不解调
（`Process.GTAOCompose` 直接相加），它把 albedo 当纯材质数据用。

**bounce 0 的 metalness 语义**。surface 存的是 `BuildBSDFContext` 语义的 effective metalness
（MRA 之后再套 Metallic→1 / Dielectric→0 覆盖）。`FPathTracingRenderer.ScatterAndTrace` 在
bounce 0 过去用的是未覆盖的原始值，surface 路径改用 effective 值。对 Metallic / Dielectric 材质，
bounce 0 的金属 lobe 选择因此跟随材质模型而不是作者填的数值——这是有意的统一。

### 统一 codec

`assets/shaders/Shader/SurfaceBuffer.slang`，地位对标 `VisibilityBuffer.slang`：

- `Surface.FPrimarySurface` + `Store` / `Load` / `LoadShading` / `LoadGeometry` / `LoadDepth` /
  `LoadBsdf` / `LoadAlbedo` / `LoadMaterialId` / `LoadSpecularAlbedo`；
- `Surface.IsValid` / `ReconstructViewPosition` / `ReconstructPosition`；
- 背景 sentinel、flags 位、material ID 无效值、BSDF word 打包（`PackBsdfWord` /
  `UnpackMetalness` / `UnpackFlags`）都只在这里出现一次；
- 窄读取器（`LoadGeometry` 只读 depth+normal，`LoadBsdf` 只读材质字）存在的原因是：一次 march
  step 不应该为了回答深度问题去读八张平面。
- 业务 shader 不允许绕过 codec 手写 RT 槽位组合。C++ 侧的槽位/格式/常量镜像在
  `src/Engine/Rendering/PipelineCommon/SurfaceBufferLayout.hpp`，两边必须同步修改。

### Build pass 边界

`Core.SurfaceBuild.comp.slang` 负责：Visibility 有效性与背景判定、三角形/skin/transform/
barycentric 解析（复用 `get_material_data`）、材质索引解析与材质纹理采样（复用 `FetchGBuffer`）、
motion/objectId/feature flags 生成、dense surface 写出。`get_material_data` 拒绝的退化样本按背景
处理。Build **不做** lighting、indirect、ray traversal、compose。

驱动方是 `PipelineCommon::SurfaceBuildPass`，三个已迁移 renderer 共用。

## Shading Scheduler

### 抽象：bucket × rate

- **bucket**：一种 shading 方式，即 kernel 边界，数量必须保持个位数。当前：`Background`、
  `Emissive`、`Standard`。
- **rate**：full / checkerboard。rate 只影响 allocation，kernel 语义不感知 parity。
  surface 路径的全部 parity 知识都在 `Shader/ShadingScheduler.slang` 里。

### 两种 allocation

1. **解析式（`r.surface.scheduler=0`，默认）**：单 `Standard` bucket 全屏 dispatch，checkerboard
   表现为压缩一半的 dispatch 宽度。零内存、零 atomic。
2. **Tile 分类 + indirect dispatch（`r.surface.scheduler=1`）**：`Task.ShadingClassify` 每个 8×8
   tile 一个 workgroup、每像素一个线程，把分桶结果 OR 进 per-bucket 64bit 掩码，由单线程 append
   一条 `{tileIndex, maskLo, maskHi}`；**每 tile 每桶恰好一次 atomic**——它直接累加 indirect
   dispatch 的 `groupCountX`，所以那个词同时是 tile 数和 append cursor。随后
   `Task.ShadingClassifyFinalize`（一个 workgroup、每 bucket 一个线程）把最终 cursor 抄进 tile
   计数、并为压实的 bucket 把 group 数改成 `ceil(n/2)`；最后每个 bucket 一次
   `vkCmdDispatchIndirect`。驱动方是 `PipelineCommon::ShadingSchedulerPass`，tile 缓冲经
   `GPUScene.ReservedAddress0` 下发，容量经 `CustomData1`。

### 半率下的 lane 压实

tile 粒度分类有一个天然陷阱：`Standard` bucket 在 checkerboard 下每个 tile 只有 32 个像素要着色，
但 8×8 tile 对应的 workgroup 有 64 条 lane。如果每条 lane 只是「测一下自己的掩码位、不中就返回」，
dispatch 的形状是 tile 的而工作密度只有一半——解析式路径把 dispatch 宽度压掉一半、lane 全部有效，
于是**开了 checkerboard 反而看不到收益**。

办法是让 `Standard` 的一个 workgroup 覆盖**两个 tile**、各由一半 lane 服务：lane `i` 取
`slot = group*2 + (i>>5)` 号 tile 掩码里的第 `i&31` 个置位（`Scheduler.NthSetBit`）。checkerboard
保证满 tile 恰好 32 个置位，所以内部 tile 精确压实，只有被屏幕边缘裁掉的 tile 会剩少量空 lane。

`ceil(n/2)` 必须等每个分类 workgroup 都退休才算得出来，这是 finalize pass 存在的唯一理由。
两个不需要它的替代方案都实测更差：**每 tile 加第二次 atomic** 让 classify 从 0.040 涨到 0.071 ms，
比省下的还多；**仍按 tile 数 dispatch、让后一半 group 立刻退出** 省掉了 atomic，但 lane 压实也一起
没了（shading 退回 0.135 ms）。

checkerboard 下 `Background`/`Emissive` 保持全率（kernel 近乎免费，且它们的结果是精确的），
`Standard` 只认领当帧 parity。因此 lighting resolve 在 scheduler 打开时必须跳过 background/emissive
像素——否则会用邻居近似值覆盖已经正确的值。

### 调度器实测结论（决策门）

Standard kernel 去掉 miss/emissive 分支后确实变快（1080p 0.186→0.152 ms，4K 0.457→0.383 ms，
约 −16%~−22%）。lane 压实之后，**开启 checkerboard 在 scheduler 打开时也确实会变快**：1080p 的
`shading + classify` 从全率 0.192 ms 降到半率 0.159 ms（0.83×，压实前是 0.98×），`Standard` kernel
的半率耗时 0.117 ms 已与解析式路径完全一致。

但**相对解析式仍然是负收益**，因此 `r.surface.scheduler` 默认关闭：分类的 0.04 ms（1080p）/
0.09 ms（4K）对 NoAmbient 这种 0.12 ms 的 Standard kernel 来说太贵——分类占到 kernel 本身的三分之一。
这不是实现瑕疵，而是「kernel 太便宜、摊不掉分类成本」。要让 scheduler 真正划算，它得接到 Standard
kernel 重得多的 renderer 上（SwTracing 的 shading 是 NoAmbient 的约 35 倍）。机制价值（为未来材质
特征桶铺路、把 parity 知识收进 allocation 层）与性能结论分开陈述；具体数字见开发计划。

### Resolve 收缩与 upscaler 契约

复制整套 surface 类 RT 的旧 resolve 已经删除；`ECheckerboardResolveSet` 只剩两个 lighting-only 集合：

- Tracing set：`RT_SINGLE_DIFFUSE` + `RT_SINGLE_SPECULAR` + 两张 hitdist + `RT_ALBEDO`
  （最后一项是解调引导，见上文），其余 surface 类 RT 全部退出；
- NoAmbient set：`RT_SINGLE_DIFFUSE` + `RT_AMBIENT`。

外部 upscaler（DLSS/FSR/SGSR2）要求 dense `RT_SCENE_COLOR`/depth/motion 的硬约束不变，所以对它们
lighting-only resolve 仍是必经之路。

**Native TAAU sparse 输入**（`r.taau.sparseCheckerboard`）则把 resolve 整个去掉：缺失 parity 一路
保持未写入，Core 到 upscaler 之间的每个 pass 都按着色率跳过它（`Process.GTAOCompose`、
`Process.Compose`、`Process.AtmosphereComposite`），`Process.NativeTemporalReproject` 在
Catmull-Rom 重建与邻域统计里剔除未着色 tap 并重新归一化，由历史补齐。深度与 motion 仍是 Build 的
全率输出，所以 disocclusion、dilation 与 slope 估计完全不受影响。

生效前提（任一不满足自动退回 resolve）：surface 路径 + checkerboard + Native TAAU + 主视图，
且当帧没有会整屏合成的 external pass。最后一条是运行时问的：
`IExternalRenderPass::PaintsWholeSceneThisFrame()` 默认由契约的 `supportsSparseShadingRate` 决定，
AuxDraw 声明自己只画光栅化到的像素，GaussianSplat 按「这帧是否真的要画 splat」回答。

## GTAO 与屏幕空间消费者

- GTAO 已移到 Build 后、Core 前，**合成方式不变**（仍由 `Process.GTAOCompose` 做 bilateral 上采样
  并只作用于 sky diffuse）。Core 直接消费 AO、删除独立上采样属于后续独立评估。
- `ScreenSpaceShadowVisibility`：march step 经 `Common.LoadOccluderPlane` 分流——surface 路径读
  dense depth+normal（重建位置 + 朝向相机的 shading 平面），非 surface renderer 继续走
  `LoadVisibilitySurfacePlane` 的精确三角形平面。注意 surface 分支给出的是 shading 平面而非几何
  三角形平面，对遮挡近似是可接受的取舍；需要精确三角形平面的调用方必须继续用旧函数。
- `TraceInScreenSpace`：surface 路径每 step 读 surface（depth 重建位置、normal 判定、material ID
  直读），不再逐步 `get_material_data`。SoftwareTracing 的每条二次光线都会走这里。
- 消费者一律按 `Surface.IsPathActive(GPUScene.CustomData2)` 门控。

## Renderer 适配

| Renderer | 状态 | Seam |
| --- | --- | --- |
| SoftwareModernNoAmbient | 已迁移 | 单体 shader 拆成 `Core.SurfaceBuild` + `Core.SwModernNoAmbientSurface`；lighting kernel 共享 `common/NoAmbientShading.slang`，legacy 入口调用同一份 kernel，两条路径不会漂移 |
| SoftwareModern | 已迁移 | `FSurfacePrimaryRayCaster : IPrimaryRayCaster` 替换 `FVisibilityBufferRayCaster` |
| SoftwareTracing | 已迁移 | 同上；ReSTIR 的 primary gate（instanceId/motion）从 surface 读取 |
| PathTracing / Lite | 不迁移 | HW primary 含 aperture/DOF，raster VB 无法重放 |
| VoxelTracing | 不迁移 | 数据来源不同 |

`IPrimaryRayCaster` 增加了 `inout FPrimarySurfaceSample outSurface`：surface caster 通过它交出
albedo / roughness / metalness / motion。这是必要的，因为从 depth 重建的顶点**没有 TexCoord**，
而这三个值恰好就是 TexCoord 在 `BuildBSDFContext` 里被用来采样出的东西
（`BuildBSDFContextFromSurface` 即此用途）。`outSurface.Valid` 同时选择渲染器的输出划分：
有 Primary Surface 时 `PrimaryHit` 只写 lighting 类 RT。

## 验证入口

- `assets/agentscripts/surface-legacy-parity.agentscript.json`：**「surface off 是 no-op」的回归**。
  在改动前后各跑一次、对比四个 renderer 的输出。NoAmbient 应逐位相同；三个随机采样器只能与自身的
  噪声地板比较（做法见开发计划 M3）。
- `assets/agentscripts/surface-noambient-strict.agentscript.json`：冻结场景、关 upscaler，
  NoAmbient legacy vs surface 逐像素对照（同时含 legacy vs legacy 的确定性基线）。
- `assets/agentscripts/surface-noambient-perf-{1080p,4k}.agentscript.json`：8 组配置的 GPU timer
  对照；用 `tools/surface_perf_table.py <report.json>` 出表。
- `assets/agentscripts/surface-noambient-density.agentscript.json`：`r.gtao.debugMode=1` 下比较
  full vs checkerboard，用来观察 surface 路径的 GTAO 输入是否与采样率无关。
- `assets/agentscripts/surface-ssshadow-ab.agentscript.json`：screen-space shadow march 成本对照。
- `assets/agentscripts/surface-tracing-converge.agentscript.json`：SwModern/SwTracing 在高 spp 下
  的 legacy/surface 收敛对照（含 legacy vs legacy 噪声地板）。
- `assets/agentscripts/surface-tracing-restir-upscaler.agentscript.json`：ReSTIR 对照 +
  FSR/TAAU/SGSR2 输入契约冒烟。
- `assets/agentscripts/surface-scheduler-{1080p,4k,dense}.agentscript.json`：调度器 A/B；
  用 `tools/surface_scheduler_table.py <report.json>` 出表。
- `engine.gpuTime.<SCOPED_GPU_TIMER 名>` 是新增的 agent query，脚本用 `assert` 步骤把数值落进
  report。

**DLSS 例外**：`gnb validate` / `gnb shot` 会强制禁用 Streamline，因此上述脚本不能证明 DLSS 生效。
需要验证 DLSS 时必须在 Windows NVIDIA 环境用非 hidden、非 agent-validation 的正常窗口路径。

## 尚未做的事

- **材质特征桶**：只有出现第二种真实 shading 方式（需要独立 kernel 的特殊 BRDF）才立项。
- **调度器扩展到 tracing 系**：SwModern/SwTracing 目前只用解析式 allocation。它们的
  Background/Emissive 像素在一个重得多的 Standard kernel 旁边占比很小，收益不明显。
- **sparse 输入扩展到 SwModern / SwTracing**：机制通用（`Process.Compose` 已经会跳过未着色像素），
  但尚未在这两个 renderer 上实测。
- **Build 与 classification 合并 dispatch**：M4 实测分类只占 0.04–0.09 ms，暂无必要。

## 与其他文档的关系

- [双平面 Visibility Buffer](massive-visibility-buffer-design.md)：Visibility ID 格式与容量契约（不变）。
- [SoftwareModernNoAmbient 渲染与 GTAO](software-modern-noambient-rendering.md)：第一批迁移对象的现状。
- [直接样本后处理与 Upscaler 输入链](direct-sample-post-chain.md)：compose/upscaler 输入契约（不变）。
- [渲染运行时架构与契约](rendering-runtime-architecture.md)：renderer contract、资源状态 tracker 规则。
- 实现入口：`Shader/SurfaceBuffer.slang`、`Shader/ShadingScheduler.slang`、`Core.SurfaceBuild.comp.slang`、
  `common/NoAmbientShading.slang`、`common/GTAOUpsample.slang`、`common/ShadingBucket.slang`、
  `common/PrimaryRayCasters.slang`、
  `common/PathTracingRenderer.slang`、`common/SceneSampling.slang`、
  `Process.CheckerboardResolve.comp.slang`、`PipelineCommon/SurfaceBuildPass.*`、
  `PipelineCommon/ShadingSchedulerPass.*`、`PipelineCommon/SurfaceBufferLayout.hpp`。
