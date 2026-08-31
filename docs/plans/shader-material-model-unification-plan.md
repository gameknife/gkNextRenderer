---
title: "PathTracing 材质模型统一（Stage 3 设计增量）"
category: plan
status: 待审阅
owner: engine/rendering
created: 2026-07-24
last_updated: 2026-08-31
---

# PathTracing 材质模型统一（Stage 3 设计增量）

> shader library 的结构重构、文件重组和 `GetRayColor`/`Render` 拆解已经完成；这些已完成阶段不再保留独立执行计划。
> 本文件只跟踪尚未实施的材质模型统一，描述设计、映射、分阶段实现与**待 owner 拍板的决策**。
> 验收标准沿用主计划：**理论正确即可，RNG 序不必保证，输出可与旧版不同**，判据 = 收敛到 PT ground
> truth。

## 1. 当前的两套材质模型（要消除的最大碎点）

同一个渲染器里并存两套互不相干的材质数学：

**A. 路径延续（`FPathTracingRenderer.ScatterAndTrace`）——衰减式 PT**
- lobe 选择：`chanceReflect = rand < Schlick(cosine, RefractionIndex)`、`chanceMetal = rand < metalness`，`chanceGGX = reflect||metal`。
- 方向：`chanceGGX` → `ggxSampling`（Bounded VNDF 绕镜像反射方向），否则 → `RandomInHemiSphere1`（余弦加权半球）。
- **throughput**：`rayColor *= albedo`，仅当命中体是 DiffuseLight **或** `!chanceReflect`（即漫反射/折射乘 albedo，镜面反射乘白）；金属色**只在末尾对 primary bounce** `RayColor *= primaryAlbedo_`。
- 无 pdf 除法、无 BSDF 求值。helper 在 `ConstFunc.slang`（`Schlick`/`ggxSampling`/`RandomInHemiSphere1`）。

**B. 直接光照 / ReSTIR（`common/BSDF.slang`）——物理 evaluator**
- `EvaluateSurfaceBSDF`：GGX `D·G·F/(4·NoV·NoL)`、精确 Smith G、Fresnel Schlick、Lambert `1/π`，带 diffuse/specular pdf。
- `SampleGlossyProposal`：绕反射方向的归一化 Phong-like proposal，**有精确 pdf**（专为 MIS）。
- `EvaluateGlossyDirect`：已用 B 做 primary specular 的 light/BSDF proposal + power-heuristic MIS。

**逐材质正确性诊断（A 相对物理正确）**

| 材质 | A 的行为 | 正确性 |
|---|---|---|
| Lambertian | 余弦采样 + ×albedo | ✅ 恰好等于 `f·cos/pdf = albedo` |
| Metallic | VNDF 绕反射 + throughput 白 + 末尾仅 primary ×albedo | ⚠ 能量近似；**多次弹射金属丢失 Fresnel/颜色**（tint 只在首弹射） |
| Mixture | 用 `Schlick(RefractionIndex)` 概率选 reflect/diffuse | ⚠ 无 `kD=(1-F)(1-metal)` 能量拆分，与 B 的 Mixture 定义不一致 |
| Dielectric | Fresnel 概率 reflect/refract，refract ×albedo | ⚠ 无 Fresnel 加权 throughput；透射被染色 |
| DiffuseLight | 终止、发光 | ✅ |

结论：**Lambertian 已正确，其余是历史遗留的能量近似**。统一到 B 会让金属/混合/介质更接近参考（这正是
"理论正确"的收益），但**会改变这些材质的成像**。

## 2. 统一目标

**单一材质模型：路径延续与直接光照共用 `BSDF.slang` 的一套 D/G/F/Lambert 与重要性采样**，throughput
一律 `f·cosθ / pdf`，emitter 命中用 **MIS**（BSDF-sample vs light-sample）替代现有
`suppressDiffuseEmitter/suppressSpecularEmitter` 的 ad-hoc 抑制。

新增单一采样入口（`BSDF.slang`，与 `EvaluateSurfaceBSDF` 同源）：

```
struct FBSDFSample {
    float3 direction;     // 世界空间出射方向 wi
    float3 throughput;    // f(wi)·cosθ / pdf（已 demodulate？见 D5）
    float  pdf;           // 立体角 pdf（delta lobe 记为 0 并置 isDelta）
    uint   lobe;          // BSDF_LOBE_DIFFUSE / REFLECTION / TRANSMISSION / DELTA
    bool   valid;
};
FBSDFSample SampleSurfaceBSDF(inout uint4 seed, FBSDFContext ctx);
```

`SampleSurfaceBSDF` 按 lobe 概率（基于 F/metalness/roughness）随机选 diffuse / specular-reflect /
（dielectric）transmission，采样方向并返回 `f·cos/pdf`。**与 `EvaluateSurfaceBSDF` 必须自洽**（同一
D/G/F），这样 BSDF-sample 与 light-NEE 的 MIS 权重才成立。

## 3. 关键子问题

### 3.1 MIS 取代 emitter 抑制
现在靠 `suppressDiffuseEmitter/suppressSpecularEmitter` + `IsRegisteredEmitterHit` 硬删"已被 NEE 覆盖
的 emitter 命中"。统一后改为标准 MIS：
- BSDF-sampled ray 命中 emitter → 贡献 × `PowerHeuristic(bsdfPdf, lightPdfOmega)`。
- NEE（light sample）→ 贡献 × `PowerHeuristic(lightPdfOmega, bsdfPdf)`（`EvaluateGlossyDirect` 已是此形）。
- delta lobe（镜面/点光）pdf=0，MIS 退化为"BSDF 全取、light 不取"。
`GetRegisteredAreaLightPdf`（已存在）提供命中 emitter 的 `lightPdfOmega`。

