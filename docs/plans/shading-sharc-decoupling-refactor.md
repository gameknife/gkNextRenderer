---
title: "Shading 重构 + SHARC 接入解耦方案（IRadianceCache 抽象）"
category: plan
status: 已实施（Phase 1–4 完成，Phase 5 待办）
owner: engine
created: 2026-06-19
last_updated: 2026-06-19
related:
  - docs/plans/sharc-official-handoff.md          # 官方库接入交接 Runbook
  - docs/plans/sharc-official-migration-plan.md    # 深度技术方案
  - docs/plans/sharc-integration-remaining-work.md # 剩余工作清单
---

# Shading 重构 + SHARC 接入解耦方案

> **给接手 agent**：本文自包含，目标是把 SHARC 的 update/query 逻辑从 `FPathTracingRenderer` 中抽离成一个独立的 `IRadianceCache` 接口实现，顺带把 shading 主循环去重。**这是一次纯重构：渲染结果必须与重构前逐像素一致（debug 容差）。** 不改算法、不改 buffer 布局、不改 C++ 调度。
>
> 前置阅读：`sharc-official-handoff.md`（理解三 pass 调度与护栏）。

## TL;DR — 一句话现状

SHARC 官方版已跑通且比旧 AmbientCube 采样快很多，但它把 update/query 的状态机**内联**进了 `FPathTracingRenderer.Render()`：7 个 `Sharc*` 字段、6 个 SHARC 专用方法、以及在 bounce-0 和 bounce-N 两处**重复粘贴**的 SHARC 更新/查询块。宏编译让它在非 SHARC pass 里退化成 stub（无性能损失），但 shader 可读性差、主循环被撕成两段。本方案用 Slang 接口 `IRadianceCache` 把这套状态机收编为三个实现（`FNullRadianceCache` / `FSharcUpdateCache` / `FSharcQueryCache`），并把 bounce-0 与 bounce-N 合并成单一循环。

---

## 1. 问题诊断（当前 `PathTracingRenderer.slang` 的污染点）

文件：`assets/shaders/common/PathTracingRenderer.slang`（769 行）。`FPathTracingRenderer` 既是路径追踪器，又被塞进了完整的 SHARC 状态机。具体污染：

### 1.1 SHARC 专用字段（共 7 个，混在通用配置里）

`struct FPathTracingRenderer` 顶部（行 151–166）的字段里，下列纯属 SHARC：

| 字段 | 行 | 性质 |
|---|---:|---|
| `EnableSharcUpdate` | 160 | SHARC |
| `EnableSharcQuery` | 161 | SHARC |
| `SharcDebugMode` | 163 | SHARC |
| `SharcUpdateSampleRatio` | 164 | SHARC |
| `SharcQueryRoughnessMin` | 165 | SHARC |
| `SharcQueryMinBounce` | 166 | SHARC |
| `UseAmbientCubeTerminal` | 159 | **通用**（保留，软追踪也用） |

### 1.2 SHARC 专用方法（共 6 个 + 1 个被它独占的工具方法）

| 方法 | 行 | 说明 |
|---|---:|---|
| `GetMaterialRoughness` | 168 | 仅被 `CanUseSharcAtBounce` 使用 |
| `CanUseSharcAtBounce` | 180 | query 资格判定 |
| `ComputeSegmentThroughput` | 202 | 仅 SHARC update 需要 |
| `ShouldRunSharcUpdateSample` | 218 | update pass 的 5×5 稀疏采样门 |
| `ApplyTerminalRadiance` | 242 | **混入** `SharcUpdatePathMiss`（行 250） |
| `WriteSharcDebugOutput/Occupancy/Radiance/SampleStale` | 681–713 | 4 个 debug 输出 |

`EvaluateDirectLighting`（行 210）本身是通用的，但目前几乎只为 SHARC update 服务。

### 1.3 `Render()` 主循环被撕成两段且重复（最严重）

