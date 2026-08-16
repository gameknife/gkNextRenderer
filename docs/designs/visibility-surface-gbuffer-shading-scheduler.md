---
title: "Visibility Surface、G-buffer 与 Shading Scheduler 设计"
category: design
status: 提案（未实现）
owner: engine/rendering
created: 2026-08-16
last_updated: 2026-08-16
---

# Visibility Surface、G-buffer 与 Shading Scheduler 设计

本文记录 Visibility Buffer 之后引入共享 surface/G-buffer 与 shading allocation 的候选架构，
以及 GTAO、屏幕空间 tracing 和 checkerboard rendering 在该架构中的位置。本文是后续评估用的
设计提案，不代表改造已经开始；当前代码仍由各 renderer 的 Core Shading 直接解析 Visibility
Buffer，并使用现有 checkerboard resolve。

## 结论摘要

当前路线可以收敛为一条明确的职责链：

```text
Visibility Buffer
        ↓
Dense Primary Surface / G-buffer Build
        ├── GTAO（可选，半分辨率）
        ├── 屏幕空间 tracing / shadow
        └── 统一 shading queue 分配（未来可扩展少量特殊 buckets）
                        ↓
                    统一 Core Shading
                        ↓
                 仅对 lighting 做必要 resolve
                        ↓
                    Compose / Upscaler
```

核心原则如下：

1. Visibility Buffer 只表示“这个像素命中了哪个 instance/triangle”，不是可直接复用的 surface。
   几何、材质、运动和 shading feature 应在共享的 Primary Surface 阶段解析一次。
2. G-buffer Build 不必一开始就变成传统 deferred renderer 的完整、过度宽的 G-buffer。它首先
   应是一个稳定的 dense surface contract，覆盖多个下游消费者真正重复需要的字段。
3. Material ID 先作为 Primary Surface 的稳定数据输出，不应在第一次实现中触发 shader 分裂。
   当前整个 renderer 只有一种 shading 方式，因此第一次只需要一个统一的 shading bucket；未来
   出现少量特殊 shading 方式时，再按材质/实例特征增加少数专用 buckets。
4. Checkerboard 应下沉为 shading allocation 的采样率策略，而不是 Core Shading 中需要理解的
   语义。Core Shader 只处理分配给它的像素，不再判断 parity 或补点规则。
5. 如果 G-buffer 是 dense 且正确的，checkerboard resolve 不应再复制 depth、normal、motion、
   object ID 或 material 数据；这些资源只需由 G-buffer Build 写一次。resolve 的对象应尽量缩减
   到 diffuse/specular/ambient 或最终 lighting。
6. GTAO 可以在 Core Shading 之前读取共享 surface，并将低成本、带噪声的 AO 交给 temporal
   upscaler 清理。但非 temporal upscaler 或截图/离线路径仍需要明确的空间 fallback。

## 当前实现的问题

### Visibility 不是 surface

当前 Visibility 由两张图组成：instance plane 保存 render proxy index，triangle plane 保存
section-local triangle index。它适合做一次命中定位，但每个消费者都必须继续完成：

- 读取 `NodeProxy`、`ModelData`、索引和三个顶点；
- 处理 skinning、world transform、normal/tangent 和 UV；
- 根据 `NodeProxy.matId[]` 将局部材质索引解析为运行时材质；
- 重新计算 barycentric 坐标并构造 `Vertex`；
- 读取材质纹理，得到 albedo、normal、roughness、metalness 等数据。

这套逻辑目前集中在 `Common.get_material_data` 一类的 shader helper 中。Core Shading 访问一次
尚可接受，但 `TraceInScreenSpace` 或 `LoadVisibilitySurfacePlane` 在一个 ray march 中多次采样
屏幕像素时，会对每个候选像素重复解析 Visibility 和三角形几何。

### 当前 RT 已经形成隐式 G-buffer

现有 render target bank 已经包含 depth、normal、albedo、object ID、motion vector、BSDF data
等输出。问题不在于完全没有这些数据，而在于这些数据由各 renderer 在 Core Shading 中边解析边写，
缺少一个共享的生产阶段和清晰的消费契约。

因此，候选方案不应机械地新增一套与现有 RT 并行的资源，而应优先评估：

