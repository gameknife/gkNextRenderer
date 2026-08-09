---
title: "PathTracing Slang Shader 库重构计划"
category: plan
status: 计划中
owner: engine/rendering
created: 2026-07-24
last_updated: 2026-07-24
---

# PathTracing Slang Shader 库重构计划

从 `FPathTracingRenderer` 入手，把 `PathTracing` Slang 模块（`assets/shaders/Shader/PathTracing.slang`
聚合的一组 `implementing PathTracing` 片段）梳理得更易读、优雅、合理，在**肉眼等价 / 噪声容差内**
保持效果不变。本计划分阶段执行，每阶段独立可验证；**深度阶段（统一材质模型 / 重构 bounce loop）
需单独授权后才动手**。

## 0. 决策记录（2026-07-24，已与 owner 敲定）

1. **重构深度**：分阶段。Stage 1–2（结构性重构）可在本计划批准后执行；Stage 3（深度：统一 legacy
   scatter 与 BSDF 重要性采样为单一材质模型、重构 bounce loop）作为独立阶段，动手前 checkpoint 一次
   （补 design 增量 + owner 过目）。RNG 放宽后 Stage 3 已从"高风险漂移"降为本次重构的正题。
2. **效果验收标准**：**理论正确即可**（2026-07-24 二次放宽，初版为"肉眼等价/噪声容差内"）。
   **不要求保持 RNG 调用序列，允许输出与旧版有差别**——不同的噪声实现、甚至不同的收敛值都可接受，
   只要新实现是理论正确的估计器：
   - **reference 路径**（PathTracing 全 bounce、`FNullRadianceCache`、无 ReSTIR）收敛到**无偏正确解**，
     并作为其它路径的 ground truth。
   - **缓存/近似路径**（SHARC / AmbientCube terminal、`ExitAfterFirst`、SwModern）正确实现其
     **既定近似**——不要把有意的 GI 近似"修"成别的东西（近似的收敛目标是 reference 路径，允许有
     approximation gap，但 gap 的性质不能变）。
   验证从"对齐旧截图"改为"对齐理论正确参考"（见 §7）。
3. **文件重组**：是。拆分 `Shading.slang`、删死代码、按职责重命名文件，并同步更新
   `Shader/PathTracing.slang` 聚合顺序。

## 1. 现状诊断（重构目标）

**要保留并强化的骨架**：`IRayTracer / IDirectIlluminator / IRadianceCache / IPrimaryRayCaster`
四个接口 + Slang 泛型单态化，是干净的抽象；`common/BSDF.slang` 已是材质数学的单一来源；
`IRadianceCache` 把 SHARC/ReSTIR 与渲染器解耦。重构顺着这套骨架做，不推翻。

**碎点清单**（按优先级）：

| # | 问题 | 位置 |
|---|---|---|
| P1 | **两套并存材质模型**：路径延续用 legacy 散射（`Schlick`/`ggxSampling`/`chance*`），直接光/ReSTIR 用 `BSDF.slang` 重要性采样。同一渲染器两套 GGX/Fresnel。 | `PathTracingRenderer.slang:809` `GetRayColor` ↔ `BSDF.slang`；helper 在 `ConstFunc.slang:7,314` |
| P2 | **`GetRayColor` 巨函数**：~120 行混采样/追踪/发光体/终止，7 个 out/inout 参数，`float4(0.5,0.5,0.5,0)` 魔法值。 | `PathTracingRenderer.slang:809-931` |
| P3 | **`Render` 主循环交织**：`b==0` 特判与通用 bounce 逻辑纠缠，`SecondaryDirectMode` 三分支内联，一次迭代直接光求值 2~3 次，累积 flag 手工穿线。 | `PathTracingRenderer.slang:933-1148` |
| P4 | **两个直接光照器逐行重复**：`FHardwareDirectIlluminator` 与 `FSoftwareTracingDirectIlluminator` 的 Sun/Light/Primary + ReSTIR 分支结构相同，仅差 tracer 类型与太阳阴影方式（≈120 行重复）。 | `PathTracingRenderer.slang:212-330, 470-570` |
| P5 | **重复灯扫描**：`IsRegisteredAreaLightHit` / `IsRegisteredLightHit` / `GetRegisteredAreaLightPdf` 同构循环。 | `PathTracingRenderer.slang:613-687` |
| P6 | **太阳盘常量 `cos(0.25°)` / 立体角** 5 处重复；`TracingExtras` 可用性/参数表样板 SHARC/ReSTIR 各写一遍。 | `PathTracingRenderer.slang:171,187,202,766`；`Sharc.slang:9-23`；`Restir.slang:20-34` |
| P7 | **命名/风格考古分层**：`RandomSeed_` vs `randomSeed`、`Camera` vs `camera`、`trace_dir`/`niOverNt` snake_case、中英混注。 | 全库 |
| P8 | **`Shading.slang` 杂物间**：接口 + primary caster + 一堆 `Common` helper 混装，含 ~160 行 `#if 0` legacy DoF 死代码。 | `Shading.slang:401-559` |

