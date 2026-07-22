---
title: "SoftwareTracing Direct Lighting 与 ReSTIR DI 同步计划"
category: plan
status: 已实施（Android 平台复验受本机工具链阻塞）
owner: engine/rendering
created: 2026-07-22
last_updated: 2026-07-22
---

# SoftwareTracing Direct Lighting 与 ReSTIR DI 同步计划

> 2026-07-22 已完成 M0–M3 与 M4 的主机端交付。现行架构、性能数据、验证入口与修改护栏见
> [Tracing Direct Lighting 与 ReSTIR DI 架构](../designs/pathtracing-restir-design.md)；本文件仅保留为实施审计记录，不再是待办入口。
> macOS shader/C++、单测、同步验证、收敛、运动、切换、性能和 VisualTest 已通过；Android
> `gradle build` 在 native 配置前因 Android SDK 缺少项目指定的 CMake 3.31.6 中止，需在具备
> 对应 SDK component 的环境补跑，不能视为 Android shader 已验证。

本文规划把 PathTracing 已有的面光源 Direct Lighting 与 primary-surface ReSTIR DI
同步到 SoftwareTracing。现有算法与估计器约束以
[Tracing Direct Lighting 与 ReSTIR DI 架构](../designs/pathtracing-restir-design.md)为基线；本文只描述
SoftwareTracing 的差异、公共层重构、执行顺序和验收口径。

里程碑必须按 M0 → M4 顺序执行。每期结束先完成该期验收，再进入下一期；
`r.restir.enable=false` 始终表示“经典单样本面光 NEE”，而不是退回当前
SoftwareTracing 缺失面光 NEE 的状态。

## 1. 当前缺口与目标状态

### 1.1 当前代码事实

- `Core.SwTracing.comp.slang` 使用 `FShadowMapDirectIlluminator`，只计算太阳 CSM，
  没有注册面光源的 NEE。
- `FPathTracingRenderer` 的首个 diffuse bounce 已按“存在面光 NEE”抑制命中已注册
  emitter 的重复贡献；SoftwareTracing 没有对应 NEE 时，这会直接丢失本应出现的面光贡献。
- `FSoftwareRayTracer::TraceSegment()` 当前固定返回 `false`，也没有
  `TraceAreaLightSegment()`。直接复用 ReSTIR 会把所有软件 shadow segment 当作无遮挡，
  产生系统性漏光。
- `RestirPrimaryGather()`、`Core.RestirSpatialShade.comp.slang` 和 C++ reservoir 生命周期
  都绑定在 Hardware/PathTracing：shader 硬编码 `FHardwareRayTracer`，第二阶段需要 TLAS，
  资源状态内嵌于 `PathTracingRenderer`，扩展表名也仍是 `FPathTracingExtras`。
- 两个 renderer 已经共享 `FPathTracingRenderer` 的 primary G-buffer 输出和
  `SamplePostChain`，因此 ObjectId、motion、depth、normal、single diffuse 这些 ReSTIR
  输入不需要另建一套纹理或颜色 history。

### 1.2 目标状态

```text
SoftwareTracing main dispatch
  primary hit + software indirect path
  CSM sun direct
  ├─ r.restir.enable=false: 1-sample area-light NEE + finite DDA visibility
  └─ r.restir.enable=true : initial RIS + finite DDA visibility + temporal merge
                              -> intermediate reservoir
  [buffer + G-buffer barrier]
Core.SwRestirSpatialShade
  shared spatial/visibility/shading algorithm + software finite DDA visibility
  -> final reservoir + RT_SINGLE_DIFFUSE += area direct
  [SamplePostChain]
```

PathTracing 保持相同估计器和现有硬件可见性；公共重构完成后，两条 renderer 只在
“面光有限段是否被遮挡”这一策略上不同。

## 2. 已选边界与待覆盖假设

