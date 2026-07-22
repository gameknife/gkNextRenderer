---
title: "BSDF-aware Direct Lighting 与光滑材质光源成像开发计划"
category: plan
status: 已实施
owner: engine/rendering
created: 2026-07-22
last_updated: 2026-07-22
---

# BSDF-aware Direct Lighting 与光滑材质光源成像开发计划

> 实施结果（2026-07-22）：M0–M4 的核心链路已落地。公共 BSDF evaluator、diffuse/specular
> 通道拆分、有限太阳盘、glossy/delta direct proposal、按 lobe 的 emitter suppression，以及
> material-aware diffuse ReSTIR 已接入 PathTracing/SoftwareTracing。验证复用现有
> `MaterialShowcase.proc` 并新增 `bsdf-direct-smoke.agentscript.json`。现行契约已经回写
> [Tracing Direct Lighting 与 ReSTIR DI 架构](../designs/pathtracing-restir-design.md)。
>
> 与原计划的明确差异：near-delta dielectric transmission 已修正，rough dielectric 的完整
> microfacet BTDF 与 path continuation 统一 sampler 尚未实现；该限制不阻塞本次“低 roughness
> 光源成像”目标，已在现行设计中记录。

本文规划修正 PathTracing / SoftwareTracing 当前 Direct Lighting（经典 NEE 与 ReSTIR DI）
对所有接收面都使用 Lambert 模型的问题。目标是在保持现有 diffuse lighting channel、
ReSTIR 生命周期和 software/hardware 可见性分工的前提下，让直接光按真实材质 lobe 计算，
并让低 roughness 的 `MaterialMixture`、`MaterialMetallic` 与 `MaterialDielectric` 能稳定呈现
area light 矩形轮廓和解析太阳盘。

当前实现与资源所有权以
[Tracing Direct Lighting 与 ReSTIR DI 架构](../designs/pathtracing-restir-design.md)为基线。
该 design 中“Lambert-only、无 specular direct”是本计划要替换的现行限制；计划完成后必须把
最终契约回写 design，并让本文件退出待办入口。

## 1. 问题定义

### 1.1 当前代码事实

- `IDirectIlluminator` 只接收 `position + normal`，没有 view direction、material、有效
  roughness/metalness、front/back face 或 IOR。
- `Common.EvaluateAreaLightSample()` 固定返回
  `Le * NoL * lightCos / (pi * distance^2)`，本质是 albedo-demodulated Lambert；经典 NEE、
  ReSTIR target 与 ReSTIR final shading 都复用这一结果。
- 太阳路径虽然在 0.25 度半角锥内随机产生 shadow ray，但 BRDF 始终使用锥中心的
  `NoL / pi`；随机方向不参与 shading，也不存在解析太阳盘的 miss radiance。
- primary direct 只加到 `FinalColor / RT_SINGLE_DIFFUSE`，不写
  `FinalReflection / RT_SINGLE_SPECULAR`。
- `MaterialDielectric` 因此收到不应存在的白色 diffuse direct；`MaterialMixture` 收到完整
  Lambert direct，但 dielectric Fresnel reflection 只能依赖低概率 path continuation 偶然命中
  emitter，实时低 spp 下被 diffuse 项淹没。
- registered-emitter suppression 用 `!chanceGGX` 近似“本路径由 NEE 覆盖”。dielectric
  transmission 同样满足该条件，透过玻璃命中注册 emitter 时会被错误清零。
- 当前 GGX evaluator（SoftwareModernNoAmbient）与 path sampler 分散在不同文件，roughness
  到 alpha 的映射和采样方向定义没有共享契约，不能直接在其上叠加可靠 MIS。

### 1.2 用户可见失败

1. roughness 接近 0 的非金属 `MaterialMixture` 看起来仍以漫反射为主，area light 轮廓弱或不可见。
2. `MaterialDielectric` 表面出现明显 diffuse 亮度，而不是 Fresnel reflection / transmission。
3. 解析太阳只能产生 Lambert 亮度和阴影，镜面中没有有限角直径的太阳盘。
4. 开启 ReSTIR 后错误结果更稳定：reservoir 优化的是 Lambert target，并没有优化 specular lobe。
5. 单纯提高 spp 只能逐渐暴露残存的 BSDF emitter-hit 路径，不能修正错误的 diffuse 能量和
   dielectric transmission suppression。

