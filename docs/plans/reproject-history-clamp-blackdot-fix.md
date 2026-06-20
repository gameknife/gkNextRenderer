---
title: "ReProject 历史钳制黑点问题 — 根因分析与改进方案"
category: plan
status: 🚧 Phase A 已实现，Phase B/C 待办
owner: engine
created: 2026-06-20
last_updated: 2026-06-20
related: assets/shaders/Process.ReProject.comp.slang, assets/shaders/Process.AtrousWavelet.comp.slang, assets/shaders/Process.ReProjectSimple.comp.slang, src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp, docs/plans/raytracing-denoiser-relax-plan.md
---

# ReProject 历史钳制黑点问题 — 根因分析与改进方案

> 背景：ReLAX 计划 Phase 0 给 PathTracing 的 `ReProject` 启用了 **YCoCg 方差裁剪历史钳制**（`ClampHistoryVariance`, `mean ± 4σ`），用来去掉移动高光/阴影的残影（拖影）。残影确实减轻了，但**累积结果出现大量黑点（椒盐状暗斑），整体质量比启用前更差**。启用前该段是注释掉的，仅靠**宽松的 objectid 逐 tap 拒绝**去残影——残影效果差一些，但历史帧用得更全、画面更干净。
>
> 本文定位黑点根因，给出"既去残影又多留历史"的改进方案，并附后续开发计划，供其他 agent 接手实现。本文是 [光追自研降噪改进计划 — ReLAX 风格演进](raytracing-denoiser-relax-plan.md) 的 Phase 0 补丁 + Phase 1（temporal-moment 方差）的提前落地，建议与之合并阅读。

---

## 1. 涉及代码与当前行为

### 1.1 钳制实现

`ClampHistoryVariance`（`assets/shaders/Process.ReProject.comp.slang:69`）：在**当前帧 1spp** `SourceDiffuse` 上取 3×3 邻域，转 YCoCg，算 `mean`、`sigma`，得到盒子 `[mean - γσ, mean + γσ]`，把重投影得到的历史颜色 clamp 进盒子：

```hlsl
// Process.ReProject.comp.slang:83
const float3 mean  = m1 / 9.0;
const float3 sigma = sqrt(max(0, m2 / 9.0 - mean * mean));
const float3 lo = mean - gamma * sigma;     // gamma = 4.0
const float3 hi = mean + gamma * sigma;
return ycocg2rgb(clamp(rgb2ycocg(history), lo, hi));
```

调用处（`Process.ReProject.comp.slang:266`）对 **diffuse 和 specular 都做**，`kHistoryClampGamma = 4.0`：

```hlsl
historyDiffuse  = ClampHistoryVariance(SourceDiffuse,  ipos, imageMax, historyDiffuse,  4.0);
historySpecular = ClampHistoryVariance(SourceSpecular, ipos, imageMax, historySpecular, 4.0);
```

### 1.2 管线数据流（关键前提）

`PathTracingRenderer::Render`（`src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp:349`）每帧顺序：

1. RT pass → `RT_SINGLE_DIFFUSE`（1spp，已解调）。
2. **reproject pass** → 读 `PrevDiffuse`（= 上一帧的 `RT_ACCUMLATE_DIFFUSE`，由 `CopyToHistory` 拷来），写 `RT_ACCUMLATE_DIFFUSE`。**钳制盒子建立在当前帧 1spp `SourceDiffuse` 上。**
3. a-trous pass → 读 `RT_ACCUMLATE_DIFFUSE`，ping-pong 落 `RT_ATROUS_OUT`，**累积 buffer 不被改写**。
4. compose → 读 `RT_ATROUS_OUT`。
5. copy pass → `RT_ACCUMLATE_DIFFUSE` 拷进历史，供下一帧。

**因此喂给 ReProject 的历史是"纯时域累积值"（a-trous 之前），这本身是对的（SVGF 惯例，避免空间过糊自激反馈）。问题不在数据流，而在钳制盒子的来源是 1spp 噪声。**

---

## 2. 黑点根因（Root Cause）

钳制的本质是：**用一个"干净、已收敛、偏亮"的历史值，去 clamp 进一个"由 1spp 噪声临时拼出来的盒子"**。两个操作数的噪声/收敛程度严重不对等，这就是黑点的结构性来源。具体分四点：