- 哪些现有 RT 可以直接成为 Primary Surface 输出；
- 哪些字段需要新增，例如 resolved material ID 或 feature flags；
- 哪些 renderer 和模式确实有足够多的消费者，值得支付一次 surface build 的成本。

### 当前 checkerboard resolve 的语义和成本

现有 checkerboard Core Shading 每帧只计算一半横向像素，resolve 再为另一半像素选择相邻的
已计算像素。这个步骤不是 temporal reconstruction，也不是基于深度、运动或材质的真正重建；
它本质上是 neighbor copy。

当前 tracing resolve 不仅处理 diffuse/specular，还会复制 albedo、normal、object ID、depth、
motion、hit distance、BSDF data 等资源。这样做是因为后续 compose 和 upscaler 仍然要求这些
资源看起来是 dense 的，但在物体边缘、深度不连续、运动不连续和不同材质相邻的位置，复制邻居
会带来错误的 surface 语义，可能表现为 disocclusion、ghosting 或错误的 temporal rejection。

按当前格式估算，tracing resolve 目标资源约为 68 bytes/pixel，resolve 一半像素时，额外的
逻辑读写约为每个 full-resolution pixel 68 bytes；4K 下约 564 MB/frame，60 FPS 约 34 GB/s。
这只是未考虑 cache 的理论 image traffic，不等于实际 DRAM 流量，但足以说明 resolve 不是免费
步骤。NoAmbient 资源约为 40 bytes/pixel，4K 下同样有约 332 MB/frame 的额外逻辑流量。

对于很重的 PathTracing 或 SoftwareTracing，checkerboard 节省的计算可能仍然值得；但对 Core
较轻的路径，特别是 NoAmbient，checkerboard 加上大范围 resolve 可能把节省的计算转化为带宽。

## Primary Surface / G-buffer Build

### 推荐的最小 dense contract

第一阶段不建议写 world position。由 depth 和 camera 参数重建 position，减少带宽和存储压力。
推荐优先统一以下字段：

| 字段 | 作用 | 备注 |
| --- | --- | --- |
| Depth | 深度测试、重建位置、屏幕空间 tracing | 背景/无效像素必须有统一 sentinel |
| Normal + Roughness | GTAO、tracing、BRDF 和 upscaler | 法线空间与编码方式必须统一 |
| Albedo | compose、temporal rejection、材质分支 | 保持与现有 direct-sample 契约一致 |
| Resolved Material ID | 指向运行时材质表 | 存最终材质身份，不存未经解析的局部 index |
| Material/Feature Flags | 决定 shading bucket | normal map、MRA、emissive、alpha/special 等 |
| Object ID | motion/disocclusion/debug | 必须使用当前统一 object identity 契约 |
| Motion Vector | temporal upscaler 和历史拒绝 | 保持 render-pixel 单位 |
| 可选 BSDF/Hit Data | tracing 或特殊材质 | 只有存在消费者时才加入 shared contract |

`material ID` 的值应在 Build 阶段完成 `NodeProxy.matId[]` 的 local-to-global 解析，后续 shader
不应再次为同一像素做材质索引跳转。Material ID 负责身份，Feature Flags 负责策略；二者不能
混为一个“每种材质一条 shader”的接口。

### Build 阶段的边界

Build pass 需要处理：

- Visibility 有效性与背景判定；
- 三角形顶点、skin、transform、barycentric 和几何属性解析；
- 材质索引解析及材质表读取；
- 写入 dense primary surface；
- 必要时生成 motion、object identity 和 shading feature。

它不应承担完整的 lighting、复杂 indirect、ray traversal 或最终 compose。这样可以让同一份
surface 被 GTAO、屏幕空间 tracing、shadow 和不同 Core Shading 复用，同时保持职责可测试。

Build pass 可以和 classification 放在同一个 compute dispatch 中，也可以先写 surface、再做
classification。前者减少一次读取，后者更容易调试和逐步迁移；初版应优先选择可验证性。

## Material ID 与 Shading Scheduler

### 第一次实现：只有一个 shading bucket

当前整个 renderer 只有一种 shading 方式，所以第一次实现不应为了“material-aware”而提前引入
多套 Core Shader。建议直接建立一个统一的 shading queue/bucket：

- G-buffer Build 解析并写出 resolved material ID；
- 所有有效 surface pixel 进入同一个 shading bucket；
- dispatch 只根据有效像素数量以及 checkerboard 采样率决定本帧分配哪些像素；
- Core Shading 从 queue 读取像素并执行现有的统一 shading 方式。