## 2. 目标与非目标

### 2.1 目标

- 建立 shader 侧单一 BSDF evaluator/sampler，统一 Lambert、GGX reflection、Fresnel、metallic
  mixing 与 dielectric transmission 的值、PDF 和 roughness 语义。
- 经典 area-light NEE 按接收材质计算 `f_bsdf(wo, wi) * abs(NoL) / p_light`，输出拆分为
  demodulated diffuse 与完整 specular radiance。
- `MaterialMixture` 使用 glTF metallic-roughness 语义：
  `F0 = lerp(DielectricF0(IOR), baseColor, metalness)`，
  `kD = (1 - F) * (1 - metalness)`；其“dielectric part”是 Fresnel/GGX reflection，不是透明折射。
- `MaterialDielectric` 不再获得 diffuse direct；支持 GGX reflection，并覆盖直接可见的
  microfacet transmission。体积吸收不在本计划内。
- 解析太阳拥有统一的有限太阳盘定义；light sampling、shadow、BSDF-direction hit 与 miss
  radiance 使用相同角半径和能量约定。
- 低 roughness / delta lobe 使用 BSDF-direction 策略，高 roughness 使用 light NEE；两者重叠时
  使用 MIS，不靠笼统 emitter suppression 防双计。
- ReSTIR 继续只负责 area-light diffuse lobe，但该 diffuse target 必须 material-aware；specular
  direct 由独立 estimator 负责，不能再被 reservoir 的 diffuse target 淹没。
- PathTracing 与 SoftwareTracing 共享 BSDF/灯光数学，只保留 hardware ray query 与 software
  finite DDA 两套 visibility 实现。

### 2.2 非目标

- 不在本任务中实现 ReSTIR specular reservoir、ReSTIR GI、caustics cache、色散、薄膜或参与介质。
- 不改变 SoftwareModern / SoftwareModernNoAmbient 的渲染路径和 baseline；可以复用其 GGX 数学，
  但行为迁移需单独验证，不能顺手改外观。
- 不把太阳放入现有 area-light reservoir；太阳仍是单独的低方差 analytic-light estimator。
- 不用 roughness 阈值简单关闭所有 NEE。`MaterialMixture` 即使非常光滑仍有 diffuse lobe，必须保留
  正确的 diffuse direct，同时单独计算其 dielectric specular lobe。
- 不承诺一次性修复当前 path tracer 的所有多 bounce 能量误差；本计划只改与直接光、BSDF/PDF
  契约、emitter hit 和其必要 throughput 记账直接相关的部分。

## 3. 已选设计

### 3.1 BSDF 上下文与返回值

新增公共 shader 模块（建议 `assets/shaders/common/BSDF.slang`），核心类型如下；最终命名可按
Slang 约束调整，但数据边界不得退回散落参数：

```text
FBSDFContext
  Position
  ShadingNormal / GeometricNormal
  Wo                         // 指向上一顶点/相机
  BaseColor
  PerceptualRoughness        // glTF roughness，统一 clamp 策略
  Alpha                      // GGX alpha = roughness^2
  Metalness
  EtaOutside / EtaInside
  MaterialModel
  FrontFace

FBSDFEval
  DiffuseDemodulated         // 不含 baseColor，供 RT_SINGLE_DIFFUSE
  Specular                   // 完整 BRDF/BTDF，供 RT_SINGLE_SPECULAR
  DiffusePdf / SpecularPdf
  Flags                      // diffuse/reflection/transmission/delta

FBSDFSample
  Wi
  Value
  Pdf
  Flags
```

`BuildBSDFContext()` 必须在 normal map、MRA texture 和 material factor 合并后生成有效参数，不能让
经典 NEE、ReSTIR gather 和 path continuation 各自重复一份 texture 解释逻辑。

### 3.2 材质语义

| MaterialModel | Diffuse direct | Specular reflection | Transmission |
|---|---|---|---|
| Lambertian | `baseColor / pi` | 无 | 无 |
| Mixture | `kD * baseColor / pi` | dielectric/metallic GGX，F0 按 metalness 混合 | 无 |
| Metallic | 无 | GGX，`F0 = baseColor` | 无 |
| Dielectric | 无 | dielectric GGX，F0 来自 IOR | microfacet BTDF |
| Isotropic | 保持现状，暂不进入 surface NEE | 无 | 无 |
| DiffuseLight | emitter，不作为 receiver 计算 direct | 无 | 无 |

