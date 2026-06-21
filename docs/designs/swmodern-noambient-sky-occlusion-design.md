---
title: "SwModernNoAmbient 天光遮蔽（屏幕空间 GTAO）设计与开发计划"
category: design
status: 待实现
owner: engine
created: 2026-06-21
last_updated: 2026-06-21
---

# SwModernNoAmbient 天光遮蔽（屏幕空间 GTAO）设计与开发计划

> 状态：⚪ 待实现（草案，供后续 agent 开发）
> 目标渲染器：`Vulkan::SoftwareModernNoAmbient::SoftwareModernNoAmbientRenderer`（枚举 `ERT_SoftwareModernNoAmbient`，UI 名 `SoftwareModernNoAmbient`）
> 本期范围：**仅屏幕空间 GTAO**。体素大尺度天光遮蔽**本期不做**，列入 §8 未来工作（不排期）。
> 关联文件：
> - `src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.{hpp,cpp}`
> - `assets/shaders/Core.SwModernNoAmbient.comp.slang`
> - `assets/shaders/Process.ReProjectSimple.comp.slang`、`assets/shaders/Process.ComposeSimple.comp.slang`
> - `assets/shaders/common/BindlessTexture.slang`（RT slot 枚举）
> - 参考：`Process.DenoiseJBF.comp.slang`（双边滤波 / tonemap 一致性）、`SoftwareModernRenderer.cpp`（完整路径编排）

---

## 1. 背景与目标

`SwModernNoAmbient` 是「低配 / 无 AmbientCube」的轻量 deferred 路径：直接读 visibility buffer，重建命中点，做 **天光（sky IBL diffuse）+ 太阳（Lambert + CSM）** 的直接着色，再经一段时序累积与合成输出。相比完整的 `SoftwareModern`，它**不烘焙、也不采样 AmbientCube**。

它的天光项目前**完全无遮蔽**：每像素直接取 `SampleIBLDiffuse(sky)` 乘 albedo（`Core.SwModernNoAmbient.comp.slang:138-144`）。凹角、墙根、室内、桌底同样吃满天光，导致画面发灰、缺接触感与体积感。

本设计为该路径补上一个**高效的屏幕空间 GTAO**（Ground-Truth / Horizon-Based AO），用 visibility buffer 现成的深度 + 法线，遮蔽天光项的接触级与中近景自遮蔽。GTAO 半分辨率 + 时序累积，预算友好。

> **关于体素大尺度遮蔽**：初版设想用 CPU 体素 sky-visibility 做更大尺度遮蔽。本期明确**不做**——既不引入运行时 `TraceOcclusionDDA` 体素步进，也不引入 `requestVoxel` 门控解耦。原因与后续可行方向见 §8。本期 GTAO 是完全自洽、独立可用的交付。

### 目标（In Scope）

- 为 `SwModernNoAmbient` 增加一个高效 GTAO pass（半分辨率 + 时序累积）。
- 仅遮蔽**天光（间接）项**，不破坏太阳直接光既有的 CSM 阴影、不压暗自发光。
- 双边联合上采样，避免边缘漏光。
- 提供调试视图与开关（cvar / UserSettings）。

### 非目标（Out of Scope）

- 体素大尺度天光遮蔽（DDA 或 CPU 预烘焙）—— 本期不做，见 §8。
- `requestVoxel` 门控解耦 —— 本期不引入。
- AmbientCube 颜色烘焙 / 多次反弹 GI —— 那是 `SoftwareModern` 的职责。
- specular / 反射遮蔽 —— 本路径目前是纯 diffuse 输出。

---

## 2. 现状分析

### 2.1 渲染管线（三段 compute）

`SoftwareModernNoAmbientRenderer::CreateSwapChain` / `Render`（`SoftwareModernNoAmbientRenderer.cpp:24-117`）构成三段 compute + 一次 history copy：

