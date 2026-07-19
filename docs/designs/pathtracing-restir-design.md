---
title: "PathTracing ReSTIR 设计方案"
category: design
status: 提案（未实现；落地后按里程碑转正为现行）
owner: engine/rendering
created: 2026-07-19
last_updated: 2026-07-19
---

# PathTracing ReSTIR 设计方案

配套执行顺序见 [ReSTIR 开发计划](../plans/pathtracing-restir-plan.md)。本文记录设计边界、数据契约和取舍；计划完成前本文状态为"提案"，不得当作当前架构引用。

## 1. 背景与动机

当前 PathTracing 渲染器已有三类样本复用手段：

| 手段 | 位置 | 复用维度 |
|---|---|---|
| NEE（太阳 + 面光源） | `FHardwareDirectIlluminator`（`assets/shaders/common/PathTracingRenderer.slang`） | 无复用：每像素每帧对面光源只抽 1 个 CDF 样本 |
| SHARC 辐射缓存 | `Core.SharcUpdate/Resolve/Query` 三 pass + `IRadianceCache` 抽象 | 世界空间 hash voxel 内跨像素、跨帧复用**间接**辐射 |
| 屏幕空间重投影 | `Process.ReProject.comp.slang` + à-trous | 像素级跨帧复用**最终颜色**（denoise 语义，非采样语义） |

缺口在**直接光采样本身**：`DirectIlluminate` 对面光源做亮度×面积加权 CDF 选灯（`Scene::UpdateLights` 预计算，`reserved1`=cdf、`reserved2`=pdf，上限 `kMaxLightCount=1024`），然后在灯面上均匀取 1 个点、打 1 根 shadow ray。多灯、遮挡复杂或灯面积大的场景中，这 1 个样本命中"又亮又可见"的概率很低，噪声全部推给后端 denoiser 扛。

ReSTIR（Reservoir-based Spatio-Temporal Importance Resampling）把"选哪盏灯的哪个点"这一离散/低维采样问题改为流式 RIS + 蓄水池跨帧跨像素复用：每像素每帧仍只付 1~2 根 shadow ray，但有效候选数可达数百（M 次时空累积），且复用的是**候选样本**而非最终颜色，不与现有 denoiser 冲突。

## 2. 范围界定

**主线（本设计覆盖）：ReSTIR DI，只作用于 primary vertex 的面光源直接光。**

- 替换目标：`FPathTracingRenderer::Render` 尾部对 primary vertex 的一次 `directIllum.DirectIlluminate` 调用中的**面光源部分**。
- 太阳 NEE 保持现状独立走锥形 shadow ray（delta 光方差本来就低，且与 CSM/SoftwareTracing 路径共享约定）；末期里程碑可选并入候选池。
- 天空 IBL 仍走 BSDF 采样 miss 路径，不做 environment ReSTIR（可作远期候选，不在本设计内）。

**显式不在范围：**

- 次级 bounce 的直接光策略（`SecondaryDirectMode`、SHARC update 的逐 hit 直接光记账）不动——这是 SHARC 缓存语义的一部分，动了会污染缓存内容。
- ReSTIR GI / ReSTIR PT（全路径复用、reconnection shift）：只在计划 M5 中作为**探索项**，需 M1–M4 验收后单独授权。
- Ray pipeline：维持 ray query。
- 镜面直接光：现状 `DirectIlluminate` 输出就是 Lambert-only（`M_1_PI`），ReSTIR 目标函数沿用该约定，不引入 specular direct。

## 3. 与现有系统的边界

### 3.1 直接光集成点（唯一改动面）

`FPathTracingRenderer::Render` 中 sample 循环结束后的：

```slang
float4 directColor = float4(0, 0, 0, 0);
directIllum.DirectIlluminate(RandomSeed_, hitPrimaryVertex_.Position, hitPrimaryVertex_.Normal, directColor);
FinalColor += directColor;
```

改为：ReSTIR 启用时，此处只加太阳项；面光源项由 ReSTIR 蓄水池流程给出并写入同一 `RT_SINGLE_DIFFUSE` 通道。ReSTIR 关闭时行为逐位不变（与 `FNullRadianceCache` 同样的"编译掉/短路掉"原则）。

保持不变的约定：