`MaterialMixture` 的 dielectric component 使用材质 IOR（glTF 默认约 1.46），不能继续把 Fresnel
概率只用于随机分支，却让显式 direct estimator 完全看不到它。diffuse 和 specular 分量必须满足
能量守恒；不能在 compose 后再凭经验压暗 Lambert 项。

### 3.3 输出通道契约

新增统一返回结构 `FDirectLighting`：

```text
FDirectLighting
  DiffuseDemodulated
  SpecularRadiance
```

- diffuse 仍写 `RT_SINGLE_DIFFUSE`，最终由 `Process.Compose` 乘 `RT_ALBEDO`。
- specular 写 `RT_SINGLE_SPECULAR`，其中已经包含 Fresnel、metal tint、BRDF/BTDF 与光源 radiance，
  compose 不再乘 baseColor。
- pure dielectric 的 direct diffuse 必须严格为 0；debug lighting 模式不得改变能量分类。
- SHARC 仍是 view-independent diffuse radiance cache，只接收 diffuse direct。view-dependent
  specular 不写 SHARC，也不能以某一相机方向污染 cache。

### 3.4 Area light 采样契约

把现有 `EvaluateAreaLightSample()` 拆成两层：

1. `SampleAreaLightGeometry()`：只返回 light point、light normal、`Le`、`wi`、distance、
   `pdfArea` 与换算后的 `pdfSolidAngle`。
2. `EvaluateDirectBSDF()`：接收 `FBSDFContext + wi`，返回拆分后的 diffuse/specular 与 BSDF PDF。

经典 light-sampled estimator 使用：

```text
L = Le * f_bsdf(wo, wi) * abs(NoL) * visibility / pdfLightOmega
pdfLightOmega = (lightSelectionPdf / area) * distance^2 / abs(lightNoL)
```

所有 source PDF、Jacobian 和 MIS weight 只在这一契约下计算，禁止继续把 Lambert 的 `1/pi`
藏在 area-light geometry helper 中。

### 3.5 太阳盘与能量约定

- 以当前实现使用的 0.25 度半角为唯一 `SunAngularRadius` 来源，提炼公共 helper；shadow、shading、
  reflected-ray hit 和 miss 不能各自硬编码。
- `Camera.SunColor` 保持“太阳盘积分后的 irradiance/强度”语义，以避免现有 diffuse 亮度突变。
- 均匀采样太阳 solid angle，`pdfSun = 1 / omegaSun`；用于 shading 的 disk radiance 为
  `SunColor / omegaSun`，因此积分后的 Lambert 结果保持 `SunColor * NoL / pi` 量级。
- 任意 BSDF ray miss 时，公共 environment evaluator 必须同时判断 sky IBL 与 analytic sun disk；
  否则 exact mirror 永远无法看到解析太阳。
- 太阳 shadow ray 必须沿实际 sampled direction，BRDF 也必须评估同一方向；不能再用随机方向测
  visibility、用锥中心方向算 shading。

### 3.6 光滑与 delta lobe 策略

只做 light sampling 对近 delta GGX 方差极高，也无法估计数学上的 exact delta。采用混合策略：

- 非 delta surface：至少一个 light candidate，按完整 BSDF evaluator shading。
- glossy surface：增加一个显式 specular-lobe BSDF-direction candidate；`MaterialMixture` 的 specular
  candidate 独立于 diffuse lobe 采样，不能因 Fresnel 概率只有几个百分点而大多数帧完全不采高光。
- exact/near-delta reflection：light-sampled specular 权重为 0，由确定性的 reflection-direction
  candidate 命中注册 area light 或 analytic sun disk。
- dielectric transmission：使用 transmission-direction candidate；命中 emitter 时按 BTDF/Fresnel
  记账，绝不套用 diffuse emitter suppression。
- 两种 proposal 都有效时使用 power heuristic MIS。roughness 阈值只决定 proposal 策略，不得
  改变 BSDF 本身或删除 `MaterialMixture` 的 diffuse lobe。

delta 阈值必须集中定义并通过 roughness sweep 验证连续性；不能在 illuminator、path sampler、
ReSTIR 和 emitter-hit 四处使用不同 magic number。

