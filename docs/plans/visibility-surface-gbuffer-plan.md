---
title: "Visibility Surface / G-buffer / Shading Scheduler 开发计划"
category: plan
status: M0–M4 与 M5a/M5b/M5c 已完成；仅剩触发条件式扩展
owner: engine/rendering
created: 2026-08-16
last_updated: 2026-08-16
related_design: ../designs/visibility-surface-gbuffer-shading-scheduler.md
---

# Visibility Surface / G-buffer / Shading Scheduler 开发计划

设计与契约见[当前架构](../designs/visibility-surface-gbuffer-shading-scheduler.md)。本文保留每个
里程碑的**实测数据与决策门结论**——这些数字是后续判断调度器成本模型的唯一依据，不能只留在 Git
历史里。

> **2026-08-16 后续变更**：`r.surface.build`、`r.surface.scheduler`、`r.gtao.applyInCore`、
> `r.taau.sparseCheckerboard` 四个 cvar 全部删除，它们描述的行为成为唯一路径——三个 software
> renderer 一律走 surface 路径 + tile 调度器，GTAO 由 Core Shading 上采样并应用，checkerboard
> lighting 在前提满足时以 sparse 形式交给 Native TAAU（前提不满足仍走 lighting-only resolve，
> 这条路径因此保留）。迁移前的 inline 入口、解析式全屏 allocation、compose 端 AO 应用，以及只
> 用于这些开关 A/B 的 agentscript 一并移除。因此本文中「默认 off」「legacy vs surface」
> 「analytic vs scheduler」的对照只作为**历史实测记录**阅读，不再是可执行的配置。

测试环境：Windows x86_64 / RTX 5070 Ti / `windows` preset。全部数据由
`gnb validate --script assets/agentscripts/surface-*.agentscript.json` 采集，`engine.gpuTime.<name>`
经 `assert` 步骤落进 agent report，用 `tools/surface_perf_table.py` 与
`tools/surface_scheduler_table.py` 出表。

## 完成状态

| 里程碑 | 范围 | 状态 |
| --- | --- | --- |
| M0 | 契约与脚手架（codec、C++ layout 头、cvar） | 完成 |
| M1 | SoftwareModernNoAmbient 拆分 Build + Core | 完成 |
| M2 | 屏幕空间消费者改读 surface | 完成 |
| M3 | SoftwareModern / SoftwareTracing 迁移 | 完成 |
| M4 | Shading Scheduler：tile 分类 + indirect dispatch | 完成，默认 off（相对解析式净收益为负） |
| M4b | scheduler 的 lane 压实 | 完成，半率相对全率 0.98× → **0.83×** |
| M4c | scheduler 扩展到 SwModern / SwTracing | 完成，默认 off；代价从 1.13–1.40× 降到 0.97–1.10× |
| M5a | legacy checkerboard 退场（checkerboard 现在依赖 surface 路径） | 完成 |
| M5b | Native TAAU sparse 输入 | 完成，默认 off，**净收益为正** |
| M5c | GTAO 合成下沉 | 完成，默认 off，**只有在半率着色下才划算** |
| M5 其余 | 触发条件式扩展 | 未立项 |

当时的 cvar：`r.surface.build`（默认 0）、`r.surface.scheduler`（默认 0）、
`r.taau.sparseCheckerboard`（默认 0）、`r.gtao.applyInCore`（默认 0）——后三者都隐含依赖第一个。
前两个现已删除（见文首注记），后两个仍在且仍默认关闭。

## M1 — NoAmbient Build + Core 拆分

### 一致性

冻结场景（`sys.tickPhysics=0`、`sys.tickAnimation=0`）、关 upscaler、full rate、关 GTAO 与
SS shadow，playground.glb 1280×720：

| 对照 | mean | max |
| --- | --- | --- |
| legacy vs legacy（确定性基线） | 0.0000 | 0 |
| legacy vs surface | **0.0000** | **0** |

