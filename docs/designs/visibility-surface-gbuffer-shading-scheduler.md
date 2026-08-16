---
title: "Visibility Surface、G-buffer 与 Shading Scheduler 设计"
category: design
status: 设计定稿（待实施）
owner: engine/rendering
created: 2026-08-16
last_updated: 2026-08-16
related_plan: ../plans/visibility-surface-gbuffer-plan.md
---

# Visibility Surface、G-buffer 与 Shading Scheduler 设计

本文是该方向的**设计定稿**，由早期提案对照当前代码与主流渲染器实践修订而来。
执行顺序与验收标准见[开发计划](../plans/visibility-surface-gbuffer-plan.md)；实施尚未开始，
当前代码仍由各 renderer 的 Core Shading 直接解析 Visibility Buffer。

## 结论摘要

目标职责链：

```text
Visibility Buffer（Rast.VisibilityPass，现状不变）
        ↓
Surface Build（新 compute pass：解析几何/材质/motion，写 dense Primary Surface）
        ├── GTAO（半分辨率，读 dense depth/normal，前移到 Core 之前）
        ├── 屏幕空间 shadow / tracing（读 dense surface，不再逐步重解几何）
        └── Shading Scheduler（allocation：bucket × rate，checkerboard 下沉为 rate）
                        ↓
            Core Shading（只消费分配到的像素，读 surface，不再解析 Visibility）
                        ↓
            Lighting-only checkerboard resolve（不再复制 surface 资源）
                        ↓
                    Compose / Upscaler（契约不变）
```

核心决策（相对早期提案的修订以 ★ 标注，理由见下文）：

1. Visibility Buffer 只表示命中定位；几何、材质、motion 在 Surface Build 解析一次，
   通过统一 codec 模块（`Shader/SurfaceBuffer.slang`，对标 `VisibilityBuffer.slang` 的地位）读写。
2. Primary Surface **复用现有 RT bank**，不新增一套并行资源；契约主要是"由谁写、写什么语义"
   的收口，外加 resolved material ID / feature flags 落入现有 `RT_BSDF_DATA`。
3. ★ 第一阶段**不建 pixel queue**。现有 `ResolveShadingPixel` 已是解析式（零内存）的
   allocation 函数；单 bucket + checkerboard rate 用解析映射即可。queue + indirect dispatch
   推迟到 bucket 成员资格变成数据依赖时（M4），且采用 **tile 粒度**分类而非 per-pixel append。
4. ★ 首批真实 bucket 是 **Background(sky) 与 Emissive**，不是材质特征桶。当前每个 Core shader
   的 miss / `MaterialDiffuseLight` 路径都是独立的 early-out 写出集，天然构成第二、第三个
   shading 方式，可以在不引入任何材质系统改动的前提下验证 scheduler 机制。
5. Checkerboard 下沉为 per-bucket 采样率策略；Surface Build 永远 full-rate，Core 按 rate
   分配，resolve 收缩为 lighting-only。
6. GTAO 前移到 Build 之后、Core 之前；首版保持 compose 端应用 AO 的现状语义，只改变可行的
   执行顺序，不同时改 AO 的合成方式。
7. ★ Renderer 迁移顺序：**SoftwareModernNoAmbient 先行**，SoftwareModern / SoftwareTracing
   通过新的 `FSurfacePrimaryRayCaster` 复用同一 seam 第二批跟进；PathTracing 系与 VoxelTracing
   不在本轮范围。

## 当前实现的问题（对照代码）

### 循环依赖：Core 的输出是 Core 想要的输入

这是整个改造最硬的论据。dense depth（`RT_PREV_DEPTHBUFFER`）与 normal（`RT_NORMAL`）
是 Core Shading 的**输出**，因此：

- `Common.ScreenSpaceShadowVisibility` 在 Core 内部被调用时无 dense depth 可用，只能通过
  `LoadVisibilitySurfacePlane` 对每个 march step 重新读 Visibility、取 3 个顶点、做矩阵变换
  和平面求交（`SceneSampling.slang`）。每个受影阴光源 4–12 步，每步一次完整几何重建。
- `TraceInScreenSpace` 每个 march step 调一次完整的 `Common.get_material_data`
  （3 顶点 fetch + barycentric 求解 + 属性插值）。
- GTAO 被迫排在 Core 之后，因为它读的 depth/normal 由 Core 写出；这又反过来决定了 AO 只能
  在 compose 阶段应用，Core 无法直接消费 AO。

主流引擎的 depth/G-buffer → SSAO/SSR → shading 顺序在当前架构里**无法表达**。Surface Build
前置即是打破这个环。

### Visibility 不是 surface，每个消费者重复解码