## 2. 不变量与护栏（任何阶段都必须守住）

摘自 `docs/designs/pathtracing-restir-design.md` §9 与 `RadianceCache.slang` 注释，是本次重构的红线：

1. **~~非缓存路径的 RNG 调用序列~~（已放宽，不再要求）**：owner 2026-07-24 明确 RNG 序不必保证、
   输出可有差别、理论正确即可。因此**允许自由改动 `RandomFloat*` 的调用位置/次数与采样策略**。
   代价：不能再用"对齐旧截图"做逐位回归，验证改为"收敛到理论正确参考"（见 §7）。
   缓存实现"只在需要时抽随机数"的**接口契约**仍保留（`FNullRadianceCache` 路径不应因缓存插桩产生
   无意义随机数开销），但不再要求与旧版逐位一致。
2. **Slang 裸声明 struct 不保证应用字段默认初始化器**：新增 struct 字段必须给显式 `__init`，
   所有声明点显式赋值（未初始化的 `RestirPrimary` 曾致 device lost）。
3. **同一 invocation 内 storage image 写后读不可见**：当前像素 primary 数据（instanceId/motion）
   只能从 renderer 状态传参，不能回读本 dispatch 刚写的 G-buffer。
4. **竞态关键逐帧状态走录制 push constant**（`CustomData0/1`），不放 host-visible buffer。
5. **空间复用结果不回写时域历史**（ReSTIR §4）。
6. **单一 evaluator**：p̂ / ReSTIR shading 用 `Common.EvaluateLightDiffuseSample`；经典 NEE 与它共享
   `EvaluateSurfaceBSDF`。重构不得产生第二套 evaluator（P1 的方向是**收敛到一套**，不是再加一套）。
7. **5 个入口 shader 的参数语义**：`Core.PathTracing` / `Core.SharcUpdate` / `Core.SharcQuery` /
   `Core.SwTracing` / `Core.SwModern` / `Core.SwModernNoAmbient` 各自对 `FPathTracingRenderer`
   的字段组合（`ExitAfterFirst`/`ForceExitAfterFirst`/`SampleDownscale`/`SecondaryDirectMode`/
   `UseAmbientCubeTerminal`/`WriteOutputs`/`FuzzyTracing`/`EnableSoftTracingAdaptiveOffset`）语义
   必须逐一保持。重构公共 API 时先把这张组合表钉死（见 §5 表）。

## 3. 阶段划分

### Stage 0 — 基线固定（前置，必做）

产出重构前的参考图与数值基线。用途分两层：Stage 1 拿它做 bit 级对照；Stage 2/3 因允许输出变化，
它退为**定性 sanity 参考**（有没有把某条路径改崩），真正的正确性判据是"PathTracing 全 bounce 无缓存
路径收敛出的 ground truth"（见 §7）——这条参考应在 Stage 0 就先跑一版收敛图存档。

- `gnb shot --scene ManyLightsShowcase.proc`、`CornellBox.proc`、`MaterialShowcase.proc`
  （PathTracing / SoftwareTracing / SwModern 三条渲染路径各截一组）。
- 跑现有 agentscript：`restir-*`、`bsdf-direct-classic-smoke`、`bsdf-direct-smoke`、
  `swrestir-converge-nee` / `swrestir-converge-ris`，保存输出。
