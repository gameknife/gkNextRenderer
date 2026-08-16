---
title: "Visibility Surface / G-buffer / Shading Scheduler 开发计划"
category: plan
status: 待实施
owner: engine/rendering
created: 2026-08-16
last_updated: 2026-08-16
related_design: ../designs/visibility-surface-gbuffer-shading-scheduler.md
---

# Visibility Surface / G-buffer / Shading Scheduler 开发计划

设计决策与契约见[设计定稿](../designs/visibility-surface-gbuffer-shading-scheduler.md)；
本文只记录执行顺序、每个里程碑的范围、验收与回退。里程碑按依赖排序，每个里程碑独立可
提交、cvar 门控、可回退；**不允许跨里程碑合并提交**。

## 全局约束（所有里程碑适用）

- 构建：Engine 层改动用 `./gnb.sh build`（默认 gkNextRenderer + gkNextUnitTests）；不做全量。
- 视觉验证：`gnb shot --scene assets/models/playground.glb`（及至少一个含 emissive 与
  skinned mesh 的场景）；baseline 回归跑 `gkNextVisualTest`。
- 性能记录：新增 pass 一律挂 `SCOPED_GPU_TIMER`；每个里程碑的 journal 必须附 1080p 与 4K
  的 GPU timer 对照表（surface 路径开/关、checkerboard 开/关）。
- 开关：新增 cvar `r.surface.build`（默认 off，直至 M2 验收通过后再讨论默认值）；
  scheduler 相关用 `r.surface.scheduler`（M4）。cvar 注册与默认值遵循
  [Editor Settings 与 CVar 架构](../designs/editor-settings-and-cvars.md)。
- 资源状态：Build pass 的所有 image 状态经 `FResourceStateTracker` 的 `FImageUse` 声明，
  不手写 barrier；renderer contract（`RendererDescriptors`）先于调度改动更新。
- 失败语义：`get_material_data` 拒绝样本按背景处理，与现状一致；不得引入静默 fallback。

## M0 — 契约与脚手架（无行为变化）

范围：

1. 新增 `assets/shaders/Shader/SurfaceBuffer.slang`：`FPrimarySurface`、
   `Surface.Load/Store/IsValid/ReconstructPosition`、背景 depth sentinel 常量、
   feature flags 位定义、material ID 无效值。所有常量以现状值收口（sentinel=1000 等），
   不改变任何现有输出。
2. 新增 C++ layout 头（`src/Engine/Rendering/PipelineCommon/`，对标
   `VisibilityBufferLayout.hpp`）：surface 字段 ↔ RT 槽位/格式对应表。
3. 注册 `r.surface.build` cvar（默认 off）。
4. `RT_BSDF_DATA` 语义扩展先只写文档与 flags 位定义，不改 shader。

验收：编译通过；`gnb shot` 与改动前逐像素一致（无 shader 行为变化）；unit tests 通过。

回退：纯新增文件，直接 revert。

## M1 — SoftwareModernNoAmbient 拆分 Build + Core

范围：

1. 新增 `Core.SurfaceBuild.comp.slang`：从 `Core.SwModernNoAmbient` 中拆出 visibility 解析、
   `get_material_data`、`FetchGBuffer`、motion（含 `RT_MOTIONMOMENT` 副作用）、objectId、
   depth、背景写出；额外写 `RT_BSDF_DATA = {resolved material ID, f16 metalness | flags}`，
   `RT_NORMAL.w` 统一写 roughness（修正现状 w=1 分歧）。Build **永远 full-rate**。
2. `Core.SwModernNoAmbient` 改为读 `Surface.Load`：不再 include VisibilityBuffer、不再调
   `get_material_data`/`FetchGBuffer`；position 由 depth 重建；metalness/roughness/albedo/
   material ID 来自 surface（MRA 采样从 Core 删除）。
3. `SoftwareModernNoAmbientRenderer::Render` 增加 Build dispatch（在 shading 前），image
   transition 相应拆分；`r.surface.build=off` 时走旧单体 shader（保留旧 SPIR-V 路径）。
4. checkerboard resolve：NoAmbient set 收缩为 `RT_SINGLE_DIFFUSE` + `RT_AMBIENT`
   （仅 surface 路径下；旧路径保持旧 resolve set）。
5. GTAO dispatch 前移到 Build 之后、Core 之前（仅 surface 路径下）。合成方式不变，仍由
   `Process.GTAOCompose` 应用。

验收：

- full-rate 下 surface 路径与旧路径 `gnb shot` 视觉一致，`gkNextVisualTest` diff 在阈值内；
- checkerboard 下 depth/normal/motion/objectId 为真 dense（抽查边缘像素无邻居复制值）；
- GTAO 开/关、四档 quality、outline flags、HDR/SDR 全对照；
- journal 附 GPU timer 对照表（见全局约束），并明确回答决策门：NoAmbient 在无 GTAO/无
  SS shadow 场景下 surface 路径是否净变慢；若是，记录 cvar 联动策略建议。

回退：`r.surface.build=off` 即回旧路径；代码层面 revert 单提交。

## M2 — 屏幕空间消费者改读 surface

范围：