| 顺序 | Pass | Shader | 主要 I/O |
| --- | --- | --- | --- |
| 1 | 着色 | `Core.SwModernNoAmbient.comp.slang` | 读 `RT_MINIGBUFFER`；写 `RT_SINGLE_DIFFUSE` / `RT_OBJEDCTID_0` / `RT_PREV_DEPTHBUFFER` / `RT_MOTIONVECTOR` / `RT_MOTIONMOMENT` / `RT_NORMAL` |
| 2 | 时序累积 | `Process.ReProjectSimple.comp.slang` | 读 `RT_SINGLE_DIFFUSE` + history，写 `RT_ACCUMLATE_DIFFUSE`（push const 16B：TemporalFrames / prevDiffuseIdx / historyValid / taaEnabled） |
| 3 | 合成 | `Process.ComposeSimple.comp.slang` | 读 `RT_ACCUMLATE_DIFFUSE`，描边 + tonemap → `RT_DENOISED` |
| 4 | history copy | `TemporalResolve::CopyToHistory` | `RT_ACCUMLATE_DIFFUSE` → `RT_SINGLE_PREV_DIFFUSE` |

GTAO pass 将插入第 1 与第 2 之间（§4）。

### 2.2 着色：天光项无遮蔽（核心缺口）

`Core.SwModernNoAmbient.comp.slang:135-156`：

```slang
float3 N = normalize(hitVertex.Normal);
float3 albedo = primaryAlbedo.rgb;

// 天光：无遮蔽，直接吃满 IBL diffuse
float3 ambient = float3(0);
if (Camera.HasSky)
{
    float3 irr = Common.SampleIBLDiffuse(Camera.SkyIdx, N, Camera.SkyRotation).rgb * Camera.SkyIntensity;
    ambient = albedo * irr;          // ← 没有任何 occlusion 因子
}

// 太阳：Lambert + CSM 阴影（已有遮蔽，保持不变）
float3 direct = float3(0);
if (Camera.HasSun)
{
    float NoL = max(dot(Camera.SunDirection.xyz, N), 0.0f);
    float viewDist = -mul(Camera.ModelView, float4(hitVertex.Position, 1.0f)).z;
    float shadow = Common.SampleSunShadowCSM(hitVertex.Position, N, viewDist);
    direct = albedo * Camera.SunColor.rgb * (NoL * shadow * M_1_PI);
}

color = float4(ambient + direct, 1.0f);   // ← ambient 与 direct 已合并，无法事后分离
```

**两个要点**：

- `ambient`（天光 = 间接环境光）是**唯一需要被 GTAO 遮蔽**的部分；`direct` 已有 CSM。
- 着色把 `ambient + direct` 合并写进 `RT_SINGLE_DIFFUSE`，**事后无法分离**。要只遮天光，必须在着色阶段把天光项单独输出到一张 target（§5.3）。

### 2.3 可用的 visibility / G-buffer 资源

着色 pass 末尾写出（`Core.SwModernNoAmbient.comp.slang:160-171`）：

- `RT_NORMAL`：**世界空间法线**（`float4(normalize(hitVertex.Normal), 1)`，line 171）。
- `RT_PREV_DEPTHBUFFER`：**NDC 深度** `clipPos.z / clipPos.w`（line 161、169）。
- `RT_MOTIONVECTOR`、`RT_MOTIONMOMENT`、`RT_OBJEDCTID_0`：供时序 / 描边用。

GTAO 所需「深度 + 法线 + 相机矩阵」三要素齐备：法线现成；视空间位置由 NDC 深度 + `Camera.ProjectionInverse` 反算（着色 pass 重建主光线已用同一套矩阵，line 49-52）。UBO 内 `ModelView / Projection / ProjectionInverse / ModelViewInverse / ViewProjection` 均可用。

> 注意：`RT_PREV_DEPTHBUFFER` 存的是 NDC 深度，不是线性视深度。GTAO 内部以视空间 Z 做水平角积分，需先反投影成视空间位置再算（§5.2）。

### 2.4 RT slot 占用（`BindlessTexture.slang:8-53`）

已用：`0-28`（其中 24/25/26/27/28 为 atrous/splat），`50`（`RT_TEMP_USAGE0`），`100-102`（swapchain）。`RT_COUNT = 128`。

**空闲可用**：`29、30、31 …`（直到 49）。本设计新增 target 取 `29` 起（§6.1）。

---

## 3. 总体方案

单级、屏幕空间，完全自洽：