### R1 — 盒子建立在 1spp 噪声上，均值被压低
1spp 已解调 diffuse 邻域极噪：大量 tap 接近 0（NEE 被遮挡、路径没找到光），偶有一个（被 firefly 钳过的）亮 tap。9 个样本的 `mean` 因此**被暗 tap 主导、系统性偏低**，`sigma` 中等。盒子上沿 `hi = mean + 4σ` 会**落在已收敛历史亮度之下**。

### R2 — 下沿钳制把亮历史拉黑（黑点的直接成因）
残影主要是"亮拖尾/暗拖尾"，真正需要的是**上沿**钳制（压住滞留的亮条纹）。但 `mean ± γσ` 是**对称**的，1spp 噪声的下尾被当成了信号 → 干净的亮历史被 clamp 到偏低的 `hi`/盒子内 → 该像素变暗。由于 1spp 邻域**每帧重新洗牌**，这种"被压黑"是**逐帧随机闪烁**的 → 表现为满屏跳动的**黑色椒盐点**。

### R3 — 钳制每帧把历史拉向当帧噪声均值 → 等效历史变短
即使没到"变黑"，每帧 clamp 也会把收敛历史朝当帧 1spp 均值拉一点。累积越久越干净，但盒子只允许它干净到"当帧 1spp 盒子"的程度。**钳制在和它本该保护的累积对着干**——这正是用户观察到的"历史帧用得更少、质量变差"。

### R4 — 中心 firefly 钳制的不对称放大了 R1
`FireflyClamp` 只对**中心**当前样本生效（`Process.ReProject.comp.slang:170`）；`ClampHistoryVariance` 读的 8 个邻域 tap 是**未经 firefly 钳制的原始** `SourceDiffuse`。任何一个过暗（≈0）或过亮 tap 都能左右盒子；下沿尤其被零 tap 拽低。

### R5 — 静态区也中招（最反直觉、最该先修）
当 `motionInRange < 0.1`（静止）时，`FilterHistoryColor` 把四个 prev primitive id 全强制成当前 id（`Process.ReProject.comp.slang:231`），历史被**完整采用**，objectid 拒绝形同关闭。此时**唯一能改写历史的就是这个钳制**。而静止场景里当帧 1spp 仍然每帧重噪，盒子照样抖、照样逐帧压黑收敛历史 → **本该最干净的静态区反而冒黑点**。这解释了"启用前更干净"：旧路径在静态区对历史零改写。

> 对照：`ReProjectSimple`（SoftwareModern 用，`Process.ReProjectSimple.comp.slang:60`）用**严格 min/max AABB** 钳制且工作良好——因为 SoftwareModern 是可见性缓冲/延迟路径，`SourceDiffuse` **不是 1spp 路径追踪**，邻域盒子有意义。把同一套严格/半严格钳制套到 1spp PT 数据上，必然 R1–R5。

---

## 3. 为什么旧的"仅 objectid"留历史更多
旧路径**完全不做颜色钳制**，残影仅靠 `FilterHistoryColor` 逐 tap 比对 `prev primitive id == current`（不匹配就用当帧值替换那个 tap）。在同物体表面上历史**逐帧零损耗**地累积，所以干净、历史长。代价是 objectid **抓不到同物体内部的光照变化**（移动的高光、动态阴影边界在同一物体上滑动）→ 这类残影漏过去。

**结论：我们要的不是"砍掉钳制"，而是让钳制"只在真正需要的地方、用稳定的统计、且永不把历史拉成黑点"。**

---

## 4. 改进方案

设计原则（三条同时满足才算合格）：
1. **盒子来源要稳**：用**空间预滤波后的当前估计**（而非逐帧 1spp 原始邻域）当盒子中心，半宽由方差给出。
2. **钳制要非对称 + 有下限保护**：以**亮度（Y）为主**收紧上沿、放松下沿与色度；给历史一个相对下限地板，杜绝黑点。
3. **强度随置信度自适应**：**静态 + 长历史 → 几乎不钳（全留历史）**；**有运动 / 年轻像素（刚遮挡）→ 收紧钳制（去残影）**。这条直接兑现"既去残影又多留历史"。