- SHARC 路径：开 `r.sharc` 截 `gnb shot` 一组，含 debug occupancy/stale 视图。
- 记录基线到 `.spec/journal/` 或本计划附录（截图路径 + signed-mean）。

> 无 C++/CMake 改动，纯截图 + 数值留档。

### Stage 1 — 结构性重构（低风险，纯机械改动）· 本计划批准后可执行

顺序按"改动面从小到大、越靠后越接近核心 loop"排列，每小步独立构建 + 截图对照。
Stage 1 全是**等价变换**（抽 helper、合并同构照明器、重命名、移动文件），逻辑不变，因此**天然应保持
bit 级一致**——即使 RNG 约束已放宽，此阶段若出现可见差异，通常意味着抽取引入了 bug，应回退定位而非
接受。

- **1a 公共常量与访问器收敛**（P6）
  - 新增 `FSunDisk`（`common/Lighting.slang` 或就近）：集中 `cosRadius = cos(0.25°)`、
    `solidAngle`、`sunPdf`、`SampleSunDirection`、`EvaluateAnalyticSunDisk`。替换 5 处内联常量。
  - `TracingExtras` 访问器统一：`FTracingExtras* GetTracingExtras()` + 字段级可用性判定
    （`SharcIsAvailable`/`RestirIsAvailable` 复用它），消掉 SHARC/ReSTIR 各写一遍的 null 判。
  - 风险：极低，纯提取，RNG 序不变（`SampleSunDirection` 内部随机数调用位置保持一致）。

- **1b 灯扫描去重**（P5）
  - 抽 `IterateLights` 风格 helper 或一个 `FRegisteredLightQuery { bool isHit; float pdfOmega; }`
    返回结构，让 `IsRegisteredAreaLightHit`/`IsRegisteredLightHit`/`GetRegisteredAreaLightPdf`
    共用一条扫描。注意三者的命中判据（area vs point、是否要 pdf）差异要参数化，不能合并语义。
  - 风险：低，纯函数无随机数。

- **1c 合并两个直接光照器**（P4，Stage 1 收益最大项）
  - 把 `FHardwareDirectIlluminator` 与 `FSoftwareTracingDirectIlluminator` 的公共主体（`SunIlluminate`
    的 BSDF 累积、`LightIlluminate` 的 NEE、`DirectIlluminatePrimary` 的 ReSTIR 分支）提成**泛型
    基底**，差异项抽成策略：
    - 可见性 tracer：已是 `IRayTracer`，泛型参数化即可。
    - **太阳阴影方式**：HW 走 `TraceOcclusion(sampledDir)`；SW 走 `SampleSunShadowCSM` + CSM。
      抽成一个 `ISunShadow`（或 `SunShadow` 小接口）由两个照明器分别提供。
    - shadow-origin 偏移细节（SW 的 cascade 自适应）留在各自实现。
  - 首选方案：`struct FDirectIlluminator<TTracer : IRayTracer, TSun : ISunShadow> : IDirectIlluminator`，
    Hardware/SoftwareTracing 退化为 typedef/thin wrapper。若泛型嵌套导致 Slang 单态化问题，
    退化为一个共享 `namespace Common` 的自由函数 + 两个薄 struct 转发。
  - **护栏**：`RestirPrimary`/`PrimaryInstanceId`/`PrimaryMotionPixels` 字段与显式 `__init` 必须
    在合并后仍逐点保留（§2-②）。RNG 序：SunIlluminate → LightIlluminate 的随机数消费顺序保持。
  - 风险：中低。这是 Stage 1 里最需要仔细对照 RNG 的一步，单独一次提交 + 全路径截图。

- **1d 命名统一**（P7）
  - 成员统一 `camelCase_`（`RandomSeed_`→`randomSeed_` 等，但注意这是 mutating 方法遍布的核心
    struct，改名要一次性全量替换）；局部与参数 `camelCase`；干掉 `trace_dir`/`niOverNt`/`trace_next`
    等 snake_case。注释语言统一（保留必要中文设计注释，术语英文）。
  - 风险：低（纯重命名），但改动面广，独立提交、靠编译器兜底。