```
                         ┌─────────────────────────────────────────────┐
  visibility buffer ───► │ Pass 1  Core.SwModernNoAmbient.comp          │
  (RT_MINIGBUFFER)       │   - direct(sun+CSM)/emissive → RT_SINGLE_DIFFUSE
                         │   - sky irradiance           → RT_AMBIENT (新)
                         │   - world normal             → RT_NORMAL
                         │   - ndc depth                → RT_PREV_DEPTHBUFFER
                         └───────────────┬─────────────────────────────┘
                                         ▼
                         ┌─────────────────────────────────────────────┐
                         │ Pass 1.5  Core.GTAO.comp (新)                 │ 半分辨率
                         │   GTAO(depth+normal) → ao  (RT_GTAO)          │
                         │   双边上采样到全分辨率                          │
                         │   RT_SINGLE_DIFFUSE += RT_AMBIENT * ao         │ ← 只遮天光
                         └───────────────┬─────────────────────────────┘
                                         ▼
                         Pass 2 ReProjectSimple → Pass 3 ComposeSimple → copy history
```

设计要点：

1. **拆分天光项**：着色把 `ambient` 单独写 `RT_AMBIENT`，`RT_SINGLE_DIFFUSE` 只保留 `direct` + emissive（§5.3）。这样遮蔽 pass 才能精确地只压暗天光，不动太阳 CSM、不压暗自发光。
2. **GTAO 半分辨率 + 时序**：计算集中在该 pass，半分辨率 + 少切片/步数，并把结果 fold 回 diffuse 后交给已有 `ReProjectSimple` 时序累积收敛（§5.4），低成本去噪。
3. **双边上采样**：半分辨率 AO 经 depth/normal 联合双边上采样回全分辨率，避免边缘漏光。

---

## 4. 新增 GTAO Pass 的位置与资源

在着色 pass 与 `ReProjectSimple` 之间插入 `Core.GTAO.comp.slang`：

- 类型：`ZeroBindCustomPushConstantPipeline`（传半分辨率比例 / 帧号 / 半径等 push const）或 `ZeroBindPipeline`（参数走 UBO/cvar），构造方式参照 `SoftwareModernNoAmbientRenderer.cpp:27-32`。
- 分辨率：半分辨率（`RenderExtent / 2`），dispatch `GetSafeDispatchCount(w/2, 8) × GetSafeDispatchCount(h/2, 8)`，参照 `SoftwareModernNoAmbientRenderer.cpp:61-63`。若上采样 + fold 单独成全分辨率子步，则该步用全分辨率 dispatch。
- barrier：着色 pass 已对 `RT_NORMAL` / `RT_PREV_DEPTHBUFFER` 等做 `WRITE→READ` 转换（`SoftwareModernNoAmbientRenderer.cpp:65-76`）。GTAO 读取前确保这些 target 可读；输出 `RT_GTAO`（半分辨率 AO）与回写 `RT_SINGLE_DIFFUSE` 后插入 barrier，供 `ReProjectSimple` 读取。
- timer：加 `SCOPED_GPU_TIMER("gtao pass")`，与现有 pass 一致便于 benchmark。

> 实现可分两个 dispatch：①半分辨率算 AO → `RT_GTAO`；②全分辨率双边上采样 + `RT_SINGLE_DIFFUSE += RT_AMBIENT * ao`。也可合一（半分辨率算、全分辨率回写时上采样）。建议先两步打通，再视性能合并。

---

## 5. 详细设计

### 5.1 GTAO Pass 位置

见 §4。下文为算法与整合细节。

### 5.2 GTAO 算法（屏幕空间）

采用 horizon-based / GTAO 思路（参考 Activision GTAO 2016、XeGTAO 工程实现，**只取算法、不拷贝带许可证代码**，以引擎现有 slang 风格自写）：