| 议题 | 本计划采用的方案 | 原因 |
|---|---|---|
| CVar | PathTracing/SoftwareTracing 共用 `r.restir.*` | 参数语义和算法相同，避免两套配置漂移 |
| 默认值 | 保持当前代码的 `r.restir.enable=false` | SoftwareTracing 是非 RT/移动端回退路径；1080p 双 reservoir 约 63 MiB，不应无提示默认分配 |
| 关闭 ReSTIR | 仍运行经典 1-sample 面光 NEE | ReSTIR 是采样器替换，不是 Direct Lighting 总开关；也与 PathTracing 语义一致 |
| 太阳 | 继续走 CSM，不进入 reservoir | 太阳是低方差 delta 光，且 CSM 是 SoftwareTracing 的既有契约 |
| 面光可见性 | 纯软件、有限长度、open-segment 的级联 voxel DDA | 保持无 TLAS 平台可用；不能在 SwTracing 内偷偷依赖 ray query |
| 作用范围 | 只改 SoftwareTracing；不改 SoftwareModern | 两者虽共用 `FShadowMapDirectIlluminator`，本任务没有授权改变 raster+GI renderer 的光照外观 |
| ReSTIR 范围 | 只替换 primary diffuse 面光 NEE | 不扩展太阳、specular direct、secondary bounce、SHARC 或 ReSTIR GI |
| progressive | 任一 renderer 进入 progressive accumulation 时都降级为 RIS-only | 保证与经典 NEE 收敛到同一参考，不把 reservoir history 混入离线颜色累积 |
| 多视图 | 仍只允许 primary view 使用 ReSTIR | reservoir 按 primary render extent 分配；Transient/缩略图继续走经典 NEE |

若实施前决定让 SoftwareTracing 默认开启 ReSTIR，或需要 renderer 独立 CVar，必须先修改本节、
内存/性能验收和 `UserSettings` 迁移策略，不能在 M4 临时改默认值。

## 3. 目标架构

### 3.1 Shader 公共层

1. 保持 `Common.EvaluateAreaLightSample()` 为经典 NEE、ReSTIR p-hat 和最终 shading 的
   唯一面光几何/辐射来源。
2. 把 `RestirPrimaryGather()` 改为对 tracer 泛型化，不再接收具体
   `FHardwareRayTracer`。
3. 把 `Core.RestirSpatialShade.comp.slang` 的重建、空间合并、历史写回、debug 和 shading
   主体下沉到公共函数；保留两个薄入口：
   - `Core.RestirSpatialShade.comp.slang`：实例化 `FHardwareRayTracer`；
   - `Core.SwRestirSpatialShade.comp.slang`：实例化 `FSoftwareRayTracer`。
4. 两个入口分别使用 `ZeroBindWithTLASPipeline` 和 `ZeroBindPipeline`，不使用 runtime
   renderer 分支，也不让软件平台创建空 TLAS descriptor。
5. 新增 SoftwareTracing 专用 direct illuminator：太阳部分复用 CSM，面光部分使用
   software tracer。不要直接给 `FShadowMapDirectIlluminator` 增加面光逻辑，以免
   SoftwareModern 被隐式改变。

### 3.2 软件有限段可见性契约

`FSoftwareRayTracer` 必须补齐真正的有限段查询，ReSTIR 接入不得以现有恒 `false` 的
`TraceSegment()` 作为占位实现：

- 查询区间是 shading point 与 sampled emitter point 之间的 open segment；终点 margin
  与硬件路径同语义，不能把目标 emitter 自身当成 blocker。
- 最大 DDA 距离必须来自 segment length，不能沿用太阳遮挡的固定 80m。
- 起点偏移按所在 AmbientCube cascade 的 voxel unit 自适应，不能照搬硬件路径的
  `EPS2`。要同时覆盖近处自遮挡、远处深度重建误差和薄遮挡体漏过三类测试。
- 有限段函数只回答遮挡，不混用 screen-space trace；否则相机外 blocker、当前帧 depth
  和 temporal reservoir 会形成视角相关的可见性不一致。
- 若粗 voxel 使目标 emitter 所在 cell 提前命中，优先修正 open-segment/目标 cell 规则，
  不得简单忽略所有 `MaterialDiffuseLight` voxel（其他 emitter 仍应遮挡）。

### 3.3 C++ 资源与生命周期

新增 `PipelineCommon::RestirDI`（建议文件
`src/Engine/Rendering/PipelineCommon/RestirDI.{hpp,cpp}`），由 `VulkanBaseRenderer` 单例式
持有、按需分配，PathTracing 和 SoftwareTracing 共用同一对 reservoir：

- 统一拥有 ping/pong、runtime parameters、extent、pending clear、frame stamp、
  `lastFrameIndex/lightsGeneration/historyGeneration` 和 buffer barrier。
- renderer 切换时依赖现有 `historyGeneration` 失效首帧 history；第一帧全覆盖
  intermediate/final 后，第二帧才能 temporal reuse。
- 两个 logic renderer 常驻时不得各自保留一份 63 MiB reservoir；切换 renderer 后
  GPU 内存仍只有一份。
