---
title: "大气散射与高度雾（开发计划）"
category: plan
status: 未开工
owner: engine/rendering
created: 2026-07-29
last_updated: 2026-07-29
---

# 大气散射与高度雾开发计划

> 设计方案见 [`docs/designs/atmosphere-and-height-fog-design.md`](../designs/atmosphere-and-height-fog-design.md)。
> 术语、数据契约、不变量、单位约定均以设计文档为准，本文只做任务分解与验收。
> M0→M3 严格串行，每期产物是下一期输入；单个里程碑内的任务可并行。
> 每完成一个里程碑必须先跑完该期"验收"再进入下一期。

## 总览

| 里程碑 | 一句话 | 主要产出 | 依赖 |
|---|---|---|---|
| ~~**P**~~ | ~~前置：bindless 3D 纹理支持~~ **已完成（2026-07-29）** | 见 [前置计划](bindless-3d-texture-support-plan.md) | — |
| M0 | 参数与骨架落地，画面零变化 | `FAtmosphereParams` + `AtmosphereSetting` 反射 + UBO 地址 + cvar + 空 subsystem | — |
| M1 | 程序化天空替换 IBL，含日落红移与 GI 一致 | 3 张 LUT + `common/Sky.slang` + 7 处采样点 + CPU SH/透射率 | M0 |
| M2 | 大气透视与高度雾，五渲染器统一 | AP volume + composite pass + `RenderViewToBank` 接入 | M1 + **P** |
| M3 | 打磨、demo、回归基线与文档收口 | 日夜 demo 场景 + visual test baseline + design 转"现行" | M2 |
| M4 | （未授权）体积雾光轴 / PT 介质散射 | — | M3 + 单独授权 |

