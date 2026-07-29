---
title: "大气散射与高度雾架构"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-29
last_updated: 2026-07-29
---

# 大气散射与高度雾架构

本文定义 gkNextEngine 的程序化大气（sky atmosphere）与高度雾（height fog）架构。
核心约束是**与五条现有渲染路径解耦**：大气子系统只做"生产者"，渲染器通过既有抽象消费，
不在任何 logic renderer 内部出现大气代码。

实现入口：`src/Engine/Rendering/Atmosphere/`、`assets/shaders/Sky.*.comp.slang`、
`assets/shaders/common/Sky.slang`、`assets/shaders/Process.AtmosphereComposite.comp.slang`。

M0–M3 已于 2026-07-29 实现并验收。历史任务分解见
[大气散射与高度雾开发计划](../plans/atmosphere-and-height-fog-plan.md)；M4 是按需候选方向，
不属于当前实现承诺。

## 当前实现与验证入口

- `AtmosphereTimeOfDay.proc` 是昼夜效果演示场景，由 Environment `AnimationTrack` 连续驱动太阳高度角。
- `assets/agentscripts/atmosphere.agentscript.json` 扫描正午、日落与夜间状态，执行运行时断言并截图。
- `gkNextRenderer` 的 Renderer Settings 中提供 `Atmosphere & Fog` 面板，可人工调整大气、空中透视、
  高度雾和 SkyView LUT 分辨率倍率。
- `FogStartDistance` 是**相对相机的射线距离**：介质积分从
  `cameraPosition + rayDirection * FogStartDistance` 开始，不依赖世界原点。
- M4 的 froxel 体积雾光轴、PathTracing 真正介质散射和移动端降级留待明确需求后另行设计。

## 目标与非目标

**目标**

- 物理基础的程序化天空：Rayleigh + Mie + 臭氧吸收 + 多次散射，随太阳高度角连续变化（地平线红移、蓝时刻、黄昏）。
- 大气透视（aerial perspective）：远处几何按距离与高度产生消光与内散射。
- 指数高度雾：可独立于大气开关的美术向雾，含起始距离、最大不透明度、雾色。
- 太阳直射色随大气透射率变化（日落变红），且**不改动任何直接光照 / CSM / ReSTIR 代码**。
- 五个渲染器（PathTracing / SoftwareTracing / SoftwareModern / VoxelTracing / SoftwareModernNoAmbient）表现一致。
- GI（AmbientCube 烘焙）与天空保持一致，不出现"天空变了但间接光没变"。

**明确的非目标**（本设计不覆盖，后续里程碑另行授权）

- froxel 体积雾与体积阴影（god rays / 光轴）。当前设计留出接口但不实现。
- PathTracing 的真正介质散射（光线在雾中多次散射）。PT 与光栅渲染器一样走屏幕空间大气透视。
- 云层、天气系统、降水。
- Android / iOS 降级路径。目标平台为 Windows / Linux 桌面。

## 现状与约束

改动前的事实（均已在代码中核实）：

- 全仓没有任何 fog 实现。天空的唯一来源是 HDR equirect IBL。
- 天空辐射采样只有一个入口族：`Common.SampleIBL` / `SampleIBLRough` / `SampleIBLDiffuse`
  （`assets/shaders/common/SceneSampling.slang`）。高频走 bindless 贴图，辐照度走
  `GPUScene.HDRSHs[SkyIdx]` 的 3 阶球谐。
- 全仓天空采样点共 7 处：`common/PathTracingRenderer.slang`（bounce miss、primary miss）、
  `Core.SwModernNoAmbient.comp.slang`（背景 ×2、diffuse irradiance、specular reflection）、
  `common/AmbientCubeBaker.slang`（GI 烘焙）、`Bake.ClearAmbientCubeCache.comp.slang`（cache 默认值）。
- 太阳是解析 disk：角半径 0.25°，`Common.EvaluateAnalyticSunDisk` 返回 `SunColor.rgb / SunSolidAngle()`。
  即**引擎把 `UniformBufferObject::SunColor` 当作太阳辐照度使用**。