- 只在 `r.restir.enable && primary view` 时确保资源；关闭状态不新分配。已分配资源可保留到
  swapchain/device 生命周期结束，避免 CVar 抖动造成 GPU allocation churn。
- runtime 调参写入沿用 host-visible parameters；`temporalValid` 等逐帧竞态位继续通过
  已录制的 `GPUScene.CustomData1` 传递。
- 将不再只属于 PathTracing 的 `FPathTracingExtras/PtExtras` 语义化重命名为
  `FTracingExtras/TracingExtras`，但保持 64B 布局和字段偏移不变。PathTracing 表同时填
  SHARC + ReSTIR 地址；SoftwareTracing 表只填 ReSTIR 地址。每张表仅在地址变化时写入，
  禁止逐帧覆盖 host-visible 地址表。

## 4. 里程碑

### M0：公共地基与软件可见性

**目标**：两种 tracer 能实例化同一套 ReSTIR 算法；功能仍只在 PathTracing 生效，
PathTracing 画面和性能无回归。

**任务：**

1. 提炼 `PipelineCommon::RestirDI`，把 reservoir 分配、参数更新、clear、barrier、history
   连续性从 `PathTracingRenderer` 移出；PathTracing 先迁移到公共服务。
2. `FPathTracingExtras` 无布局变化地重命名，并同步 C++/Slang/Apple 地址访问器、
   `static_assert` 和 SHARC 可用性判断。
3. 泛型化 gather 与 spatial-shade 主体，保留现有硬件入口作为第一个实例化调用点。
4. 为 `FHiVoxelDDARayTracer/FSoftwareRayTracer` 增加有限距离 occlusion 和
   `TraceAreaLightSegment`；为 software shadow origin 建立单一 helper，经典 NEE 与 ReSTIR
   两个阶段共用。
5. 在 M0 就新增但暂不 dispatch `Core.SwRestirSpatialShade.comp.slang` 薄入口，让
   hardware/software 两种模板实例化都进入正常 shader 构建；新增文件后执行一次
   `--reconfigure`。

**验收：**

- `./gnb.sh build gkNextRenderer gkNextUnitTests --reconfigure` 通过。
- PathTracing 的既有 `restir-m1/m2/m3` agentscript 结果不变；ReSTIR on/off 截图与重构前
  对照无结构性 diff。
- `r.restir.enable=false` 时不创建 reservoir；开启后日志只出现一份 allocation。
- 专用可见性 fixture 同时验证：无 blocker 为 visible、box blocker 为 occluded、目标 emitter
  不自遮挡、目标前另一个 emitter 仍遮挡、超过 80m 的有限段仍按真实终点判断。

**停止条件：** software DDA 无法稳定区分目标 emitter cell 与 segment 内 blocker 时，
先记录截图/距离/cascade 数据并回到可见性契约设计，不进入 M1，更不能用“恒无遮挡”降级。

### M1：SoftwareTracing 经典面光 Direct Lighting

**目标**：`r.restir.enable=false` 下，SoftwareTracing 具备与 PathTracing 同估计器语义的
单样本面光 NEE；太阳继续保持原 CSM 结果。

**任务：**

1. 新增 `FSoftwareTracingDirectIlluminator`（最终命名可按现有文件语境调整）：
   `SunIlluminate=CSM`，`AreaIlluminate=SelectAreaLight + EvaluateAreaLightSample + software
   TraceAreaLightSegment`。
2. `Core.SwTracing.comp.slang` 改用该 illuminator；ReSTIR 尚未打开时
   `DirectIlluminatePrimary()` 直接走经典 area NEE。
3. 确认 registered-emitter suppression 与新增 NEE 成对生效，避免首 bounce 双计或漏计。
4. 保持 `Core.SwModern.comp.slang` 和 `FShadowMapDirectIlluminator` 行为逐位不变。

**验收：**

- `ManyLightsShowcase.proc`（无太阳/天空）在 renderer type 1 下 receiver 获得面光，柱体产生
  软件阴影；不能再是全黑，也不能穿 blocker 漏光。
- CornellBox 与 conf_room 各做 SoftwareTracing/PT 对照：允许 voxel shadow 边界更粗，
  但灯色、衰减方向、遮挡拓扑必须一致。
- 太阳-only 场景改动前后截图 diff 为零或仅有随机序列变化；若随机序列被 area 分支消耗，
  在 `LightCount==0` 时不得额外取随机数。
- SoftwareModern baseline 无 diff。