- **1e 文件重组 + 删死代码**（P8，见 §4 目标布局）
  - 删 `Shading.slang:401-559` 的 `#if 0` legacy DoF caster。
  - 拆 `Shading.slang` → `Interfaces.slang`（4 个 interface）+ `PrimaryRayCasters.slang`
    （3 个 caster）+ `SceneSampling.slang`（motion/SH/IBL/gbuffer/screenspace/EncodeObjectId）。
  - 直接光照相关（`FSunDisk`、照明器、`EvaluateLight*`、`SelectLight`）归入 `Lighting.slang`；
    `FPathTracingRenderer` 独占 `PathTracingRenderer.slang`。
  - 同步更新 `Shader/PathTracing.slang` 的 `__include` 顺序（注意 `implementing` 片段间的符号
    依赖顺序）。
  - 风险：低（移动为主），但 `__include` 顺序错会编译失败，靠 `gnb build` 兜底。

**Stage 1 验收**：`gnb build`（PathTracing/UnitTests）+ `gnb build --all` 抽验受影响入口；
全部 Stage 0 截图逐一对照，因是等价变换应保持 bit 级一致（signed-mean ≈ 0）；有差异即定位 bug。

### Stage 2 — 主循环与 GetRayColor 拆解（中风险）· 批准后可执行

此阶段**不改材质模型**，只重整控制流与冗余求值。RNG 约束已放宽，可直接去掉可证明冗余的直接光求值
（哪怕这改变随机数序列），只要路径构建仍理论正确、收敛到与 reference 一致的解。

- **2a `GetRayColor` 拆段**（P2）
  - 拆成 `SampleScatterDirection`（选 reflect/metal/refract + 出射方向）、`TraceSegment`
    （追踪 + albedo 累乘）、`ClassifyTerminal`（发光体/miss/backface 终止）三段。
  - 用返回 struct `FBounceResult { float3 direction; bool exited; bool hitMiss; float hitDist;
    bool hitReflect; bool hitMetal; float3 terminalRadiance; }` 取代 7 个 out/inout 参数。
  - `float4(0.5,0.5,0.5,0)` backface 魔法值提成具名常量并注释来源（或在 Stage 3 里随统一模型移除）。
  - RNG 序不必保留，但拆段后散射方向的**采样分布**必须与原先等价（否则不是"重整"而是"改模型"，
    那属于 Stage 3）。

- **2b `Render` 主循环重整**（P3）
  - 把 `b==0` first-bounce tail 与后续 bounce 的 secondary-direct/RR 逻辑拆成清晰的
    `HandleFirstBounceTail` / `HandleSecondaryDirect` / `HandleRussianRoulette`，主循环只留骨架。
  - 收敛一次迭代内的直接光求值：目前 `startLighting` / `segmentLighting` / secondary
    `directLighting` 分散求值，梳理成单一路径（在肉眼等价容差内可去掉可证明冗余的求值）。
  - `SecondaryDirectMode` 的三分支用具名 helper 或 `switch` 表达，去掉裸 `== 0u/1u/2u` 魔法数。
  - 累积 flag（hitReflect/hitMetal/hitDist 仅取 bounce 0）用 `FPathAccumulator` 收拢。
  - **护栏**：cache 回调（`BeginPath`/`OnSurfaceHit`/`OnSegmentThroughput`/`OnMiss`/
    `TryQueryRadiance`）的**语义**（每个 hit/segment/miss 记一次、throughput 是 after/before 比值）
    对 SHARC 记账至关重要——即使随机数序列变了，记账语义不能变，重整后必须逐一对照 SHARC
    debug 视图与收敛图。

**Stage 2 验收**：全路径 `gnb shot` + `swrestir-converge-*` / `bsdf-direct-*` agentscript；
各路径收敛结果与 Stage 0 的 PathTracing ground-truth 一致（近似路径保持既定 gap）；
SHARC occupancy/stale 视图无结构性变化。

### Stage 3 — 统一材质模型（深度）· ⚠ 动手前 checkpoint