- 五个渲染器全部写 `Bindless.RT_PREV_DEPTHBUFFER`（当帧 NDC 深度，名字是历史遗留），
  GTAO 已依赖它重建世界位置。
- 五个渲染器全部以写 `Bindless.RT_SCENE_COLOR` 结束。
- `VulkanBaseRenderer::RenderViewToBank()` 中 `logicRenderer.Render()` 之后是唯一
  "所有渲染器都跑完、upscale/resolve 之前"的统一位置，且天然按 RenderView bank 工作。
- `UniformBufferObject` 在 `assets/shaders/common/BasicTypes.slang` 中由 C++ 与 Slang 共享
  （C++ 侧经 `Engine/Assets/GPU/UniformBuffer.hpp` include）。
- `Scene::MarkEnvDirty()` 当前是空实现；AmbientCube 每帧逐 cascade 持续烘焙，因此天空变化会渐进收敛。
- `VoxelTracing` 的 contract 只声明 `ERenderOutput::Color`，不含 `Depth`。

## 架构总览

大气子系统是纯生产者：算 LUT、发布参数、提供一个 composite pass。引擎侧只有**三个消费接缝**。

```
                    ┌─────────────────────────────────────────┐
                    │  FAtmosphereSubsystem (Engine/Rendering) │
                    │  · 参数脏检测与 LUT 生命周期              │
                    │  · 每帧 dispatch 4 个 compute pass        │
                    │  · CPU 侧 SH 投影 + 太阳透射率            │
                    └───────────────┬─────────────────────────┘
                                    │ 发布
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
  Seam A: 辐射源              Seam B: 视线介质            Seam C: GI 一致性
  common/Sky.slang         Process.Atmosphere-        hdrSphericalHarmonics_
  （7 个采样点各改一行）      Composite.comp.slang        + ubo.SunColor
                            （RenderViewToBank 内一次）   （渲染器零改动）
```

### Seam A — 天空辐射：一个函数替换现有 IBL 调用

新增 `assets/shaders/common/Sky.slang`，提供唯一的天空查询入口：

```slang
namespace Common
{
    public bool   AtmosphereEnabled();
    // 世界空间方向的天空辐射亮度。roughness 只用于 IBL 回落路径的 mip 选择。
    public float4 SampleSkyRadiance(float3 dir, float roughness);
    // Lambert cosine 卷积后的天空辐照度（供 diffuse 着色与 GI 使用）。
    public float4 SampleSkyIrradiance(float3 normal);
}
```

内部分发：

```
SampleSkyRadiance(dir, roughness):
    if (!AtmosphereEnabled())
        return Camera.HasSky ? SampleIBL(SkyIdx, dir, SkyRotation, roughness)
                               * SkyColor * SkyIntensity
                             : 0;
    return SampleSkyViewLut(dir) * Camera.SunColor.rgb * SkyColor.rgb * SkyLuminanceScale;
```

7 个采样点各改一行。**五个渲染器的着色逻辑、AmbientCube 烘焙、GTAO compose 均无需其它改动。**

> 曾评估过的备选方案：把大气渲成 equirect 贴图，经 `GlobalTexturePool::BindSampleTexture`
> 绑定后把 `EnvironmentSetting::SkyIdx` 指过去，可做到渲染器零改动。**不采用**，原因是
> 它需要 GPU→CPU readback 才能产出 SH（延迟一帧）、多一次 equirect 重采样损失，且
> `SampleIBL` 中 `min(float4(10,10,10,1), ...)` 的硬 clamp 会截断太阳附近的高亮度。
> 该路径可作为 M1 前的一次性原型验证手段，不作为交付形态。

### Seam B — 大气透视与高度雾：一个 pass 覆盖全部渲染器

`Process.AtmosphereComposite.comp.slang`，在 `RenderViewToBank()` 里 `logicRenderer.Render()` 之后执行：

1. 读 `RT_PREV_DEPTHBUFFER`，用 `Camera.ProjectionInverseUnJit` / `ModelViewInverse` 重建世界位置与视距。
2. 天空像素（深度为远平面/无效值）**跳过**——天空辐射本身已含全部大气效应，再叠加会双重计数。
3. 视距在 `AerialPerspectiveMaxDistance` 内：采 AP froxel volume 得到 `(inScatter, transmittance)`；
   超出则钳到最后一片（Hillaire 的标准做法）。
