---
title: "高斯溅射（SOG 加载 + GS 渲染模式）设计与开发计划"
category: design
status: 已完成
owner: engine
created: 2026-06-18
last_updated: 2026-06-18
---

# 高斯溅射（SOG 加载 + GS 渲染模式）设计与开发计划

> 状态：**✅ 已完成**。SOG v2 加载、完整 SH、硬件 billboard、GPU 近似分桶排序、场景深度遮挡、整资产选择、独立累积合成、组件反射和回归配置均已落地。
>
> 关键决策（已与 owner 对齐）：
> 1. **加载**：支持 PlayCanvas **SOG v2** 格式，**打包 `.sog`（ZIP）与非打包 `meta.json` + `.webp` 两种变体都要**。
> 2. **光栅化**：**方案 A —— 硬件实例化 billboard + 固定功能 alpha 混合**（不做 tile compute 光栅器）。
> 3. **排序**：**GPU 近似分桶排序**（按视深，逐帧）。
> 4. **球谐**：**完整 SH（1–3 阶）**，视角相关颜色。
> 5. **数据驻留**：**加载时解码为紧凑 GPU buffer（SoA）**，shader 直接采样。
> 6. **与 mesh 共渲染**：高斯**不写 visibility buffer**，但**采样场景深度做正确遮挡**；合成发生在 logic renderer 产出 HDR 之后、resolve/DLSS 之前。
>
> 参考实现：[playcanvas/splat-transform](https://github.com/playcanvas/splat-transform)（SOG 读写参考）、[PlayCanvas SOG 格式规范](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/)、[PlayCanvas Splat Rendering Architecture](https://developer.playcanvas.com/user-manual/gaussian-splatting/rendering-architecture/)、原论文 *3D Gaussian Splatting for Real-Time Radiance Field Rendering*（Kerbl et al., SIGGRAPH 2023）。
> 本仓库可对照参考：`src/Modules/ScadLoader/`、`src/Modules/LDrawLoader/`（模块化 loader 范式）、`docs/designs/scad-loader-design.md`、`docs/guides/soft-mesh-shader-gpu-driven-submit.md`（GPU-Driven 提交）。

## 当前实现状态（2026-06-18）

- 已完成：打包 `.sog` / 非打包 `meta.json`、WebP 反量化、协方差、SH 1–3 阶 palette、GPU buffer 上传。
- 已完成：4096 桶 GPU counting sort（直方图 → 前缀和 → scatter）、GPU 视锥裁剪、`VkDrawIndirect` 可见实例提交。
- 已完成：EWA billboard、预乘 alpha、场景深度测试、`Grape.sog` 视觉验证。
- 已完成：整资产 AABB、射线选择、选中框、相机聚焦和绕物旋转。
- 已完成：`GaussianSplatComponent` 反射/脚本属性（显隐、拾取、透明度），节点世界变换和多 SOG 模型全局 GPU 排序。
- 已完成：独立 `RT_SPLAT_ACCUM` + compute over compose，`RT_DENOISED` 不再承担 color attachment。
- 已完成：`r.splat.bucketCount / maxCount / sigma` 性能 CVar、`show.gaussianSplats` 开关及 visual test 场景配置。
- 实现取舍：GPU 数据采用紧凑 AoS + 原生 SOG SH palette；排序阶段输出有序索引而非缓存完整 `VisibleSplat`。该变体减少中间显存和写带宽，billboard VS 现场计算投影。

---

## 1. 目标与范围

### 1.1 目标
让引擎能像加载 `.gltf` / `.ldr` / `.scad` 一样，直接加载高斯溅射资产并实时渲染：

```
./gnb run gkNextRenderer --load-scene "assets/splats/garden.sog"
./gnb run gkNextRenderer --load-scene "assets/splats/garden/meta.json"   # 非打包目录
```

并满足：

- 新增 **`SOGLoader`**：解析 SOG v2（含打包/非打包），反量化出每个高斯的属性（位置、协方差、SH、不透明度）。
- 新增一种**专用渲染模式 / pass（GaussianSplatPass）**：把高斯数据按 3DGS 方式光栅化合成进当前画面。
- **与普通 mesh 共渲染**：在任意 logic renderer（SoftwareModern / PathTracing / …）下都能叠加显示，并与不透明场景产生**正确遮挡**。
- 完整 **SH（1–3 阶）** 视角相关着色。

### 1.2 非目标（本期不做）
- 高斯**写入 visibility buffer**（owner 明确不要求；高斯不参与 GI / 路径追踪求交，仅做前向合成）。
- 高斯参与硬件/软件光线求交、阴影投射、AmbientCube 烘焙。
- tile-based compute 光栅器（性能终局方案，本期不做，但接口需为其预留，见 §6.7）。
- 高斯训练 / 编辑 / 导出（仅消费、渲染）。
- LOD 流式加载（SOG 支持，但本期不做；数据结构上预留）。

---

## 2. 背景：3DGS 渲染原理速览

一个高斯溅射资产是一组各向异性 3D 高斯（"splat"），每个高斯包含：

- **位置 μ**（3D 均值）。
- **协方差 Σ = R·S·Sᵀ·Rᵀ**，由旋转四元数 `q` 与各轴缩放 `s=(sx,sy,sz)` 构成（决定椭球形状与朝向）。
- **不透明度 α∈[0,1]**。
- **颜色（球谐）**：DC 项（SH0，视角无关基色）+ 高阶 AC 项（SH1–3，视角相关），按视线方向 `dir` 评估出 RGB。

实时渲染（前向 alpha 合成，方案 A）流程：

1. **投影 + 裁剪**：把 μ 变换到视/裁剪空间；视锥/背面剔除；把 3D 协方差经 EWA 投影成屏幕空间 **2D conic**（2×2 协方差的逆），并算出椭圆包围盒尺寸（约 3σ）。
2. **着色**：用视线方向评估 SH → 每高斯的 RGB；结合 α。
3. **排序**：按视深对可见高斯排序（本方案用 **GPU 近似分桶**），保证 alpha 混合顺序（由远到近）。
4. **光栅化**：每个可见高斯画一个**屏幕对齐四边形（billboard）**，片元里用 conic 评估 2D 高斯权重 `w=exp(-0.5·dᵀΣ⁻¹d)`，输出 **预乘颜色 (rgb·α·w, α·w)**，硬件固定功能混合累加。
5. **遮挡**：片元采样**场景深度**，位于不透明面之后的贡献被裁剪（见 §6.6）。

> EWA splatting / 2D 协方差投影与 conic 评估的数学，参考原论文与 PlayCanvas/gsplat 的开源 shader；§6.5 给出落地形式。

---

## 3. SOG v2 格式规范摘要

> 权威规范见 [PlayCanvas SOG 文档](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/)。下面是实现 SOGLoader 必需的要点。

### 3.1 文件集
一个 SOG 数据集 = 一个 `meta.json` + 若干 8-bit WebP 图（**默认无损 WebP**，逐像素对应一个高斯，row-major、左上为原点）：

| 文件 | 用途 | 通道 |
| --- | --- | --- |
| `meta.json` | 元数据 + 文件名引用 | — |
| `means_l.webp` | 位置低 8 位 | R,G,B |
| `means_u.webp` | 位置高 8 位 | R,G,B |
| `scales.webp` | 各轴尺寸（codebook 索引） | R,G,B |
| `quats.webp` | 朝向（压缩四元数） | R,G,B,A |
| `sh0.webp` | 基色 DC + 不透明度 | R,G,B,A |
| `shN_centroids.webp` | SH 调色板（可选，高阶 SH） | R,G,B |
| `shN_labels.webp` | 每高斯的调色板索引（可选） | R,G |

像素 `(x,y)` 在所有属性图（`shN_centroids` 除外）中对应同一个高斯：`i = x + y*W`，`count ≤ W*H`，尾像素忽略。**坐标系右手系：x 右、y 上、z 后（−z 朝前）**。

### 3.2 `meta.json` 关键字段
```
version: 2
count: number                       // 高斯数量
antialias: boolean
means: { mins:[3], maxs:[3], files:[means_l, means_u] }   // log 域范围
scales: { codebook:[256 float], files:[scales] }          // 线性域
quats:  { files:[quats] }
sh0:    { codebook:[256 float], files:[sh0] }             // gamma 域 DC
shN?:   { count, bands(1..3), codebook:[256 float], files:[centroids, labels] }  // gamma 域 AC
```

### 3.3 反量化（SOGLoader 必须精确实现）
- **位置**（16-bit/轴，跨两图）：
  `q = (u<<8)|l`（各通道）→ `n = lerp(mins, maxs, q/65535)` → **解 log**：`p = sign(n)·(exp(|n|)−1)`。
- **朝向**（quats，26-bit smallest-three）：R/G/B 存三个分量，反量化 `(c/255−0.5)·2/√2`；A∈{252..255}，`mode=A−252` 指示被省略（最大）的分量；`d=sqrt(max(0,1−(a²+b²+c²)))`，按 mode 回填得到单位四元数。
- **缩放**：`s = scales.codebook[index]`（每通道 0..255 索引；线性域，场景单位）。
- **DC 基色 + 不透明度**（sh0）：`SH_C0=0.2820947918`；`rgb = 0.5 + codebook[index]·SH_C0`（gamma 域）；`a = sh0.a/255`。
- **高阶 SH**（可选 palette）：`label = labels.r + (labels.g<<8)`；每条目按 `bands` 决定系数数 `coeffs=[3,8,15][bands-1]`；centroids 每行 64 条目，`u=(label%64)*coeffs+c, v=floor(label/64)`，像素 R/G/B 是 `shN.codebook` 索引，分别对应三个颜色通道的该 SH 系数。

### 3.4 打包变体
`.sog` = 上述文件的 **ZIP**（文件位于归档根）。Reader 必须解压后按 `meta.json` 解析。→ **需要一个 ZIP 读取库**（见 §8 依赖）。

---

## 4. 引擎现状与集成点分析

> 以下行号基于当前仓库；实现时以实际代码为准。

### 4.1 资产加载链路
- **Loader 注册表** `Assets::FLoaderRegistry`（`src/Engine/Assets/Loaders/LoaderRegistry.hpp/.cpp`）：模块在启动时 `RegisterSceneLoader({".ext"}, fn)` 注册按扩展名分发的场景 loader；`FSceneLoaderFn` 产出 `nodes / models / materials / lights / tracks / skeletons`。
- **分发** `SceneList::LoadScene`（`src/Engine/Runtime/Scene/SceneList.cpp:208`）：glTF 内建；其余按扩展名查注册表（`:235`）。
- **模块注册范式**：`src/Modules/ScadLoader/ScadModule.cpp:10`（`.scad`）、`src/Modules/LDrawLoader/LDrawModule.cpp:12`（`.ldr/.mpd`）。
- **问题**：`FSceneLoaderFn` 只产 mesh 型 `Model`，**高斯不是 mesh**，需新增并行产物通道（见 §6.2）。

### 4.2 场景 / 节点 / 组件
- `Assets::Scene`（`src/Engine/Assets/Core/Scene.hpp`）持有 `models_ / nodes_ / nodeProxies`，`Reload(...)` 负责 GPU 上传。
- `Assets::Node`（`src/Engine/Assets/Core/Node.h`）支持任意 `Component`（`AddComponent/GetComponentPtr`）。
- `Runtime::RenderComponent`（`src/Engine/Runtime/Components/RenderComponent.h`）是 mesh 渲染组件范式（`REFLECT_COMPONENT` 反射、`modelId_` 引用 `Model`）。→ 高斯将新增并行的 `GaussianSplatComponent` + `SplatModel`。

### 4.3 渲染管线（关键：合成注入点）
引擎是 **compute-deferred + Visibility Buffer + 全 Bindless + GPU-Driven 单 draw** 架构：

- **Bindless 渲染目标槽位**（C++/Slang 共享）`assets/shaders/common/BindlessTexture.slang:8`：`RT_SINGLE_DIFFUSE / RT_ALBEDO / RT_NORMAL / RT_MINIGBUFFER / RT_DENOISED(=9) / RT_PREV_DEPTHBUFFER(=10) / …`，`RT_COUNT=128`，含 `RT_TEMP_USAGE0=50`、`RT_SWAPCHAIN*`。**可在此新增 splat 用槽位**。
- **PreRender 顺序**（`src/Engine/Rendering/VulkanBaseRenderer.cpp:982`）：AS 更新 → skinning → GPU cull → clear → **DispatchVisibilityPass** → SunShadow。
- **Visibility Pass**（`src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp:323`）：用 GPU-Driven indirect draw 光栅出 mini-G-buffer（深度 + objectId 等），随后 copy 到 storage image `RT_MINIGBUFFER`（`:360-383`）。**这是高斯做遮挡要读的"场景深度"来源**。
- **Render / Resolve**（`src/Engine/Rendering/VulkanBaseRenderer.cpp:1281`）：当前 logic renderer `Render()` 产出 HDR 到 `RT_DENOISED`（`:1325`）；随后 resolve：DLSS 评估或把 `RT_DENOISED` blit 到 swapchain（`:1361`）；可选 wireframe；barrier to present。
- **SoftwareModern**（`src/Engine/Rendering/SoftwareModern/SoftwareModernRenderer.cpp`）：compute deferred shading → reproject → compose，全部走 `RT_*` storage image。
- **图形管线工具** `Vulkan::GraphicsPipelineBuilder`（`src/Engine/Vulkan/GraphicsPipelineBuilder.hpp`）：方案 A 的实例化 billboard 管线在此构建（混合态 / 深度态 / 顶点输入）。

**注入点结论**：高斯合成 pass 应在 **logic renderer 产出 `RT_DENOISED` 之后、resolve(DLSS/blit) 之前**执行——这样高斯：① 处于线性 HDR 空间，后续被 DLSS/tonemap 正常接管；② 在 RenderExtent 分辨率合成（DLSS 前，像素更少）；③ 能读 `RT_MINIGBUFFER` 深度做遮挡。

---

## 5. 总体架构

```
                       ┌─────────────── 加载期（CPU） ───────────────┐
 .sog / meta.json ──▶  SOGLoader  ──▶  反量化 → CPU splat arrays  ──▶  SplatModel（解码上传 GPU SoA buffer）
   (zip / 目录)        (libwebp +                                    (pos, cov3D, opacity, sh0, shN palette)
                        nlohmann-json +                                      │
                        miniz)                                               ▼
                                                          Scene 注册 splatModels_  +  Node 挂 GaussianSplatComponent

                       ┌─────────────── 每帧（GPU） ────────────────┐
 GaussianSplatPass:  ① project+cull+SH 评估 (compute) → 紧凑可见记录 + drawCount
                     ② GPU 近似分桶排序 (compute) → 按视深的有序索引
                     ③ indirect 实例化 billboard 光栅 (graphics, 硬件 alpha 混合)
                        · VS 展开四边形（conic 包围盒），读排序记录
                        · FS 评估 2D 高斯权重，采样 RT_MINIGBUFFER 深度做遮挡，输出预乘颜色
                        → 写入 RT_SPLAT_ACCUM（RGBA16F，预乘）
                     ④ compose (compute) → 把 RT_SPLAT_ACCUM "over" 合成进 RT_DENOISED
                       └──────── 在 logic renderer 之后、resolve 之前由 VulkanBaseRenderer 驱动 ────────┘
```

设计要点：
- **排序产物与光栅器解耦**：①②产出"有序可见记录 buffer + 间接 draw 参数"；③只消费它。后续若上 tile compute 光栅器（非本期），可替换③而复用①②。
- **独立累积目标 `RT_SPLAT_ACCUM`**：避免给 `RT_DENOISED` 增加 color-attachment 用法耦合；高斯先在自己的 RGBA16F 目标上做硬件混合，再用一个轻量 compute "over" 合成回 `RT_DENOISED`。
- **遮挡在片元做**：FS 采样场景深度逐片元裁剪 → 支持轮廓处的部分遮挡，且**完全不写** visibility/depth buffer。

---

## 6. 详细设计

### 6.1 SOGLoader（新模块 `src/Modules/SplatLoader/`）
镜像 ScadLoader 结构。建议文件：

- `SplatModule.{hpp,cpp}`：`Modules::Splat::Register()` 向注册表登记 `.sog` 与非打包入口（见 §6.2 分发）。
- `FSogLoader.{h,cpp}`：主流程。输入文件路径 →
  1. **定位文件集**：`.sog` → 用 ZIP 读取器（miniz）在内存解出各成员；非打包 → 以 `meta.json` 所在目录解析其 `files[]`。
  2. **解析 meta.json**（`nlohmann::json`）：版本校验（必须 `version==2`，否则 warn 并尽力解析）、读 `count / means.mins/maxs / *.codebook / shN.bands` 等。
  3. **解码 WebP**（`libwebp` `WebPDecodeRGBA`）：每图得 8-bit RGBA 像素。校验各图尺寸一致、`count ≤ W*H`。
  4. **反量化**（§3.3）→ 填充 CPU SoA：`positions[count]`、由 `quat+scale` 现场算 **3D 协方差上三角 6 float**、`opacity[count]`、`sh0Rgb[count]`、高阶 SH（解 palette 后**展开为每高斯系数**或保留 palette+label，见 §6.3 取舍）。
  5. **坐标系转换**：SOG 为 RH y-up −z forward；按引擎相机/裁剪约定做一次性变换（位置 + 协方差旋转部分；§7）。
- `FSogTypes.h`：`FSplatCpuData`（SoA 数组）、`FSogMeta` 结构。
- `FSplatQuant.{h,cpp}`：纯函数反量化 + 单测目标（位置 log、smallest-three 四元数、codebook、SH_C0），**便于离线单测**。

> **降维实现顺序（bring-up，不缩范围）**：可先只解 `sh0`（DC + α）跑通"有像素"，再接 `shN` 全 SH。见 §11 阶段划分。

### 6.2 资产产物通道与场景集成（最小侵入）
高斯不是 mesh，方案：

- **注册表新增并行入口**：`FLoaderRegistry::RegisterSplatLoader({".sog"}, FSplatLoaderFn)` + `FindSplatLoader(ext)`。`FSplatLoaderFn` 签名产出 `std::vector<SplatModel>& splats` 与 `std::vector<std::shared_ptr<Node>>& nodes`（节点上挂 `GaussianSplatComponent`，引用 splat 索引）。
- **非打包 `meta.json` 分发**：扩展名 `.json` 会与其他 json 冲突，故在 `SceneList::LoadScene` 按 **basename==`meta.json`** 或路径以 `meta.json` 结尾时显式走 splat loader；`.sog` 走扩展名匹配。`SceneList` 扫描资产目录时把 `assets/splats/**/*.sog` 与 `**/meta.json` 纳入场景列表分组。
- **Scene 持有 splat 集合**：`Scene` 增 `std::vector<SplatModel> splatModels_` 与上传逻辑（在 `Reload`/`OnPostLoadScene` 时把 CPU SoA 上传为 GPU buffer）。提供 `SplatModels()`、`HasSplats()`。

> 备选（更省事但更侵入）：直接给现有 `FSceneLoaderFn` 加一个 `std::vector<SplatModel>&` 出参——需改所有 loader 签名 + 分发，**不推荐**。

### 6.3 GPU 数据布局（SoA，解码后驻留）
`Assets::SplatModel`（建议 `src/Engine/Assets/GPU/SplatModel.{hpp,cpp}`）持有 device-local SSBO，全 bindless 可寻址：

- `positions`：`float3`（或 `float4` 对齐）/高斯。
- `covariance`：3D 协方差上三角 **6×float16/float**（加载时由 quat+scale 预计算，省去运行时构造）。
- `colorOpacity`：`sh0Rgb (gamma→linear 由 shader 决定) + α`，`float4` 或 `rgba16`。
- `shCoeffs`：高阶 SH。**取舍**：
  - (a) **每高斯展开**：`count × coeffs × 3` half，采样最快，显存最大（3 阶≈45 half/点）。
  - (b) **保留 palette**：`shN_centroids`(调色板) + 每点 `label`（u16）。显存小很多（label 1×u32/点），采样多一次间接。**推荐 (b)**：契合 SOG 原生、显存可控，全 SH 下尤其划算；shader 用 label 取 palette。
- `count`、AABB（用于裁剪/排序范围）、`shBands`。

> 显存预算示例（百万高斯，方案 (b)）：pos 12B + cov 12B(half6) + colorα 8B + label 4B ≈ **36B/点 ≈ 36MB/百万点** + 共享 palette（≤64k×45 half）。方案 (a) 约 +90MB/百万点。

### 6.4 渲染模式 / Pass：`GaussianSplatPass`
- 放 `src/Engine/Rendering/GaussianSplat/GaussianSplatPass.{hpp,cpp}`，由 `VulkanBaseRenderer` 持有（类似 `Shadow::ShadowMapPass` / overlay_ 成员），**不新增 `ERendererType`**——它是跨 logic renderer 的叠加 pass，在 §4.3 注入点统一驱动，从而满足"和正常 mesh 共渲染"（无论当前是 SoftwareModern 还是 PathTracing）。
- 由 `VulkanBaseRenderer::Render()` 在 logic renderer 之后、resolve 之前调用 `splatPass_->Execute(cmd, imageIndex)`；当 `!scene.HasSplats()` 时整体跳过（零成本）。
- 提供 `ShowFlags` 开关（`src/Engine/Runtime/Config/ShowFlags.hpp` 加 `ShowSplats`）。

Pass 内部子步骤（GPU 资源）：
1. **per-splat 记录 buffer**（`VisibleSplat`：clip 后屏幕 xy、conic abc、深度 key、预乘前的 rgb、α）。
2. **drawCount / indirect 参数 buffer**（`VkDrawIndirectCommand`，instanceCount 由 cull 写入）。
3. **排序键值 + 有序索引 buffer**。
4. `RT_SPLAT_ACCUM`（新 bindless 槽，RGBA16F，COLOR_ATTACHMENT|STORAGE）。

### 6.5 投影 + 裁剪 + SH 评估（compute，shader：`Splat.Project.comp.slang`）
对每个高斯：
- 取相机 UBO（`Assets::UniformBufferObject`，含 view/proj、相机位置、RenderExtent）。
- 视空间深度 `z`；视锥剔除（含 3σ 半径外扩）+ 小屏幕尺寸剔除。
- **2D conic**：`Σ' = J·W·Σ·Wᵀ·Jᵀ`（W=view 旋转，J=投影雅可比），取左上 2×2 + 抗锯齿正则（`+0.3` 对角，匹配 `meta.antialias`），求逆得 conic `(a,b,c)`；包围盒半径由 conic 特征值 ~3σ。
- **SH 评估**：视线 `dir = normalize(μ_world − camPos)`，按 `shBands` 取 palette（方案 6.3b）评估 AC，叠加 DC → linear RGB（注意 SOG 的 DC 是 `0.5 + c·SH_C0` 的 gamma 域基色，按 PlayCanvas 约定处理；AC 加到该基色上后再 clamp）。
- 用 `atomicAdd` 写入紧凑可见记录 + instanceCount。

### 6.6 排序（GPU 近似分桶，shader：`Splat.SortBucket.comp.slang`）
- 由 cull 阶段得到可见集深度范围 `[zmin, zmax]`（或用场景 AABB 投影估计）。
- 将深度量化到 `N` 个桶（如 256/1024/4096，作为 CVar 可调）；**counting sort**：直方图 → 前缀和（exclusive scan）→ scatter 索引，得**由远到近**的有序索引 buffer。
- 桶内不再细排（近似）；桶数足够时混合误差可接受（Web 端常用做法）。可选：桶内对小集合做局部冒泡/双调细化作为质量旋钮。
- 输出供 indirect 实例化按序读取。

### 6.7 光栅化（graphics，方案 A，shader：`Splat.Billboard.vert/frag.slang`）
- **管线**（`GraphicsPipelineBuilder`）：无顶点缓冲；`gl_InstanceIndex` → 取排序后记录；VS 用内置 4 顶点（triangle-strip / 两三角）按 conic 包围盒在屏幕空间展开 billboard 角点；输出 clip 位置（z 写成高斯中心深度或常数，**深度写关闭**）、conic、颜色、α、局部椭圆坐标。
- **混合态**：预乘 alpha——`srcColor=ONE, dstColor=ONE_MINUS_SRC_ALPHA, srcAlpha=ONE, dstAlpha=ONE_MINUS_SRC_ALPHA`；按排序由远到近绘制（一次 `vkCmdDrawIndirect`，instanceCount=可见数）。
- **深度态**：`depthTestEnable=false, depthWriteEnable=false`（遮挡改在 FS 手动做，§6.6 注入点不挂场景深度附件，避免 framebuffer 兼容约束）。
- **FS**：`power = -0.5·(a·dx² + 2b·dx·dy + c·dy²)`；`w = exp(power)`，`power>0` 或 `w<1/255` 丢弃；`outAlpha = α·w`；**遮挡**：用 `gl_FragCoord` 采样 `RT_MINIGBUFFER` 重建场景线性深度 `sceneZ`，若高斯片元深度 `> sceneZ`（被不透明面遮挡）则丢弃（或按 soft 阈值衰减）；输出 `(rgb·outAlpha, outAlpha)` 预乘。
- 目标：`RT_SPLAT_ACCUM`（先 clear 为 0）。

> billboard 展开/包围盒/conic FS 评估可直接对照 gsplat / PlayCanvas 的 WebGPU 顶点&片元 shader 落地，再翻成 Slang。

### 6.8 合成（compute，shader：`Splat.Compose.comp.slang`）
- 读 `RT_SPLAT_ACCUM`（预乘）与 `RT_DENOISED`，做 "over"：`dst.rgb = src.rgb + dst.rgb·(1−src.a)`，写回 `RT_DENOISED`。
- 仅 RenderExtent 区域；之后交回引擎原 resolve（DLSS/blit）。barrier：`RT_SPLAT_ACCUM` color-attachment-write → shader-read；`RT_DENOISED` 在合成前后做 general 读写 barrier（对齐现有 `InsertBarrier` 用法）。

### 6.9 帧内集成与 barrier
- `VulkanBaseRenderer` 增 `std::unique_ptr<GaussianSplat::GaussianSplatPass> splatPass_`；`CreateSwapChain/DeleteSwapChain` 管理其资源（含 `RT_SPLAT_ACCUM` 创建，复用 `CreateStorageImage`）。
- 在 `Render()`（`:1325` 之后、`:1329` resolve 之前）插入 `splatPass_->Execute(...)`。`ReferenceMode` 分支同理或直接跳过。
- 所有 SSBO/图像状态切换沿用现有 `ImageMemoryBarrier` / `VkBufferMemoryBarrier` 范式（参考 `VulkanBaseRenderer.GpuDriven.cpp`）。

---

## 7. 坐标系与变换
- SOG：右手系 **x 右 / y 上 / z 后（−z 朝前）**。需与引擎相机/世界约定核对（**开放项，实现首步用一个已知 .sog 验证朝向**）。
- 每高斯世界变换 = 节点 `WorldTransform()` × 局部。**协方差**只受旋转+缩放影响：`Σ_world = M_rot · Σ_local · M_rotᵀ`（M 含节点缩放）。位置走完整 4×4。
- 若需镜像/换轴（如 z 翻转），位置与协方差旋转矩阵都要一致处理，避免左右手不一致导致镜像/光照翻面。

---

## 8. 依赖与构建
- **已具备**：`libwebp`（WebP 解码）、`nlohmann-json`（meta.json）、`glm`、`stb`（`vcpkg.json:47,49,73`）。
- **需新增**：ZIP 读取器以支持打包 `.sog`。推荐 **`libzip`** 或 header-only **`miniz`**（vcpkg 均有）。加入 `vcpkg.json` dependencies，桌面+移动端可用即可（移动端若不便，可先仅桌面打包变体）。
- **CMake**：新增 `src/Modules/SplatLoader/`（参考 ScadLoader 在 `src/CMakeLists.txt` 的接法）、`src/Engine/Rendering/GaussianSplat/`；`find_package`/link `WebP::webp`、`miniz`/`libzip::zip`。新增 Slang shader 进 `assets/shaders/` 构建。
- **资产**：`assets/splats/` 放样例；`assets/CMakeLists.txt` 增拷贝规则；`gnb paks` 可选纳入。
- **应用入口注册**：在各 program 启动注册处调用 `Modules::Splat::Register()`（对照 `Modules::Scad::Register()` 调用点）。

---

## 9. 编辑器 / 反射 / 脚本
- `GaussianSplatComponent`（`src/Engine/Runtime/Components/`）：`REFLECT_COMPONENT`，暴露 `splatModelId / visible / opacityScale / showFlags`，自动获得 PropertyPanel + QuickJS 绑定（参考 `RenderComponent` 反射注册）。
- `ShowFlags.ShowSplats` 开关；编辑器 outline/选择：本期可仅支持整资产显隐（包围盒选择），逐高斯拾取非目标。
- TypeScript 定义（`assets/typescript/Engine.d.ts`）补 `GaussianSplatComponent` 镜像（如走脚本控制）。

---

## 10. 测试与验证
- **单元测试**（Catch2，`src/Tests/Test_SogLoader.cpp`）：对 `FSplatQuant` 纯函数做已知向量校验（位置 log 往返、smallest-three 四元数四种 mode、codebook、SH_C0 基色、palette 索引）；小型合成 `meta.json`+webp 端到端解码 count/AABB 校验。
- **视觉验证**：`assets/configs/visual_test.json` 增高斯场景；`gnb shot --scene assets/splats/<x>.sog` 出图肉眼判遮挡/朝向/颜色；`gkNextVisualTest` 加 baseline。
- **共渲染验证**：一个 mesh + 高斯混合场景，确认高斯被前方 mesh 正确遮挡、且不污染 visibility buffer / GI。
- **成功标志**：日志 `uploaded scene [...] to gpu`；画面出现正确排序、与场景遮挡一致的高斯。

---

## 11. 分阶段开发计划

> 每阶段含：任务 / 主要文件 / 验收。Approach A 为唯一光栅路线；SH 先 DC 跑通再上全阶（实现顺序，非缩范围）。

### Phase 0 — 脚手架与依赖
- 任务：建 `src/Modules/SplatLoader/`、`src/Engine/Rendering/GaussianSplat/` 骨架；`vcpkg.json` 加 zip 库；CMake 接入；`Modules::Splat::Register()` 空实现并在入口调用；新增 `RT_SPLAT_ACCUM` bindless 槽 + `ShowFlags.ShowSplats`。
- 验收：全量 `gnb build` 绿；注册表能识别 `.sog`/`meta.json`（命中空 loader 返回失败但不崩）。

### Phase 1 — SOGLoader（非打包，DC-only）
- 任务：`FSogLoader` 解析 `meta.json` + 解码 webp + 反量化（位置/quat/scale/sh0/α）；`FSplatQuant` 纯函数 + 单测；先不解 `shN`。
- 文件：`FSogLoader.*`、`FSplatQuant.*`、`FSogTypes.h`、`Test_SogLoader.cpp`。
- 验收：对样例 `meta.json` 解出正确 `count`/AABB；单测全过。

### Phase 2 — GPU 资源 + 组件 + 场景集成
- 任务：`SplatModel`（SoA SSBO 上传，预计算 3D 协方差）；`GaussianSplatComponent`（反射）；`FLoaderRegistry::RegisterSplatLoader` + `SceneList` 分发（`.sog` 占位、`meta.json` basename 分发）；`Scene.splatModels_` + 上传。
- 验收：加载非打包 SOG 后场景含 splat 资源，节点挂组件，GPU buffer 上传成功（日志 + 显存）。

### Phase 3 — 渲染 pass 最小可见（DC-only，方案 A）
- 任务：`GaussianSplatPass` + 三 shader（project/cull、bucket sort、billboard raster）+ compose；接入 `Render()` 注入点；写 `RT_SPLAT_ACCUM` → 合成 `RT_DENOISED`。暂不做遮挡（或仅 per-splat 粗测）。
- 验收：`gnb shot` 能看到正确排序、DC 颜色的高斯叠加在画面上；显隐开关生效。

### Phase 4 — 深度遮挡 + 与 mesh 共渲染
- 任务：FS 采样 `RT_MINIGBUFFER` 重建场景深度做逐片元遮挡裁剪；mesh+splat 混合场景验证；确认不写 visibility buffer。
- 验收：高斯被前方 mesh 正确遮挡；切换 logic renderer（SoftwareModern/PathTracing）均正常叠加。

### Phase 5 — 打包 `.sog`（ZIP）
- 任务：miniz/libzip 在内存解包；loader 统一走打包/非打包；`SceneList` 扫描 `*.sog`。
- 验收：`--load-scene xxx.sog` 与对应非打包目录结果一致。

### Phase 6 — 完整 SH（1–3 阶，视角相关）
- 任务：loader 解 `shN`（palette 方案 6.3b：centroids + label 上传）；project shader 按视线评估全 SH。
- 验收：转动相机时高光/视角相关色变化正确；与 PlayCanvas Viewer 同资产目视一致。

### Phase 7 — 打磨：编辑器 / 反射 / 测试 / 文档
- 任务：PropertyPanel + QuickJS 绑定；visual_test baseline；样例资产入库；性能旋钮 CVar（桶数、3σ 系数、最大高斯数）；落 `AGENT_GUIDE/GaussianSplat.md` + 更新 `docs/README.md` 索引与本文件状态。
- 验收：单测 + 视觉回归绿；文档完备。

---

## 12. 性能考量
- 方案 A 受 **overdraw** 限制：用 3σ 收紧包围盒、`w<1/255` 早丢弃、小屏幕尺寸剔除、可选不透明度阈值过滤降低过度绘制。
- 在 **RenderExtent（DLSS 前）** 合成，像素更少。
- 桶数是质量/性能旋钮；必要时桶内局部细排。
- 全 SH 用 palette 方案控显存；超大资产留 LOD/decimate 后路（splat-transform `--decimate`）。
- **终局**：若 profiling 证明 overdraw 受限，可在同一 pass 接口下替换为 tile-based compute 光栅器（①②排序产物已解耦，便于切换）。

## 13. 风险与开放问题
- **坐标系/手性**：SOG（RH y-up −z）↔ 引擎裁剪空间约定需首版用已知资产校正（朝向/镜像/Y 翻转）。
- **`meta.json` 分发**：扩展名冲突，靠 basename 特判；需在 `SceneList` 扫描与单文件加载两条路径都处理。
- **`RT_DENOISED` 用法**：选独立 `RT_SPLAT_ACCUM` + compose 以避免改动其 usage；若改为直接混入需加 COLOR_ATTACHMENT 用法并核对 DLSS/blit 读时序。
- **移动端 zip/webp**：libwebp 跨平台 OK；zip 库移动端可用性需确认（否则移动端先支持非打包）。
- **抗锯齿正则**：`meta.antialias` 影响 2D 协方差 `+` 正则项，需与训练设置一致以免糊/硬边。
- **半透明排序近似**：分桶近似在强重叠半透明区可能有轻微 popping，可调桶数或局部细排缓解。

## 14. 参考资料
- [PlayCanvas SOG 格式规范](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/)
- [playcanvas/splat-transform（README + 库 API）](https://github.com/playcanvas/splat-transform)
- [PlayCanvas Splat Rendering Architecture（全局排序 / work buffer）](https://developer.playcanvas.com/user-manual/gaussian-splatting/rendering-architecture/)
- 3D Gaussian Splatting for Real-Time Radiance Field Rendering（Kerbl et al., SIGGRAPH 2023）
- 本仓库：`docs/designs/scad-loader-design.md`、`docs/guides/soft-mesh-shader-gpu-driven-submit.md`、`src/Modules/ScadLoader/`、`assets/shaders/common/BindlessTexture.slang`