1. `Common.ScreenSpaceShadowVisibility` march step 改读 dense depth+normal（surface 路径
   门控分支）；`LoadVisibilitySurfacePlane` 保留为非 surface renderer 的现有路径。
2. NoAmbient Core 的 SS shadow 调用切到 surface 分支。
3. `TraceInScreenSpace` 增加 surface 分支（本里程碑仅实现，不切换调用方——SwTracing 在 M3
   切换）。

验收：

- SS shadow 画质对照（contact shadow 场景 + 快速运动）；march 步进语义不变（fail-open、
  border fade、plane bias 行为保留）；
- GPU timer：受影阴光源较多的场景下 Core 时间应可测下降；journal 记录数值；
- 旧路径（surface off / 其他 renderer）行为不变。

回退：分支门控，off 即旧行为。

## M3 — SoftwareModern / SoftwareTracing 迁移

前置核验（先做再动手，结论写进 journal）：

- `BuildBSDFContext` 与 bounce 链路是否依赖 primary `TexCoord`；
- `PrimaryHit` 中非 dielectric 的 position nudge 如何在 depth 重建路径下复现；
- ReSTIR primary gate（instanceId/motion）从 surface 读取的一致性。

范围：

1. 新增 `FSurfacePrimaryRayCaster : IPrimaryRayCaster`（`PrimaryRayCasters.slang`）：从
   surface 重建 primary vertex 状态，替换 SwModern/SwTracing 入口 shader 里的
   `FVisibilityBufferRayCaster`（surface 路径门控）。
2. `FPathTracingRenderer` 的 surface 类输出（albedo/normal/objectId/motion/depth/BSDF/
   specular albedo）职责移交 Build：surface 路径下 `WriteOutputs` 收缩为仅 lighting 类
   （diffuse/specular/hitdist）。
3. 两个 renderer 的 `Render()` 增加 Build dispatch 与 transition；tracing checkerboard
   resolve set 收缩为 diffuse/specular/两张 hitdist（`RT_SPECULAR_ALBEDO` 归 Build 全率）。
4. SwTracing 的 `TraceInScreenSpace` 调用切到 M2 的 surface 分支。

验收：

- 两个 renderer full-rate 视觉一致 + visual test baseline；
- DLSS/FSR 路径冒烟（upscaler 输入契约未破坏：dense scene color/depth/motion）；
  DLSS 验证遵循 AGENTS.md 的 Streamline 限制（需 Windows 非 hidden 路径时先告知用户）；
- checkerboard 下 resolve 流量收缩生效（timer + 带宽估算记录）；
- ReSTIR 开/关对照无回归。

回退：cvar off 回旧 caster 与旧 resolve set。

## M4 — Shading Scheduler：tile 分类 + indirect dispatch

范围：

1. 新增 classification pass（Build 后）：8×8 tile 粒度，生成 per-bucket tile list +
   per-pixel 位掩码。首批 bucket：`Background`、`Emissive`、`Standard`。
2. 新增 Background / Emissive 轻量 kernel（从现有 Core 的 early-out 写出路径平移）；
   `Standard` kernel 不再含 miss/emissive 分支。
3. `vkCmdDispatchIndirect` 按 bucket 派发；checkerboard 作为 per-bucket rate 作用于
   allocation（`Standard` 可 checkerboard，`Background`/`Emissive` full-rate，成本可忽略）。
4. `ResolveShadingPixel` 从 Core 语义中退役，parity 知识全部收进 scheduler 头。
5. `r.surface.scheduler` 门控；off 时保留 M1–M3 的解析式 allocation 全屏路径做 A/B。

验收：

- A/B：scheduler on/off 视觉一致；GPU timer 对照（分类 + indirect 开销 vs Standard kernel
  去分支收益），1080p/4K、天空占比大/小两类场景都测；
- 若净收益为负，journal 明确记录并保持默认 off——本里程碑的机制价值（为未来材质桶铺路）
  与性能结论分开陈述。

回退：cvar off；classification pass 不运行。

## M5 —（触发条件式，不排期）

以下项只有触发条件满足才立项，不属于本计划的承诺范围：

- **材质特征桶**：出现第二种真实 shading 方式（独立 kernel 的特殊 BRDF）时，由 material ID
  查 feature mask 归桶；bucket 数以 GPU profiler 为准。
- **Native TAAU sparse 输入**：在 missing-pixel mask 前提下评估 checkerboard lighting 不做
  resolve、由 TAAU 直接利用历史；外部 upscaler 继续 dense contract。
- **GTAO 合成下沉**：Core 直接消费 AO、删除独立上采样；需先解决非 temporal / 截图路径
  fallback。
- **Build 与 classification 合并 dispatch**：仅当 M4 实测分类读回成本显著时评估。

## 里程碑依赖与产出总览

```text
M0 契约脚手架
 └→ M1 NoAmbient Build+Core 拆分（决策门：轻 Core 带宽实测）
     └→ M2 SS shadow / SS tracing 消费者
         └→ M3 SwModern + SwTracing（FSurfacePrimaryRayCaster）
             └→ M4 Scheduler（tile 分类 + Background/Emissive/Standard buckets）
                 └→ M5 触发条件式扩展
```

每个里程碑完成后按 Spec Workflow 写 journal；性能对照表与决策门结论是 journal 的必填项，
不写数字不算完成。