在最终代码上两次独立复跑均为逐位一致。基线本身也是逐位一致的，说明测量确定，
`0 = 0` 不是「两边都没跑」的假象——脚本用 `assert` 记录了
`cvar.r.surface.build`（false/true/false）与 `engine.gpuTime.surface build`（0 / 0.082 / 0）。

需要注意这是**无 jitter**（`r.upscaler.type=0`）下的结果：此时 `ViewProjection` 与
`ProjectionInverse` 都不含 jitter，depth 往返是精确的。开启 temporal upscaler 后，surface 路径的
位置来自 depth 重建、法线/反照率经过 f16 往返，理论上会有轻微残差；M1 中间版本上曾测到
mean 0.107 / max 23（集中在轮廓与细结构，10 倍放大的差分图仍近乎全黑）。这属于 deferred
G-buffer 的固有量化代价，不是逻辑差异。

### GPU timer 对照（1080p / 4K，playground.glb，单位 ms）

"consumers" = GTAO + screen-space shadow 打开；"bare" = 两者关闭（决策门场景）。

| 配置 | shadingpass | gtao | compose | cb resolve | surface build | 合计 |
| --- | --- | --- | --- | --- | --- | --- |
| 1080p consumers full legacy | 0.355 | 0.225 | 0.176 | 0.000 | 0.000 | **0.756** |
| 1080p consumers full surface | 0.205 | 0.225 | 0.178 | 0.000 | 0.180 | **0.788** |
| 1080p consumers cb legacy | 0.566 | 0.225 | 0.178 | 0.260 | 0.000 | **1.229** |
| 1080p consumers cb surface | 0.219 | 0.227 | 0.180 | 0.090 | 0.180 | **0.897** |
| 1080p bare full legacy | 0.353 | — | 0.078 | 0.000 | 0.000 | **0.430** |
| 1080p bare full surface | 0.215 | — | 0.076 | 0.000 | 0.180 | **0.471** |
| 1080p bare cb legacy | 0.555 | — | 0.092 | 0.262 | 0.000 | **0.909** |
| 1080p bare cb surface | 0.230 | — | 0.078 | 0.092 | 0.180 | **0.579** |
| 4K consumers full legacy | 0.885 | 0.553 | 0.436 | 0.000 | 0.000 | **1.874** |
| 4K consumers full surface | 0.500 | 0.553 | 0.435 | 0.000 | 0.428 | **1.916** |
| 4K consumers cb legacy | 1.360 | 0.552 | 0.434 | 0.608 | 0.000 | **2.954** |
| 4K consumers cb surface | 0.636 | 0.551 | 0.439 | 0.238 | 0.429 | **2.292** |
| 4K bare full legacy | 0.885 | — | 0.176 | 0.000 | 0.000 | **1.061** |
| 4K bare full surface | 0.508 | — | 0.174 | 0.000 | 0.428 | **1.110** |
| 4K bare cb legacy | 1.303 | — | 0.203 | 0.807 | 0.000 | **2.313** |
| 4K bare cb surface | 0.565 | — | 0.188 | 0.221 | 0.429 | **1.403** |

### 决策门结论

> **问题**：NoAmbient 在无 GTAO / 无 SS shadow 的场景下，surface 路径是否净变慢？

**Full rate：是，但幅度很小。** bare 配置下 +0.041 ms（1080p，+9.5%）、+0.049 ms（4K，+4.6%）。
Build 的固定成本略高于 Core 变轻的收益。

**Checkerboard：不，surface 路径大幅更快。** bare 配置下 −36%（1080p）与 −39%（4K），
consumers 配置下 −27%（1080p）与 −22%（4K）。原因有二：resolve 从 6 张 RT 收缩到 2 张；
更关键的是 legacy 的 checkerboard shading 反而**比全率还贵**（1080p 0.566 vs 0.355），
因为半率 dispatch 让 6 张 RT 的写入变成跨步访问，带宽没省而 lane 利用率下降。surface 路径的
Core 只写 2 张，这个反常消失了。

**联动建议**：`r.checkerboardRendering` 默认是 on，所以在默认配置下 surface 路径就是净收益，
不需要设计文档设想的「仅在 GTAO/SS shadow 启用时激活」的 cvar 联动。真正需要谨慎的是
「全率 + 无消费者」这一组合，代价约 0.04 ms。