下面分"立即可做（不加 buffer）"与"治本（加 temporal moment）"两层。

### 4.1 立即修复（A 方案，无需新增 buffer，建议先落地）

本 shader 里**已经算好了** `spatialDiffuseSample`（5×5 法线+objectid 加权预滤波，`Process.ReProject.comp.slang:198`）。直接复用它当盒子中心，把对称方差盒换成"**以滤波均值为心、亮度主导、非对称、运动/历史自适应**"的软钳制：

```hlsl
// 伪代码：替换 ClampHistoryVariance 的调用语义
// filteredCur = spatialDiffuseSample.rgb （已是几何加权预滤波，比 1spp 稳得多）
// var         = 邻域亮度方差（可用 3x3，或直接复用 a-trous 的 7x7 估计思路）
// staticConf  = 置信度: 历史越长、运动越小 → 越接近 1（越不钳）

float ClampStrengthFromConfidence(float historyLen, float nMax, float motionInRange)
{
    float histConf   = saturate(historyLen / nMax);          // 长历史 → 高
    float motionConf = saturate(motionInRange / 0.5);        // 有运动 → 高
    // 静态+长历史 → strength≈0（几乎不钳，留全历史）
    // 运动/年轻   → strength≈1（收紧，去残影）
    return saturate(max(motionConf, 1.0 - histConf));
}

float3 ClampHistorySoft(float3 history, float3 filteredCur, float varLuma,
                        float strength)
{
    float3 hy = rgb2ycocg(history);
    float3 cy = rgb2ycocg(filteredCur);
    float  sigma = sqrt(max(varLuma, 0.0));

    // 非对称：上沿紧（压亮拖尾）、下沿松（防黑点）；色度放得更松
    float gammaHiY = lerp(8.0, 2.5, strength);   // 运动时收紧到 2.5σ
    float gammaLoY = lerp(8.0, 5.0, strength);   // 下沿始终更松
    float gammaC   = lerp(12.0, 6.0, strength);  // Co/Cg 容噪，放很松

    float3 lo = float3(cy.x - gammaLoY * sigma, cy.yz - gammaC * sigma);
    float3 hi = float3(cy.x + gammaHiY * sigma, cy.yz + gammaC * sigma);
    float3 clamped = clamp(hy, lo, hi);

    // 相对下限地板：历史亮度永不低于滤波均值的 k 倍 → 杜绝黑点
    clamped.x = max(clamped.x, cy.x * lerp(0.9, 0.5, strength));

    // 软钳：按 strength 在原历史与硬钳之间插值，去掉硬边
    float3 outY = lerp(hy, clamped, strength);
    return ycocg2rgb(outY);
}
```

要点：
- **盒子心改用 `spatialDiffuseSample`**（已算好，零额外采样），R1/R4 直接消失。
- **亮度主导 + 非对称下沿 + 地板**：R2 黑点消失。
- **`strength` 由 `historyLen`（alpha，`Process.ReProject.comp.slang:276`）和 `motionInRange`（`:221` 已算）驱动**：静态长历史 `strength→0`，等价旧的"零改写"干净行为（R5 消失、R3 缓解），运动区才收紧去残影。
- specular 单独给更小的 `gammaHi`（更易残影）但同样保留下限地板；或直接用 `RT_NORMAL.w` 的 roughness 调 `strength`（低粗糙度镜面 → 收紧）。

> 进一步简化版（最小改动、先验证方向）：保留现有 `ClampHistoryVariance`，但 (a) 盒子心换成 `spatialDiffuseSample`，(b) `gamma` 从常数 4 改成 `lerp(8.0, 3.0, strength)`，(c) 加 `clamped = max(clamped, mean * 0.5)` 地板。三行就能看出黑点是否消除。

### 4.2 治本（B 方案，落地 ReLAX 计划的 temporal-moment 方差）

A 方案用空间方差，仍有抖动残留（盒子半宽每帧变）。**真正的稳定解是时域方差**——这正是 [ReLAX 计划 §10 后续可选 1](raytracing-denoiser-relax-plan.md) 标注的工作：