4. 若高度雾开启，再叠加解析指数高度雾（与大气透视相乘合成，顺序在下面"合成顺序"一节固定）。
5. `sceneColor.rgb = inScatter + sceneColor.rgb * transmittance`。
6. **保持 `sceneColor.a` 不变**——`Process.Compose.comp.slang` 用 alpha=0 表示 `noSkyBackground`
   （编辑器透明背景），雾不得污染该语义。

门控条件（不出现任何 renderer-type switch）：

```cpp
HasAll(contract.outputs, ERenderOutput::Color | ERenderOutput::Depth) && atmosphereEnabled
```

VoxelTracing 因此自动豁免。

**为什么放在 upscale 之前**：大气透视是低频信号，在渲染分辨率施加后交给 DLSS/FSR 与几何一起
重建，与 UE 的做法一致；放在 upscale 之后会与 motion vector 不匹配并在边缘产生 ghosting。

### Seam C — GI 与太阳色一致性：CPU 侧产出，渲染器零改动

两件事都在 CPU 完成，因此下游代码完全不知道大气存在：

1. **天空 SH**：`SkyIrradianceProjector` 用同一套大气模型做低成本方向积分（默认 128 方向 × 16 步
   raymarch），投影成 3 阶 SH，写入 `GlobalTexturePool::GetHDRSphericalHarmonics()` 的保留槽位。
   既有的 `Scene::UpdateHDRSH()` 上传路径原样复用，`AmbientCubeBaker` / `SampleIBLDiffuse` 无改动。
2. **太阳透射率**：CPU 求 `TransmittanceToSun(cameraAltitude, sunZenith)`，直接乘进
   `Engine.CameraUbo.cpp` 里填的 `ubo.SunColor`。于是 CSM 直接光、ReSTIR DI、
   `EvaluateAnalyticSunDisk`、PathTracing 的 sun NEE **全部自动获得日落红移**，一行都不用改。

## 数据契约

### FAtmosphereParams

C++ 与 Slang 共享定义，放在 `assets/shaders/common/Atmosphere.slang`（沿用 `BasicTypes.slang`
的 `#ifdef __cplusplus` / `ALIGN_16` 模式）。

```slang
public struct ALIGN_16 FAtmosphereParams
{
    public float3 RayleighScattering;   public float RayleighDensityH;    // 散射系数 1/km，标高 km
    public float3 MieScattering;        public float MieDensityH;
    public float3 MieAbsorption;        public float MiePhaseG;           // Henyey-Greenstein g
    public float3 OzoneAbsorption;      public float OzoneCenterAltitude;
    public float3 GroundAlbedo;         public float OzoneWidth;

    public float BottomRadius;          // 行星半径 km（默认 6360）
    public float TopRadius;             // 大气顶 km（默认 6460）
    public float WorldUnitsPerKm;       // 世界单位 -> km 的换算
    public float WorldOriginAltitude;   // 世界 y=0 对应的海拔 km

    public uint  TransmittanceLutId;    // bindless sample slot
    public uint  MultiScatterLutId;
    public uint  SkyViewLutId;
    public uint  AerialPerspectiveLutId;

    public float AerialPerspectiveMaxDistance; // 世界单位
    public float SkyLuminanceScale;     // 美术向天空亮度倍率（默认 1.0）
    public uint  Flags;                 // bit0 天空 bit1 大气透视 bit2 高度雾
    public float Pad0;

    public float3 FogInscatteringColor; public float FogDensity;
    public float  FogHeightFalloff;     public float FogBaseHeight;
    public float  FogStartDistance;     public float FogMaxOpacity;
};
```

### UBO 只增加一个地址字段

`UniformBufferObject` 已经很臃肿，**不允许**为大气展开一堆散字段。只在尾部追加：

```slang
public uint64_t AtmosphereParams;   // 0 == 大气关闭
public uint64_t AtmosphereReserved0; // 保持 16 字节尾对齐
```