### checkerboard 下 surface RT 的密度

密度是**结构性保证**而非经验结论：surface 路径下只有 `Core.SurfaceBuild` 写这些 RT，它按
`pixel = DTid.xy` 全屏 dispatch、不经过任何 parity 映射，而 `SoftwareModernNoAmbient`
resolve 集合只复制 `RT_SINGLE_DIFFUSE` 与 `RT_AMBIENT`（M5a 之后它是唯一的 NoAmbient 集合）。

可观测的旁证（`r.gtao.debugMode=1`，GTAO 只吃 depth+normal，因此它的输出隔离出了 surface 数据质量）：

| 对照 | mean | >8 占比 |
| --- | --- | --- |
| legacy：full vs checkerboard | 0.366 | 0.270% |
| surface：full vs checkerboard | 0.231 | 0.164% |
| full rate：legacy vs surface（基线） | 0.254 | 0.181% |

surface 路径下「全率 vs 半率」的偏差已经落到与基线同量级，即 **GTAO 的输入基本与采样率无关**；
legacy 则明显更大。

## M2 — 屏幕空间消费者改读 surface

`Common.LoadOccluderPlane` 分流 march step；`TraceInScreenSpace` 增加 surface 分支。

screen-space shadow march 增量成本（NoAmbient，full rate，关 GTAO）：

| 场景 | legacy（on − off） | surface（on − off） | 降幅 |
| --- | --- | --- | --- |
| LightingShowcase.proc 1080p | 1.641 − 0.461 = **1.180** | 0.954 − 0.299 = **0.655** | −44% |
| conf_room.glb 1080p（相机埋在几何里，march 命中率高） | 6.121 − 2.376 = **3.745** | 3.730 − 1.719 = **2.011** | −46% |

**验证缺口（诚实记录）**：仓库里现有的场景在 NoAmbient 下都没能产生**肉眼可见**的屏幕空间接触
阴影——`r.lightObject.screenSpaceShadow` 开/关的截图在两条路径下都逐位相同（march 确实在跑，
timer 证明了这一点，只是可见范围内找不到遮挡体）。因此 M2 的画质对照只证明了
「legacy 与 surface 完全一致」，没有证明「阴影本身正确」。需要画质对照时得先构造一个带
LightObject 且有明确接触阴影的场景。

## M3 — SoftwareModern / SoftwareTracing 迁移

### 前置核验结论

1. **`BuildBSDFContext` 是否依赖 primary TexCoord**：依赖，但只用于采样出 base color、
   roughness、metalness 三个值——而这三个值正是 surface 已经存下的。新增
   `BuildBSDFContextFromSurface` 直接接收它们，surface 不需要增补 TexCoord 字段。
   `RestirSpatialShade` 早就这么做了，是现成先例。
2. **`PrimaryHit` 中非 dielectric 的 position nudge**：`position -= rayDir * SceneEpsilonScale * 0.01`
   只依赖 rayDir 与重建位置，depth 重建路径下逐字复现即可。
3. **ReSTIR primary gate**：`instanceId` 从 `RT_OBJECTID_0` 解码（`OBJECT_ID_INSTANCE_MASK`），
   `motion` 直接读 `RT_MOTIONVECTOR`，两者都是 Build 的全率输出。
4. **额外发现**：`ScatterAndTrace` 在 bounce 0 用的是未经材质模型覆盖的原始 metalness，
   而 surface 存的是 effective 值。对 Metallic/Dielectric 材质这会改变 bounce 0 的 lobe 选择概率；
   已作为有意的统一记录在设计文档里。

### 一致性

两个 renderer 都是随机采样器，单帧截图之间本身就有巨大的 Monte Carlo 噪声，因此对照必须带
「legacy vs legacy」噪声地板。playground.glb 1280×720，`r.samples=64`，冻结场景，16×16 块均值
用于抵消逐像素噪声：