- 累积时同步累积亮度一阶/二阶矩 `m1 = lerp(m1_prev, lum, α)`、`m2 = lerp(m2_prev, lum², α)`，存 `RT_DIFFUSE_MOMENTS`（RG16F）。
- `Var_temporal = max(0, m2 - m1²)`，历史不足时退回 7×7 空间估计（disocclusion fallback）。
- 钳制半宽 = `σ_clamp * sqrt(Var_temporal)`，**逐帧稳定、不随 1spp 抖**。配合 4.1 的非对称+自适应，钳制能收得足够紧去残影，又不产生黑点、不无谓丢历史。
- 该 moment buffer 同时让 a-trous 的 `SigmaLuma` 能调更低而平面不 mottling（一举两得，见 ReLAX 计划 §10）。

---

## 5. 后续开发计划（供接手 agent）

> 路径：**先 4.1 A 方案快修验证 → 再 4.2 B 方案治本**。每步独立可验收。

### Phase A — 软钳制快修（低风险，0 新 buffer）— ✅ 已实现 (2026-06-20)
- [x] 在 `Process.ReProject.comp.slang` 用 4.1 的 `ClampHistorySoft` 替换 `ClampHistoryVariance` 调用。盒子心传 `spatialDiffuseSample.rgb`/`spatialSpecularSample.rgb`；旧 `ClampHistoryVariance` 删除。
- [x] `strength` 由 `ClampStrengthFromConfidence(historyLen, nMax)` 给出；`historyLen` 计算上移到钳制之前以便驱动 strength。
- [x] firefly（R4）：盒子心改用几何加权预滤波的 `spatialDiffuseSample`（已对 firefly 鲁棒），方差读原始邻域但 firefly 只会放宽盒子上沿、不会致黑点，故无需额外 firefly。
- [x] CVar 暴露：`r.reproject.clampGammaHi`(2.5) / `clampGammaLo`(5.0) / `clampFloor`(0.5)，经 `UserSettings` + push constant `FReprojectPushConstants` 传入 shader（`PathTracingRenderer.cpp`）。

#### Phase A 补丁：移动镜头噪点修复（2026-06-20）

初版 Phase A 用 `max(motionConf, 1-histConf)` 驱动 strength，**引入了相机平移时的满屏噪点回归**：相机一动 `motionInRange` 拉满 → `strength≈1` → 连**已收敛的长历史像素**也被钳进以**逐帧抖动的 1spp** `spatialDiffuseSample` 为心、`gammaHi=2.5σ` 的窄盒子 → 干净历史每帧被拽向噪声 → 噪点。根因：**屏幕运动量是错误的收紧信号**——平移扫过已累积的静态几何恰恰最该信任历史；而真正的"移动高光在静态面上滑动"的残影其屏幕运动≈0，`motionConf` 既有害又抓不到它。

修正（已落地）：
- **strength 只由历史置信度驱动**：`strength = 1 - saturate(historyLen/nMax)`。长历史 → 0（静止 **与** 平移时都不钳，保持干净）；年轻/刚重置像素 → 1。移除 `motionInRange`/`clampMotionRef`（含 CVar / UserSettings / push constant 字段）。残影留给 Phase B 的时域方差。
- **specular 与 diffuse 同用 history-only strength**（移除 roughness 收紧项，避免近镜面长历史在平移时被钳出噪点）。
- **年轻像素空间回退**：`spatialFallback = saturate(1 - (historyLen-1)/6)`，temporal blend 的当前项在 `currDiffuseEst = lerp(raw1spp, spatialDiffuseSample, spatialFallback)` 之间取——刚重置（相机移动越界 objectid 重投影失败）的像素改用 5×5 几何加权空间估计而非裸 1spp，历史累积后渐隐回裸样本保锐度。
- **物体边缘零星亮点（object-aware firefly）**：`FireflyClamp` 的邻域统计**只取同 primitive 的 tap**（跳过不同 objectid 的邻居）。原先在轮廓边缘/紧邻亮天空处，亮背景 tap 抬高了 mean/sigma，使边缘 firefly 逃过钳制 → 平移时沿物体边缘冒零星亮点。同物体邻居不足 2 个时不下判（保留原值）。需把 `current_primitive_index0` 的取值上移到 firefly 之前。