### M2：SoftwareTracing RIS-only 与第二阶段 shade

**目标**：SoftwareTracing 能完整跑通两阶段 reservoir 管线；先关闭时空复用，建立无偏基线。

**任务：**

1. SoftwareTracing primary dispatch 在 ReSTIR 有效时设置
   `RestirPrimary/PrimaryInstanceId/PrimaryMotionPixels`；miss 和 primary emitter 必须写空
   intermediate reservoir。
2. `SoftwareTracingRenderer::Render()` 接入公共资源表和 frame stamp，在 main dispatch 后插入
   reservoir/G-buffer/`RT_SINGLE_DIFFUSE` barrier，dispatch
   `Core.SwRestirSpatialShade`，再运行 `SamplePostChain`。
3. M2 强制 runtime flags 为 RIS-only，用相同 software visibility tracer 完成初始胜者与最终
   胜者验证；debug mode 1–4 全部可用。
4. progressive accumulation 下永久保持 RIS-only；它只累积当前帧颜色，不读取颜色 history。

**验收：**

- 经典 NEE 与 RIS-only 各独立 progressive 600–1024 帧：signed mean 差绝对值
  `< 0.5/255`，无稳定的墙角/半影亮暗结构；若超过阈值不得进入时空复用。
- 等 spp 单帧 ManyLights：8 candidates 相比经典 1-sample NEE 明显降低选灯噪声。
- debug M/W/light-id/reuse 视图覆盖有效表面，且退出 debug 后 temporal 输入纹理未被破坏。
- `r.restir.enable=false` 不 dispatch 第二阶段、不写 ReSTIR 地址、不产生额外 GPU timer。

### M3：Temporal + Spatial 复用与跨 renderer 生命周期

**目标**：开启完整样本复用，运动、切换和多视图下无陈旧 history。

**任务：**

1. 启用现有 temporal gates：屏内、ObjectId、MotionMoment、frame 连续、
   `historyGeneration`、light generation 和 pending clear；SoftwareTracing 不另造一套规则。
2. 启用现有 spatial 几何测试、neighbor M clamp 和“center history 与 spatial shading 解耦”
   规则；不得把空间合并结果写回 temporal history。
3. 验证 PT ↔ SwTracing 切换：公共 reservoir 只能在 invalidation 后重用内存，不能跨 renderer
   合并样本。
4. 验证 resize/upscaler render extent、camera cut、场景切换、灯增删/重排、CVar on/off 和
   frame counter reset。
5. 验证 primary + transient/thumbnail 双 view：非 primary 始终经典 NEE，不能改公共 frame stamp
   或覆盖 primary reservoir。

**验收：**

- 静态 60 帧比 M2 RIS-only 再降噪；debug reuse proxy 显示有效累积。
- agentscript 驱动平移、快速旋转和动体，无遮挡漏光、拖影、陈旧灯色或黑 reservoir 块。
- renderer type 0 → 1 → 0 往返时，每次切换首帧 temporal invalid，第二帧恢复；
  GPU allocation 日志仍只有一份 reservoir。
- spatial off 精确回到 temporal-only；temporal off 精确回到 spatial-on-current-frame；两者都关
  回到 M2 RIS-only。

### M4：性能、平台、文档与交付

**目标**：形成可维护、可量化、跨非 RT 平台可用的最终实现。

**任务：**

1. 新增 SoftwareTracing 专用 agentscript（classic/RIS convergence、temporal motion、
   spatial A/B、renderer switch、perf），不要直接复用默认 renderer type 0 的旧脚本名。
2. ManyLights 1280×720 记录 main shading、software ReSTIR spatial shade 和总 GPU 时间；
   关闭路径目标为测量误差内零开销，开启路径目标为相对 classic `< 1.0 ms` 且总 shading
   增幅 `< 30%`（桌面参考 GPU）。未达标先降候选/邻居 ALU，不删除 visibility ray；仍未达标
   则保持实验开关并在文档记录平台数据。
3. 验证 1080p 内存为 `2 * width * height * 16B + parameters/table`，renderer 往返不翻倍。
4. 构建 `gkNextRenderer + gkNextUnitTests + gkNextVisualTest`；至少完成当前主机和 Android
   shader 编译，Windows NVIDIA 环境补 PathTracing hardware 实例回归。
5. 运行 SoftwareTracing 场景视觉组与全量 `gkNextVisualTest`。Direct Lighting 会有预期画面
   变化，必须逐场审查后再更新 baseline，不能把所有 diff 批量接受。