| 对照 | 逐像素 mean | 块均值 mean | 块均值 p99 |
| --- | --- | --- | --- |
| SwModern legacy vs legacy（噪声地板） | 14.449 | 0.869 | 3.633 |
| SwModern legacy vs surface | 14.441 | 0.886 | 3.820 |
| SwTracing legacy vs legacy（噪声地板） | 12.263 | 0.771 | 3.348 |
| SwTracing legacy vs surface | 12.268 | 0.771 | 3.285 |
| SwTracing + ReSTIR legacy vs legacy | — | 0.765 | — |
| SwTracing + ReSTIR legacy vs surface | — | 0.768 | — |

legacy/surface 的差异全程贴着噪声地板（偏差 ≤2%），**没有系统性偏移**。ReSTIR 开启时同样成立。

### GPU timer 对照（1080p，playground.glb，单位 ms）

| 配置 | shadingpass | cb resolve | surface build | fps |
| --- | --- | --- | --- | --- |
| SwModern full legacy | 0.690 | 0.000 | 0.000 | 652 |
| SwModern full surface | 0.563 | 0.000 | 0.180 | 625 |
| SwModern cb legacy | 0.837 | 0.391 | 0.000 | 484 |
| SwModern cb surface | 0.453 | 0.145 | 0.182 | 492 |
| SwTracing full legacy | 7.100 | 0.000 | 0.000 | 122 |
| SwTracing full surface | 6.573 | 0.000 | 0.178 | 130 |
| SwTracing cb legacy | 3.735 | 0.389 | 0.000 | 182 |
| SwTracing cb surface | 3.214 | 0.139 | 0.178 | 196 |

checkerboard resolve 从 0.39 ms 降到 0.14 ms（−64%），与「11 张 RT → 5 张（4 张 lighting +
解调用的 albedo）」的带宽收缩一致。重 Core 的 SwTracing 在 full rate 下也已经是净收益
（6.573+0.178 = 6.751 vs 7.100）。

### Upscaler 契约冒烟

surface 路径 + checkerboard 下，FSR(3) / Native TAAU(4) / SGSR2(5) × SwTracing / SwModern /
NoAmbient 共 9 组，全部产出正常的 1600×900 放大画面，无黑屏、无结构性错误。

**DLSS 未验证**：`gnb validate` 强制禁用 Streamline（见 AGENTS.md），因此本轮无法覆盖 DLSS 与
DLSS-RR。需要时必须在 Windows NVIDIA 环境用非 hidden、非 agent-validation 的正常窗口路径复测，
运行会弹窗。

## M4 — Shading Scheduler

实现：`Task.ShadingClassify.comp.slang`（8×8 tile，per-bucket 64bit 掩码，每 tile 每桶至多一次
atomic）+ `Core.SwModernNoAmbient{Standard,Background,Emissive}.comp.slang` 三个 bucket kernel +
`PipelineCommon::ShadingSchedulerPass` 的 `vkCmdDispatchIndirect`。

### A/B（单位 ms；`r.surface.build=1` 恒定，只切 `r.surface.scheduler`）

| 配置 | build | classify | shadingpass | gtao | cb resolve | compose | 合计 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1080p full analytic | 0.176 | 0.000 | 0.186 | 0.225 | 0.000 | 0.176 | **0.764** |
| 1080p full scheduler | 0.179 | 0.038 | **0.152** | 0.226 | 0.000 | 0.176 | **0.770** |
| 1080p cb analytic | 0.178 | 0.000 | 0.213 | 0.223 | 0.090 | 0.178 | **0.883** |
| 1080p cb scheduler | 0.179 | 0.040 | 0.225 | 0.228 | 0.088 | 0.180 | **0.940** |
| 4K full analytic | 0.425 | 0.000 | 0.455 | 0.530 | 0.000 | 0.430 | **1.840** |
| 4K full scheduler | 0.428 | 0.089 | **0.383** | 0.552 | 0.000 | 0.602 | **2.054** |
| 4K cb analytic | 0.429 | 0.000 | 0.537 | 0.555 | 0.393 | 0.606 | **2.520** |
| 4K cb scheduler | 0.430 | 0.093 | 0.596 | 0.735 | 0.389 | 0.436 | **2.680** |
| old_city 1080p full analytic | 0.097 | 0.000 | 0.158 | 0.037 | 0.000 | 0.082 | **0.373** |
| old_city 1080p full scheduler | 0.095 | 0.038 | **0.123** | 0.036 | 0.000 | 0.082 | **0.373** |
| old_city 1080p cb analytic | 0.095 | 0.000 | 0.174 | 0.035 | 0.072 | 0.086 | **0.461** |
| old_city 1080p cb scheduler | 0.096 | 0.038 | 0.154 | 0.036 | **0.018** | 0.084 | **0.426** |