> 注：CameraShowcase 这类**纯天空/间接光**场景在 `TemporalFrames=16` 的 reproject-only 下，表面本身就有固有颗粒（已与改动前 main baseline 逐图核对，颗粒水平一致，**非本次回归**）；要进一步压噪需开 a-trous 或加大 `TemporalFrames`/Phase B 时域方差。本次三项修复针对的是"相机平移引入的额外噪点"与"边缘零星亮点"，均已消除。

**验收**：`gnb build gkNextRenderer gkNextUnitTests` 通过，slang 编译通过，单测 49146 assertions 全过；`gnb shot --scene CameraShowcase.proc / MaterialShowcase.proc`（静态相机）几何区干净无椒盐、无崩边无回归。**移动镜头噪点**因 headless `gnb shot` 无法驱动相机运动，需在交互运行中（关闭 a-trous，`r.denoiser 0`）平移相机肉眼确认。

### Phase B — temporal-moment 方差（中风险，+1~2 buffer）
- [ ] 新增 `RT_DIFFUSE_MOMENTS`（RG16F；specular 可选）。在 ReProject 累积时同步累积 `m1/m2`（用同一 `currKeep`/α）。
- [ ] 钳制半宽改用 `sqrt(Var_temporal)`，历史不足退回 7×7 空间方差（可直接借 `Process.AtrousWavelet.comp.slang:99` 的 7×7 几何加权估计逻辑）。
- [ ] moment 复用给 a-trous 的 `lumPhi`，让 `SigmaLuma` 可下调（`Process.AtrousWavelet.comp.slang:136`）。
- [ ] 与 ReLAX 计划 Phase 1 合并记录，更新该计划 §10 后续清单状态。

**验收**：钳制可收到更紧（运动 `gammaHi≈2`）而静态/平面区零 mottling、零黑点；SSIM/PSNR 相对 `ReferenceMode` 较 Phase A 再升。

> 备注（2026-06-20）：Phase B 曾实现过一版（moment buffer + 时域方差钳制半宽 + 盒心切时域均值），但应要求**已回退**到 Phase A 调优完的状态。回退原因：因相机平移噪点修复已把 `strength` 改为纯 `1-histConf`（长历史不钳），时域方差当时仅影响 mid-history 钳制稳定性，静态 headless 截图体现不明显，收益待评估。重做时可参考此设计，并考虑用稳定时域带重新开启长历史 ghost 钳制（需交互验证平移既去残影又不冒噪点）。

### Phase C — 验证与调优（贯穿）
- [ ] `ReferenceMode`（512-sample progressive）出 ground truth，脚本算 PSNR/SSIM + 相邻帧时域稳定性（黑点会显著拉高帧间方差，是量化黑点的好指标）。
- [ ] 场景集：大面积间接光、动态光源、移动高光的镜面/粗糙材质、快速相机各一。
- [ ] `SCOPED_GPU_TIMER` 记 reproject pass 开销，确认软钳/moment 累积不显著增加（预期 < +0.1ms@1080p）。

---

## 6. 风险与回退
- **自适应 `strength` 调过头** → 运动区残影回来：`clampMotionRef` 调小、`gammaHi` 调小即可，CVar 化保证可调。
- **下限地板设太高** → 真实暗区（接触阴影/暗 GI）被抬亮：地板用**相对**滤波均值（`cy.x * k`）而非绝对值，且 k 随 strength 降到 0.5，避免压平暗部细节。
- **temporal moment 不稳**（Phase B）：历史不足时务必退回空间方差，且 7×7 邻域要足够大 + 几何加权。
- **整体兜底**：Phase A 若仍不理想，可临时把 `strength` 在静态区直接置 0（完全等价旧"仅 objectid"行为），保证不退化于改动前；残影问题留给 Phase B 的稳定方差解。

---

## 7. 一句话结论
黑点是"**干净亮历史被 clamp 进 1spp 噪声拼出来的对称小盒子、且每帧重洗**"造成的逐帧压黑；修法是**盒子心换成已算好的空间预滤波估计、钳制改亮度主导+非对称+下限地板、强度随历史长度与运动量自适应（静态长历史几乎不钳）**，治本则补上 ReLAX 计划里一直缺的 **temporal-moment 方差**让盒子半宽逐帧稳定。