### 3.7 ReSTIR 边界

本计划不把 specular 塞进现有单 reservoir。选定结构是：

```text
Primary direct
  analytic sun diffuse + specular                 // main tracing pass
  area specular light/BSDF hybrid + MIS           // main tracing pass
  area diffuse
    r.restir.enable=false -> classic BSDF-aware diffuse NEE
    r.restir.enable=true  -> material-aware diffuse ReSTIR
```

原因：`MaterialMixture` 的 diffuse target 通常远大于锐利 dielectric highlight；把两者相加为一个
scalar target 会继续让 reservoir 优先优化 diffuse。specular 又对 normal/view/material 边界高度敏感，
直接复用当前 spatial gates 容易闪烁或跨材质污染。

ReSTIR diffuse target 仍需 `kD(wo, wi, material)`，因此 pass 2 必须获得完整的最小 BSDF 数据：

- 继续读取 `RT_ALBEDO`、`RT_NORMAL`（含 effective roughness）、depth 与重建 view direction；
- 新增紧凑的 `RT_BSDF_DATA`（或等价 packed storage）保存 material model、effective metalness、
  IOR/material id；具体格式在 M0 用布局和带宽数据定案；
- temporal/spatial gate 增加 material identity 一致性，不能只凭 instance id 在多材质 mesh 内复用；
- Metallic/Dielectric/Isotropic/DiffuseLight 的 diffuse target 为 0，并每帧写空 reservoir；
- Mixture target 使用实际 `kD`，Lambertian 保持当前 Lambert 结果。

ReSTIR pass 继续只加 `RT_SINGLE_DIFFUSE`。主 pass 已写好的 specular 不由 reservoir pass 覆盖。

## 4. 里程碑

### M0：冻结参考图与建立公共 BSDF 核心

**目标**：先建立可验证的材质数学和问题基线，不改变最终画面。

**任务：**

1. 新增 `BSDF.slang` 与 `FBSDFContext/FBSDFEval/FBSDFSample`，迁移
   SoftwareModernNoAmbient 现有 Fresnel/GGX helper，但 M0 保持其调用结果不变。
2. 统一 perceptual roughness -> GGX alpha、VNDF sampling、BRDF evaluator 和 PDF；移除当前
   evaluator/sampler 对 roughness 的隐式不一致。
3. 提炼 `BuildBSDFContext()`，覆盖 factor + MRA texture + normal map + IOR + front-face。
4. 新增 `BsdfDirectLightingShowcase.proc`（最终名称可调整）：同屏放置 Lambertian、Mixture
   metalness 0/0.5、Metallic、Dielectric，roughness 至少覆盖 0、0.005、0.02、0.1、0.4、1.0；
   分别提供 rectangle area light、sun-only 和遮挡体。
5. 固化改动前 PathTracing/SoftwareTracing 截图与数值 probe，记录 dielectric diffuse 泄漏、
   mixture 高光缺失和 sun disk 缺失位置，作为预期变化依据而非新 baseline。

**验收：**

- shader 与 `gkNextRenderer/gkNextUnitTests` targeted build 通过。
- common BSDF 的 Lambert/GGX 数学用确定性 shader probe 或小型 CPU mirror test 验证：PDF 非负、
  有限，Fresnel 在 [0,1]，normal-incidence dielectric F0 与 IOR 公式一致。
- roughness 0.005 -> 0.02 -> 0.1 的 evaluator/PDF 无 NaN、Inf 或突然能量跳变。
- M0 最终截图除采样随机差外不改变现有 renderer 外观。

**停止条件**：如果 evaluator 与 sampler 不能对相同 alpha/PDF 达成一致，不进入 NEE/MIS 接线。

### M1：经典 BSDF-aware area/sun direct 与通道拆分

**目标**：ReSTIR 关闭时，primary surface 的 area light 和 sun direct 对所有材质正确分类并可见。

**任务：**

1. 将 `IDirectIlluminator` 改为接收 `FBSDFContext`，返回 `FDirectLighting`；secondary/cache 调用点
   同步传递 incoming direction，不能继续只传 position/normal。
