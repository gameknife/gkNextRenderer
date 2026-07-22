---
title: "Tracing Direct Lighting 与 ReSTIR DI 架构"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-19
last_updated: 2026-07-22
---

# Tracing Direct Lighting 与 ReSTIR DI 架构

PathTracing 与 SoftwareTracing 使用公共 BSDF-aware direct-lighting 层，并可为 primary 表面的
**面光 diffuse lobe** 启用 ReSTIR DI（蓄水池时空重采样）。两者共享 BSDF、估计器、reservoir
与复用算法，只在可见性实现上不同：PathTracing 使用 ray query，SoftwareTracing 使用有限长度的
级联 voxel DDA。`r.restir.enable` 关闭时运行经典 BSDF-aware 单样本面光 NEE；开启后仅以
material-aware ReSTIR 替换 Lambertian/Mixture 的 diffuse 面光项，specular 与解析太阳仍由主 tracing
pass 独立估计。

## 1. 范围与边界

**只替换 primary vertex 的面光源 diffuse NEE**：

- `common/BSDF.slang` 是 Lambert、GGX reflection、Fresnel、roughness/PDF 的单一数学来源；
  `FDirectLighting` 将 albedo-demodulated diffuse 与完整 specular radiance 分开。
- GGX 使用 `alpha=perceptualRoughness²` 与精确 Smith G1；不得把
  `k=(roughness+1)²/8` 的实时栅格近似带回 tracing BSDF，否则 smooth grazing highlight 会损失
  约 20–55% 能量。Mixture 的 opaque surface IOR 来自 `RefractionIndex`，材质工厂同时保持
  `RefractionIndex2` 一致；Dielectric transmission 才使用第二介质 IOR。
- 太阳不进入 reservoir：有限 0.25° 太阳盘的 light sample、shadow、BSDF sample hit 和 primary miss
  使用同一方向/能量定义；PathTracing 走 ray query，SoftwareTracing 走 CSM。
- 次级 bounce 的直接光策略（`SecondaryDirectMode`、SHARC update 的逐 hit 直接光记账）不变；**蓄水池样本不喂 SHARC**（相关样本会破坏缓存时域累积）。`Core.SharcUpdate` 入口显式 `RestirPrimary = false`。
- Lambertian 与 Mixture 可进入 diffuse reservoir；Mixture 的目标使用方向相关
  `kD=(1-F)*(1-metalness)`。Metallic、Dielectric、Isotropic 与 DiffuseLight 的 diffuse target 为 0。
- 面光 specular 使用 light proposal + glossy-direction proposal，并以 power heuristic MIS；near-delta
  reflection 使用确定性方向。registered emitter 只按被覆盖的 lobe 抑制，dielectric transmission
  命中不再被 diffuse NEE 误删。
- 当前 dielectric transmission direct 只覆盖 near-delta 确定性透射；rough dielectric microfacet BTDF、
  caustics 与 specular reservoir 不属于现行实现。
- 仅 primary view：shader 侧以 `gpuScene.CustomData0 == 0`（view bank base）判定，非 primary / Transient view 走经典单样本 NEE。
- Offline progressive（编辑器空闲渐进、still benchmark、agentscript 的 accumulated screenshot）强制降级为 **RIS-only**（时空复用全关）：保住“收敛到参考”的语义，同时充当内建的无偏对照组。

## 2. 管线结构

```
主 tracing dispatch (PathTracing 或 Core.SwTracing)
  PrimaryHit → …path loop… → DirectIlluminatePrimary:
      有限太阳盘 diffuse/specular
      面光 specular light/BSDF proposal + MIS
      Lambertian/Mixture diffuse:
        classic → BSDF-aware NEE
        ReSTIR → 初始 RIS + 初始可见性 + 时域合并 → 写 intermediate
  [barrier: reservoir + RT_SINGLE_DIFFUSE / G-buffer]
Core.RestirSpatialShade / Core.SwRestirSpatialShade
  G-buffer 重建表面 → center 胜者最终可见性 → 写 final buffer（时域历史）
  → 空间邻居合并 → shading 可见性 → RT_SINGLE_DIFFUSE += direct
  [之后进入 SamplePostChain：当前样本直接 compose → DLSS/FSR/native]
```