- `suppressRegisteredEmitter`：bounce 0 的 diffuse 路径命中已注册面光源时清零（该贡献由 NEE/ReSTIR 表达）。ReSTIR DI 仍是 NEE 的重采样形式，此 MIS 约定继续成立，无需改。
- SHARC update 路径的 `WantsDirectLighting()` 逐 hit 直接光仍用原 `DirectIlluminate`（单样本即可，缓存自己做时域累积）。**蓄水池样本不得喂给 SHARC**，否则缓存收到的是相关样本，时域累积语义被破坏。
- 独立渲染帧内 `EvaluateDirectLighting` 在 bounce 间的调用（`SecondaryDirectMode==1` 等）不动。

### 3.2 Pass 布局

现有两种模式的 dispatch 序列：

- 普通：`Core.PathTracing.comp`（单 dispatch）
- SHARC：`Core.SharcUpdate`（稀疏）→ `Core.SharcResolve` → `Core.SharcQuery`（全分辨率）

ReSTIR 分两段接入，两种模式共用：

1. **候选生成 + 时域复用**：内联在全分辨率主 dispatch（`Core.PathTracing.comp` / `Core.SharcQuery.comp`）的 `Render` 收尾处。理由：primary vertex（位置/法线/材质）在这里现成，不必重建；候选生成是纯 ALU + buffer 读，不加光线。产出：当前帧 reservoir 写入 ping buffer。
2. **空间复用 + 最终 shading**：新增 `Core.RestirSpatialShade.comp.slang`（`ZeroBindWithTLASPipeline`，需 TLAS 打 shadow ray）。读邻域 reservoir 做 1–2 轮空间合并，对胜者打 1 根 `TraceAreaLightSegment` 可见性光线，把 `W × f` 累加进 `RT_SINGLE_DIFFUSE`。此 pass 在主 dispatch 之后、`TemporalPostChain.Run` 之前执行，barrier 用 `RT_SINGLE_DIFFUSE` 的 compute→compute 读写依赖 + reservoir buffer barrier（模式同 `InsertSharcBarrier`）。

里程碑 M1（RIS-only）阶段第 2 段尚不存在：初始 RIS + 可见性 + shading 全部内联主 dispatch，先验证目标函数与权重正确，再拆 pass。

**Pass 2 的 primary 表面重建**：空间复用 pass 拿不到 `Vertex`，从 G-buffer 重建——法线取 `RT_NORMAL`、albedo 取 `RT_ALBEDO`、世界坐标由 `RT_PREV_DEPTHBUFFER`（ndc depth，`PrimaryHit` 已写）+ `ModelViewInverse`/`Projection` 逆变换重建。大世界（如 riverland 1km）远距离 fp32 深度重建精度需在 M3 验证；若 shadow ray 自遮挡不可接受，备选方案是加一张 fp32x4 世界坐标 RT（Bindless 槽位 31–49 空闲），按需再启用，不默认付带宽。

### 3.3 数据结构与资源布局

**Reservoir（16 字节/像素）：**

```slang
struct FRestirReservoir            // 16 bytes, 双缓冲 ping/pong
{
    uint lightData;                // 低 24 位: lightIndex; 高 8 位: 类型/标志（0=空, 1=面光, 预留 sun/env）
    uint packedUV;                 // 灯面采样点 uv, 2×unorm16
    float weightW;                 // 无偏贡献权重 W = (1/p̂) · (wSum/M)
    uint packedMTarget;            // 低 16 位: M (clamp 后), 高 16 位: p̂ 亮度 fp16
};
```

存样本的"生成参数"（lightIndex + uv）而非世界坐标点，复用时按当前灯 buffer 重新展开——灯动画/变换时样本自动跟随灯面，且无需 Jacobian（目标域就是灯面均匀采样域）。代价：时域复用隐含"灯索引跨帧稳定"假设，见 3.6 失效处理。

**资源挂载**：`GPUScene` push constant 已满（128 字节，14 地址 + 4 uint，`BasicTypes.slang`），无空闲地址槽。方案：把现有 `ReservedAddress0 → Assets::SharcResources` 升级为 PathTracing 扩展资源表：

```cpp
struct FPathTracingExtras          // 原 SharcResources 扩展
{
    // SHARC（原有 5 项）
    uint64_t HashEntries, LockBuffer, Accumulation, Resolved, Parameters;
    // ReSTIR（新增）
    uint64_t RestirReservoirPing;
    uint64_t RestirReservoirPong;
    uint64_t RestirParameters;     // FRestirRuntimeParameters
};
```