2. 拆分 area-light geometry 与 BSDF shading helper，按 solid-angle PDF 计算经典 NEE。
3. 对太阳实现统一 disk sampling、BSDF evaluation、visibility 和 environment sun-disk hit。
4. `FPathTracingRenderer::Render()` 分别累加 diffuse/specular direct；compose 契约保持不变。
5. 实现材质表中的 lobe 语义，尤其是：
   - Dielectric direct diffuse = 0；
   - Mixture 使用 dielectric F0 + metallic mix，保留 diffuse；
   - Metallic direct diffuse = 0。
6. 在 ReSTIR 开启但尚未完成 M3 时，Lambertian 可继续旧 diffuse reservoir；Mixture、Metallic、
   Dielectric 必须自动回退到 M1 经典 BSDF-aware direct，不能继续显示旧 Lambert-only 结果。
7. SHARC update/query 只消费新的 diffuse direct；specular 保持 live path 结果。

**验收：**

- white dielectric 在 direct-diffuse debug 通道为 0；关闭 sky/indirect 后不再出现白色 Lambert 面光。
- Mixture metalness=0 的 diffuse 仍存在，specular 中出现由 IOR 决定的 area-light highlight；
  metalness 增加时 diffuse 单调下降、specular tint 向 baseColor 过渡。
- sun-only roughness sweep 中高光宽度随 roughness 单调增大；roughness 接近 0 时能看到太阳盘。
- 相同材质在 PathTracing 与 SoftwareTracing 的无遮挡 radiance/高光位置一致；允许 software DDA
  阴影边缘更粗，不允许材质能量不同。
- Lambertian 场景与当前经典 NEE progressive reference 的 signed mean 在 `0.5/255` 内。

### M2：BSDF-direction proposal、emitter hit 与 MIS

**目标**：低 roughness 光源成像稳定，且 light sampling/BSDF sampling 不双计、不漏计。

**任务：**

1. 为 glossy reflection 和 dielectric transmission 增加分层的 BSDF-direction direct candidate；
   Mixture specular candidate 不与 diffuse candidate做概率互斥。
2. area emitter hit 返回 light index、light PDF 与命中 lobe；analytic sun hit 返回 disk PDF。
3. non-delta 重叠区对 light/BSDF proposal 使用统一 power heuristic；delta 只由 BSDF proposal 负责。
4. 删除 `!chanceGGX` 式全局 suppression，改为按“本 lobe 是否被 NEE 覆盖 + MIS weight”记账。
   Dielectric transmission 命中注册 emitter 必须保留。
5. 让 path continuation 复用 `SampleBSDF()` 的 value/pdf/flags，至少保证与本次 direct MIS 有关的
   throughput 不再使用另一套 Fresnel/roughness 逻辑。
6. 任意 emissive geometry 若未注册为 area light，仍允许 BSDF path 正常命中，只是没有 light PDF
   和 NEE proposal；不得因“非注册”而清零。

**验收：**

- exact mirror 对 rectangle emitter 显示边缘清晰、比例正确的矩形反射；相机运动时轮廓几何稳定。
- roughness 横跨 delta 阈值时平均亮度连续，不能出现阈值一侧突然翻倍或消失。
- classic light-only、BSDF-only、MIS 三组 progressive reference：MIS 与两组无偏参考的 signed mean
  差绝对值 `< 0.5/255`，并在 glossy showcase 中明显低于单策略方差。
- 透过 dielectric 直接看到注册 emitter 时不再被 suppression 清零；加 blocker 后仍正确遮挡。
- analytic sun 不在 area emitter 列表中，也能被 reflection/transmission direction 正确识别。

### M3：Material-aware diffuse ReSTIR

**目标**：ReSTIR 开启后保持 M1/M2 的 specular 结果，同时只重采样正确的 diffuse lobe。

**任务：**

1. 定案并新增最小 `RT_BSDF_DATA`；记录每像素附加字节数、格式精度和各 bit/channel 语义。
2. primary hit 全覆盖写 BSDF data；sky/emitter/miss 写显式 invalid，不能遗留上一帧材质。
3. 将 ReSTIR target helper 改为 `EvaluateAreaLightDiffuseTarget(context, lightSample)`：
   Lambertian 使用 Lambert，Mixture 使用方向相关 kD，其他材质返回 0。
4. gather、temporal re-evaluation、spatial re-evaluation 和 final shade 四处共用同一 target/shading
   helper；不能出现 target 用 kD、final 又回到 Lambert 的分叉。