6. 实现完成后把跨 renderer 的耐久契约提炼进现行 design：扩展/重命名
   `pathtracing-restir-design.md`，同步 `direct-sample-post-chain.md`、CVar 说明和
   `docs/README.md`；本计划按文档生命周期退出现行入口。

## 5. 预计文件落点

| 类别 | 文件 | 预期改动 |
|---|---|---|
| 公共 C++ | `src/Engine/Rendering/PipelineCommon/RestirDI.{hpp,cpp}`（新增） | reservoir、参数、失效、barrier、地址导出 |
| Base renderer | `VulkanBaseRenderer.{hpp,cpp}` | 按需持有/访问唯一 ReSTIR DI 服务 |
| PathTracing | `PathTracingRenderer.{hpp,cpp}` | 迁移到公共资源服务，保持 SHARC 独立 |
| SoftwareTracing | `SoftwareTracingRenderer.{hpp,cpp}` | extras、frame stamp、software spatial pass 与同步 |
| Shader 类型 | `common/BasicTypes.slang` | `FTracingExtras` 语义化重命名，布局不变 |
| Shader tracing | `common/RayTracers.slang` | software finite/open segment visibility |
| Shader direct | `common/PathTracingRenderer.slang` | SoftwareTracing 专用 CSM + area illuminator |
| Shader ReSTIR | `common/Restir.slang` + 新公共 spatial helper | tracer 泛型化、复用同一算法主体 |
| Shader entry | `Core.SwTracing.comp.slang`、新增 `Core.SwRestirSpatialShade.comp.slang` | gather 接入与 software shade entry |
| 验证 | `assets/agentscripts/swrestir-*.agentscript.json` | 收敛、运动、切换、性能脚本 |
| 文档 | `docs/designs/*`、`docs/README.md` | 完成后转为跨 renderer 现行契约 |

具体实现若能以更少文件保持同样所有权和测试边界，可以调整文件名；不得因此把资源状态复制回
两个 renderer，或复制两份 spatial 算法。

## 6. 全局风险与护栏

1. **软件可见性是首要风险。** DDA 是粗代理，不能承诺与 TLAS 阴影逐像素一致；验收关注
   遮挡拓扑、无系统性漏光和无 emitter/self-occlusion，而不是硬件阴影边缘逐位相同。
2. **不允许假实现。** `TraceSegment() == false`、无限距离 occlusion、忽略全部 emissive voxel
   都不能作为降级路径。
3. **off 路径不等于旧画面。** Direct Lighting 本身会修正 SoftwareTracing；只有 ReSTIR 的
   额外资源和 pass 必须在 off 时消失。
4. **随机序列稳定性。** 无面光场景不得因新增分支额外消耗 RNG；否则太阳/间接噪声会无意义
   改变，增加回归判断成本。
5. **写者全覆盖。** miss、primary emitter、无灯和越界像素每帧都写空 intermediate/final；
   任何早退漏写都会把陈旧 reservoir 带到下一帧。
6. **host/GPU 竞态。** 地址表只在地址变化时更新；frame stamp 只走 push constant；不要把
   renderer/view-dependent 状态写进多个 in-flight frame 共用的 host-visible 表。
7. **非 RT 平台必须是第一等路径。** SoftwareTracing 的 main/spatial pipeline 都不能请求 TLAS
   descriptor，也不能用“设备支持时才正确”的 ray query 分支掩盖 DDA 问题。
8. **禁止作用域外扩。** 不顺带给 SoftwareModern 加面光、不把太阳塞入 reservoir、不做
   ReSTIR GI；这些需要独立任务和性能/画质设计。

## 7. 完成定义

- SoftwareTracing 在 ReSTIR off 时有带软件遮挡的经典面光 NEE，太阳 CSM 与
  SoftwareModern 均无回归。
- ReSTIR on 时 RIS、temporal、spatial、debug 四条路径均可验证；progressive 收敛与经典 NEE
  达到 M2 阈值。
- camera/light/scene/extent/renderer/multi-view 生命周期无陈旧 history；切换 renderer 不双倍
  占用 reservoir 内存。
- 非 RT shader 路径编译并在至少一个无硬件 ray query 的实际平台出图；PathTracing 原有
  hardware ReSTIR 回归通过。
- targeted build、unit tests、agentscript 和 visual test 全部通过；性能/内存数据写入最终
  design 或 journal。
- 文档入口更新，计划中的长期约束已经提炼到现行 design 后，本 plan 才可标完成并退出索引。