两条第二阶段入口都调用 `common/RestirSpatialShade.slang` 的同一算法主体；硬件入口使用 `ZeroBindWithTLASPipeline + FHardwareRayTracer`，软件入口使用不含 TLAS descriptor 的 `ZeroBindPipeline + FSoftwareRayTracer`。`FSoftwareTracingDirectIlluminator` 单独承载 CSM sun + area NEE，不能把面光逻辑塞回 `FShadowMapDirectIlluminator`，否则会无意改变 SoftwareModern。

经典 direct 使用 `Common.EvaluateAreaLightDirectSample`；ReSTIR gather、重评估和 final shading
统一使用 `Common.EvaluateAreaLightDiffuseSample`。两者都调用 `EvaluateSurfaceBSDF`，不得重新内嵌
Lambert/GGX 公式。

## 3. 数据契约

**资源挂载**：`GPUScene` push constant 已满 128B，扩展资源经 `ReservedAddress0 → FTracingExtras` 表。可用性按**字段**判定（`HashEntries != 0` = SHARC、`RestirParameters != 0` = ReSTIR），不判表指针。PathTracing 的表可同时填 SHARC 与 ReSTIR 地址；SoftwareTracing 使用 ReSTIR 服务自己的只含 ReSTIR 地址表。表内容必须 view 无关，仅在地址变化时重写（稳态零 host 写，见 §9-③）。

**所有权**：`VulkanBaseRenderer` 按需持有唯一 `PipelineCommon::RestirDI`，统一拥有双 reservoir、runtime parameters、extent、clear、barrier、frame stamp 和 history 连续性。PathTracing/SoftwareTracing 往返切换只复用这份内存；`historyGeneration` 与帧不连续会禁止首帧 temporal merge。`r.restir.enable=false` 时不会新分配或 dispatch 第二阶段；已经分配的资源保留到 renderer/device 生命周期结束，避免 CVar 抖动造成 allocation churn。

**蓄水池**（`FRestirReservoir`，16B/px，双缓冲）：`LightData`（24 位灯索引 + 8 位类型）、`PackedUV`（灯面 uv，2×unorm16）、`WeightW`（fp32）、`PackedMTarget`（16 位 M + fp16 p̂）。存生成参数而非世界坐标点：复用跟随灯变换、无需 Jacobian。1080p 双缓冲 ≈ 63 MiB。

**固定角色双缓冲（无帧奇偶）**：intermediate 由 gather 写、spatial pass 读；final 由 spatial pass 写、**下一帧时域读**。每个 buffer 单一写者 pass，跨像素邻居读永远不会观察到半更新数据。

**逐帧竞态关键位走录制 push constant**：`gpuScene.CustomData1` bit1 = 本帧时域有效（PathTracingRenderer 每 dispatch 盖章）。host-visible 参数 buffer（`FRestirRuntimeParameters`）只承载调参类字段。

**最小 BSDF G-buffer**：`RT_BSDF_DATA` 为 `R32G32_UINT`（8B/px），x 保存 material index，y
按位保存 effective metalness；effective roughness 继续来自 `RT_NORMAL.a`，base color 来自
`RT_ALBEDO`，view direction 由 depth/camera 重建。sky/emitter/debug miss 写 `0xffffffff` invalid。
spatial gate 除 ObjectId/normal/depth 外还要求 material index 相同，避免同 instance 多材质串样本。

## 4. 算法

1. **初始候选**（默认 8）：现有 CDF 选灯（`Scene::UpdateLights` 预计算 reserved1/2）+ 灯面均匀 uv，流式 RIS，权重 w = p̂ / (lightPdf/area)。无效候选计入 M（等价零权重流入）。
2. **初始可见性**：对初始 RIS 胜者打 1 根 shadow ray，遮挡则 W=0 但保留 M（visibility reuse，防止复用扩散漏光）。
3. **时域合并**：motion vector 重投影读上一帧 final reservoir；屏外 / ObjectId 不一致 / `MOTIONMOMENT != 0` 时拒绝。当前像素重算 p̂ 后按 W-form 合并（候选权重 = p̂·W·M）；prev.M 钳到 `r.restir.mClamp`（默认 160）。当前像素的 instanceId/motion **由 renderer 状态传参**，不读本 dispatch 刚写的纹理（§9-②）。这只是 reservoir 复用，不读取或生成颜色 history。
4. **空间合并**（pass 2，默认 5 邻居 / 半径 16px golden-angle 螺旋）：几何测试 = 同 ObjectId +
   同 material index + 法线点积 > 0.9 + view 深度相对差 < 10%；邻居样本用 center 的完整 diffuse
   BSDF context 重算 p̂ 后 W-form 合并；**邻居 M 钳到 4×初始候选数**。