4K 的 gtao/compose 列在两行之间本应相同却有 ±0.2 ms 抖动，那是测量噪声；可靠信号是 shadingpass
与 classify 两列，它们跨两次独立采样都可复现。

### 结论

**机制按设计生效**：Standard kernel 去掉 miss/emissive 分支后稳定快 16%~22%
（1080p 0.186→0.152，4K 0.455→0.383，old_city 0.158→0.123）。scheduler 打开时 lighting resolve
还能跳过全率着色的 background/emissive 像素（old_city cb：0.072→0.018）。

**性能净收益为负或持平**，因此 `r.surface.scheduler` 默认 off：

1. 分类本身要 0.04 ms（1080p）/ 0.09 ms（4K），吃掉了大部分 kernel 收益；
2. checkerboard 下 tile 粒度无法压到半率——`Standard` bucket 仍按 8×8 tile 起 64 线程，约一半
   lane 立即返回；解析式路径则把 dispatch 宽度压掉一半、lane 全部有效。这是 tile 粒度分类的
   固有代价，不是实现瑕疵。

画质：scheduler on/off 的差异（1080p full mean 0.285）与「同一条路径两次采样」的噪声地板
（0.284，源自 GTAO 每帧轮转的采样相位）完全相同，即**视觉一致**。

## 「surface off 是 no-op」回归

`assets/agentscripts/surface-legacy-parity.agentscript.json` 在改动前的 `dev` HEAD 与改动后各跑
一次（`r.surface.build=0`，冻结场景，关 upscaler / checkerboard / ReSTIR，`r.samples=64`）：

| Renderer | 逐像素 mean | max | 块均值 mean | 判定 |
| --- | --- | --- | --- | --- |
| SoftwareModernNoAmbient | **0.00000** | **0** | 0.00000 | 逐位一致 |
| SoftwareModern | 14.459 | 151 | 0.885 | 与自身噪声地板（14.449 / 0.869）一致 |
| SoftwareTracing | 12.258 | 158 | 0.766 | 与自身噪声地板（12.263 / 0.771）一致 |
| PathTracing | 10.774 | 119 | 0.672 | 未迁移；仅 `RT_BSDF_DATA` 编码变化，落在噪声内 |

NoAmbient 的逐位一致同时证明了「legacy 入口改成调用共享 kernel」这一步没有引入任何差异。

## M4b — scheduler 的 lane 压实

**问题**：scheduler 打开时开 checkerboard 几乎没有收益（`shading + classify` 0.192 → 0.187 ms，
0.98×），而解析式路径是 0.182 → 0.117（0.64×）。原因是 `Standard` bucket 按 8×8 tile 起 64 条 lane，
半率下掩码里只有 32 位置上，另一半 lane 进来就 return——dispatch 的形状是 tile 的，工作密度只有一半。

**做法**：checkerboard 时一个 workgroup 覆盖两个 tile，lane `i` 取 `slot = group*2 + (i>>5)` 号 tile
掩码里的第 `i&31` 个置位；group 数由新增的 `Task.ShadingClassifyFinalize`（1 个 workgroup、每 bucket
一个线程）在分类完成后改成 `ceil(n/2)`。

1080p，playground.glb，NoAmbient（单位 ms）：