`Render()`（行 371–621）的结构是：**bounce-0 特殊处理**（行 423–475）+ **bounce-N 循环**（行 487–583）。两段各自粘贴了一份几乎相同的 SHARC 块：

- bounce-0：`SharcUpdatePathHit`(起点) → `GetRayColor` → miss/light/throughput 三分支（行 437–461）→ `CanUseSharcAtBounce`+`SharcQuery`（行 462–475）。
- bounce-N：同样的 throughput 三分支（行 496–520）→ 同样的 `CanUseSharcAtBounce`+`SharcQuery`（行 527–540）。

这份重复**本身**就是 shading 的债，SHARC 只是把它放大了。此外行 542–565 把"二次反弹的直接光照策略"和 SHARC 标志耦合在一起：

- `EnableSharcQuery` → 求 direct lighting，命中即终止（靠 cache 提供间接）；
- `!EnableSharcUpdate`（即朴素 PT）→ 50% 随机太阳遮挡 NEE；
- update 模式 → 两个分支都不触发（direct 已经喂给 SHARC）。

### 1.4 入口 shader 的耦合

5 个 entry shader 调用 `renderer.Render(...)`：

| Entry | cache 语义 | 编译宏 |
|---|---|---|
| `Core.PathTracing.comp.slang` | 无（朴素 PT） | 无 SHARC 宏 |
| `Core.SwModern.comp.slang` | 无 | 无 |
| `Core.SwTracing.comp.slang` | 无 | 无 |
| `Core.SharcUpdate.comp.slang` | update | `-DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=1` |
| `Core.SharcQuery.comp.slang` | query | `-DGK_ENABLE_OFFICIAL_SHARC -DSHARC_QUERY=1` |

`Core.SharcQuery.comp.slang` 里还散落着 `SharcDebugMode==3/4/5` 的分发（行 30–55），调 `renderer.WriteSharcDebug*`。

> 宏来源：`assets/CMakeLists.txt:146-156` 与 `src/Engine/Vulkan/ShaderHotReloader.cpp:378-394`（两处必须同步，见 §6 护栏）。

---

## 2. 重构目标与护栏

### 2.1 目标

1. **SHARC 从 `FPathTracingRenderer` 完全移出**：renderer 里不再出现任何 `Sharc` 字样；SHARC 逻辑收进 `Sharc.slang`（受 `GK_ENABLE_OFFICIAL_SHARC` 宏保护）。
2. **bounce-0 与 bounce-N 合并**为单一循环，消除重复块。
3. **二次直接光照策略**与 SHARC 解耦，变成 renderer 上一个通用枚举。
4. **零性能回退**：非 SHARC pass 编译产物等价于现在（Slang 对接口参数单态化，null cache 内联成空）。

### 2.2 护栏（别违反）

- **逐像素一致**：重构前后 `./gnb.bat shot` 截图必须一致（PSNR 容差内）。这是验收的硬门。
- **不改 buffer 布局 / GPUScene / C++ 调度 / CVar**：本方案只动 `assets/shaders/**`。`FSharcState`、三 pass 调度、`SharcResources` 传址全部不动。
- **不改 `Sharc.slang` 里对官方头的低层包装**（`SharcBuildParameters` / `SharcMakeHitData` / `SharcResolveOfficialEntry` 等）——只在其之上**新增** cache 实现，并把 renderer 里的状态机逻辑**搬**进来。
- **默认行为不变**：`Core.PathTracing/SwModern/SwTracing` 走 null cache，输出与今天一致。
- **CMake 宏不变**：`Core.Sharc*` 的 `-D` 宏维持现状；新增/改名 shader 文件需 `--reconfigure`。

---

## 3. 目标架构

### 3.1 核心抽象：`IRadianceCache` 接口

放在新文件 `assets/shaders/common/RadianceCache.slang`（或并入 `Shading.slang` 的接口区，行 277–300 旁）。接口刻意用**通用辐亮度缓存**的语义命名，不出现 `Sharc`：