1. **重建视空间位置**：由像素 uv + `RT_PREV_DEPTHBUFFER`（NDC 深度）经 `Camera.ProjectionInverse` 反投影到视空间。`RT_NORMAL` 是世界空间法线，用 `Camera.ModelView` 变换到视空间统一坐标系。
2. **水平角积分**：每像素取 `nSlices`（建议 2–3）个方向切片，每切片沿屏幕步进 `nSteps`（建议 4–6）采样深度，求左右地平线角，按 GTAO 余弦加权积分得到可见度 `ao ∈ [0,1]`。
3. **半径与衰减**：世界空间有效半径建议 0.5–2m，随视距投影到屏幕像素半径；超半径样本按距离 falloff 忽略，避免远景漏光/过暗。
4. **厚度启发**：加 thickness heuristic（对深度差过大的样本做 falloff），缓解 haloing。
5. **bent normal（可选，Phase 2）**：GTAO 可顺带输出 bent normal，让天光 irradiance 沿可见方向偏移采样，方向性更准。v1 仅输出标量 AO。
6. **输出**：半分辨率 `ao` 写 `RT_GTAO`。

「高效」的关键：半分辨率 + 少切片/步数 + 时序累积；不追求单帧 ground-truth，靠多帧 + 抖动收敛。

### 5.3 着色端拆分天光项

修改 `Core.SwModernNoAmbient.comp.slang`（line 135-167）：

- 计算 `ambient`（天光 irradiance × albedo）后，**单独写入 `RT_AMBIENT`**（新 target），并将 `RT_SINGLE_DIFFUSE` 改为只写 `direct`（外加 emissive / DiffuseLight 等非天光项）。
- emissive 自发光（`MaterialDiffuseLight`，line 129-132）与 sky miss（line 59-69）不参与遮蔽，照常进 `RT_SINGLE_DIFFUSE`，对应像素 `RT_AMBIENT` 写 0。
- 调试覆盖（`DebugDraw_ShadowCascadeCoverage`，line 114-119）路径保持原样（直接走 `RT_SINGLE_DIFFUSE`，`RT_AMBIENT` 置 0）。

GTAO pass 末尾合并：`RT_SINGLE_DIFFUSE[p] += RT_AMBIENT[p] * ao`。

> 备选（v0 最省改动，质量略差）：不拆分，直接 `RT_SINGLE_DIFFUSE *= ao`。缺点是太阳直接光被 GTAO 二次压暗（与 CSM 叠加 over-darken），自发光被错误压暗。**仅可作为最初打通管线的临时验证，不作最终形态。**

### 5.4 时序去噪与上采样

GTAO 半分辨率 + 少样本必然有噪。收敛途径：

- **v1（推荐，最省）**：GTAO pass 把 `RT_AMBIENT * ao` fold 回 `RT_SINGLE_DIFFUSE`，**复用现有 `ReProjectSimple` 的 TAA**（`Process.ReProjectSimple.comp.slang`）让噪声随多帧收敛；每帧抖动 GTAO 切片角 / 步进相位。优点：零额外 history 资源。缺点：AO 噪声混入颜色 history，运动边缘可能少量拖影。
- **v2（可选增强，Phase 2）**：为 `ao` 开独立 history（如 `RT_GTAO_PREV`），用 `RT_MOTIONVECTOR` 重投影 + 深度/objectId 校验做专门 AO 时序滤波，再 fold。质量更好，成本更高。

**上采样**：半分辨率 → 全分辨率统一用 depth/normal 联合双边（joint bilateral），避免边缘漏光（可参考 `Process.DenoiseJBF.comp.slang` 的双边权重思路）。

---

## 6. 数据结构与接口改动清单

### 6.1 RT slot（`assets/shaders/common/BindlessTexture.slang`）

新增（取空闲段 29 起）：

```slang
public static const int RT_AMBIENT = 29; // 着色拆分出的天光项 (rgb, 全分辨率 RGBA16F)
public static const int RT_GTAO    = 30; // GTAO 结果 (R, 半分辨率 R8/R16F)
// 可选 v2: public static const int RT_GTAO_PREV = 31; // AO 独立 history
```

- 需在 C++ 侧 storage image 注册/创建处同步分配这些 bindless target（参考现有 RT_* 创建路径）。
- 该枚举在 `#ifndef __cplusplus` 之外同时被 C++ 与 slang 引用，两侧编译都需通过。

### 6.2 着色 shader

`Core.SwModernNoAmbient.comp.slang`：天光项拆分输出（§5.3）。