配套语义修正：`SharcIsAvailable()` 现在判 `Sharc != nullptr`（判表指针）；升级后表在 ReSTIR-only 模式下也存在，可用性必须改判**字段**——`HashEntries != 0` 为 SHARC 可用、`RestirParameters != 0` 为 ReSTIR 可用。这是本设计中唯一触碰 SHARC 代码的点，属于纯管道判定，不改缓存算法。

**生命周期**：reservoir 是屏幕分辨率时序状态，归属与失效规则对齐 `TemporalResolve`（见 `docs/designs/temporal-history-and-denoising.md`）：

- 按 render extent 分配，extent 变化即重建并清零；
- `TemporalResolve::IsHistoryValidForFrame()` 为假的帧（camera cut、scene changed、renderer 切换、非连续帧……），时域复用整帧禁用（读侧 M 视为 0），不引入独立的失效状态机；
- 多视图：首版只对 primary view（`ActiveViewBankBase()==0`）启用完整 ReSTIR；非 primary / Transient view 退回旧单样本 NEE 路径。理由：PiP/缩略图分辨率小、无稳定历史，蓄水池收益低且要为每 view 双缓冲付显存。后续按需扩展为 per-view 分配。

显存：1080p 全屏 16B × 2 buffer ≈ 66 MiB；1440p ≈ 118 MiB。与 SHARC 2^21 表同量级，可接受；4K 原生（无 DLSS）≈ 265 MiB，文档化为已知成本。

### 3.4 算法各阶段

**目标函数**：p̂ = luminance(Le · surfaceCos · lightCos · lightArea / (π · dist²))，即现有 `DirectIlluminate` 面光路径的 unshadowed 贡献亮度（Lambert-only，albedo 解调后的 lighting 域——与 `RT_SINGLE_DIFFUSE` 存"未乘 albedo 的光照"的约定一致）。

1. **初始候选（默认 8 个）**：沿用现有 CDF 线性选灯（源 pdf = `reserved2`）+ 灯面均匀 uv（源 pdf = 1/area），流式 RIS 进蓄水池。候选生成 0 光线。
2. **初始可见性**：对初始 RIS 胜者打 1 根 shadow ray；被遮挡则 `weightW = 0`（样本保留在蓄水池参与后续 M 计数）。这是标准的"visibility reuse"，防止时空复用把阴影区样本扩散成漏光。
3. **时域复用**：用 `RT_MOTIONVECTOR` 重投影读上一帧 pong buffer，接受条件沿用 `Process.ReProject` 的拒绝语义——重投影落点在屏内、`ObjectId` 一致、`RT_MOTIONMOMENT == 0`（动体像素本来就走 4× sampleMultiplier 补偿，不吃历史）。合并前先按当前像素重算 p̂（灯可能动了），M clamp 默认 20×初始候选数。
4. **空间复用（1 轮，默认 5 邻居，半径 30px）**：邻居接受条件：法线点积 > 0.9、线性深度相对差 < 10%、ObjectId 一致。采用 biased 变体（不做邻居间 Jacobian/收缩补偿），几何测试控制 bias 可见度——与实时业界主流一致，代价是接缝处轻微变暗，验收时盯边缘。
5. **最终 shading**：对最终胜者重算 f 与可见性（1 根 ray），输出 `f × W` 加入 diffuse 通道。

每像素光线预算：现状面光 NEE 1 根 → ReSTIR 2 根（初始可见性 + 最终 shading；M1 内联阶段两者合一仍是 1 根）。太阳 1 根不变。

### 3.5 与 denoiser / 累积链的互动

- ReSTIR 输出仍写 `RT_SINGLE_DIFFUSE`，`TemporalPostChain`（reproject → à-trous → JBF compose）输入契约不变，**零改动接入**。
- 蓄水池时域复用会让相邻帧输出相关，`ReProject` 的 history clamp 统计假设（帧间独立噪声）轻度失真。预期影响是残噪呈低频"绺状"而非白噪；à-trous 对此更友好而非更差。M2 验收显式对比 clamp 行为（黑点回归场景）。
- FireflyClamp 保留：ReSTIR 的 W 在 p̂ 极小时会产生离群亮点，前端 clamp 仍是必要保险。
- **Offline progressive 模式（`IsOfflineProgressiveRenderActive()`）强制降级为 RIS-only（M=初始候选，不做时空复用）**：时空复用带来帧间相关与 biased 空间合并，会污染"1024 帧收敛到参考"的语义。RIS-only 是无偏的，收敛目标不变——这同时提供了内建的 bias 对照组。

### 3.6 失效与边界情况