换句话说，第一次 queue 化的主要目的，是把 checkerboard 从 Core Shader 语义中移到 allocation
层，而不是立即实现材质分类或多套 shading pipeline。Material ID 仍然值得在此阶段写入 G-buffer，
因为它可以服务于 screen-space 消费者、调试、temporal rejection，并为未来特殊 shading 方式
留下稳定的 surface contract；但它不参与第一次的 dispatch 分裂。

### 后续扩展：少量特殊 shading buckets

未来如果出现特殊的 shading 方式，再由 material ID 加材质/实例 feature flags 将像素分到少量
专用 buckets。仍然不建议为每个 material ID 创建独立 dispatch。材质数量和场景动态性会导致：

- dispatch 数量不可控；
- pipeline/material binding 管理复杂；
- queue 太碎，降低 occupancy 和 cache locality；
- 动态材质切换产生大量间接参数更新。

届时可以由 material ID 查表得到 feature mask，再归入有限数量的 shading buckets，例如：

- `OpaqueSimple`：无 normal map、无额外 screen-space 需求的简单材质；
- `OpaqueNormalMapped`：需要 normal/MRA 采样的材质；
- `OpaqueScreenSpace`：需要 GTAO、screen-space shadow 或 tracing 的材质；
- `OpaqueGI`：需要更重 GI 或额外光照策略的材质；
- `Emissive` / `SpecialMaterial`：自发光、特殊 BRDF、透明边界等单独路径；
- `Fallback`：无法归入稳定 bucket 的材质。

实际 bucket 数应以 GPU profiler 和真实的 shader 差异为准。没有独立 shading 语义的材质，继续
留在统一 bucket 中；只有确实需要不同 shader 策略的特殊材质才拆出专用 bucket。

### Queue 和间接 dispatch

第一次实现只需要为统一 bucket 追加类似下列的逻辑条目：

```cpp
struct ShadingItem
{
    uint2 pixel;
};
```

首版由像素位置定位 G-buffer，Material ID 作为 surface 字段读取，不需要重复放进 queue。Build
同时生成该统一 bucket 的计数和 indirect dispatch 参数，Core Shading 只消费这一个 queue。这样
Core Shader 不再需要知道：

- 当前帧 checkerboard 的 parity；
- 哪个像素是 missing pixel；
- Visibility 需要如何解析；
- 该像素是否来自某种调度分支。

第一版可以保留 full-screen fallback，用 A/B 测试确认单一 queue 的成本；如果 queue 本身收益
有限，也可以先使用固定 dispatch + allocation mask。后续增加特殊 buckets 时，再评估 atomic
append、compaction、indirect dispatch barrier 和像素访问局部性，不能只看理论上减少了 shader
分支。

## Checkerboard 下沉到 allocation 层

### 新语义

Checkerboard 不再表示“Core Shader 需要自己补齐一张图”，而表示“某个 shading bucket 本帧只
分配一部分像素”。因此 allocation 阶段可以按 bucket 选择：

- full-rate：统一 bucket 完整分配所有有效像素；
- checkerboard-rate：统一 bucket 按当前 frame phase 分配一半像素；
- 其他 rate：未来可扩展到 tile 或 variable-rate 策略。

Core Shading 收到的只是一个明确的像素队列。它不需要包含 `ResolveShadingPixel`、checkerboard
flag 或任何 parity 语义。

### 哪些 bucket 可以 checkerboard

第一次只有统一 bucket，checkerboard 只作用于这一条统一 shading queue。未来出现特殊 buckets 后，
才需要进一步决定哪些 bucket 保留 full-rate。以下类型通常更适合保留 full-rate，或至少需要单独
验证：

- sharp specular、镜面高光和高频反射；
- emissive 或强对比度材质；
- thin geometry、alpha edge 和细小几何；
- 运动快、反复 disocclusion 的区域；
- 依赖精确 object/material identity 的特殊路径。

更重、具有 temporal stability 且能从 upscaler 获益的 GI、ambient 或 screen-space lighting，
可以优先试验 checkerboard。

### Resolve 的目标

引入 dense G-buffer 后，resolve 不再需要复制 surface 资源：