### 6.3 新 shader

`assets/shaders/Core.GTAO.comp.slang`（GTAO + 双边上采样 + 合并回写）。

### 6.4 渲染器编排

`SoftwareModernNoAmbientRenderer.{hpp,cpp}`：新增 `gtaoPipeline_`（及可选上采样/合并 pipeline），在着色与 `ReProjectSimple` 之间插入 dispatch + barrier + timer。

### 6.5 开关（cvar / UserSettings）

新增 `GTAOEnable`（总开关）、GTAO 强度/半径参数、调试视图枚举（输出 `ao` / `RT_AMBIENT` / bent normal）。参考现有 `UserSettings` 取用方式（`SoftwareModernNoAmbientRenderer.cpp:81-83` 的 `settings.TAA / settings.DLSS`）与 `EngineCVars`。

> 本期**不**改动 `FRendererRequirements`、`Scene.cpp` arena、GI 烘焙门控——这些只与体素大尺度遮蔽相关，属 §8 未来工作。

---

## 7. 开发计划（分阶段）

> 验证遵循 AGENTS.md「定向构建」：改 Engine 层 / shader 用 `./gnb build gkNextRenderer gkNextUnitTests`；shader 改动需确保 `.slang` 重新编译为 `.spv`。

### Phase 1 — GTAO 打通（核心交付）

- 着色拆分天光项 → `RT_AMBIENT`（§5.3）；新增 `RT_AMBIENT` / `RT_GTAO` target（§6.1）。
- 新建 `Core.GTAO.comp.slang`：半分辨率 GTAO（2 切片 × 4 步起步），世界→视空间重建（§5.2）；输出 `RT_GTAO`。
- 双边上采样 + fold `RT_AMBIENT * ao` 回 `RT_SINGLE_DIFFUSE`。
- 渲染器插入新 pass + barrier + timer；复用 `ReProjectSimple` 收敛（每帧抖切片角）。
- 验证：调试视图看 `ao`；凹角/墙根/接触阴影出现；`GTAOEnable` A/B 对比；确认太阳 CSM 与自发光不被二次压暗；`gnb benchmark` 看帧时间增量。
- 交付：天光接触级遮蔽可用且不破坏既有直接光。

### Phase 2 — 质量打磨

- 双边联合上采样（depth+normal）调参，修边缘漏光 / haloing。
- GTAO thickness heuristic、半径 falloff 标定；强度参数 cvar 化。
- 可选 bent normal：天光 irradiance 沿可见方向采样（方向性更准）。
- 可选 v2 独立 AO history（§5.4），减运动拖影。
- 验证：多场景（室内 living_room、playground 户外、debug_draw）观感核对；运动序列看闪烁/拖影回归。

### Phase 3 — 开关、调试与文档

- 补齐 cvar / UserSettings 开关与调试视图枚举（§6.5）。
- 更新 `docs/README.md` 索引状态；本设计文档转「已完成 / 现行」。

---

## 8. 未来工作：体素大尺度天光遮蔽（本期不做，仅留方向）

GTAO 是屏幕空间方法，天然丢失**屏幕外遮挡**与**远距离大体积遮挡**（室内整体压暗、天井/峡谷层次）。这部分需更大尺度的遮蔽源，初版设想是利用 CPU 体素数据的 sky-visibility。本期明确**不排期、不写实现**，仅记录方向与两个已知前提，供将来评估：

- **数据来源现状**：CPU 体素化（`ProbeBaker.cpp` `VoxelizeCube`）生成的是**方向距离场**（`VoxelData.distanceToSolid_*`，`BasicTypes.slang:242-248`），并**没有** skyVisibility 字段；skyVisibility 目前只存在于 GPU 烘焙的 `AmbientCube`（`BasicTypes.slang:233-234`），NoAmbient 不跑该烘焙。
- **前提一（数据可用性）**：体素 arena 仅在 `RegisteredRendererRequirements().requestAmbientCube` 为真时分配/填充（`Scene.cpp:158-198`）；**独立运行 NoAmbient 时体素数据不存在**。要在独立运行下使用，需要某种门控解耦（如 `requestVoxel`）——本期不引入。
- **前提二（计算方式）**：将来若做，可选 (A) 运行时对体素距离场做半球 DDA（复用 `RayTracers.slang` 的 `TraceOcclusionDDA`，line 58-151）——本期明确不用；或 (B) 在 CPU 体素化阶段预烘焙 per-voxel sky-vis（向上半球射线统计），运行时三线性采样。两者都依赖前提一先解决。
- **可降级性**：将来接入时应做成「体素数据存在才生效、否则优雅降级为纯 GTAO」，与本期 GTAO 正交叠加（如 `occ = min(ao, skyVis)`）。