**这是本次重构的正题。** owner 已放宽 RNG/输出约束（理论正确即可），本阶段风险从"输出漂移"降为
"实现正确性"——不再需要匹配旧输出，只需新模型是无偏正确的 PT。目标是消除 P1 的两套材质模型：让
路径延续（scatter）与直接光照共用 `BSDF.slang` 的一套 GGX/Fresnel/Smith 与重要性采样，形成 MIS
一致的单一路径构建。

> **门槛已降低但仍建议 checkpoint 一次**：动手前补一份 design 增量（描述统一后的采样/throughput/MIS
> 与各近似路径如何落到新模型上），owner 过一眼即可开工；不再是硬性"逐场景容差判定"的重门。

- 候选方向（授权后再细化设计文档）：
  - 用 `SampleGlossyProposal` / `EvaluateSurfaceBSDF` 体系替换 `GetRayColor` 里的
    `ggxSampling`+`Schlick`+`chance*` 散射，统一 Lambert/GGX/Dielectric 的采样与 throughput。
  - bounce loop 改为标准 throughput × BSDF / pdf 累乘 + NEE MIS，去掉 legacy 的 albedo 累乘
    与 `chanceReflect` 抑制发光体的特判逻辑。
  - `ConstFunc.slang` 的 `Schlick`/`ggxSampling` 在无其它引用后移除或降级。
- **必然后果**：RNG 序与噪声实现改变，累积图与旧版不同——这是**预期且被允许的**。验收 = 新模型
  offline progressive 收敛后是否得到理论正确解：reference 路径可与已知正确的解析/参考场景对照
  （CornellBox 能量、MaterialShowcase 各 lobe 行为），近似路径对照 reference。
- 收益：这是本次重构最优雅的一步，消除全库最大的碎点（两套 GGX/Fresnel、legacy `chance*` 特判、
  albedo 累乘 hack）。放宽约束后风险主要落在"是否真的无偏"，用收敛对照即可判定。

## 4. 目标文件布局（重组后）

```
assets/shaders/Shader/PathTracing.slang        # 聚合入口，__include 顺序按依赖
assets/shaders/common/
  BasicTypes.slang          # 数据布局（不动）
  BSDF.slang                # 材质数学单一来源（不动；Stage 3 才扩展为散射统一入口）
  Interfaces.slang          # IPrimaryRayCaster / IRayTracer / IDirectIlluminator / IRadianceCache（← 从 Shading/RadianceCache 抽）
  PrimaryRayCasters.slang   # FHardware/FVisibilityBuffer/FVoxel RayCaster（← 从 Shading 抽；删 #if0 DoF）
  SceneSampling.slang       # motion/SH/IBL/gbuffer/screenspace/EncodeObjectId/PageIndex（← 从 Shading 抽）
  RayTracers.slang          # 4 个 IRayTracer 实现（基本不动）
  Lighting.slang            # FSunDisk + SelectLight + EvaluateLight* + 合并后的直接光照器（← 从 PathTracingRenderer 抽）
  RadianceCache.slang       # IRadianceCache + FNullRadianceCache（接口迁往 Interfaces 后仅留 null cache，或整体并入）
  Sharc.slang               # SHARC 缓存（复用统一 TracingExtras 访问器）
  Restir.slang              # ReSTIR reservoir + gather
  RestirSpatialShade.slang  # ReSTIR pass 2
  PathTracingRenderer.slang # 仅 FPathTracingRenderer
  ConstFunc / GeneralFunc / AmbientCube* / Tonemap / GPUScene  # 现状保留
```

> 文件命名与拆分可在 1e 落地时按 Slang `implementing` 片段的符号依赖微调；关键是**职责单一**、
> 接口集中、`Shading.slang` 这个杂物间消失。

## 5. 入口参数组合表（重构公共 API 前先钉死）

重构 `FPathTracingRenderer` 字段/方法签名时，下表任一格语义都不能变（值来自各入口 shader 当前设置）：

