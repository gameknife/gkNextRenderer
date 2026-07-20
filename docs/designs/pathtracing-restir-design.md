---
title: "PathTracing ReSTIR DI 架构"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-19
last_updated: 2026-07-20
---

# PathTracing ReSTIR DI 架构

PathTracing 渲染器的 primary 表面面光源直接光采用 ReSTIR DI（蓄水池时空重采样）。本文记录当前实现的边界、数据契约、已知偏差与修改护栏。实施过程见 Git 历史；`r.restir.enable` 当前默认开启。

## 1. 范围与边界

**只替换 primary vertex 的面光源 NEE**，其余全部不变：

- 太阳 NEE 独立走锥形 shadow ray（delta 光方差低，且与 SoftwareTracing/CSM 路径共享约定）；天空 IBL 仍走 BSDF miss 路径。
- 次级 bounce 的直接光策略（`SecondaryDirectMode`、SHARC update 的逐 hit 直接光记账）不变；**蓄水池样本不喂 SHARC**（相关样本会破坏缓存时域累积）。`Core.SharcUpdate` 入口显式 `RestirPrimary = false`。
- `suppressRegisteredEmitter` MIS 约定不变（ReSTIR 仍是 NEE 的重采样形式）。
- 目标函数为 Lambert-only（albedo 解调），与 `RT_SINGLE_DIFFUSE` 光照通道语义一致；无 specular direct。
- 仅 primary view：shader 侧以 `gpuScene.CustomData0 == 0`（view bank base）判定，非 primary / Transient view 走经典单样本 NEE。
- Offline progressive（编辑器空闲渐进、still benchmark）强制降级为 **RIS-only**（时空复用全关）：保住"收敛到参考"的语义，同时充当内建的无偏对照组。

## 2. 管线结构

```
主 PT dispatch (Core.PathTracing.comp / Core.SharcQuery.comp)
  PrimaryHit → …path loop… → DirectIlluminatePrimary:
      太阳 NEE（照旧）
      RestirPrimaryGather：初始 RIS + 初始可见性 + 时域合并 → 写 intermediate buffer
  [barrier: reservoir + RT_SINGLE_DIFFUSE / G-buffer]
Core.RestirSpatialShade.comp（第 4 条 pipeline，ZeroBindWithTLASPipeline）
  G-buffer 重建表面 → center 胜者最终可见性 → 写 final buffer（时域历史）
  → 空间邻居合并 → shading 可见性 → RT_SINGLE_DIFFUSE += direct
  [之后进入 SamplePostChain：当前样本直接 compose → DLSS/FSR/native]
```

`IDirectIlluminator` 增加 `DirectIlluminatePrimary(seed, pixel, pos, normal, color)`；除 `FHardwareDirectIlluminator` 外其余实现委托回 `DirectIlluminate`。采样几何项由 `Common.EvaluateAreaLightSample` 单一来源提供，经典 NEE、目标函数 p̂ 与 ReSTIR shading 共用，**不得分叉**。

## 3. 数据契约

**资源挂载**：`GPUScene` push constant 已满 128B，扩展资源经 `ReservedAddress0 → FPathTracingExtras` 表（原 SharcResources 的 3 个保留槽改为 `RestirReservoirPing/Pong/Parameters`）。可用性按**字段**判定（`HashEntries != 0` = SHARC、`RestirParameters != 0` = ReSTIR），不判表指针——表在任一特性启用时都存在。表内容必须 view 无关（host-visible、每视图 Render() 均可触达），仅在地址变化时重写（稳态零 host 写，见 §9-③）。

**蓄水池**（`FRestirReservoir`，16B/px，双缓冲）：`LightData`（24 位灯索引 + 8 位类型）、`PackedUV`（灯面 uv，2×unorm16）、`WeightW`（fp32）、`PackedMTarget`（16 位 M + fp16 p̂）。存生成参数而非世界坐标点：复用跟随灯变换、无需 Jacobian。1080p 双缓冲 ≈ 63 MiB。