| 配置 | shading | classify | 合计 | vs 全率 |
| --- | --- | --- | --- | --- |
| scheduler off，full | 0.182 | — | 0.182 | — |
| scheduler off，cb | 0.117 | — | **0.117** | 0.64× |
| scheduler on，full | 0.152 | 0.040 | 0.192 | — |
| scheduler on，cb（压实前） | 0.135 | 0.040 | 0.187 | 0.98× |
| scheduler on，cb（压实后） | **0.117** | 0.042 | **0.159** | **0.83×** |

`Standard` kernel 的半率耗时已经和解析式完全一致（0.117）。

**两个被否掉的替代方案**（数字留着，免得有人再试一遍）：

| 方案 | shading | classify | 合计 |
| --- | --- | --- | --- |
| 每 tile 第二次 atomic 单独维护 group 数 | 0.117 | 0.071 | 0.188 |
| 仍按 tile 数 dispatch、后一半 group 立刻退出 | 0.135 | 0.040 | 0.175 |
| finalize pass（采用） | 0.117 | 0.042 | **0.159** |

**正确性**：关掉 GTAO（唯一的时序抖动源）、冻结场景后，压实路径与解析式路径的差异 mean 0.0768，
低于同配置两次采样之间的 TAAU 收敛基线 0.0928——没有丢像素。

**仍然默认 off**：相对解析式（0.117）依旧是负收益。分类的 0.04 ms 对 NoAmbient 这种 0.12 ms 的
kernel 太贵。scheduler 要真正划算，得接到 Standard kernel 重得多的 renderer 上。

## M4c — scheduler 扩展到 SwModern / SwTracing

三个 bucket 都接上了：`Core.SwModernStandard` / `Core.SwTracingStandard`（各自的 Core Shading 体
经 `common/SwModernShading.slang` / `common/SwTracingShading.slang` 与解析式入口共用，两条路径不会
漂移），加上两个 renderer 共享的 `Core.TracingBackground` / `Core.TracingEmissive`。

### 一个会 device lost 的坑

`GPUScene.ReservedAddress0` 是唯一的 pass-local 资源槽，ReSTIR（resource table）和 scheduler
（tile buffer）都要用它；`CustomData1` 同样冲突（frame stamp vs tile 容量）。GPUScene 已经正好是
**128 字节**——push constant 的常见下限（移动端），加不了第三个槽。

真正危险的是 `RestirIsAvailable()` 只判断指针非空：scheduler 打开时它看到 tile buffer 的地址，
于是把 tile 数据当 ReSTIR 表解引用，读出垃圾指针后写进随机显存 —— **实测直接 `ERROR_DEVICE_LOST`**。

修法是在源头让排他性对所有调用方可见，而不是只在一处短路：`RestirIsAvailable()` 现在先检查
`Surface.IsSchedulerActive(CustomData2)`。C++ 侧 `SoftwareTracingRenderer` 也拒绝在 ReSTIR 打开时
启用 scheduler。

### 两次优化

| | swmodern full | swmodern cb | swtracing full | swtracing cb |
| --- | --- | --- | --- | --- |
| 初版 | 1.32× | 1.16× | 1.09× | 1.40× |
| `NthSetBit` 改 5 步二分（原本 32 次线性扫描） | 1.32× | 1.13× | 1.13× | **1.03×** |
| terminal bucket 精简（不再复用 `PrimaryHit`） | **1.06×** | **1.10×** | **0.97×** | **1.06×** |

（`sched/analytic` 的 `shading + classify + build` 合计比，1080p playground，两次独立运行取一致值。）

terminal bucket 原本直接调 `PrimaryHit`——为了不让 sky/emissive 的写出集有第二份会漂移的拷贝。
但那条路要付一整个 `Surface.Load`（八张平面）加 11 个 RWTexture 句柄，只为产生四次 store；在
主 kernel 里这些延迟被邻居的重活盖住了，独立 dispatch 之后就全部暴露。现在改成手写，并在两边
都注明 `PrimaryHit` 仍是契约权威。

### 结论

**唯一打平的是 SwTracing 全率（0.97–1.03×）**——它的 Standard kernel 有 2.9 ms，分类那 0.040 ms
只占 1.4%，coherence 的收益刚好抵掉。其余三种组合是 1.06–1.10×，仍然略负。