| 字段 \ 入口 | PathTracing | SharcUpdate | SharcQuery | SwTracing | SwModern(NoAmbient 前身) |
|---|---|---|---|---|---|
| `ExitProbability` | 0.5 | 0.01 | 0.01 | 0.5 | 0.5 |
| `ExitAfterFirst` | Camera | false | false | Camera | true |
| `ForceExitAfterFirst` | false | false | false | false | true |
| `SampleDownscale` | 1 | 2 | 1 | 2 | 2 |
| `FuzzyTracing` | false | false | false | false | true |
| `UseAmbientCubeTerminal` | true(默认) | false | false | true | true |
| `WriteOutputs` | true | **false** | true | true | true |
| `SecondaryDirectMode` | 0 | 2 | 1 | 0 | 0 |
| `EnableSoftTracingAdaptiveOffset` | false | false | false | **true** | false |
| tracer | HW | HW | HW | SW | AmbientCubeIndirect |
| illuminator | HWDirect | HWDirect(Restir=false) | HWDirect | SwTracingDirect | ShadowMapDirect |
| cache | Null | SharcUpdate | SharcQuery | Null | Null |

（`Core.SwModern.comp.slang` 用 `FAmbientCubeRayTracerIndirect` + `FShadowMapDirectIlluminator`；
`Core.SwModernNoAmbient.comp.slang` 是**独立手写栅格化 shader，不经 `FPathTracingRenderer`**——见 §6。）

## 6. 明确不动的部分（本 agent 已决策，记录理由）

- **`Core.SwModernNoAmbient.comp.slang` 的私有 BRDF**（`DistributionGGX`/`GeometrySmith` 用
  `k=(r+1)²/8` 实时近似、`FresnelSchlick`/`MaterialF0`）**保持独立，不并入 `BSDF.slang`**。
  它是栅格化 GI 路径，`pathtracing-restir-design.md` §1 明确警告不得把该实时近似带回 tracing BSDF；
  反向合并同样会无意改变 SwModern 成像。仅在其内部做命名/注释清理（若纳入 Stage 1d）。
- **`ConstFunc.slang` 的 `Schlick`/`ggxSampling`/`RandomInHemiSphere1`**：在 Stage 3 统一材质模型
  前保留；Stage 1–2 不动它们，以维持 RNG 序。
- **`BasicTypes.slang` 数据布局**、reservoir pack/unpack 位域、`TracingExtras` 内存契约：不动
  （改这些会牵动 C++ 侧与跨帧兼容）。

## 7. 验证闭环（每阶段套用）

1. **构建**：Engine 层改动 → `gnb build`（默认 `gkNextRenderer` + `gkNextUnitTests`）；文件重组或
   新增文件 → 加 `--reconfigure`；Stage 结束抽验 `gnb build --all` 确认全 program 不破。
2. **渲染验证**：`gnb shot --scene <X>` 各路径（PathTracing/SoftwareTracing/SwModern/SHARC）。
   - Stage 1（等价变换）：对照 Stage 0 截图应 bit 级一致，有差异即 bug。
   - Stage 2/3（允许输出变化）：判据不是"对齐旧截图"，而是**收敛正确性**——PathTracing 全 bounce
     无缓存路径作为 **ground truth**，用 offline progressive 收敛出干净参考；近似路径（SHARC/
     AmbientCube/SwModern）对照该 ground truth，approximation gap 的性质应保持不变。
3. **数值/收敛**：`swrestir-converge-nee|ris`、`bsdf-direct-classic-smoke|smoke`、`restir-*`
   agentscript 收敛后自检：能量守恒、各 lobe 行为、无 firefly/漏光；不再要求 signed-mean≈0。
4. **回滚点**：每小步（1a…2b…3）独立提交，出现不可解释的行为（非预期的能量变化、结构性漏光）
   即回退到上一提交定位。

## 8. 未决 / 待授权

- **Stage 3（统一材质模型）**：RNG 约束放宽后门槛已降；动手前补一份 design 增量、owner checkpoint
  一次即可开工（不再是硬性逐场景容差评审）。owner 也可在完成 Stage 1–2 后直接 green-light。
- **是否顺带把 `RadianceCache` 的 `IRadianceCache` 接口并入 `Interfaces.slang`**：1e 落地时按编译
  依赖决定，不影响语义。
- **泛型直接光照器（1c）的具体形态**（嵌套泛型 vs 共享自由函数）：取决于 Slang 单态化对
  `IDirectIlluminator` + 内部 `IRayTracer`/`ISunShadow` 双泛型的支持度，实现时以能编过且不牺牲
  可读性为准。