每个消费者都要重复：读 `NodeProxy`/`ModelData`/索引/三个顶点 → skinning/transform →
barycentric → `NodeProxy.matId[]` 材质解析 → 纹理采样。这套逻辑集中在
`Common.get_material_data` + `Common.FetchGBuffer`，Core 访问一次尚可，屏幕空间消费者按
step 重复访问不可接受。

### 隐式 G-buffer 已存在，但缺少生产阶段与契约

现有 RT bank 已包含 depth、normal、albedo、object ID、motion、BSDF data；问题是它们由各
renderer 在 Core 中边解析边写。已发现的契约分歧：

- `RT_NORMAL.w`：tracing 系（`FPathTracingRenderer.PrimaryHit`）写 roughness，
  `Core.SwModernNoAmbient` 写常量 1。统一契约时修正为 roughness。
- `Common.CalculateMotionVector` 内嵌 `RT_MOTIONMOMENT` 写入副作用；Build 接管后该副作用
  归属 Build。
- `RT_BSDF_DATA` 已经存 `{materialIndex, metalness}`（tracing 系），即 resolved material ID
  事实上已存在，只是 NoAmbient 不写、无 feature flags、无统一读写入口。

### Checkerboard resolve 复制 surface 的成本与语义错误

现状 `Process.CheckerboardResolve` 对 missing pixel 做邻居复制：Tracing set 11 张 RT 约
68 bytes/pixel，NoAmbient set 6 张约 40 bytes/pixel（4K 下分别约 564/332 MB/frame 的逻辑
读写）。邻居复制的 depth/normal/motion/objectId 在物体边缘、深度与运动不连续处是**语义错误
的 surface 数据**，会污染 temporal upscaler 的判断。dense surface 由 Build 全率写一次后，
resolve 收缩为：

- Tracing set：`RT_SINGLE_DIFFUSE` + `RT_SINGLE_SPECULAR` + 两张 hitdist ≈ 20 bytes/pixel
  （`RT_SPECULAR_ALBEDO` 是材质数据，归 Build 全率写出，退出 resolve）；
- NoAmbient set：`RT_SINGLE_DIFFUSE` + `RT_AMBIENT` ≈ 16 bytes/pixel。

需要诚实记录的反向成本：现状 checkerboard 时 Core 只为一半像素做 visibility 解码与材质采样；
Build 全率化后这部分回到每像素每帧。对重 tracing Core 这是小头；对 NoAmbient 这类轻 Core，
节省与新增可能同量级，必须实测（见决策门）。

## 主流渲染器对照

- **UE5 Nanite（5.0 material pass → 5.4 compute materials / shading bins）**：visibility buffer
  之后按材质分类，但 dispatch 单位是少量 shading bin + tile 粒度的分类表，每 bin 一次
  indirect dispatch。印证本设计"按 shading 方式分桶、不按 material ID 分桶、tile 粒度队列"。
- **Alan Wake 2（Northlight）**：VB → 一次性 material resolve 写标准 G-buffer → 常规 deferred
  shading。与本设计的 Surface Build 同构：他们的结论也是多消费者时 dense G-buffer 优于让每个
  消费者重放 VB 解码。
- **Call of Duty 系（tiled shading classification）**：按 tile 生成材质/光照特征 bitmask，
  按 permutation indirect dispatch。印证 M4 的 tile 分类而非 per-pixel atomic append。
- **主机 checkerboard 实践（PS4 Pro 世代）**：CBR 重建对象始终是 lighting/color，几何数据
  （depth/ID）保持全率，从不邻居复制 G-buffer。印证 resolve 收缩方向。
- **VRS / decoupled shading**：采样率是 shading kernel 之下的 allocation 策略，kernel 语义
  不感知 rate。印证 checkerboard 下沉为 rate 的抽象位置。

## Primary Surface 契约

### 字段与所有权（Build 写入，全率）

| RT（现有槽位） | 格式 | 语义 | 备注 |
| --- | --- | --- | --- |
| `RT_PREV_DEPTHBUFFER` | R32_SFLOAT | NDC depth | 背景 sentinel 沿用 1000，常量收口进 codec |
| `RT_NORMAL` | RGBA16F | xyz=world shading normal（含 normal map），w=roughness | 修正 NoAmbient 当前 w=1 的分歧 |
| `RT_ALBEDO` | RGBA16F | base color（diffuse 纹理已乘入） | 与 direct-sample 契约一致 |
| `RT_OBJECTID_0` | R32_UINT | `EncodeObjectId`（含 selected/hovered/locked/danger） | 不变 |
| `RT_MOTIONVECTOR` | RG32F | render-pixel 单位 motion；背景写 sky motion | 不变 |
| `RT_MOTIONMOMENT` | R16_UINT | motion 突变计数 | 副作用随 motion 计算归 Build |
| `RT_BSDF_DATA` | RG32_UINT | x=resolved material ID（`matId[]` 已解析；无效/背景=0xFFFFFFFF），y=f16 metalness ｜ 16bit feature flags | flags 见下 |
| `RT_SPECULAR_ALBEDO` | RGBA16F | specular albedo（metallic 用 base color） | 材质数据，M3 起归 Build，退出 resolve |