后续新增大气参数只改 `FAtmosphereParams`，UBO 布局不再变动。这是本设计的一条硬性不变量。

### LUT 规格

| LUT | 分辨率 | 格式 | 依赖 | 重算时机 |
|---|---|---|---|---|
| Transmittance | 256 × 64 | RGBA16F | 仅介质参数 | 参数脏 |
| MultiScattering | 32 × 32 | RGBA16F | 介质参数 + 地面反照率 | 参数脏 |
| SkyView | 192 × 108 × `r.atmosphere.skyViewLutScale` | RGBA16F | 太阳方向 + 相机海拔 | 每帧（scene-global） |
| AerialPerspective | 32 × 32 × 32 | RGBA16F | 相机视锥 + 太阳方向 | 每帧（仅 primary view） |

参数化沿用 Hillaire 2020：Transmittance 用 `(cosViewZenith, altitude)`；SkyView 在地平线附近
用 `sqrt` 非线性纬度映射保证地平线细节；AP volume 的 Z 片在 `[0, AerialPerspectiveMaxDistance]` 上线性分布。

显存合计 < 1 MB。

**辐射单位约定（关键）**：LUT 以"太阳辐照度 = 1"的无量纲单位计算，输出的天空辐射亮度单位为 `1/sr`。
运行时乘 `Camera.SunColor.rgb`（引擎已把它当辐照度用）即得引擎单位。这样天空与
`EvaluateAnalyticSunDisk` 的 `SunColor / SunSolidAngle` **自动同尺度，不引入任何魔法常数**。

副作用：大气模式下 `EnvironmentSetting::SkyIntensity` 不再参与天空亮度（它是 IBL 专用旋钮），
天空亮度由 `SunIntensity` 与 `SkyLuminanceScale` 共同决定。切换到大气模式时画面会相对现有 IBL
默认值（`SkyIntensity=100`，被 clamp 到 10 后约为 1000）变暗，属预期，需在 M1 做一次标定。

### 合成顺序（固定，不得更改）

对一个表面像素：

```
L_surface                                  logic renderer 输出
L1 = inScatter_AP + L_surface * T_AP       大气透视
L2 = fogColor * (1-T_fog) + L1 * T_fog     指数高度雾
sceneColor.rgb = L2                        alpha 不变
```

高度雾在大气透视之后，因为它在语义上是"更近的局部介质"。天空像素两者都不施加。

## Pass 调度与资源状态

- **Transmittance / MultiScattering**：参数脏时在 `BeginSceneFrame()` 中 dispatch 一次。
- **SkyView**：每帧在 `BeginSceneFrame()` 中 dispatch，视为 scene-global。
  相机海拔取 primary view。这是一处有意的近似——见"已知限制"。
- **AerialPerspective volume**：每帧只为 primary view 在 `PreRenderPerView()` 后 dispatch。
- **Composite**：每个满足 contract 门控的 view 在 `RenderViewToBank()` 中 `logicRenderer.Render()`
  之后执行。非 primary view 没有 AP volume，退化为纯解析高度雾。

资源状态一律经 `TransitionActiveViewImages` / `AuxiliaryImageStates()` 声明，不得手写 barrier：

- `RT_SCENE_COLOR`：`Compute` + `ShaderRead | ShaderWrite`（同像素读改写，无竞争）。
- `RT_PREV_DEPTHBUFFER`：`Compute` + `ShaderRead`。
- LUT 图像由大气子系统自己用 `FResourceStateTracker` 管理，写后转 `SHADER_READ_ONLY_OPTIMAL`。

## 参数所有权与编辑器集成

新增 `Assets::AtmosphereSetting`（放在 `Engine/Assets/Core/Model.hpp`，与 `EnvironmentSetting` 并列），
由 `Runtime::EnvironmentComponent` 持有并经 `REFLECT_COMPONENT` 暴露，从而自动获得：
PropertyPanel 编辑 UI、undo/redo、QuickJS 绑定、场景序列化。

运行时开关走 `GK_CVAR_*`（`EngineCVars.cpp`）：