```slang
public interface IRadianceCache
{
    // 每个 sample 开始时重置路径状态
    [mutating] void BeginPath();

    // 像素级前置门：返回 false 则该像素整帧跳过（承载 update 的 5×5 稀疏采样）
    bool ShouldShadePixel(int2 pixel, UniformBufferObject camera);

    // cache 是否需要逐次命中的直接光照（决定 renderer 要不要真的 trace shadow ray）
    bool WantsDirectLighting();

    // 记录一次表面命中（含该点直接光照）；返回 false = cache 要求提前终止路径
    [mutating] bool OnSurfaceHit(float3 position, float3 normal, float3 directLighting, float random);

    // 散射后写入上一段 throughput
    [mutating] void OnSegmentThroughput(float3 throughput);

    // 路径逃逸（天空）或落到 ambient-cube 终结时的辐亮度
    [mutating] void OnMiss(float3 radiance);

    // 在当前 bounce 尝试用缓存短路；true = 路径在此终止，radiance 为要乘上的缓存值
    bool TryQueryRadiance(Vertex vertex, bool hitReflect, bool hitMetal,
                          uint bounce, float segmentLength, out float4 radiance);
}
```

> 命中光源（emissive 终结）在 update 语义下等价于"再记一次 hit、direct = emissive 辐亮度"——复用 `OnSurfaceHit`，不单列方法（与当前行 447、行 506 一致）。

### 3.2 三个实现

**`FNullRadianceCache : IRadianceCache`**（放 `RadianceCache.slang`，**无宏保护**，所有 pass 都能编）——全部空实现：`ShouldShadePixel`→true，`WantsDirectLighting`→false，`OnSurfaceHit`→true，`TryQueryRadiance`→false。Slang 单态化后这些调用全部内联消失。

**`FSharcUpdateCache : IRadianceCache`**（放 `Sharc.slang` 的 `#ifdef GK_ENABLE_OFFICIAL_SHARC` 分支）——把现有 `Render()` 里的 update 状态机搬进来：
- 持有 `SharcState state` + `float UpdateSampleRatio`；
- `BeginPath`→`SharcInit`；`ShouldShadePixel`→原 `ShouldRunSharcUpdateSample` 逻辑；
- `WantsDirectLighting`→true；`OnSurfaceHit`→`SharcUpdateHit`；`OnMiss`→`SharcUpdateMiss`；`OnSegmentThroughput`→`SharcSetThroughput`；
- `TryQueryRadiance`→false（update 不查询）。

**`FSharcQueryCache : IRadianceCache`**（同 `#ifdef` 分支）——把 query 状态机搬进来：
- 持有 `float RoughnessMin; uint MinBounce; uint DebugMode;`；
- `ShouldShadePixel`→true；`WantsDirectLighting`→false；`OnSurfaceHit`→no-op true；`OnMiss`/`OnSegmentThroughput`→no-op；
- `TryQueryRadiance`→原 `CanUseSharcAtBounce`(私有，含 `GetMaterialRoughness`) + `SharcQuery` + `SharcDebugMode` 着色（绿=命中、红=miss 强制终止），逻辑整段从 renderer 行 462–475 / 527–540 搬过来。

> `#else`（无官方 SHARC 宏）分支里**不需要**定义这两个 struct——只有 `Core.Sharc*` entry 会引用它们，而它们恒带宏编译。`FNullRadianceCache` 始终可用，覆盖朴素 PT/软追踪。

### 3.3 `FPathTracingRenderer` 的改动

**删除**：§1.1 的 6 个 `Sharc*` 字段、§1.2 的 5 个 SHARC 方法（`GetMaterialRoughness`/`CanUseSharcAtBounce`/`ComputeSegmentThroughput`/`ShouldRunSharcUpdateSample` + 4 个 `WriteSharcDebug*`）。

**保留并泛化**：`EvaluateDirectLighting`（通用）、`UseAmbientCubeTerminal`（通用）、`ApplyTerminalRadiance`（去掉内部 `SharcUpdatePathMiss`，改为返回辐亮度，由循环调 `cache.OnMiss`）。