第一阶段**不写 world position**（由 depth + camera 重建）。Feature flags 初始集合保持最小：
`EMISSIVE`、`METALLIC`、`DIELECTRIC`、`HAS_MRA`、`HAS_NORMALMAP`；Material ID 负责身份，
flags 负责策略，二者不合并成"每材质一条 shader"的接口。

### 统一 codec

新增 `assets/shaders/Shader/SurfaceBuffer.slang`，地位对标 `VisibilityBuffer.slang`：

- `FPrimarySurface` 结构 + `Surface.Load(pixel)` / `Surface.Store(pixel, s)` /
  `Surface.IsValid(s)` / `Surface.ReconstructPosition(camera, pixel, size, depth)`；
- 背景 sentinel、flags 位定义、material ID 无效值都只在这里出现一次；
- 业务 shader 不允许绕过 codec 手写 RT 槽位组合。C++ 侧格式/槽位对应关系收口到
  `PipelineCommon` 的 layout 头（对标 `VisibilityBufferLayout.hpp`）。

### Build pass 边界

`Core.SurfaceBuild.comp.slang`（新）负责：Visibility 有效性与背景判定、三角形/skin/transform/
barycentric 解析（复用 `get_material_data`）、材质索引解析与材质纹理采样（复用
`FetchGBuffer`）、motion/objectId/feature flags 生成、dense surface 写出。`get_material_data`
拒绝的退化样本按背景处理（沿用现状语义）。

Build **不做** lighting、indirect、ray traversal、compose。Build 与 classification 分离
（先 surface 后分类），换取可调试性与逐步迁移；同 dispatch 合并留作 M4 之后的实测优化项。

## Shading Scheduler

### 抽象：bucket × rate

Scheduler 的输出是"本帧哪些像素、交给哪个 shading kernel"。两个正交维度：

- **bucket**：一种 shading 方式（v1：`Standard`；M4：`Background`、`Emissive`、`Standard`；
  未来：少量材质特征桶）。bucket 是 kernel 边界，数量必须保持个位数。
- **rate**：full / checkerboard（未来可扩展 tile/variable rate）。rate 只影响 allocation，
  kernel 语义不感知 parity —— Core 不再包含 `ResolveShadingPixel` 之外的任何 parity 知识，
  最终连该函数也从 Core 移出、归入 scheduler 头。

### 分阶段实现

1. **解析式 allocation（M1–M3）**：单 `Standard` bucket。dispatch 尺寸与 pixel 映射由
   scheduler 头（shader include + C++ `CheckerboardRendering.hpp` 演进而来）给出，零内存、
   零 atomic。这已经实现"checkerboard 从 Core 语义移到 allocation 层"的目标。
2. **Tile 分类 + indirect dispatch（M4）**：Build 后运行 classification，按 8×8 tile 生成
   per-bucket tile list + per-pixel 位掩码，`vkCmdDispatchIndirect` 按 bucket 派发；
   Background/Emissive kernel 只做各自的轻量写出。保留 full-screen fallback 开关做 A/B。
   不做 per-pixel atomic append —— 一个 tile 每 bucket 至多一次 append，局部性与 occupancy
   都优于像素队列（COD/Nanite 先例）。
3. **材质特征桶（触发条件式，非日程）**：只有当第二种真实 shading 方式出现（例如需要独立
   kernel 的特殊 BRDF），才由 material ID 查 feature mask 归入新桶。评估以 GPU profiler 为准。

### Resolve 收缩与 upscaler 契约

外部 upscaler（DLSS/FSR）要求 dense `RT_SCENE_COLOR`/depth/motion 的硬约束不变，因此
lighting-only resolve 保留为默认路径。Native TAAU 理解 sparse input + missing-pixel mask
属于独立评估项（M5），不阻塞主线；评估前不得让邻居复制的 lighting 冒充 dense 语义之外的
任何 surface 数据。

## GTAO 与屏幕空间消费者

- GTAO 移到 Build 后、Core 前。首版**不改**其合成方式（仍由 `Process.GTAOCompose` 做
  bilateral 上采样并只作用于 sky diffuse），只是顺序前移；Core 直接消费 AO、删除独立上采样
  属于后续独立评估，需保留非 temporal / 截图路径的 fallback。
- `ScreenSpaceShadowVisibility`：march step 改读 dense depth+normal（重建位置 + surface
  法线），`LoadVisibilitySurfacePlane` 的逐步几何重建退役为精确三角形平面需求方的专用
  fallback。
- `TraceInScreenSpace`：march step 改读 surface（depth 重建位置、normal 判定、material ID
  直读），不再逐步 `get_material_data`。