**固定角色双缓冲（无帧奇偶）**：intermediate 由 gather 写、spatial pass 读；final 由 spatial pass 写、**下一帧时域读**。每个 buffer 单一写者 pass，跨像素邻居读永远不会观察到半更新数据。

**逐帧竞态关键位走录制 push constant**：`gpuScene.CustomData1` bit1 = 本帧时域有效（PathTracingRenderer 每 dispatch 盖章）。host-visible 参数 buffer（`FRestirRuntimeParameters`）只承载调参类字段。

## 4. 算法

1. **初始候选**（默认 8）：现有 CDF 选灯（`Scene::UpdateLights` 预计算 reserved1/2）+ 灯面均匀 uv，流式 RIS，权重 w = p̂ / (lightPdf/area)。无效候选计入 M（等价零权重流入）。
2. **初始可见性**：对初始 RIS 胜者打 1 根 shadow ray，遮挡则 W=0 但保留 M（visibility reuse，防止复用扩散漏光）。
3. **时域合并**：motion vector 重投影读上一帧 final reservoir；屏外 / ObjectId 不一致 / `MOTIONMOMENT != 0` 时拒绝。当前像素重算 p̂ 后按 W-form 合并（候选权重 = p̂·W·M）；prev.M 钳到 `r.restir.mClamp`（默认 160）。当前像素的 instanceId/motion **由 renderer 状态传参**，不读本 dispatch 刚写的纹理（§9-②）。这只是 reservoir 复用，不读取或生成颜色 history。
4. **空间合并**（pass 2，默认 5 邻居 / 半径 16px golden-angle 螺旋）：几何测试 = 同 ObjectId + 法线点积 > 0.9 + view 深度相对差 < 10%；邻居样本在 center 表面重算 p̂ 后 W-form 合并；**邻居 M 钳到 4×初始候选数**（时域 entrenched 邻居不得主导选择，否则可见性错配样本吃能量）。
5. **最终可见性 + shading**：center 胜者先验证（其判决写入 final buffer = 时域链的可见性反馈）；合并胜者若非 center 样本再补 1 根 ray。每像素合计 2–3 根 shadow ray（经典 NEE 为 1 根面光 + 1 根太阳）。

**时域链与空间去耦（关键结构决策）**：final buffer 存"center 经最终可见性"的蓄水池，**空间合并结果只用于本帧 shading、绝不回写历史**。有偏空间合并在可见性边界变暗，回写历史会跨帧复利（实测 13–16% 能量丢失；去耦后 1–2%）。与 RTXDI 结构一致。

## 5. 生命周期与失效

- 蓄水池按 `ActiveViewRenderExtent()` 分配，extent 变化重建并清零（DLSS 模式切换自动覆盖）。
- 时域有效 = RenderView `historyGeneration` 未变化 ∧ 帧号连续 ∧ 灯集合 generation 不变 ∧ 无 pendingClear。灯 generation 由 `Scene::UpdateLights` 对（数量 + lightMatIdx 序列）做 FNV 签名维护，纯变换/调色不失效。
- **写者全覆盖契约**：primary miss 像素与发光体 primary（Render 早退不走 gather）由入口 shader 显式 `RestirStoreEmpty`，spatial pass 对无效像素写空 final——任何像素每帧必须被写，否则下游消费陈旧蓄水池。

## 6. 已知偏差与精度

- RIS-only：与经典 NEE 收敛逐位一致量级（signed diff ≈ -0.03/255）。
- 时域：visibility-reuse 记账带来 ≈ -0.08/255，不可见。
- 空间：半影带局部 ≤ 0.7%（-1.7/255）变暗——有偏合并在可见性边界的固有代价，肉眼不可辨；调 `spatialRadius`/`spatialSamples` 可进一步换取。
- estimator 偏差验证直接比较 single/progressive 输出；引擎不再有颜色 history clamp 干扰结果。
- pass 2 深度重建必须精确复刻 `FVisibilityBufferRayCaster` 的 ndc 约定：**`(pixel/size)*2-1`，无 +0.5 半像素**（GTAO 的 +0.5 是自洽 AO 才没暴露）。差半像素在 40m 处偏数厘米、shadow ray 全体自遮挡。shadow 起点用深度比例偏移 `max(EPS2, |viewZ|·1e-4)`。