所以 `r.surface.scheduler` 继续默认 off。整轮下来把代价从 1.13–1.40× 压到 0.97–1.10×，机制在三个
renderer 上都正确、稳定；但**分类那 0.04 ms 的固定成本，需要比 SwTracing 更重的 kernel 才能真正
赚回来**。下一个真正有意义的推动是让 bucket 承载它本来的目的——不同的 shading 方式（材质特征桶），
那时分类是**必需**的而不是可选优化，成本模型才会翻转。

## 一个排查陷阱

`engine.checkerboardActive` 报的是**引擎级资格**，不是每个 renderer 的实际决定。SoftwareTracing 在
`SoftwareTracingRenderer.cpp` 里用 `ConfigureCheckerboardShading(gpuScene, !restirEnabled)` 自己否掉了
checkerboard，所以 **ReSTIR 打开时 SwTracing 全率着色，而这个 query 仍然报 True**（实测 cb/full = 1.00）。
排查半率没生效时，先看 `engine.gpuTime.shadingpass` 的全率/半率比值，不要只信这个 query。

`r.surface.scheduler` 也只接在 SoftwareModernNoAmbient 上；SwModern / SwTracing 对它是空开关
（`engine.gpuTime.shading classify` 恒为 0 即可确认）。

## M5a — legacy checkerboard 退场

Checkerboard 曾经要复制整套 surface 类 RT 才能工作，那是在没有 Build pass 的时代唯一的做法，也是
它在轮廓处伪造 depth/motion/objectId 的根源。Build 存在之后这套做法没有保留价值，已整体删除：

- `IsCheckerboardRenderingActive()` 增加 `IsSurfacePathActive()` 前提——**checkerboard 现在是
  Primary Surface 路径独有的能力**；
- `ECheckerboardResolveSet` 只剩两个 lighting-only 集合（`Tracing`、`SoftwareModernNoAmbient`），
  `CopyTracingOutputs`（11 张 RT）、`CopySoftwareModernNoAmbientOutputs`（6 张）与
  `RESOLVE_SCENE_COLOR` 全部删除；
- `common/CheckerboardRendering.slang` 删除，parity 知识全部收进 `Shader/ShadingScheduler.slang`
  （`ResolveAnalyticPixel` / `IsActiveParity` / `MissingPixel` / `ReconstructionSource`），
  兑现 M4 计划里 "`ResolveShadingPixel` 从 Core 语义中退役" 那一条；
- **PathTracing、PathTracingLite、VoxelTracing 因此不再支持 checkerboard**，在 upscaler 下一律
  全率着色。它们没有 Build pass，也无法有（HW primary 含 aperture/DOF，raster VB 无法重放）。

## M5b — Native TAAU sparse 输入

`r.taau.sparseCheckerboard`（默认 off）。开启后 checkerboard 的缺失 parity **不再被邻居复制**：
resolve pass 整个不跑，Core 到 upscaler 之间的每个 pass 都按着色率跳过未着色像素
（`Process.GTAOCompose`、`Process.Compose`、`Process.AtmosphereComposite`），
`Process.NativeTemporalReproject` 在 Catmull-Rom 重建与邻域统计里剔除未着色 tap 并重新归一化，
由历史补齐。深度与 motion 仍然是 Build 的全率输出，所以 disocclusion、dilation、slope 估计都不受影响。

生效前提（任一不满足自动退回 resolve 路径）：surface 路径 + checkerboard + Native TAAU + 主视图，
且当帧没有会整屏合成的 external pass。后者是运行时判断：`IExternalRenderPass::PaintsWholeSceneThisFrame()`
默认由契约里的 `supportsSparseShadingRate` 决定，AuxDraw 声明自己只画自己光栅化的像素，
GaussianSplat 则按"这帧是否真的要画 splat"回答。

1080p / 4K，playground.glb，NoAmbient，Native TAAU（单位 ms）：