- 消费者必须按 renderer 是否声明 surface contract 门控：surface 未启用的 renderer 继续走
  现有路径（同一函数内部分支，由 scheduler/cvar 决定）。

## Renderer 适配

| Renderer | 批次 | Seam | 说明 |
| --- | --- | --- | --- |
| SoftwareModernNoAmbient | 第一批（M1–M2） | 拆分单体 shader 为 Build + Core | 消费者最多（GTAO+SS shadow+Core），带宽风险也最大——最早在风险最高处实测 |
| SoftwareModern | 第二批（M3） | 新 `FSurfacePrimaryRayCaster : IPrimaryRayCaster` | 框架 seam 干净：替换 `FVisibilityBufferRayCaster`，`FPathTracingRenderer` 的 surface 输出职责移交 Build（`WriteOutputs` 收缩） |
| SoftwareTracing | 第二批（M3） | 同上共享 caster | ReSTIR 的 primary gate 数据（instanceId/motion）从 surface 读取 |
| PathTracing / Lite | 不迁移 | — | HW primary 含 aperture/DOF，raster VB 无法重放；可选择性消费 material ID/motion 字段 |
| VoxelTracing | 不迁移 | — | 数据来源不同，避免无效 surface build |

M3 前置核验项：`BuildBSDFContext` 及后续 bounce 是否依赖 primary `TexCoord`／dielectric 的
position nudge（`PrimaryHit` 中非 dielectric 沿 ray 回退 epsilon）。若确需 TexCoord，surface
增补 RG16F 字段或该 renderer 保留局部重解码——以实测定，不预设。

## 成本、风险与决策门

- **Build 固定成本**：单消费者场景先写后读可能慢于 inline。决策门：M1 完成后在 1080p/4K 各测
  Build+Core+resolve 总 GPU time 与旧路径对比；若 NoAmbient 无消费者收益时净变慢，surface
  路径默认仅在 GTAO 或 SS shadow 启用时激活（cvar 联动），而不是强制全局。
- **带宽**：新增写出仅 `RT_BSDF_DATA` 扩展语义一项；其余字段现状已写。checkerboard 下
  Build 全率化的新增解码成本 vs resolve 收缩的节省，必须分 renderer 实测记录。
- **Scheduler 成本**：tile 分类、indirect 参数、barrier 都是 M4 的 A/B 对象；单 bucket 阶段
  零新增成本是硬性要求。
- **Alpha/特殊材质/动态状态**：per-instance override、材质热切换、skin 都经由 Build 每帧
  重新解析，无静态表假设；classification 保留 fallback bucket。
- **Temporal 契约**：surface 全率 dense 后，upscaler 的 motion/depth/objectId 输入不再含
  邻居复制的伪造数据，这是画质回归的预期收益点，也要纳入验证（silhouette/快速运动场景）。

## 验证标准

- 对照：surface 路径开/关（cvar），checkerboard 开/关，GTAO 四档 quality，编辑器 outline
  flags，HDR/SDR，材质 preview SceneOverride。
- 指标：1080p 与 4K 的 Build / GTAO / Core / resolve GPU timer（`SCOPED_GPU_TIMER`）；
  checkerboard 开关前后帧时间；`gnb shot` 静态对照 + `gkNextVisualTest` baseline diff；
  silhouette、thin geometry、快速运动、材质切换、emissive 的连续帧稳定性。
- 一致性：full-rate 下 surface 路径与旧 inline 路径的输出应视觉一致（visual test diff 阈值
  内）；checkerboard 下 surface 类 RT 必须逐像素 dense（不再出现邻居复制值）。

## 与现有文档和代码的关系

- [双平面 Visibility Buffer](massive-visibility-buffer-design.md)：Visibility ID 格式与容量契约（不变）。
- [SoftwareModernNoAmbient 渲染与 GTAO](software-modern-noambient-rendering.md)：第一批迁移对象的现状。
- [直接样本后处理与 Upscaler 输入链](direct-sample-post-chain.md)：compose/upscaler 输入契约（不变）。
- [渲染运行时架构与契约](rendering-runtime-architecture.md)：renderer contract、资源状态 tracker 规则；Build pass 需在 `FRendererContract`/`RendererDescriptors` 声明。
- 实现入口：`VisibilityBuffer.slang`、`GeneralFunc.slang`、`SceneSampling.slang`、
  `PrimaryRayCasters.slang`、`PathTracingRenderer.slang`、`Process.CheckerboardResolve.comp.slang`、
  `Core.GTAO.comp.slang`、`CheckerboardRendering.{slang,hpp}` 及各 renderer 的 Core/dispatch 代码。

执行顺序、每阶段验收与回退见[开发计划](../plans/visibility-surface-gbuffer-plan.md)。