- 灯集合变化（增删灯、材质切换 DiffuseLight）：CDF 重排后 lightIndex 语义漂移。`Scene::UpdateLights` 侧维护一个 lights generation 计数，写入 `FRestirRuntimeParameters`；变化帧时域读侧 M 归零。灯只变换/变色不重排时无需失效（样本按新灯面重展开）。
- 光照环境变化（sun/sky 切换）：不影响面光蓄水池，无需失效（太阳不在池内）。
- DLSS/upscaler：ReSTIR 全程 render resolution，与现有 temporal 链一致，无额外约定。
- `LightCount == 0`：直接跳过，行为同现状。

### 3.7 CVar 与调试

沿用 `EngineCVars.cpp` 的 GK_CVAR 模式：

```
r.restir.enable          bool   false   总开关（默认关，M4 验收后再议默认值）
r.restir.candidates      uint   8       初始候选数
r.restir.temporal        bool   true    时域复用
r.restir.mClamp          uint   160     时域 M 上限（≈20×candidates）
r.restir.spatial         bool   true    空间复用
r.restir.spatialSamples  uint   5       空间邻居数
r.restir.spatialRadius   float  30.0    空间半径（px）
r.restir.debugMode       int    0       0 关; 1 M 热力图; 2 W; 3 胜者灯索引着色; 4 时域复用命中率
```

调试视图走 `SharcDebugMode` 同款模式（entry shader 里 `WriteDebugColor` 短路）。

## 4. 性能预算

- 光线：+1 根/像素（最终 shading 与初始可见性各 1，替换原面光 NEE 的 1）。
- 带宽：reservoir 读写 ≈ 16B 写 + 时域 16B 读 + 空间 5×16B 读 ≈ 112B/px，另加 pass 2 的 G-buffer 读。
- ALU：候选生成 8× 目标函数评估（每个含一次 CDF 线性扫描，1024 灯上限下最坏 1024 次比较——候选生成阶段可换二分或 alias table，M1 先线性，profile 后定）。
- 预算门槛：1080p、RTX 级桌面卡，ReSTIR 两段合计 < 0.6 ms（`SCOPED_GPU_TIMER` 计量）。超预算优先降 candidates/spatialSamples，而非砍可见性光线。

## 5. 验证策略

- **正确性（bias）**：offline progressive 1024 帧作参考图。对照组：旧 NEE、RIS-only、完整 ReSTIR 三者各累积收敛后与参考 diff。RIS-only 必须逐位收敛一致（无偏）；完整 ReSTIR 允许边缘轻微变暗但需肉眼不可辨（diff 阈值对齐 visual test 的 5）。
- **等 spp 噪声**：`CornellBox.proc`（面光基线）、`conf_room.glb`（真实房间面光）、新增多灯压力场景（程序化 8×8 灯阵 proc 场景，M1 一并加入 DemoScenes）单帧截图对比。
- **时域行为**：`gnb validate` agentscript 驱动相机平移/急转，盯 lag、ghost、disocclusion 噪声爆点；对照 `temporal-history-and-denoising.md` 修改护栏的验证清单（静止收敛、平移、快速旋转、物体边缘、renderer 切换、双 RenderView）。
- **回归**：`r.restir.enable=false` 时 `gkNextVisualTest` 全量 baseline 逐位不变（默认关保证存量 baseline 稳定到 M4）。
- 日常肉眼验证走 `gnb shot --scene CornellBox.proc` / `--scene assets/models/conf_room.glb`。

## 6. 备选路线与否决理由

- **全独立 ReSTIR pass 链（候选生成也拆出主 dispatch）**：G-buffer 重建 primary vertex 需补材质信息，且候选生成本身无光线、拆出去只多付一遍 G-buffer 带宽。否决，保留内联。
- **无偏空间复用（talbot MIS / pairwise MIS）**：每邻居多付可见性光线或全对 p̂ 重估，实时预算内收益不匹配。首版 biased + 几何测试，把无偏版留给 offline 模式的远期选项。
- **世界空间 ReSTIR（灯格/hash 蓄水池，类 ReGIR）**：与 SHARC hash grid 思路同构，对超多灯（>10⁴）才有明显收益；当前 `kMaxLightCount=1024` 且典型场景灯数十级，屏幕空间足够。列为远期，不设计。
- **把太阳并入候选池**：省 1 根 ray，但太阳是低方差 delta 光，进池反而稀释面光候选质量，且 SoftwareTracing/CSM 路径无法对齐。仅 M4 作为可选实验。