**前置计划 P 已于 2026-07-29 完成**：引擎支持 bindless 3D 纹理，M2 的 AP volume 直接用真 3D 图像，
2D tile atlas 退化路径已作废。用法见
[渲染运行时架构 · Bindless 资源维度](../designs/rendering-runtime-architecture.md#bindless-资源维度)。

通用规则（每个里程碑都适用）：

- **构建口径**：本计划全部改动位于 Engine 层与 shader，用
  `./gnb.sh build gkNextRenderer gkNextUnitTests`（Windows: `gnb.bat build ...`）。
  **新增 `.slang` 文件的那一次需要 `--reconfigure`**，之后增量即可。不要全量 `--all`。
- **视觉验证口径**：改动后用 `gnb shot --scene <X>` 截图肉眼确认；跨渲染器验证时逐个切
  `r.rendererType` 再截。回归基线用 `gkNextVisualTest`。
- **回归红线**：大气关闭时，`gnb shot` 的输出必须与改动前逐位一致。任何一期破坏这条都算未完成。
- **不改动范围**：`ThirdParty/`、`.spec/specs/`、`ARCHIVE.md`。

---

## M0 — 参数与骨架

目标：把数据通路和开关全部打通，但一个像素都不改变。这一期做完，后续每期都能独立开关验证。

### 任务

1. **`FAtmosphereParams` 共享定义**
   新建 `assets/shaders/common/Atmosphere.slang`，按设计文档写 `FAtmosphereParams`，
   沿用 `BasicTypes.slang` 的 `#ifdef __cplusplus` / `ALIGN_16` 模式。
   C++ 侧在 `Engine/Assets/GPU/UniformBuffer.hpp` 附近 include（与 `BasicTypes.slang` 同法）。
2. **UBO 追加地址字段**
   在 `assets/shaders/common/BasicTypes.slang` 的 `UniformBufferObject` 尾部追加
   `uint64_t AtmosphereParams` 与 `uint64_t AtmosphereReserved0`。
   ⚠️ 该结构 C++/Slang 共享，改完必须确认 `sizeof` 两侧一致且 16 字节尾对齐。
3. **`Assets::AtmosphereSetting`**
   在 `Engine/Assets/Core/Model.hpp` 与 `EnvironmentSetting` 并列新增，含 `Reset()` 默认值
   （地球标准大气：BottomRadius 6360、TopRadius 6460、MiePhaseG 0.8 等）。
4. **反射到编辑器**
   `Runtime::EnvironmentComponent` 持有 `AtmosphereSetting`，在
   `EnvironmentComponent.cpp` 的 `RegisterReflection()` 中按现有 `PropertyPresets::Editable`
   模式补齐属性，分组名用 `"Atmosphere"` / `"Height Fog"`。
5. **cvar**
   在 `Engine/Runtime/Config/EngineCVars.cpp` 补 `r.atmosphere.*` 五个开关，
   对应字段加到 `UserSettings.hpp`。默认全部关闭。
6. **子系统骨架**
   新建 `src/Engine/Rendering/Atmosphere/AtmosphereSubsystem.{hpp,cpp}`：
   参数脏检测、GPU params buffer（host-visible，每帧写）、`BeginSceneFrame` / `ApplyToView` 空实现。
   在 `VulkanBaseRenderer` 中以 `std::unique_ptr` 持有，构造/析构与 swapchain 生命周期挂接。
   `Engine.CameraUbo.cpp` 中填 `ubo.AtmosphereParams`（关闭时写 0）。
7. ~~3D 图像 bindless 可行性验证~~ —— 由
   [Bindless 3D 纹理支持（前置计划）](bindless-3d-texture-support-plan.md)完成，本期不再承担。
   结论：**真 3D volume**，M2 无需再做形态选择。

### 验收

- `./gnb.sh build gkNextRenderer gkNextUnitTests --reconfigure` 通过。
- `gnb shot --scene assets/models/playground.glb` 与改动前截图逐位一致。
- 编辑器（`gnb editor`）中 EnvironmentComponent 面板出现 Atmosphere / Height Fog 分组且可编辑。
- 新增 Catch2 单测：`FAtmosphereParams` 的 C++/Slang `sizeof` 与字段偏移一致性检查
  （可参照现有 `Test_ComponentSystem.cpp` 的组织方式）。

---

## M1 — 程序化天空

目标：天空由大气模型驱动，日落红移正确，GI 一致，五渲染器表现一致。

### 任务

1. **Transmittance LUT** — `assets/shaders/Sky.Transmittance.comp.slang`，256×64 RGBA16F。
   参数化 `(cosViewZenith, altitude)`。只在参数脏时 dispatch。
2. **MultiScattering LUT** — `assets/shaders/Sky.MultiScatter.comp.slang`，32×32 RGBA16F。
   Hillaire 的各向同性多次散射近似。只在参数脏时 dispatch。
3. **SkyView LUT** — `assets/shaders/Sky.SkyView.comp.slang`，192×108 RGBA16F。
   地平线附近 `sqrt` 非线性纬度映射。每帧在 `BeginSceneFrame()` dispatch。
4. **`common/Sky.slang`** — 实现 `AtmosphereEnabled()` / `SampleSkyRadiance()` / `SampleSkyIrradiance()`，
   按设计文档的分发逻辑与单位约定（`* Camera.SunColor.rgb`，**不引入魔法常数**）。
5. **替换 7 个采样点**（各一行）：
   - `common/PathTracingRenderer.slang:348`（bounce miss）
   - `common/PathTracingRenderer.slang:606`（primary miss）
   - `Core.SwModernNoAmbient.comp.slang:76`（背景）
   - `Core.SwModernNoAmbient.comp.slang:103`（退化样本背景）
   - `Core.SwModernNoAmbient.comp.slang:171`（diffuse irradiance）
   - `Core.SwModernNoAmbient.comp.slang:175`（specular reflection）
   - `common/AmbientCubeBaker.slang:135`（GI 烘焙）
   另需处理 `Bake.ClearAmbientCubeCache.comp.slang:13-19` 的六方向默认值。
   ⚠️ 行号是撰写时快照，实施前用 `rg "SampleIBL"` 重新定位。
6. **CPU 侧 SH 投影** — `Engine/Rendering/Atmosphere/SkyIrradianceProjector.{hpp,cpp}`：
   128 方向 × 16 步 raymarch → 3 阶 SH → 写 `GlobalTexturePool::GetHDRSphericalHarmonics()`
   的保留槽位。太阳方向变化超阈值（默认 `cos(0.5°)`）才重算。
7. **CPU 侧太阳透射率** — 求 `TransmittanceToSun(cameraAltitude, sunZenith)`，
   在 `Engine.CameraUbo.cpp` 中乘进 `ubo.SunColor`。**不得在着色器里重复施加**。
8. **亮度标定** — 记录一组在 `SunIntensity` 默认值下观感合理的
   `SkyLuminanceScale` / `SunIntensity` 组合，写进设计文档的"标定基线"小节。

### 验收

- 五个渲染器（切 `r.rendererType` 0..4）在同一场景同一时刻的天空色调一致，无明显跳变。
- 太阳高度角扫描三点各截一图：正午、贴地平线（应见红移与地平线亮带）、地平线以下（夜空，不得出 NaN / 负值）。
- 日落时太阳直射光与 CSM 阴影色调同步变红——验证 Seam C 生效。
- GI 不变黑：切到 SoftwareModern / PathTracing，室内外过渡处间接光正常。
- `r.atmosphere.enable=0` 时画面与 M0 基线逐位一致。
- SkyIrradianceProjector 的 SH 投影加 Catch2 单测（给定太阳方向，SH 的 L0 系数应为正且量级合理）。

---

## M2 — 大气透视与高度雾

目标：远景消光与内散射、指数高度雾，一个 pass 覆盖全部渲染器。

### 任务

1. **AerialPerspective volume** — `assets/shaders/Sky.AerialPerspective.comp.slang`，
   32×32×32 RGBA16F 真 3D 图像。用 `VulkanBaseRenderer::CreateStorageImage3D` 在
   `RES_VOLUME_BASE` 区间取槽（**绝对槽位，不经 `ViewRT()`**），采样器用
   `SamplerConfig::VolumeLut()`，写后采样之间插 `GENERAL -> SHADER_READ_ONLY_OPTIMAL`。
   Z 片在 `[0, AerialPerspectiveMaxDistance]` 线性分布，RGB 存内散射、A 存平均透射率。
   每帧只为 primary view dispatch。
2. **Composite pass** — `assets/shaders/Process.AtmosphereComposite.comp.slang`：
   深度重建 → 天空像素跳过 → 采 AP volume（超距钳最后一片）→ 叠解析高度雾
   → `inScatter + color * transmittance` → **alpha 原样保留**。
3. **调度接入** — `VulkanBaseRenderer::RenderViewToBank()` 中 `logicRenderer.Render()` 之后调用
   `atmosphere_->ApplyToView(...)`，门控为
   `HasAll(contract.outputs, ERenderOutput::Color | ERenderOutput::Depth) && enabled`。
   资源状态经 `TransitionActiveViewImages` 声明。
4. **非 primary view 退化** — 无 AP volume 时只施加解析高度雾，不得读到未初始化的 volume。
5. **debug 可视化** — 实现 `r.atmosphere.debugMode` 的 1/2/3 三档。

### 验收

- 五个渲染器（PathTracing / SoftwareTracing / SoftwareModern / SoftwareModernNoAmbient）
  远景雾表现一致；VoxelTracing 自动跳过且不报错、不崩。
- 编辑器场景中 `noSkyBackground` 透明背景未被雾污染（对着空场景截图，背景仍透明）。
- DLSS / FSR 开与关各截一张快速移动相机的图，无 ghosting、无雾闪烁。
- 缩略图 / 材质预览（多视图）正常渲染，不出现未初始化数据。
- `r.atmosphere.aerialPerspective=0` + `r.atmosphere.heightFog=0` 时与 M1 基线逐位一致。
- GPU timer：`Sky.*` + `AtmosphereComposite` 合计 < 0.5 ms @1080p（用 `gnb dashboard` 或 profiler 读）。

---

## M3 — 打磨与收口

### 任务

1. **Demo 场景** — 新增或改造一个户外场景（建议基于 `assets/scad/proc/` 的地形场景），
   带日夜循环脚本（TypeScript，驱动 `SunElevation`），用于肉眼与录屏验证。
2. **Visual test baseline** — 在 `assets/configs/visual_test.json` 加 2~3 个大气场景条目
   （正午 / 日落 / 夜间），生成 baseline。
3. **Agent 验证脚本** — 写 `assets/agentscripts/atmosphere.agentscript.json`：
   驱动太阳角度扫描 + 断言 `engine.frameRate` 与截图，纳入 `gnb validate`。
4. **文档收口** — 把标定基线、实际性能数字回填设计文档，
   把 `status` 从"提案"改为"现行"；本计划从 `docs/README.md` 的现行文档面移除或标完成。
5. **索引更新** — `docs/README.md` 的 Designs 段加入本设计。

### 验收

- `gkNextVisualTest` 全绿，新 baseline 稳定可复现。
- `gnb validate --script assets/agentscripts/atmosphere.agentscript.json` 退出码 0。
- 既有单测全绿。
- 设计文档 status 为"现行"，且其中没有未回填的占位数字。

---

## M4 — 后续方向（本计划不含，需单独授权）

- **froxel 体积雾 + 体积阴影**：`Core.VolumetricFogInject/Scatter.comp.slang`，采 CSM 出光轴。
  composite pass 直接改采 froxel volume 替代解析 AP，对外契约不变。
- **PathTracing 介质散射**：在 `ScatterAndTrace` 的每段 segment 上加透射率与单次散射估计。
  会触碰 SHARC 缓存假设与 ReSTIR 的 target function，风险集中，需要独立的一致性校准工作。
- **移动端降级**：LUT 分辨率减半、AP volume 退化为解析高度雾。

## 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| ~~3D 图像 bindless 不受支持~~ | ~~M2 阻塞~~ | **已消除**：[前置计划](bindless-3d-texture-support-plan.md)完成，引擎原生支持 3D bindless |
| 亮度标定与现有 IBL 默认值差异过大 | 用户切换后画面突变 | M1 明确标定基线并写进文档；大气默认关闭 |
| `UniformBufferObject` 改动破坏 C++/Slang 布局一致 | 大面积错误且难定位 | M0 加 `sizeof`/偏移单测 |
| 天空像素判定不准（不同渲染器远平面写法不一） | 天空被二次施雾，地平线出接缝 | M2 逐渲染器核对深度写入值（PT 写 `1.0`/`1000`，NoAmbient 写 `1000`），统一判定阈值 |
| 大气透视与 DLSS 交互产生 ghosting | 动态场景画质退化 | 固定在 upscale 之前施加；M2 验收含 DLSS 开关对比 |