```text
Dense G-buffer：depth / normal / motion / object / material 由 Build 完整写入
Sparse lighting：diffuse / specular / ambient 由 Core 按分配结果写入
Lighting resolve：只补齐后续契约要求的 lighting
```

对于仍要求 dense `RT_SCENE_COLOR`、depth 和 motion 的外部 upscaler，可以暂时保留轻量 lighting
resolve。未来如果 Native TAAU 能理解 sparse input，应增加 missing-pixel mask，让 upscaler
直接利用历史，而不是把邻居 surface 拷贝到缺失像素。

因此，现有按 `Tracing`、`NoAmbient`、`SceneColor` 粗粒度定义的 resolve set，后续应演进为
resource-specific 或 lighting-only contract，而不是继续把整个 G-buffer 当成 checkerboard 的
resolve 输出。

## GTAO 与屏幕空间消费者

### GTAO

GTAO 的候选链路是：

```text
G-buffer Build
    ↓
半分辨率 GTAO（读取 dense depth/normal）
    ↓
Core Shading 直接组合 AO 与 ambient
```

这允许 GTAO 在 Core Shading 之前完成，Core 只需采样 AO 和 surface，而不必在最后的 compose 阶段
再单独解释一套 AO 语义。对于已经有 temporal upscaler 的实时路径，可以先使用低样本、带噪声的
AO，让 upscaler 负责时域清理，从而尝试删除独立的重型降噪步骤。

但“去掉 GTAO 独立降噪 pass”不能简单等同于删除所有 GTAO 后处理：当前 GTAO compose 还承担
半分辨率到全分辨率的边缘感知上采样，以及部分 outline/tonemap 相关工作。改造时应拆清：

- AO 的估计是否已经在 Build 后、Core 前产生；
- AO 的空间上采样是否仍需要；
- 非 temporal upscaler、native presentation、截图和离线路径使用什么 fallback。

如果保留非 temporal fallback，实时 temporal 路径和稳定截图路径不应共用含糊的“默认有历史”
假设。AO 的随机序列也应按帧变化足够快，避免把慢变化的固定噪声误当成可由 upscaler 收敛的
temporal sample。

### 屏幕空间 tracing / shadow

`TraceInScreenSpace` 和 `LoadVisibilitySurfacePlane` 是最直接的受益者。它们可以改为读取：

- depth 重建位置；
- normal 判断命中/拒绝；
- material ID 或 feature flags 判断材质相关分支；
- object ID/motion 辅助 disocclusion 和 temporal rejection。

这样 ray march 的每一步不再重新读取 Visibility、索引和三角形顶点。需要精确三角形平面时，
仍可保留专门的 fallback，但不应让所有屏幕空间消费者默认走昂贵的几何重解析。

## Renderer 适用范围

| Renderer | 评估优先级 | 原因 |
| --- | --- | --- |
| SoftwareModern | 高 | Visibility、屏幕空间消费者和 Core shading 共享面数据明显，适合验证完整链路 |
| SoftwareModernNoAmbient | 高/需测量 | GTAO 与 surface 复用直接，但 Core 较轻，必须实测 Build 与 resolve 的带宽是否值得 |
| SoftwareTracing | 中高 | 屏幕空间 fallback 和材质特征分支有收益，但 tracing 自身的资源契约要单独核对 |
| PathTracing / PathTracingLite | 中 | primary hit 已有丰富 `Vertex`/`NodeProxy` 状态，不能假设普通 G-buffer 能替代全部 primary decode |
| VoxelTracing | 低 | 主要 lighting 数据来源不同，先保持现有路径，避免为了统一而引入无效 surface build |

建议先在 SoftwareModern 和 SoftwareModernNoAmbient 做对照实验，再决定是否把共享 contract
扩展到 tracing renderer。PathTracing 可以复用 material ID、motion 或 upscaler 相关字段，
但不应成为第一批强制迁移目标。

## 主要成本和风险

### 额外 surface build 成本

如果某个 renderer 只有一个简单的 Core Shading 消费者，先写 dense G-buffer 再读取，可能比直接
在 Core 中一次性完成解析更慢。因此 Build 应具备按 renderer/feature 开启的能力，不能假设任何
场景都自动获益。

### 带宽和资源生命周期

Normal、albedo、material、object、motion 等字段的格式、清除、layout transition 和跨 view bank
生命周期必须成为明确契约。新增字段的收益要和每帧全分辨率读写成本比较，避免用“减少重复 decode”
掩盖了更大的 RT 带宽。