5. temporal/spatial gate 增加 material identity；roughness/normal/view 的额外 gate 只允许作为质量优化，
   不能替代当前像素 target re-evaluation。
6. ReSTIR pass 只读写 `RT_SINGLE_DIFFUSE`；M2 生成的 specular direct 在 main pass 后保持不变。
7. progressive 继续 RIS-only；经典 diffuse NEE 与 RIS-only 以新的 material-aware estimator 重新做
   收敛对照。

**验收：**

- ReSTIR on/off 时 `RT_SINGLE_SPECULAR` 的差异只来自随机序列，不得因 pass 2 被清空或覆盖。
- Dielectric/Metallic pixel 的 reservoir 恒为空且 diffuse direct 为 0；Mixture 与 Lambertian 有有效
  reservoir。
- 多材质同一 mesh/instance 的边界无跨材质 reservoir 污染；ObjectId 相同也必须被 material gate
  拒绝。
- Mixture classic diffuse 与 RIS-only 各自 progressive 600–1024 帧，signed mean 差绝对值
  `< 0.5/255`；temporal/spatial 模式无稳定高光拖影，因为 specular 不进入 reservoir。
- `r.restir.enable=false` 不分配 reservoir，但基础 BSDF G-buffer 是否常驻须按实际 consumer 定案；
  若仅 ReSTIR 使用，关闭路径不得承担额外 storage 带宽。

### M4：回归、性能与文档交付

**目标**：覆盖 renderer、材质、光源、roughness、运动和平台，形成可维护的最终契约。

**任务：**

1. 新增 agentscript：材质 roughness sweep、area/sun A/B、dielectric transmission、ReSTIR
   material boundary、camera orbit 和 classic/RIS convergence。
2. PathTracing 与 SoftwareTracing 分别记录 1280x720：main shading、specular direct candidate、
   ReSTIR pass 与总 GPU 时间；同时记录 `RT_BSDF_DATA` 显存/带宽增量。
3. 检查 DLSS/RR 限制：普通 agent validation 只验证 pre-upscale 输入；真实 DLSS/RR 需按
   `AGENTS.md` 的非 hidden Windows NVIDIA 路径另行验证并提前告知用户。
4. targeted build `gkNextRenderer + gkNextUnitTests`，运行新增 agentscript、相关 visual tests，
   最后按预期变化逐场审查 baseline；不得批量接受所有材质亮度变化。
5. 至少在 Windows hardware PathTracing 和一个无 ray-query 的 SoftwareTracing 平台编译/出图；
   Android 若受 SDK component 阻塞，必须保留明确的未验证状态。
6. 更新 `pathtracing-restir-design.md`：删除 Lambert-only 限制，写入 BSDF context、通道拆分、
   diffuse-only ReSTIR、delta/MIS、sun disk 与 SHARC diffuse-only 契约；同步
   `direct-sample-post-chain.md` 的输入通道说明。
7. 完成后从 `docs/README.md` 的待实施 Plans 删除本计划入口；耐久信息进入 design 后，本 plan
   按文档生命周期删除或移出当前文档面。

**性能门槛：**

- Lambertian/no-light fast path不得发射额外 specular ray。
- `r.restir.enable=false` 不得承担 reservoir pass 或仅为 ReSTIR 服务的 G-buffer 写入。
- glossy specular candidate 的成本必须按实际 glossy pixel coverage 计；若全屏 glossy 使 main shading
  GPU 时间增加超过 20%，优先做材质分支/候选复用和波前一致性优化，不能删除 visibility/MIS
  获得虚假性能。
- ReSTIR material-aware target 相对当前 ReSTIR pass 的增量需单独记录；若超过 15%，先优化 packed
  data 和 texture fetch，再讨论降低候选数。

## 5. 预计文件落点