- `r.atmosphere.enable` — 大气天空总开关（关闭时回落 HDR IBL）
- `r.atmosphere.aerialPerspective` — 大气透视开关
- `r.atmosphere.heightFog` — 高度雾开关
- `r.atmosphere.skyViewLutScale` — SkyView LUT 分辨率倍率（性能调试用）
- `r.atmosphere.debugMode` — 0 关 / 1 只看 inScatter / 2 只看 transmittance / 3 只看 SkyView LUT

`AtmosphereSetting` 是**场景数据**（存进 glb/scad），cvar 是**运行时覆盖**，与既有 GTAO 的划分一致。
主程序的 Renderer Settings 同时暴露一组直接写入当前 Environment 的人工测试控件；它们是调试入口，
不改变上述场景数据所有权。

## 不变量

修改本子系统时必须守住：

1. 大气代码不得出现在任何 `LogicRendererBase` 子类内部。渲染器只通过 `common/Sky.slang` 消费。
2. UBO 只持有 `AtmosphereParams` 地址，不得展开字段。
3. composite pass 只按 `FRendererContract` 位门控，不得引入 renderer-type 判断。
4. 天空像素不施加大气透视与高度雾（会双重计数）。
5. `sceneColor.a` 的 `noSkyBackground` 语义不得被修改。
6. LUT 辐射单位是"太阳辐照度 = 1"，与 `EvaluateAnalyticSunDisk` 共尺度；不得引入独立的亮度魔法数。
7. 太阳透射率在 CPU 侧折进 `ubo.SunColor`，不得在着色器里重复施加。
8. 大气关闭时，全部代码路径必须与改动前逐位一致（`gnb shot` 可验证）。

## 已知限制与取舍

- **SkyView LUT 是 scene-global**，用 primary view 的相机海拔计算。多视图若海拔差异巨大
  （例如缩略图相机在地下、主相机在山顶）天空会有偏差。当前多视图用例（缩略图、材质预览）不受影响。
- **AP volume 只给 primary view**。次级 view 退化为解析高度雾，远景大气透视缺失。
- **PathTracing 的二次反弹不带介质**。屏幕空间大气透视只作用于 primary ray 的可见表面，
  镜面反射中的远景不会有雾。这是本期明确的取舍，见"非目标"。
- **3D 图像的 bindless 支持已就绪**，不再是不确定项。引擎侧见
  [渲染运行时架构 · Bindless 资源维度](rendering-runtime-architecture.md#bindless-资源维度)：
  `VulkanBaseRenderer::CreateStorageImage3D` 创建并绑定 volume 资源，
  shader 侧用 `Bindless.GetStorageTexture3D<T>()` / `Bindless.GetSampleTexture3D()` 访问。
  AP volume 因此直接用真 3D 图像，2D tile atlas 退化方案已废弃。
  实施时需注意三点：volume 槽位取自 `RES_VOLUME_BASE` 区间且**不经 `ViewRT()` 重映射**；
  采样器必须用 `SamplerConfig::VolumeLut()`（三轴 clamp、无各向异性），否则 LUT 边界会出接缝；
  写后采样之间要有 `GENERAL -> SHADER_READ_ONLY_OPTIMAL` 的 layout 转换。
- **时间推进的 GI 收敛有延迟**。AmbientCube 每帧只烘一个 cascade，太阳快速移动时间接光会滞后若干帧。
  已确认可接受。

## 修改检查表

新增或修改大气相关代码时至少核对：

- 大气开 / 关两种状态下，五个渲染器都跑过一遍。
- 关闭时画面与基线逐位一致。
- 太阳在地平线以下（夜间）、贴地平线（日落）、天顶（正午）三种角度都不出 NaN / 负值。
- 编辑器透明背景（`noSkyBackground`）未被雾污染。
- DLSS / FSR 开启与关闭下，摄像机快速移动不产生 ghosting 或雾闪烁。
- 至少两个不同相机的多视图场景（缩略图 + 主视图）渲染正常。
- GPU timer 中大气相关 pass 合计 < 0.5 ms @1080p。