## 7. CVar 与调试

| CVar | 默认 | 说明 |
|---|---|---|
| `r.restir.enable` | true | 总开关；默认使用 ReSTIR DI |
| `r.restir.candidates` | 8 | 初始候选数（时域健康时 2 即接近同质量） |
| `r.restir.temporal` / `mClamp` | true / 160 | 时域复用 / M 上限 |
| `r.restir.spatial` / `spatialSamples` / `spatialRadius` | true / 5 / 16 | 空间复用（关闭只影响邻居数，两个 pass 恒运行） |
| `r.restir.debugMode` | 0 | 1=M 热力图 2=W 3=胜者灯着色 4=复用 proxy（红=无复用） |

Debug 视图由 pass 2 从本地合并状态渲染（race-free）；颜色用 0–255 nit 尺度（compose tonemap 约定）。**每帧运行的 debug overlay 只准替换光照颜色（`WriteDebugColorOnly`），踩 ObjectId/Motion 会毁掉被观测的时域状态**。

## 8. 性能

720p ManyLights（64 灯）全链 +0.50ms（RTX 4070）；预算 0.6ms。超预算先降 `candidates`/`spatialSamples`，不砍可见性光线。

## 9. 修改护栏（实施中验证过的失败模式）

1. **Slang 裸声明 struct 不保证应用字段默认初始化器**。新增字段必须给显式 `__init`，且所有入口声明点显式赋值——未初始化的 `RestirPrimary` 曾把 SharcUpdate pass 送进空指针路径（device lost，且 GPU 相关：一块卡上垃圾恰为 0 不崩）。
2. **同一 invocation 内 storage image 写后读不可见**。当前像素的 primary 数据（instanceId/motion）从 renderer 状态传参；只有前置 pass 写的纹理（ObjectId1/MotionMoment/G-buffer）可读。
3. **竞态关键的逐帧状态严禁放 host-visible buffer**（host 覆写与 in-flight GPU 帧竞争），必须走录进 command buffer 的 push constant。
4. **空间复用结果不得回写时域历史**（§4）。
5. p̂ / shading / 经典 NEE 共用 `Common.EvaluateAreaLightSample`，改面光模型只改这一处。
6. 蓄水池是屏幕空间时序状态：新增失效条件必须体现在 RenderView `historyGeneration` 或 ReSTIR 自身 generation 检查中；不要依赖已删除的颜色 history。

## 10. 否决路线与远期方向

- **无偏空间合并（Talbot/pairwise MIS）**：每邻居额外可见性光线或全对 p̂ 重估，实时预算不匹配；biased + 几何测试 + M 钳制已达肉眼不可辨。
- **世界空间 ReSTIR（ReGIR）**：`kMaxLightCount=1024` 且典型场景灯数十级，屏幕空间足够；>10⁴ 灯再议。
- **太阳并入候选池**：推迟。太阳是低方差 delta 光，进池稀释面光候选质量，且 SoftwareTracing/CSM 路径无法对齐；省 1 根 ray 的收益不抵。
- **ReSTIR GI（未授权备忘）**：蓄水池存首个间接 bounce 重连点（位置/法线/出射辐射），reconnection shift + Jacobian；本项目特有协同点是 **SHARC 缓存作重连点辐射的廉价目标函数评估**（候选评估零光线，最终 shading 才验证）。需单独立项授权。

## 11. 验证方式

- 日常：`gnb shot --scene ManyLightsShowcase.proc`（8×8 灯阵压力场景，`DemoScenes.cpp`）/ `CornellBox.proc` / `assets/models/conf_room.glb`。
- 脚本：`assets/agentscripts/restir-*.agentscript.json`（quick 等 spp 对比、converge-nee/restir 收敛无偏、noclamp-* 去 clamp 偏差判定、perf、m2-temporal 运动与失效、m3-ab 空间 A/B）。
- 无偏判定方法：restir on/off 两个独立进程在 offline progressive RIS-only 模式各自收敛后 diff；signed mean 与半影区域分布是关键指标，不是只看 mean abs。