| 类别 | 文件 | 预期改动 |
|---|---|---|
| 公共 BSDF | `assets/shaders/common/BSDF.slang`（新增） | context、eval、sample、PDF、Fresnel/GGX/BTDF |
| 灯光/路径 | `assets/shaders/common/PathTracingRenderer.slang` | direct interface、area/sun estimator、通道拆分、MIS 接线 |
| 路径采样 | `assets/shaders/common/ConstFunc.slang` | VNDF/roughness helper 迁移或删除重复实现 |
| Shader 接口 | `assets/shaders/common/Shading.slang` | `IDirectIlluminator` 与 context/result 类型 |
| ReSTIR | `assets/shaders/common/Restir.slang`、`RestirSpatialShade.slang` | material-aware diffuse target 与 material gate |
| G-buffer 类型 | `assets/shaders/common/BindlessTexture.slang`、`BasicTypes.slang` | packed BSDF data slot/布局 |
| Renderer 资源 | `VulkanBaseRenderer*`、PathTracing/SoftwareTracing resource declarations | 新 storage、barrier、按需生命周期 |
| Compose 契约 | `Process.Compose.comp.slang` | 原则上不改公式，仅补充/验证通道契约 |
| 参考实现 | `Core.SwModernNoAmbient.comp.slang` | 迁移公共 GGX helper，保持 baseline |
| 场景/验证 | `DemoScenes.cpp`、`assets/agentscripts/bsdf-direct-*.json` | roughness、area/sun、transmission、收敛测试 |
| 文档 | `docs/designs/pathtracing-restir-design.md`、`direct-sample-post-chain.md` | 完成后固化最终架构 |

文件名可因现有 module/import 约束调整，但 BSDF evaluator、light geometry sampling 和 ReSTIR target
必须保持单一来源，不能为了少改 import 又复制回多个入口 shader。

## 6. 风险与护栏

1. **不要用“roughness 很小就跳过 direct”作为修复。** Mixture 仍有 diffuse lobe；正确做法是
   分离 lobe 和 proposal。
2. **不要把 combined diffuse+specular luminance 塞进现有单 reservoir。** 这会让 diffuse target
   淹没小而亮的高光，并把当前空间复用偏差扩展到 view-dependent specular。
3. **不要只加 GGX 值而忽略 PDF/MIS。** emitter geometry 仍由 path sampling 可达；没有配对记账会
   双计，继续使用 suppression 又会漏掉 dielectric transmission。
4. **几何法线与 shading 法线必须分工。** BSDF 用 shading normal，ray origin/hemisphere/透射侧判断
   需要 geometric normal，避免 normal map 导致光线从错误一侧发出。
5. **太阳能量语义必须先冻结。** 若把 `SunColor` 同时当 radiance 和积分强度，有限太阳盘会按
   solid angle 放大或缩小数万倍。
6. **ReSTIR material data 必须全覆盖且有 identity。** 当前 ObjectId 只有 instance 级语义，不能
   代表 primitive/material 边界。
7. **SHARC 不缓存 specular。** 当前缓存没有 view direction 维度，把 GGX direct 写入会造成随相机
   移动粘在世界中的高光。
8. **SoftwareTracing 可见性不能降级。** area light 与 BSDF-direction candidate 都必须使用真实有限
   segment/occlusion；不能为追平 hardware 画面返回恒 visible。
9. **baseline 变化必须分类。** Dielectric 变暗、Mixture 高光增强、sun 镜像出现属于预期；
   Lambertian 平均亮度漂移、无灯场景变化、非 tracing renderer 变化属于回归。

## 7. 完成定义

- MaterialDielectric 在直接光 diffuse 通道为 0，reflection/transmission 不再被 emitter suppression
  错删。
- 非金属 MaterialMixture 同时保留能量守恒的 diffuse 和 dielectric Fresnel/GGX highlight；低
  roughness 能辨认 area light 轮廓。
- Metallic、Mixture、Dielectric 的 area/sun specular 都进入 `RT_SINGLE_SPECULAR`，Lambert direct
  进入 `RT_SINGLE_DIFFUSE`。
- roughness 接近 0 时 area rectangle 与有限太阳盘可见；跨 delta 阈值无明显亮度断层。
- classic NEE、BSDF sampling 与 MIS 收敛一致，无双计；ReSTIR classic/RIS diffuse 收敛一致。
- ReSTIR 只重采样 material-aware diffuse，不覆盖或拖影 specular；多材质边界不串 reservoir。
- PathTracing 与 SoftwareTracing 数学一致、可见性策略各自正确；SoftwareModern baseline 无意外变化。
- targeted build、单测、agentscript、visual validation 和指定平台验证完成，性能/内存数据写入现行
  design；最终 design 与 README 索引已更新。