5. **最终可见性 + shading**：center 胜者先验证（其判决写入 final buffer = 时域链的可见性反馈）；合并胜者若非 center 样本再补 1 根 visibility query。硬件路径是 open-segment ray query；软件路径是以真实灯点为终点的有限 DDA，起点偏移按所在级联的 voxel unit 自适应。不得退化为固定 80m、恒无遮挡或忽略所有 emissive voxel。

**时域链与空间去耦（关键结构决策）**：final buffer 存"center 经最终可见性"的蓄水池，**空间合并结果只用于本帧 shading、绝不回写历史**。有偏空间合并在可见性边界变暗，回写历史会跨帧复利（实测 13–16% 能量丢失；去耦后 1–2%）。与 RTXDI 结构一致。

## 5. 生命周期与失效

- 蓄水池按 `ActiveViewRenderExtent()` 分配，extent 变化重建并清零（DLSS 模式切换自动覆盖）；重建时必须先销毁 buffer 再释放其绑定 memory。
- 时域有效 = RenderView `historyGeneration` 未变化 ∧ 帧号连续 ∧ 灯集合 generation 不变 ∧ 无 pendingClear。灯 generation 由 `Scene::UpdateLights` 对（数量 + lightMatIdx 序列）做 FNV 签名维护，纯变换/调色不失效。
- **写者全覆盖契约**：primary miss 像素与发光体 primary（Render 早退不走 gather）由入口 shader 显式 `RestirStoreEmpty`，spatial pass 对无效像素写空 final——任何像素每帧必须被写，否则下游消费陈旧蓄水池。

## 6. 已知偏差与精度

- PathTracing RIS-only：与经典 NEE 收敛逐位一致量级（signed diff ≈ -0.03/255）。
- SoftwareTracing CornellBox 600 帧：RIS-only 相对经典 NEE 的 RGB signed mean 为 `+0.053/+0.054/+0.050`（/255），RGB MAE 为 `0.582/0.569/0.588`（/255）。
- 时域：visibility-reuse 记账带来 ≈ -0.08/255，不可见。
- 空间：半影带局部 ≤ 0.7%（-1.7/255）变暗——有偏合并在可见性边界的固有代价，肉眼不可辨；调 `spatialRadius`/`spatialSamples` 可进一步换取。
- estimator 偏差验证直接比较 single/progressive 输出；引擎不再有颜色 history clamp 干扰结果。
- pass 2 深度重建必须精确复刻 `FVisibilityBufferRayCaster` 的 ndc 约定：**`(pixel/size)*2-1`，无 +0.5 半像素**。硬件 shadow 起点用深度比例偏移 `max(EPS2, |viewZ|·1e-4)`；软件 shadow 起点按所在级联的 voxel unit 自适应。软件阴影是粗 voxel 代理，允许边缘更粗，但不允许 blocker 拓扑漏失或 emitter 自遮挡。

## 7. CVar 与调试

| CVar | 默认 | 说明 |
|---|---|---|
| `r.restir.enable` | false | ReSTIR 总开关；关闭时使用经典单样本面光 NEE |
| `r.restir.candidates` | 8 | 初始候选数（时域健康时 2 即接近同质量） |
| `r.restir.temporal` / `mClamp` | true / 160 | 时域复用 / M 上限 |
| `r.restir.spatial` / `spatialSamples` / `spatialRadius` | true / 5 / 16 | 空间复用（关闭只影响邻居数，两个 pass 恒运行） |
| `r.restir.debugMode` | 0 | 1=M 热力图 2=W 3=胜者灯着色 4=复用 proxy（红=无复用） |

Debug 视图由 pass 2 从本地合并状态渲染（race-free）；颜色用 0–255 nit 尺度（compose tonemap 约定）。**每帧运行的 debug overlay 只准替换光照颜色（`WriteDebugColorOnly`），踩 ObjectId/Motion 会毁掉被观测的时域状态**。

## 8. 性能

| 路径 | 平台 / 场景 | 结果 |
|---|---|---|
| PathTracing | RTX 4070，720p ManyLights（64 灯） | 全链 +0.50ms |
| SoftwareTracing classic | Apple M3 Max / MoltenVK，1280×720 ManyLights | 59.88 FPS |
| SoftwareTracing ReSTIR | 同上，8 candidates / temporal+spatial | 46.88 FPS（总帧时间约 +4.65ms，约 +27.7%） |

