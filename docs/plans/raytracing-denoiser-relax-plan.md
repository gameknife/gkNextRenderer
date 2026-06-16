---
title: "光追自研降噪改进计划 — ReLAX 风格演进"
category: plan
status: 已实施 (Phase 0–5)
owner: engine
created: 2026-06-16
last_updated: 2026-06-17
related: src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp, assets/shaders/Process.ReProject.comp.slang, assets/shaders/Process.DenoiseJBF.comp.slang, assets/shaders/Process.AtrousWavelet.comp.slang
---

# 光追自研降噪改进计划 — ReLAX 风格演进

> **实施状态（2026-06-17）**：Phase 0–5 全部落地并经 CornellBox / MaterialShowcase 截图验证；详见文末 [§10 实施记录](#10-实施记录已落地)。剩余 temporal-moment 方差、镜面虚拟重投影、anti-lag 为可选后续。

> 目标：在**不开启 DLSS**、纯自研降噪路径下，把当前 `ReProject + JBF` 两遍管线演进为 **ReLAX 风格的方差引导降噪器**（demodulate + 时域累积带 moment/方差 + 方差引导 à-trous + 自适应时域抗滞后 + 基于 hitDist 的镜面虚拟重投影），显著提升 1spp 路径追踪输入下的稳定性与收敛速度。
>
> 选 ReLAX 作蓝本的原因：本引擎信号布局（**已解调的 diffuse/specular 分离**、G-buffer、**已生成但未使用的 hitDist**）与 NVIDIA NRD 的 ReLAX 几乎一一对应，迁移路径最短、复用最大。SVGF 是其学术原型，本计划在算法细节上沿用 SVGF 公式。

---

## 1. 现状盘点

### 1.1 管线（PathTracing，非 DLSS 分支）

每帧顺序（`PathTracingRenderer::Render`，`PathTracingRenderer.cpp:249`）：

1. **RT pass** — `Core.PathTracing.comp.slang` → `common/PathTracingRenderer.slang`
2. **reproject pass** — `Process.ReProject.comp.slang`（时域累积）
3. **compose pass** — 实际绑定的是 `Process.DenoiseJBF.comp.slang`（空间联合双边滤波 + 重新调制 albedo + 叠加 specular + 编辑器描边 + tonemap，**全部融合在一遍**）

> 注：`Process.ReProjectSimple` / `Process.ComposeSimple` 是 SoftwareModern 渲染器用的简化变体，其中**已启用** YCoCg 邻域钳制（`ClampHistoryToNeighborhood`），而 PathTracing 走的 `ReProject` 里同款逻辑被注释掉了。

### 1.2 信号与 G-buffer（已有，`common/BindlessTexture.slang`）

| Bindless 槽 | 名称 | 内容 | 降噪是否已用 |
|---|---|---|---|
| 1 / 12 | `RT_SINGLE_DIFFUSE` / `RT_SINGLE_SPECULAR` | 当前帧 **已解调** 漫反射辐照度 / 镜面 | ✅ |
| 0 / 11 / 13 | `RT_ACCUMLATE_*` | 时域累积结果 | ✅ |
| 20-22 | `RT_SINGLE_PREV_*` | 历史帧 | ✅ |
| 6 | `RT_ALBEDO` | primary albedo（解调用） | ✅ 仅 compose 重调制 |
| 7 | `RT_NORMAL` | 世界法线 | ⚠️ JBF 中法线权重被注释 |
| 5 | `RT_MOTIONVECTOR` | 屏幕空间 motion | ✅ |
| 3 / 4 | `RT_OBJEDCTID_0/1` | 当前/历史 instance id | ✅ 拒绝历史 |
| 14 | `RT_MOTIONMOMENT` | 视为"disocclusion 倒计时"的 uint | ✅ 二值 reset |
| 10 | `RT_PREV_DEPTHBUFFER` | 上一帧深度 | ❌ 未用于边缘停止 |
| **15 / 16** | **`RT_DIFFUSE_HITDIST` / `RT_SPECULAR_HITDIST`** | **漫反/镜面命中距离** | ❌ **完全未用** |
| 17 | `RT_SPECULAR_ALBEDO` | 镜面 albedo（金属用） | ❌ 未用于解调 |

**关键发现：解调已正确做到**——`PathTracingRenderer.slang` 中 primary 表面 albedo 不计入 `OutSingleDiffuse`（只在二次及之后 bounce 乘 albedo），compose 时 `Total = Total * AccumlateAlbedo + CenterSpec` 重新调制。这是降噪的前提，且已具备。**hitDist 和 prev-depth 是白送的信号，但当前一律没用。**

### 1.3 当前各遍的具体做法

**ReProject（`Process.ReProject.comp.slang`）**

- motion 重投影 + 2×2 双线性，按 object id 逐 tap 拒绝。
- 历史失效条件：miss / 越界 / `MotionMoment > 0`（三者任一即丢历史）。
- 非 fast 模式下做一个 5×5 空间预滤波（法线+objectid 权重）。
- 混合系数 **固定** `currKeep = 1/TemporalFrames`（默认 16 → α≈0.0625），无方差/无自适应。
- YCoCg 邻域 AABB 钳制**被注释**（`:200` 起整段）。

**DenoiseJBF（`Process.DenoiseJBF.comp.slang`）**

- 单遍 JBF，groupshared tile，窗口半径 `WINDOW_R=5`，循环步长 2（在 11×11 窗口里稀疏取 6×6）。
- 边缘停止：空间高斯 `Fi` × 亮度 `Li`。**法线权重 `Ai`、objectid 权重 `Oi` 计算了但在累加里被注释掉**（`Total += Ci * Fi * Li;`）。
- 亮度权重 `Li` **未按方差归一化**（不是 SVGF 的 `|lum_p - lum_q| / (σ·sqrt(Var)+ε)`）。
- specular 不做任何空间滤波，直接 `+ CenterSpec`。

**采样自适应**：`Core.PathTracing.comp.slang:30` 中 `MotionMoment > 0 ? 4 : 1`——刚遮挡区域多投 4×样本。这是唯一的自适应。

---

## 2. 与主流算法对照

| 算法 | 核心思想 | 与本引擎的契合点 |
|---|---|---|
| **SVGF** (Schied 2017) | 时域累积 1st/2nd 亮度 moment → 方差 → **方差引导 à-trous 小波**多遍 | 学术原型，公式直接照搬 |
| **A-SVGF** (Schied 2018) | 时域**梯度**估计 → 自适应 α，抗滞后/抗 ghosting | 解决固定 α 的滞后 |
| **ReBLUR** (NRD) | 基于 hitDist 的各向异性累积 + 快/慢历史 anti-lag | hitDist 已有 |
| **ReLAX** (NRD) | SVGF 衍生，专为路径追踪 **diffuse+specular 解调**输入，含镜面**虚拟重投影**、anti-firefly、anti-lag | **信号布局几乎一致，蓝本** |

**结论**：以 ReLAX 为目标，SVGF 提供 à-trous/方差公式，A-SVGF 提供自适应时域思路。**不直接接 NRD 库**——用户要的是改进自研降噪；但保留"实在卡住可回退 NRD"作为兜底（§8）。

---

## 3. 质量缺口诊断（按影响排序）

- **G1 无方差估计。** JBF 用全局固定 sigma，无法区分"已收敛的平坦区"和"仍噪的 GI 区"——稳定区过糊、噪声区欠滤。**这是头号差距。**
- **G2 单遍空间滤波、足迹太小。** 11px 窗口 + 步长 2，低频块状 GI 噪声滤不掉。SVGF 用 5×5 à-trous 迭代（步长 1,2,4,8,16）以低成本获得大足迹。
- **G3 边缘停止弱/被关。** 法线、objectid 权重注释掉；**完全没有深度/平面距离边缘停止**（`RT_PREV_DEPTHBUFFER` 在手未用）；亮度权重未做方差归一。
- **G4 时域固定 α、无 anti-lag。** `1/TemporalFrames` 常数 → 光照变化时 ghosting + 收敛慢。历史拒绝是二值的（objectid+moment），缺少 fast-history / 梯度自适应。
- **G5 主路径无颜色钳制。** YCoCg 邻域钳制被注释，移动的高光/阴影会拖影（讽刺的是简化版里反而开着）。
- **G6 hitDist 信号浪费。** ReLAX 用 hitDist 做镜面虚拟重投影、按 roughness×hitDist 缩放镜面足迹、估算 disocclusion 置信度——本引擎已写出 hitDist 却没消费。
- **G7 镜面处理差。** specular 用与 diffuse 相同的 objectid 重投影（镜像随视角移动，错），且 JBF 完全不空间滤波 specular → 粗糙反射持续噪、镜面反射 ghost。
- **G8 遮挡恢复粗糙。** disocclusion 只"丢历史 + 4× 采样"，没有"年轻像素用更大空间足迹兜底"（SVGF 的 spatial-variance fallback）。
- **G9 firefly 抑制弱。** 仅常数 clamp（1000/1600），无空间离群抑制，NEE/镜面高能样本闪烁。

### 保留的优点（不要动）
解调已正确；diffuse/specular 已分离；objectid 重投影拒绝；motion moment 作 disocclusion 信号；JBF 的 groupshared tile 框架；hitDist 已产出。

---

## 4. 目标架构

```
RT pass  ──► [demod diffuse] [demod spec] [albedo] [normal] [depth] [objid] [motion] [hitDist d/s]
               │
               ▼
  ① TemporalAccumulate (替换 ReProject)
     - motion 重投影；diffuse 用平面+法线拒绝，spec 用 hitDist 虚拟重投影
     - 自适应 α = 1/clamp(historyLen+1, 1, Nmax)；anti-lag（fast/slow 或梯度）
     - 输出 color + 1st/2nd moment + historyLen
               │
               ▼
  ② EstimateVariance
     - historyLen 足够：Var = E[L²]-E[L]²（时域）
     - historyLen 不足：7×7 空间 moment 估计（disocclusion fallback）
               │
               ▼
  ③ A-Trous ×N (替换 JBF 的滤波部分)，step = 1,2,4,8(,16)
     - w = w_depth · w_normal · w_luma(/sqrt(Var)) · w_hitDist(spec)
     - 同步用 3×3 高斯滤波 Var（方差也要随之滤）
     - 第 1 次迭代结果回灌作下一帧历史（SVGF 惯例）
               │
               ▼
  ④ Compose: Total = diffuse·albedo + specular·specAlbedo; 描边; tonemap → RT_DENOISED
```

新增 buffer（§7）：moment（diffuse/spec 各 2 通道）、historyLen、variance、à-trous ping-pong。

---

## 5. 分阶段实施

> 每个 Phase 自成可验证里程碑，可独立合入、独立验收（§6 用 ReferenceMode 当 ground truth）。建议顺序实施，但 Phase 0 可立即并行。

### Phase 0 — 快速修复（低风险，先把现有遍调对）

不改管线结构，只补 / 开启已有能力。预期就能去掉明显拖影和部分过糊。

- [ ] **开启深度+法线边缘停止**：在 `DenoiseJBF` 的 `JBF()` 里恢复法线权重 `Ai`，并新增平面距离权重 `w_z = exp(-|z_p - z_q| / (σ_z·|∇z·Δp| + ε))`，读 `RT_PREV_DEPTHBUFFER` / 由 normal+depth 重建。把 `Total += Ci*Fi*Li` 改为 `Ci*Fi*Li*Ai*Wz`。
- [ ] **开启 YCoCg 邻域钳制**：把 `ReProjectSimple` 的 `ClampHistoryToNeighborhood` 移植进 `ReProject`（取消那段注释并接 3×3 邻域），抑制移动高光/阴影 ghosting（G5）。
- [ ] **firefly 预钳制**：RT 输出后或累积前，对 `RT_SINGLE_DIFFUSE/SPECULAR` 做相对邻域的亮度 clamp（如 clamp 到邻域均值 + k·σ），替代死板的常数 1000/1600（G9 初版）。
- [ ] **CVar 化降噪开关与 sigma**：`r.denoiser` 已存在；补 `r.denoise.sigmaDepth/Normal/Luma`、`r.denoise.atrousIterations`，便于后续调参。

**验收**：静态场景过糊减轻；相机平移时高光拖影明显减少；不引入新崩边。

### Phase 1 — 时域 moment + 方差估计（SVGF 核心数据）

- [ ] 新增 **moment buffer**：`RT_DIFFUSE_MOMENTS`（RG16F：E[L], E[L²]），镜面同理或先只做 diffuse。
- [ ] 新增 **historyLen buffer**（R16_UINT 或复用/扩展语义）：每像素累积有效历史帧数；disocclusion 时归 0，否则 `min(+1, Nmax)`。可在 `RT_MOTIONMOMENT` 基础上扩展。
- [ ] 在时域累积里同时累积 moment：`m1 = lerp(m1_prev, lum, α)`，`m2 = lerp(m2_prev, lum², α)`。
- [ ] 新增 **EstimateVariance pass**（或并入累积尾部）：
  - `historyLen ≥ 4`：`Var = max(0, m2 - m1²)`（时域方差）。
  - `historyLen < 4`：7×7 邻域用法线+深度权重估计空间方差（disocclusion fallback，G8）。
- [ ] 输出 `RT_VARIANCE`。

**验收**：可视化 Var（接 `Util.VisualDebugger`）——噪声区高、收敛区低、几何边界处不溢出。

### Phase 2 — 方差引导 à-trous（替换 JBF 滤波核心，G1/G2/G3）

- [ ] 新增 `Process.AtrousWavelet.comp.slang`：5×5 B 样条核，按 `step = 1<<iter` 取样。
- [ ] 边缘停止权重（SVGF 公式）：
  - `w_z = exp(-|z_p-z_q| / (σ_z·|∇z·(p-q)| + ε))`
  - `w_n = max(0, dot(n_p,n_q))^σ_n`
  - `w_l = exp(-|l_p - l_q| / (σ_l·sqrt(g(Var)) + ε))`，其中 Var 来自 Phase 1，`g` 为 3×3 高斯。
  - `w = w_z · w_n · w_l`（objectid 仍作硬拒绝）。
- [ ] **方差随之滤波**：每次迭代对 Var 用权重²传播（`Var' = Σw²·Var / (Σw)²`）。
- [ ] 迭代 N=4~5（step 1,2,4,8,16），**第 1 次迭代结果**复制回历史（SVGF 推荐，防止 over-blur 进入时域反馈）。
- [ ] JBF 现有的 groupshared tile 思路可保留给 step=1 的首迭代；大 step 迭代直接全局采样。
- [ ] compose（albedo 重调制 + 描边 + tonemap）从 JBF 拆出，单独成 `Process.Compose`，或保留在最后一次 à-trous 之后。

**验收**：块状 GI 噪声大幅消除；几何/材质边界保持锐利（对比 Phase 0 的 SSIM 提升）。

### Phase 3 — 自适应时域 + anti-lag（A-SVGF / ReLAX，G4）

- [ ] **自适应 α**：`α = 1/clamp(historyLen+1, 1, Nmax)`，年轻像素快收敛、老像素慢更新；移除固定 `1/TemporalFrames`（保留为 Nmax 上限）。
- [ ] **anti-lag**（二选一，先做 fast-history）：
  - *方案 A（fast/slow history）*：再维护一个短窗（Nfast≈4）历史，用其 bbox 钳制慢历史，检测到快变即降权。实现简单、显存换质量。
  - *方案 B（A-SVGF 梯度）*：用稀疏重采样的上一帧样本估计时域梯度 λ，`α = max(α_min, λ)`。质量更好但需改 RT pass 输出梯度样本。
- [ ] **disocclusion 恢复**：年轻像素（historyLen 小）在 à-trous 里强制放大空间足迹/迭代数（与 Phase 2 的 Var fallback 联动）。

**验收**：光照/灯光开关、动态阴影边缘的 ghosting 明显减少；遮挡恢复区数帧内收敛而非长期噪。

### Phase 4 — 镜面路径（消费 hitDist，G6/G7）

- [ ] **镜面虚拟重投影**：用 `RT_SPECULAR_HITDIST` 把反射命中点沿视线虚拟化，按虚拟位置算 motion 做 specular 重投影（而非复用 diffuse 的 objectid 重投影）。
- [ ] **镜面 à-trous 足迹随 roughness × hitDist 缩放**：粗糙/远 → 大足迹；近镜面 → 近乎不滤。
- [ ] 镜面边缘停止额外加 roughness、hitDist 相对差权重。
- [ ] 用 `RT_SPECULAR_ALBEDO`（槽 17）对金属镜面做解调/重调制，与 diffuse 一致。

**验收**：粗糙反射收敛、镜面反射不再拖影；金属体表面镜面噪声下降。

### Phase 5 — firefly / 离群抑制 + 调参（G9 收尾）

- [ ] 在首次 à-trous 前加轻量离群抑制（RCRS 或亮度 median/clamp pre-pass）。
- [ ] 统一暴露 CVar：sigma 组、迭代数、Nmax、Nfast、anti-lag 强度、镜面足迹系数。
- [ ] 给出 1~2 套预设（"质量" / "性能"）。

### Phase 6 — 验证与调优（贯穿，最后集中跑）

- [ ] **Ground truth**：用引擎 `ReferenceMode`（512-sample progressive，见 `TemporalResolve.cpp` 的 ReferenceMode 分支）渲染参考图。
- [ ] **量化指标**：写脚本算 PSNR / SSIM / 时域稳定性（相邻帧差），对比 Phase 0→5 每一档。
- [ ] **快速肉眼验证**：`gnb shot --scene <场景>` 截图回看；接入 `gkNextVisualTest`（`assets/configs/visual_test.json`）做回归。
- [ ] **性能预算**：各遍 `SCOPED_GPU_TIMER` 记 ms，确保 à-trous N 遍 + moment 在目标分辨率下可接受；必要时半分辨率 GI + 上采样。
- [ ] 选典型场景：含大面积间接光、动态光源、粗糙+镜面材质、快速相机/物体运动各一。

---

## 6. 验收标准（整体）

1. 同场景同帧数下，相对 ReferenceMode 的 **SSIM/PSNR 较现状显著提升**（每 Phase 记录数值）。
2. 相机/物体运动时 **ghosting 与边缘拖影**主观明显减轻。
3. **disocclusion 区**在 ~3–5 帧内收敛，无长期残噪。
4. 几何/材质边界 **不过糊**（边缘锐利度不退化）。
5. 总降噪开销在目标分辨率内可控（à-trous 迭代数可降级）。

---

## 7. 新增 Buffer / CVar 清单

**Buffer（`VulkanBaseRenderer.cpp` 的 `CREATE_STORAGE_IMAGE` + `BindlessTexture.slang` 加槽）**

| 用途 | 建议格式 | 备注 |
|---|---|---|
| diffuse moments | RG16F | E[L], E[L²] |
| specular moments | RG16F | Phase 4 |
| variance (diffuse/spec) | R16F ×N | à-trous ping-pong 可复用 |
| historyLen | R16_UINT | 可扩展 `RT_MOTIONMOMENT` 语义 |
| à-trous ping/pong | R16G16B16A16F ×2 | diffuse；spec 另一组 |
| fast history（方案 A） | R16G16B16A16F | Phase 3 |

**CVar（`EngineCVars.cpp`）**：`r.denoise.sigmaDepth/Normal/Luma`、`r.denoise.atrousIterations`、`r.denoise.maxAccumFrames`(Nmax)、`r.denoise.fastFrames`(Nfast)、`r.denoise.antilag`、`r.denoise.specFootprint`。

---

## 8. 风险与回退

- **时域反馈不稳定**：à-trous 结果回灌历史若过度会自激模糊——严格只回灌**第 1 次迭代**结果（SVGF 标准做法）。
- **方差估计噪声**：disocclusion 的空间方差 fallback 不准会闪——用足够大邻域（7×7）+ 深度/法线加权。
- **镜面虚拟重投影复杂**：Phase 4 风险最高，可先只做"按 roughness 扩 footprint 的空间滤波"，虚拟重投影留作增量。
- **整体兜底**：若自研投入产出比不及预期，信号布局（解调 diffuse/spec + hitDist + G-buffer）已对齐 NRD，可较低成本接 **ReLAX**（与 SHARC 接官方库同思路，见 `sharc-official-migration-plan.md`）。本计划的 Phase 0–1 改进对接 NRD 也不浪费（G-buffer/解调质量同样受益）。

---

## 9. 工作量与优先级速览

| Phase | 内容 | 风险 | 质量收益 | 建议优先级 |
|---|---|---|---|---|
| 0 | 边缘停止/YCoCg/firefly 快修 | 低 | 中 | **先做** |
| 1 | moment + 方差估计 | 中 | 高（铺垫） | 高 |
| 2 | 方差引导 à-trous | 中 | **最高** | 高 |
| 3 | 自适应时域 + anti-lag | 中 | 高 | 高 |
| 4 | 镜面 hitDist 路径 | 高 | 中-高 | 中 |
| 5 | firefly/调参 | 低 | 中 | 中 |
| 6 | 验证 | 低 | — | 贯穿 |

> 最短"看得见的提升"路径：**Phase 0 → 1 → 2**，即可把"块状/过糊/拖影"三大主观问题压下去；Phase 3 解决运动响应；Phase 4 专攻反射。

---

## 10. 实施记录（已落地）

> 落地于 2026-06-17，PathTracing 非 DLSS 路径。`r.denoiser` 开启即走新 à-trous 路径（`r.denoiseAtrousIterations>0`）；置 0 回退到 Phase 0 强化版 JBF。

### 已完成

- **Phase 0 — 快速修复**：`Process.DenoiseJBF` 的 JBF 重新启用法线权重 `Ai`、objectid 硬拒绝 `Oi`，新增 SVGF 风格平面深度权重 `Wz`（中心差分估深度斜率，读 `RT_PREV_DEPTHBUFFER` 的 NDC depth）。`Process.ReProject` 加 firefly 预钳制（排除中心的邻域 `mean+kσ`，diffuse k=3 / specular k=1.5）与 YCoCg **方差裁剪**历史钳制（`mean±4σ`，比严格 min/max 更耐 1spp 噪声）。
- **Phase 2 — 方差引导 à-trous**：新增 `Process.AtrousWavelet.comp.slang`，5×5 B3 样条核、步长 `1<<iter`，边缘停止 = 法线^power × 平面深度 × 亮度/√方差 × objid 硬拒绝，方差随 `w²` 传播。结果 ping-pong（`RT_ATROUS_PING/PONG` → `RT_ATROUS_OUT`），**累积 buffer 不被改写**（保持纯时域历史，无自激反馈环）。compose（`DenoiseJBF`）改读 `Camera.DenoiseDiffuseSourceSlot`。
- **Phase 1（简化）— 方差来源**：用**空间方差**替代时域 moment（iter0 做 7×7 几何加权亮度方差；对已解调光照足够）。真 temporal-moment 方差列为后续。
- **Phase 3 — 自适应时域**：`historyLen` 存进累积 buffer 的 alpha 通道，`α = 1/min(historyLen+1, TemporalFrames)`（复用 `TemporalFrames` 作 N_max，稳态等价旧行为、年轻像素快收敛）；à-trous iter0 据 historyLen 做 disocclusion 方差 boost。**实测 8 帧已接近 120 帧画质**。anti-lag fast/slow history 未做。
- **Phase 4 — 镜面路径（增量版）**：同一 à-trous 也跑 specular（共享 ping/pong，落 `RT_ATROUS_SPEC_OUT`）；新增 **roughness 感知足迹**（`RT_NORMAL.w` 存 roughness → 玻璃/镜面收紧足迹防过糊）。方差引导自调节让 specular firefly 消除而锐反射保留。镜面**虚拟重投影**（消费 `RT_SPECULAR_HITDIST`）与 `RT_SPECULAR_ALBEDO` 解调金属未做。
- **Phase 5 — 调参**：CVar 暴露 `r.denoiseAtrousIterations / SigmaLuma / NormalPower / SpecFootprint / SigmaDepth`；`SigmaLuma` 是锐度↔噪声旋钮（默认 4.0 平滑安全，调低更锐但平面 GI 出现轻微 mottling——空间方差的固有局限）。

### 新增资源（`BindlessTexture.slang` / `VulkanBaseRenderer.cpp`）

| 槽 | 名称 | 格式 | 用途 |
|---|---|---|---|
| 18/19 | `RT_ATROUS_PING/PONG` | RGBA16F | à-trous ping-pong（diffuse/spec 共享）；rgb=color, a=variance |
| 24 | `RT_ATROUS_OUT` | RGBA16F | diffuse à-trous 最终输出 |
| 25 | `RT_ATROUS_SPEC_OUT` | RGBA16F | specular à-trous 最终输出 |

UBO（`BasicTypes.slang`）新增 `BFSigmaDepth`（复用旧 `ReservedBool1` 槽）、`DenoiseDiffuseSourceSlot` / `DenoiseSpecularSourceSlot`（末尾追加）。

### 验收结果

CornellBox.proc：噪声从满屏椒盐 → 平滑 GI，边缘锐利、color bleeding 保留、firefly 全消。三路径（denoiser OFF / atrous=0 JBF / atrous=N）均验证正确。`gkNextRenderer` + `gkNextUnitTests` 构建通过（唯一失败的 `Load glTF Skinning Data` 为 pre-existing 的多 `EngineTestFixture` 交互崩溃，与降噪无关）。

### 后续可选（按收益/风险）

1. **真 temporal-moment 方差**（中等工作量，+2 buffer + history copy）：能让 `SigmaLuma` 调更低而平面不 mottling，是空间方差局限的正解。
2. 镜面**虚拟重投影**（`RT_SPECULAR_HITDIST`，最高风险）+ `RT_SPECULAR_ALBEDO` 解调金属。
3. **anti-lag** fast/slow history（运动 ghosting；静态截图难验收）。