---

## 9. 性能预算与风险

**预算（1080p，参考量级，需 `gnb benchmark` 实测校准）**：

- GTAO 半分辨率、2 切片 × 4 步：目标 < 0.5ms。
- 双边上采样 + fold：< 0.2ms。
- 合计目标 < 0.7ms 增量；超预算则降切片/步数或关 bent normal。

**风险与对策**：

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| GTAO 屏幕空间漏光 / haloing | 边缘暗带、错误亮边 | thickness heuristic、半径 falloff、双边上采样 |
| 半分辨率 + 少样本噪声 | 闪烁、运动拖影 | 时序抖动 + `ReProjectSimple` 收敛；必要时上 v2 独立 AO history |
| 天光 + 太阳被双重压暗 | 整体过暗 | §5.3 拆分天光项，遮蔽**只乘 ambient**，不动 direct/emissive |
| NDC 深度反投影精度 | 远景 AO 失真 | 用现成相机逆矩阵反算视空间；远景本就超 GTAO 半径，靠 falloff 收敛到 1.0 |
| 屏幕外 / 大尺度遮挡缺失 | 室内整体偏亮 | 本期已知局限，属 §8 未来工作；GTAO 半径内细节优先 |

---

## 10. 验证方案

- **调试视图**：cvar 切换输出 `ao` / `RT_AMBIENT` / bent normal，单独肉眼核对每一路。
- **A/B 对比**：`GTAOEnable` 开关前后截图对比（凹角、墙根、桌底、室内）。
- **回归场景**：`docs/gallery` 既有场景（living_room 室内、playground 户外、debug_draw）跑一遍，确认无漏光/过暗/闪烁，太阳 CSM 与自发光不受影响。
- **性能**：`gnb benchmark` 量 GTAO pass 增量（新增 `SCOPED_GPU_TIMER`）。
- **单测/编译**：`./gnb build gkNextRenderer gkNextUnitTests` 通过；`.slang → .spv` 重新编译无误。

---

## 11. 关键文件索引

| 用途 | 文件 |
| --- | --- |
| 目标渲染器编排 | `src/Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.{hpp,cpp}` |
| 着色（天光拆分） | `assets/shaders/Core.SwModernNoAmbient.comp.slang` |
| 新 GTAO pass | `assets/shaders/Core.GTAO.comp.slang`（新增） |
| 时序 / 合成 | `assets/shaders/Process.ReProjectSimple.comp.slang`、`Process.ComposeSimple.comp.slang` |
| 双边滤波参考 | `assets/shaders/Process.DenoiseJBF.comp.slang` |
| RT slot 枚举 | `assets/shaders/common/BindlessTexture.slang` |

---

## 12. 给后续开发 agent 的提示

- **本期只做 GTAO**，不要触碰 `FRendererRequirements` / `Scene.cpp` arena / GI 烘焙门控 / 体素数据——那些属 §8 未来工作。
- 拆分天光项是「只遮天光、不动太阳 CSM、不压暗自发光」的关键；不要图省事直接 `RT_SINGLE_DIFFUSE *= ao`。
- GTAO 实现只取算法思路，**不要拷贝带许可证的第三方源码**；以引擎现有 slang 风格自写。
- 改 `BindlessTexture.slang` 枚举会同时影响 C++ 与 slang 两侧，两边都要编过。
- 新 pass 记得加 `SCOPED_GPU_TIMER` 并在 benchmark 里核预算。
- 注意 `RT_PREV_DEPTHBUFFER` 是 NDC 深度，GTAO 内部要先反投影成视空间再做水平角积分。