Apple 数据来自 hidden immediate-mode 的 `engine.frameRate`，只能表示端到端相对开销，不能替代隔离 GPU timer；SoftwareTracing 在该平台未达到 `<1ms` 的理想增量，因此保持实验性、默认关闭。超预算先降 `candidates`/`spatialSamples`，不删除最终可见性查询。

## 9. 修改护栏（实施中验证过的失败模式）

1. **Slang 裸声明 struct 不保证应用字段默认初始化器**。新增字段必须给显式 `__init`，且所有入口声明点显式赋值——未初始化的 `RestirPrimary` 曾把 SharcUpdate pass 送进空指针路径（device lost，且 GPU 相关：一块卡上垃圾恰为 0 不崩）。
2. **同一 invocation 内 storage image 写后读不可见**。当前像素的 primary 数据（instanceId/motion）从 renderer 状态传参；只有前置 pass 写的纹理（ObjectId1/MotionMoment/G-buffer）可读。
3. **竞态关键的逐帧状态严禁放 host-visible buffer**（host 覆写与 in-flight GPU 帧竞争），必须走录进 command buffer 的 push constant。
4. **空间复用结果不得回写时域历史**（§4）。
5. p̂ / ReSTIR shading 共用 `Common.EvaluateAreaLightDiffuseSample`；经典 NEE 与它共享
   `EvaluateSurfaceBSDF`，修改材质 lobe 不得产生第二套 evaluator。
6. 蓄水池是屏幕空间时序状态：新增失效条件必须体现在 RenderView `historyGeneration` 或 ReSTIR 自身 generation 检查中；不要依赖已删除的颜色 history。
7. `RT_SINGLE_DIFFUSE` 的第二阶段既读又写，资源声明必须同时包含 `ShaderRead | ShaderWrite`；reservoir barrier 同时覆盖 intermediate/final 的 shader read/write。

## 10. 否决路线与远期方向

- **无偏空间合并（Talbot/pairwise MIS）**：每邻居额外可见性光线或全对 p̂ 重估，实时预算不匹配；biased + 几何测试 + M 钳制已达肉眼不可辨。
- **世界空间 ReSTIR（ReGIR）**：`kMaxLightCount=1024` 且典型场景灯数十级，屏幕空间足够；>10⁴ 灯再议。
- **太阳并入候选池**：推迟。太阳是低方差 delta 光，进池稀释面光候选质量，且 SoftwareTracing/CSM 路径无法对齐；省 1 根 ray 的收益不抵。
- **ReSTIR GI（未授权备忘）**：蓄水池存首个间接 bounce 重连点（位置/法线/出射辐射），reconnection shift + Jacobian；本项目特有协同点是 **SHARC 缓存作重连点辐射的廉价目标函数评估**（候选评估零光线，最终 shading 才验证）。需单独立项授权。

## 11. 验证方式

- 日常：`gnb shot --scene ManyLightsShowcase.proc`（8×8 灯阵压力场景，`DemoScenes.cpp`）或 `CornellBox.proc`。`conf_room.glb` 属可选资产，使用前先确认当前 pak 确实包含。
- PathTracing 脚本：`assets/agentscripts/restir-*.agentscript.json`。
- BSDF direct smoke：`bsdf-direct-classic-smoke.agentscript.json` 与
  `bsdf-direct-smoke.agentscript.json` 分别以 classic / ReSTIR temporal+spatial 独立进程运行
  `MaterialShowcase.proc`，覆盖 Lambertian/Metallic/Mixture/Dielectric roughness 矩阵。截图后需
  留出实际时间等待 JPEG flush；只等待若干 engine frame 可能得到截断文件和伪灰块。
- SoftwareTracing 脚本：`swrestir-smoke`（classic/RIS/temporal/spatial/debug）、`swrestir-converge-nee` 与 `swrestir-converge-ris`（真正的 accumulated screenshot）、`swrestir-temporal`、`swrestir-switch`、`swrestir-perf`。
- 无偏判定方法：restir on/off 两个独立进程在 offline progressive RIS-only 模式各自收敛后 diff；signed mean 与半影区域分布是关键指标，不是只看 mean abs。
- 2026-07-22 macOS/MoltenVK 主机路径已通过；Android `gradle build` 尚未进入 native 编译，因为验证机的 Android SDK 缺少项目指定的 CMake 3.31.6。补齐该 SDK component 后必须补跑 Android shader/C++ 构建。