**新增一个通用字段**（替代二次光照与 SHARC 的耦合）：

```slang
public uint SecondaryDirectMode = 0; // 0=PlainSunNEE(朴素PT) 1=DirectTerminateIfLit(query) 2=None(update)
```

**新增一个通用 debug 输出**替代 4 个 `WriteSharcDebug*`：

```slang
public void WriteDebugColor(float4 color) { /* 原 WriteSharcDebugOutput 的 G-buffer 写入 */ }
```

SHARC debug 的颜色由 `Sharc.slang` 现成的 `SharcDebugOccupancy` / `SharcDebugSampleStale` / `SharcQuery` 在 entry shader 算好后传进来（见 §3.5）。

**`Render` 签名**（沿用现有"接口参数"风格，Slang 会按具体类型单态化，零开销）：

```slang
[mutating]
public void Render(IRayTracer tracer, IDirectIlluminator directIllum,
                   inout IRadianceCache cache, int sampleMultiplier)
```

> 备选：若 codegen 显示有间接开销，改成显式泛型 `Render<TCache : IRadianceCache>(...)`。先按接口参数走，与现有 `IRayTracer`/`IDirectIlluminator` 一致。

### 3.4 合并后的单一循环（核心，替换行 402–603）

```text
if (!cache.ShouldShadePixel(pixel_, Camera)) return;     // 原 ShouldRunSharcUpdateSample 前置
... diffuse-light 主命中早退（行 386-398，原样保留）...
for (i in samples):
    RayColor = 1
    cache.BeginPath()
    vertex = primaryVertex; dir = primaryDir
    // 主命中先记一次 hit
    float3 d0 = cache.WantsDirectLighting() ? EvaluateDirectLighting(illum, vertex) : 0
    bool cont = cache.OnSurfaceHit(vertex.pos, vertex.normal, d0, rand())
    for (uint b = 0; b < maxBounces && cont; ++b):
        before = RayColor.rgb
        bool exit = GetRayColor(tracer, vertex, dir, RayColor, seed, hitReflect, hitMetal, hitDist, miss, term)
        if (exit):
            if (miss)              cache.OnMiss(term)
            else if (any(term>0))  cache.OnSurfaceHit(vertex.pos, vertex.normal, term, rand())  // 光源终结
            break
        cache.OnSegmentThroughput(throughput(before, RayColor.rgb))
        cont = cache.OnSurfaceHit(vertex.pos, vertex.normal,
                                  cache.WantsDirectLighting() ? EvaluateDirectLighting(illum, vertex) : 0, rand())
        if (!cont) break
        float4 cached
        if (cache.TryQueryRadiance(vertex, hitReflect, hitMetal, b+1, hitDist, cached)) { RayColor *= cached; break; }
        // —— 二次直接光照策略（替换行 542-565）——
        if (!hitReflect):
            if (SecondaryDirectMode == 1):                       // query: 命中即终止
                float3 dr = EvaluateDirectLighting(illum, vertex)
                if (any(dr>0)) { RayColor *= float4(dr,1); break; }
            else if (SecondaryDirectMode == 0 && Camera.HasSun && rand()<0.5):  // 朴素 PT 太阳 NEE
                ... 原行 551-565 sun-occlusion ...
            // SecondaryDirectMode == 2 (update): 不做
        // —— RR + 终结（原行 567-582，ApplyTerminalRadiance 改为 OnMiss 版本）——
        if (b == maxBounces-1 || earlyExit):
            if (dielectric) RayColor *= 0
            else { float4 t = AmbientTerminal(vertex); RayColor *= t; cache.OnMiss(t.rgb); }
            break
    ... hitMetal/hitReflect 累加、primary direct、输出（行 587-620 原样）...
```

bounce-0 与 bounce-N 现在是同一段代码；SHARC 的三处重复块塌缩为接口调用。`WantsDirectLighting()` 的编译期常量让 null/query cache 不会真去 trace shadow ray（保持朴素 PT 不做无谓 shadow trace 的现状）。