### Queue 调度成本和局部性

atomic append、间接参数生成、barrier 和分桶后的像素访问顺序都会产生成本。材质很多但 feature
相同的场景，不应因为 material ID 不同而拆成大量 queue。后续如果 queue 局部性不足，可以评估
tile-local queue，但这属于第二阶段优化。

### Alpha、特殊材质和动态状态

透明/alpha test、实例 override、emissive、特殊 BRDF、材质热切换和动画 skin 都可能使静态
material table 不足。Classification 必须允许 per-instance/per-pixel feature 覆盖，并保留
fallback bucket。

### Temporal 契约

“由 upscaler 清噪”依赖正确的 motion、depth、object/material identity 和 camera cut reset。
Checkerboard 的邻居复制不能继续伪造这些 surface 数据；否则 upscaler 可能得到看似 dense、
实际语义错误的输入。

## 建议的渐进式验证顺序

以下是评估和实现时的候选顺序，不是已经批准的开发计划：

1. 引入可选的 Primary Surface Build，只写现有 depth/normal/albedo/object/motion 等 RT，保持
   现有 Core Shading 输出作为对照；比较图像、surface 数据和 GPU timer。
2. 将 `TraceInScreenSpace`、`LoadVisibilitySurfacePlane` 等重复解析路径改为优先读取 dense
   surface，保留开关以便 A/B 测试。
3. 让 GTAO 在 Build 后、Core 前运行；先保留非 temporal fallback，再测试低样本噪声 AO 交给
   temporal upscaler 的效果。
4. 将 SoftwareModern 的 Core Shading 改为读取 shared surface，并验证 checkerboard 关闭时与
   原路径一致。
5. 将统一 shading bucket 接入 queue/indirect dispatch；先只验证像素分配和 checkerboard rate，
   保留 full-screen fallback，不引入材质分桶。
6. 将 checkerboard parity 从 Core Shader 移到统一 bucket 的 allocation，resolve 缩减为
   lighting-only，并记录外部 upscaler 对 dense 输入的硬性要求。
7. 只有在出现明确的特殊 shading 方式后，再增加少量 feature buckets，并分别验证其收益。
8. 在有 missing-pixel mask 的前提下，单独评估 Native TAAU 的 sparse checkerboard 输入；外部
   upscaler 暂时继续使用 dense lighting resolve。

## 评估标准

评估不能只看 Core Shader 的 dispatch 数，应同时记录：

- 1080p 与 4K 下的 Build、consumer、queue、resolve GPU time；
- checkerboard 开关前后的实际显存读写和峰值带宽；
- 无屏幕空间消费者与重 tracing 消费者两种场景；
- silhouette、thin geometry、深度不连续、快速运动、材质切换和 emissive 的 temporal 稳定性；
- GTAO 在 temporal upscaler、native presentation、截图模式下的差异；
- dense G-buffer 与旧 Core inline decode 的 surface/lighting 对照图；
- 外部 DLSS/FSR 与 Native TAAU 对 dense 或 sparse contract 的实际限制。

只有在 surface build 的固定成本被多个消费者摊薄，并且 checkerboard resolve 的资源范围显著
缩小时，这条路线才有稳定的总体收益。

## 与现有文档和代码的关系

- [双平面 Visibility Buffer](massive-visibility-buffer-design.md)：当前 Visibility ID 的格式、
  容量和有效性契约。
- [SoftwareModernNoAmbient 渲染与 GTAO](software-modern-noambient-rendering.md)：当前
  NoAmbient、GTAO 和 compose 的实现背景。
- [直接样本后处理与 Upscaler 输入链](direct-sample-post-chain.md)：当前不在 renderer 内做
  颜色 history/独立 denoiser，以及 upscaler 输入契约。
- [渲染运行时架构与契约](rendering-runtime-architecture.md)：render target、RenderView 和
  renderer 生命周期的现行边界。

相关实现入口包括 `VisibilityBuffer.slang`、`GeneralFunc.slang`、`SceneSampling.slang`、
`Process.CheckerboardResolve.comp.slang`、`Core.GTAO.comp.slang` 和各 renderer 的 Core/dispatch
代码。本文不改变这些实现；它只为后续改造提供共同的术语、边界和验证顺序。