| 配置 | build | shading | gtao | cb resolve | compose | 合计 | fps |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1080p full rate | 0.181 | 0.182 | 0.236 | 0.000 | 0.313 | **0.912** | 476 |
| 1080p cb + resolve | 0.181 | 0.121 | 0.236 | 0.090 | 0.315 | **0.942** | 462 |
| 1080p cb + sparse | 0.181 | 0.119 | 0.233 | — | 0.311 | **0.844** | 476 |
| 4K cb + resolve | 0.431 | 0.539 | 0.555 | 0.272 | 0.459 | **2.255** | 221 |
| 4K cb + sparse | 0.431 | 0.541 | 0.551 | — | 0.635 | **2.157** | 231 |

结论：**resolve pass 消失是净收益**，1080p −10.4%、4K −4.3%。静态画面上 sparse 与 resolve 的差异
（1080p mean 0.378）略高于同配置噪声地板（0.305）——两者都是近似，差别不代表谁更准；sparse 真正的
优势在运动下（历史补齐尊重 motion，邻居复制不尊重），本轮的静止场景测不出这一点，记为验证缺口。

## M5c — GTAO 合成下沉

`r.gtao.applyInCore`（默认 off）。开启后 Core Shading 自己做 GTAO 的 3×3 双边上采样并把遮蔽乘进
ambient 项，`Process.GTAOCompose` 不再上采样。上采样代码收进 `common/GTAOUpsample.slang`，
两条路径共用，保证"用户 strength 只应用一次"这条规则不会在搬家过程中被破坏。

1080p，playground.glb（单位 ms）：

| 配置 | shading | compose | 合计 |
| --- | --- | --- | --- |
| full rate，compose 端应用（默认） | 0.182 | 0.313 | **0.912** |
| full rate，Core 端应用 | 0.385 | 0.186 | **0.987** |
| cb + sparse，compose 端应用 | 0.119 | 0.311 | **0.844** |
| cb + sparse，Core 端应用 | 0.227 | 0.188 | **0.831** |

结论：**下沉的收益完全取决于着色率**。同样的九抽样双边滤波，放进本来就寄存器吃紧的 shading kernel
比放在小小的 compose kernel 里贵得多（full rate +0.203 vs −0.127，净 +8%）；但 shading 一旦降到半率，
它只为一半像素付费而 compose 是全率的，于是反过来赢 1.5%。因此默认关闭，并建议**与 checkerboard
同开**。当前实测最佳组合是 `cb + sparse + gtao.applyInCore` = 0.831 ms，相对 M5 之前的
`cb + resolve` 基线 0.942 ms 快 12%。

架构收益与性能分开陈述：下沉之后 GTAO 不再是一个只能作用于 compose 的后置项，Core 可以在着色时
消费遮蔽——这是未来让 AO 参与直接光/镜面决策的前提，即使今天的帧时间不划算。

## M5 剩余（触发条件式，未立项）

- **材质特征桶**：出现第二种真实 shading 方式（需要独立 kernel 的特殊 BRDF）时，由 material ID
  查 feature mask 归桶。
- **sparse 输入扩展到 SwModern / SwTracing**：机制是通用的（`Process.Compose` 已经会跳过未着色
  像素），但尚未在这两个 renderer 上实测。
- **Build 与 classification 合并 dispatch**：M4 实测分类只占 0.04–0.09 ms，暂无必要。

## 已知遗留

- `Process.AtmosphereComposite` 仍用 `ProjectionInverseUnJit` 重建世界坐标，而 surface depth 是
  jittered 的。这是迁移前就存在的分歧，本轮没有触碰。
- 仓库里没有提交 visual test baseline（`assets/visual_test_baselines/` 为空），所以
  `gkNextVisualTest` 只能作为 7 个场景的渲染冒烟（本轮 7/7 通过），无法做 baseline diff。
  逐像素回归由本文列出的 agent script 承担。
- `gkNextUnitTests` 有 5 个用例 / 9 条断言失败（Test_PhysicsSync、Test_TerrainWalkable）。已在
  未改动的 `dev` HEAD 上复现同样的失败，与本次改动无关。