### 3.5 入口 shader 改造

```slang
// Core.PathTracing / Core.SwModern / Core.SwTracing
FNullRadianceCache cache;
renderer.SecondaryDirectMode = 0;
renderer.Render(tracer, dIlluminator, cache, sampleMultiplier);

// Core.SharcUpdate
FSharcUpdateCache cache;
cache.UpdateSampleRatio = params.UpdateSampleRatio;
renderer.SecondaryDirectMode = 2;
renderer.UseAmbientCubeTerminal = false;   // 原行 20
renderer.Render(tracer, dIlluminator, cache, 1);

// Core.SharcQuery
FSharcQueryCache cache;
cache.RoughnessMin = params.QueryRoughnessMin;
cache.MinBounce   = params.QueryMinBounce;
cache.DebugMode   = params.DebugMode;
renderer.SecondaryDirectMode = 1;
// debug 分发改为：
if (params.DebugMode == 3u) { renderer.WriteDebugColor(float4(SharcDebugOccupancy(DTid.xy, size),1)); return; }
if (params.DebugMode == 5u) { renderer.WriteDebugColor(float4(SharcDebugSampleStale(DTid.xy, size),1)); return; }
if (!renderer.PrimaryHit(rayCaster)) return;
if (params.DebugMode == 4u) { /* WriteSharcDebugRadiance 等价：SharcQuery + primaryAlbedo，见原行 700-708 */ ... return; }
renderer.Render(tracer, dIlluminator, cache, sampleMultiplier);
```

> `WriteSharcDebugRadiance`（原行 700–708）需要 `primaryAlbedo_` 与 `hitPrimaryVertex_`，是 renderer 私有态。两种落法：(a) 在 renderer 上保留一个**非 SHARC 命名**的小方法 `WriteDebugCachedRadiance(float4 cached)`；(b) 暴露 getter。推荐 (a)，保持 renderer 不依赖 `SharcQuery` 符号——entry 先 `SharcQuery(...)` 再把结果传进 `WriteDebugCachedRadiance`。

---

## 4. 文件地图（改动清单）

| 文件 | 改动 |
|---|---|
| `assets/shaders/common/RadianceCache.slang` | **新增**：`IRadianceCache` 接口 + `FNullRadianceCache` |
| `assets/shaders/Common.slang` | `__include "common/RadianceCache.slang"`（放在 `Sharc.slang` **之前**，让 cache 接口先可见） |
| `assets/shaders/common/Sharc.slang` | **新增** `FSharcUpdateCache` / `FSharcQueryCache`（`#ifdef` 分支内）；低层包装不动 |
| `assets/shaders/common/PathTracingRenderer.slang` | **主战场**：删 SHARC 字段/方法；`Render` 加 `inout IRadianceCache cache`；合并 bounce 循环；加 `SecondaryDirectMode` + `WriteDebugColor` + `WriteDebugCachedRadiance` |
| `assets/shaders/Core.PathTracing.comp.slang` | 传 `FNullRadianceCache` + `SecondaryDirectMode=0` |
| `assets/shaders/Core.SwModern.comp.slang` | 同上 |
| `assets/shaders/Core.SwTracing.comp.slang` | 同上 |
| `assets/shaders/Core.SharcUpdate.comp.slang` | 传 `FSharcUpdateCache` |
| `assets/shaders/Core.SharcQuery.comp.slang` | 传 `FSharcQueryCache`；debug 分发改 `WriteDebugColor` |
| `assets/shaders/Util.SharcCompileTest.comp.slang` | 顺带验证两个 cache struct 能编（compile spike） |

C++ / CMake：**零改动**（除非新增的 `RadianceCache.slang` 未被 `shader_common_files` glob 收录——核对 `assets/CMakeLists.txt` 的 common glob，必要时 `--reconfigure`）。

---

## 5. 执行计划（每 Phase 一道验收门）