### 3.2 Dielectric
Fresnel `F` 决定 reflect/transmit 分支概率；throughput = `F/pF` 或 `(1-F)/pT`（+ 折射的 η² radiance
scale，若跨介质）。near-delta 走确定性；rough dielectric BTDF 暂不做（超出本次范围，维持现状注释）。

### 3.3 近似 / 缓存路径（不在统一范围，但要兼容）
- **AmbientCube terminal / `ExitAfterFirst` / `ForceExitAfterFirst`**（SwModern、GI bake 风格）：这些是
  有意的 GI 近似，**统一模型只改"继续弹射时的 scatter+throughput"**，terminal 收尾逻辑不动。
- **SHARC**：`OnSegmentThroughput(before, after)` 记录的是 throughput 比值；新 throughput 语义变了，
  SHARC 累积会随之变（预期）。需在 SHARC-on 场景对照验证缓存不发散。
- **ReSTIR**：只处理 primary diffuse，独立于 scatter 模型；不受影响（但 §5 要回归）。

### 3.4 SwModern 私有 BRDF 不并入
`Core.SwModernNoAmbient.comp.slang` 的 `k=(r+1)²/8` 实时近似 BRDF 保持独立（主计划 §6 已定）。

## 4. 分阶段实现（每步独立验证，判据=收敛正确性）

- **S3.1**：`BSDF.slang` 加 `SampleSurfaceBSDF`（diffuse/specular-reflect/transmission），与
  `EvaluateSurfaceBSDF` 同源。先不接线，写 Catch2 白盒（能量守恒、pdf 归一、furnace test）。
- **S3.2**：`ScatterAndTrace` 的 scatter+throughput 换成 `SampleSurfaceBSDF`（暂保留 emitter 抑制）。
  验证 MaterialShowcase / CornellBox 收敛 vs 当前 PT ground truth：Lambertian 应几乎不变；金属/混合/
  介质变化到更物理。
- **S3.3**：emitter 抑制 → MIS（§3.1）。验证发光体不重复计数、无漏光。
- **S3.4**：清理 `ConstFunc.slang` 的 `Schlick`/`ggxSampling`/`RandomInHemiSphere1`（确认无其它引用后
  移除或降级）。
- 每步：`gnb build` + 收敛对照（cornellbox/materialshowcase/gibootcamp）+ SHARC-on 视觉 + ReSTIR 回归。

## 5. 风险与护栏
- **能量/发散**：新 throughput 若 pdf 处理不当会产生 firefly 或变暗；每步 furnace/能量检查。
- **SHARC 记账漂移**：throughput 语义变化必须在 SHARC-on 收敛下确认缓存稳定、不发散。
- **ReSTIR 回归**：跑 `restir-*` / `bsdf-direct-*` agentscript，确认 primary diffuse 不受牵连。
- **护栏沿用**：单一 evaluator（不得再造第二套）、Slang 裸声明显式 `__init`、**结构体成员不可直接作
  `out` 参**（Stage 2a 踩过）、§5 入口×字段组合表语义不变。
- 验证 oracle 已就绪：收敛 agentscript（关缓存，噪声底 mean≈0.003 in cornellbox）+ `compare.py`；
  判据用 signed-mean + 差异图字符 + 能量，而非 bit 级。

## 6. 待 owner 拍板的决策

- **D1 范围**：做到 S3.3（完整 MIS-correct PT，最优雅）还是只做 S3.2（throughput 正确、保留 emitter
  抑制，改动更小）？**建议 S3.3**（MIS 才是真正去掉 ad-hoc 抑制的正解）。
- **D2 specular proposal**：统一用 `BSDF.slang` 的 Phong-like `SampleGlossyProposal`（有精确 pdf、与
  `EvaluateGlossyDirect` 一致、MIS 就绪）还是保留 Bounded VNDF？**建议 Phong-like**（MIS 自洽优先；
  VNDF 方差略优但 pdf 与现有 MIS 不统一）。
- **D3 多次弹射金属 tint**：统一后金属每次弹射都按 F 染色（更正确），会改变多次弹射金属外观。确认接受
  这一可见变化（属预期的"理论正确"）。
- **D4 介质透射染色**：现状 refract ×albedo（把 albedo 当透射色）。统一后透射 throughput 用 `(1-F)`，
  透射色由材质定义决定；确认是否保留"albedo 作透射染色"的既有观感，或转为物理透射。
- **D5 diffuse demodulation**：`RT_SINGLE_DIFFUSE` 是 albedo-demodulated 的。路径延续的 diffuse
  throughput 是否也要 demodulate（与直接光通道一致），还是路径 throughput 用完整 albedo、只在最终
  写 G-buffer 时 demodulate？需对齐 denoiser 的 albedo-demod 约定。**建议**：路径 throughput 保持完整
  能量，demod 只在 primary 直接光通道（维持现状 denoiser 契约），S3.2 时具体核对。

---

请 owner 就 D1–D5 给方向；确认后我按 S3.1→S3.4 落地，每步验证。