> 构建命令（见 `AGENTS.md` / `sharc-official-handoff.md`）：
> ```bash
> ./gnb.bat build gkNextRenderer gkNextUnitTests            # 改 shader/Engine
> ./gnb.bat build gkNextRenderer --reconfigure              # 新增 shader 文件或改 CMake
> ./gnb.bat shot --scene assets/models/playground.glb --frames 300
> ./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300
> ```

**Phase 0 — 基线快照（先做）**：当前 `main` 上对两个场景各跑一次 `shot`（含 SHARC on/off、`r.sharc.debugMode=3/4/5`），存为 golden 参考图。后续每 Phase 与之逐像素比对。验收：golden 落盘。

**Phase 1 — 接口与 Null 实现**：新增 `RadianceCache.slang`（接口 + `FNullRadianceCache`），接入 `Common.slang`。`Render` 加 `inout IRadianceCache cache` 形参但**循环暂不动**，仅在 5 个 entry 传 `FNullRadianceCache`/对应占位。验收：`--reconfigure` 全量编过，5 个 entry 出 `.spv`；朴素 PT 截图 == Phase 0。

**Phase 2 — 合并 bounce 循环（仍用 SHARC 内联，行为不变）**：把 bounce-0 与 bounce-N 合并成 §3.4 的单循环，但此刻 SHARC 调用**仍是**现有的 `SharcUpdatePathHit` 等内联写法（先只解决"两段重复"）。这一步最容易引入偏差，务必逐像素比对 SHARC update/query 两个场景。验收：四种模式（PT / SwModern / SHARC update / SHARC query）截图 == Phase 0。

**Phase 3 — 搬迁 SHARC 状态机进 cache**：把内联的 SHARC 调用替换为 `cache.*`；实现 `FSharcUpdateCache` / `FSharcQueryCache`（逻辑从 renderer 搬入 `Sharc.slang`）；renderer 删除所有 `Sharc*` 字段/方法；`SecondaryDirectMode` 接管二次光照。验收：四种模式截图 == Phase 0；`grep -i sharc PathTracingRenderer.slang` 无结果。

**Phase 4 — debug 分发与收尾**：4 个 `WriteSharcDebug*` → `WriteDebugColor`/`WriteDebugCachedRadiance` + entry 端分发；`Util.SharcCompileTest` 引用两个 cache 验证可编。验收：`r.sharc.debugMode=3/4/5` 三个 debug view == Phase 0。

**Phase 5（可选，shading 深化重构）**：在解耦稳定后再做，**不与上面混提**：
- 拆 `GetRayColor`（行 256–369）为 `SampleScatterDirection`（BSDF 采样）+ `ShadeSegment`（trace+albedo），去掉 `*DontCare` 命名残留；
- `FHardwareDirectIlluminator` 等 4 个 illuminator（行 13–149）从 `PathTracingRenderer.slang` 拆到独立 `DirectIlluminators.slang`；
- 评估 `EvaluateDirectLighting` 在 query 模式的重复求值（§3.4 里 update 与 query-secondary 可能各算一次）。
验收：每项独立提交、独立截图比对。

---

## 6. 关键风险

1. **逐像素回归**：合并 bounce 循环时 SHARC update 的 hit/throughput 调用次序极易错位（当前 bounce-0 在循环外多记一次起点 hit）。§3.4 已对齐次序：起点 hit 在循环外、段后 hit 在循环内、`OnSegmentThroughput` 在两次 hit 之间。**Phase 2 必须单独验收**，别和 Phase 3 合并。
2. **Slang 接口参数单态化**：`inout IRadianceCache` 能否被单态化成零开销取决于 Slang 版本（核对 `assets/CMakeLists.txt` 打印的 `slangc --version`）。若 SHARC pass 出现寄存器/性能回退，退化为显式泛型 `Render<TCache:IRadianceCache>`。
3. **`mutating` + `inout` 接口方法**：确认 Slang 允许 `[mutating]` 接口方法经 `inout` 形参调用（`FSharcUpdateCache.state` 需跨调用持久）。Phase 1 的 compile spike 就能证伪。
4. **宏两处同步**：若 Phase 调整了 `Core.Sharc*` 需要的 `-D` 宏，`assets/CMakeLists.txt:146-156` 与 `src/Engine/Vulkan/ShaderHotReloader.cpp:378-394` **必须同时改**，否则 hot reload 与 CMake 构建行为分叉。本方案默认不动宏。
5. **include 顺序**：`FNullRadianceCache` 必须在 `PathTracingRenderer.slang` 之前可见；`Common.slang` 的 `__include` 顺序要把 `RadianceCache.slang` 排在 `Sharc.slang`、`PathTracingRenderer.slang` 之前。

---

## 7. 完成定义（DoD）

- [x] `grep -i sharc assets/shaders/common/PathTracingRenderer.slang` 无任何结果
- [x] `FPathTracingRenderer` 不再含 `EnableSharcUpdate/Query`、`SharcDebugMode` 等 6 个 SHARC 字段与 5 个 SHARC 方法
- [x] SHARC update/query 状态机全部落在 `Sharc.slang` 的 `#ifdef GK_ENABLE_OFFICIAL_SHARC` 分支（`FSharcUpdateCache` / `FSharcQueryCache`）
- [x] bounce-0 与 bounce-N 合并为单循环，无重复 SHARC 块
- [x] `./gnb.bat build gkNextRenderer --reconfigure` + `gkNextUnitTests` 通过（唯一失败 `Test_PhysicsSync` SIGSEGV 与本次纯 shader 改动无关，预存在）
- [x] 四种模式 + 三个 debug view 验收：PT/SwModern 走 `FNullRadianceCache`；SHARC on/off 截图差异落在 run-to-run 噪声地板内（PT off：golden-vs-after PSNR 39.24 dB ≈ run-to-run 39.37 dB；SHARC on：26.95 ≈ 26.90），debug 3/4/5 渲染正常
- [x] `r.sharc.enable` on/off 重启切换正常，默认 off 行为不变
- [x] 更新本文件 front-matter `status`

> **实现备注（给后续 agent）**：
> - 为保证逐像素一致，**未照搬 §3.4 伪代码**：§3.4 会让 b=0 也跑 secondary-direct/RR，导致 naive PT 在 V1 多消耗随机数、随机流错位。实际实现把 b==0 的尾部（ExitAfterFirst 终结）与 b>=1 的尾部（secondary-direct + RR + terminal）分开，scatter + SHARC accounting + query 三块合一，重复块消除但随机消费顺序与原版逐项对齐。
> - 接口 `OnSurfaceHit` 改为接 `inout uint4 randomSeed`（而非 §3.1 的 `float random`）：只有 update cache 内部 `RandomFloat(seed)`，null/query cache 不抽样，从而 naive PT/软追踪随机流不被污染。
> - `OnSegmentThroughput(before, after)` 接收前后 RayColor，throughput 计算（原 `ComputeSegmentThroughput`）下沉到 `Sharc.slang`。
> - `Render` 用显式泛型 `Render<TCache : IRadianceCache>(..., inout TCache cache, ...)`（§3.3 备选方案），保证单态化零开销与 `inout` 可变性。
> - debug：renderer 暴露 `GetSize()`/`GetPrimaryVertex()`/`WriteDebugColor`/`WriteDebugCachedRadiance`，entry shader 自行调 `SharcQuery`/`SharcDebug*` 再传值，renderer 不依赖任何 `Sharc` 符号。

---

## 8. 参考

- 当前真实符号：`assets/shaders/common/PathTracingRenderer.slang`（`FPathTracingRenderer.Render` 行 371–621）、`assets/shaders/common/Sharc.slang`（低层包装 + stub）
- 三 pass / 资源 / CVar：`sharc-official-handoff.md` §2 文件地图
- 官方库：https://github.com/NVIDIA-RTX/SHARC ｜ 集成指南 `docs/Integration.md`
