---
title: "SHARC — 切换到官方 NVIDIA-RTX/SHARC 库 迁移计划"
category: plan
status: 待实现
owner: engine
created: 2026-06-16
last_updated: 2026-06-16
related:
  - docs/plans/sharc-integration-plan.md
  - docs/plans/sharc-integration-remaining-work.md
---

# SHARC — 切换到官方 NVIDIA-RTX/SHARC 库 迁移计划

> 决策：放弃自研哈希缓存，改接官方 RTXGI SHARC 库。
> 原则：**复用已有的引擎侧脚手架，只换 shader 侧算法 + buffer 布局**，并解决 Slang/BDA 与官方 HLSL 头的绑定问题。

## 0. 复用 vs 重写

当前自研版本已经把引擎侧搭好了，这些**保留**：

| 已有脚手架 | 文件 | 处置 |
|---|---|---|
| CVar 全套 | `EngineCVars.cpp` / `UserSettings.hpp` | 保留，按官方参数补充/重命名 |
| 三 pass 调度 + GPU timer | `PathTracingRenderer.cpp` Update→Resolve→Query | 保留 |
| buffer 分配 / 生命周期 / 清零 | `PathTracingRenderer.cpp` `Ensure/Clear/ParametersSharcResources` | 保留，改 buffer 布局 |
| barrier 序列 | `InsertSharcBarrier` | 保留，按官方 RW/Read 表核对 |
| GPUScene 二级资源表 | `ReservedAddress0 -> SharcResources` | 保留 |

这些**替换/删除**：

| 自研件 | 文件 | 处置 |
|---|---|---|
| 自研缓存数学 | `assets/shaders/common/Sharc.slang` | **删除**，换官方 `SharcCommon.h` 逻辑 |
| 自研 buffer 结构体 | `SharcHashEntry/Accumulation/Resolved` | **替换**为官方布局（见 §3） |
| path loop 插桩 | `common/PathTracingRenderer.slang` 中 `SharcAccumulate/SharcQuery/CanUseSharcAtBounce` | **重写**为官方 `SharcInit/UpdateHit/UpdateMiss/SetThroughput/GetCachedRadiance` |

结论：**这不是从零重写，是替换 shader 算法层 + 布局对齐 + 解决编译/绑定。**

## 1. 官方库要点（已核对 Integration.md）

三 pass：Update（稀疏 RT 写 cache）、Resolve（compute，合并历史 + stale 驱逐 + 清 accumulation）、Render/Query（RT，命中查缓存 early-out）。

Shader 头：`Shaders/Include/SharcCommon.h`（已 include `HashGridCommon.h`）、`SharcTypes.h`；GLSL 需要先 include `SharcGlslHelpers.h`。

关键 API（按 pass）：

- Update：`SharcInit()` → 每 hit `SharcUpdateHit()`（返回 false 可提前终止）→ miss `SharcUpdateMiss()` → 选定新方向后 `SharcSetThroughput()`，之后 throughput 可重置为 1。
- Resolve：每 entry 调 `SharcResolveEntry()`，入参含 max accumulated frames 与 `staleFrameNumMax`。
- Query：每个可用 hit（一般排除 primary）调 `SharcGetCachedRadiance()`，成功即终止 path。
- HashGrid：`HashGridParameters{cameraPosition, logarithmBase=SHARC_GRID_LOGARITHM_BASE, sceneScale, levelBias=SHARC_GRID_LEVEL_BIAS}`，debug 用 `HashGridDebugColoredHash()` / `HashGridDebugOccupancy()`、`GetVoxelSize()`。

编译 define：`SHARC_UPDATE`、`SHARC_QUERY`、`SHARC_USE_FP16`、`SHARC_ENABLE_64_BIT_ATOMICS`、`SHARC_ENABLE_SH_ENCODING`、`SHARC_ENABLE_RESPONSIVE_LIGHTING`、`SHARC_PROPAGATION_DEPTH`(默认 2)、`SHARC_RADIANCE_SCALE`、`SHARC_STALE_FRAME_NUM_MIN`。

> 注意：API 的精确签名（参数顺序、`SharcParameters`/`SharcState`/`SharcHitData` 字段）需在 vendor 头文件时对照 `SharcCommon.h`/`SharcTypes.h` 最终确认——本计划据 Integration.md 编写，落地时以头文件为准。

## 2. 核心风险：Slang/BDA vs 官方 HLSL 头

这是整个迁移的关键，必须在 Phase 1 解决。

官方头是 HLSL 风格，缓存访问走 `RWStructuredBuffer`（hash entries / voxel data / voxel data prev），并通过 `SharcParameters` 结构体把 buffer 句柄传进各函数。本引擎 shader 是 **Slang + buffer device address（BDA 指针）**，不走 descriptor 绑定的 `RWStructuredBuffer`。

两条可选路线：

- **A.（推荐）Slang 消费 HLSL 头 + BDA 包装**：Slang 能 include HLSL `.h`。把官方 `SharcParameters` 的 `RWStructuredBuffer` 成员替换为 Slang 指针（`uint64_t*` / 结构体指针），用现有 `Bindless.GetGpuscene().Sharc` 的 BDA 喂进去。需确认 `InterlockedAdd/CompareExchange`、`float16_t`、64-bit atomic 在 Slang→SPIR-V 下等价。
- **B. 这几个 permutation 改用 DXC（HLSL）编译**：与引擎 Slang 主体混编，path tracer loop 在 Slang，跨语言调用困难，**不推荐**。

无论哪条，都要让 16-bit / fp16 在 CMake + hot reload 生效（`SHARC_USE_FP16`，`float16_t4` resolved / `float16_t3` 权重；SPIR-V 要开 16-bit storage/arithmetic capability）。若 64-bit atomic 不可用，置 `SHARC_ENABLE_64_BIT_ATOMICS 0` 并启用 lock buffer（现有 `LockBuffer` 正好接上）。

## 3. Buffer 布局对齐

官方默认布局（每 voxel 共 40B）：

| Buffer | stride | 说明 |
|---|---:|---|
| Hash entries | 8B | uint64 哈希 |
| Accumulation | 16B | 本帧累积 radiance + sample count |
| Resolved | 16B | 跨帧 radiance（`float16_t4`）+ 总 sample + 额外数据 |

开 `SHARC_ENABLE_SH_ENCODING`：accumulation 32B、resolved 24B（每 voxel 64B）。`2^22` entries → 默认 160 MiB / SH 256 MiB。

任务：把 C++ 侧 `SharcHashEntry/AccumulationEntry/ResolvedEntry`（`BasicTypes.slang` + C++ 镜像）改成与 `SharcTypes.h` 一致的 stride，`EnsureSharcResources` 的 `sizeof(...)` 自动跟随。lock buffer 仅在 64-bit atomics 关闭时分配。

## 4. 分阶段

### Phase 1 — Vendor + Compile Spike（先做，验证 §2）
- [ ] vendor 官方头到 `assets/shaders/third_party/sharc/`（`SharcCommon.h`/`HashGridCommon.h`/`SharcTypes.h`，及 GLSL helper 视情况）
- [ ] 写 Slang `RWStructuredBuffer`→BDA 包装宏/wrapper（路线 A）
- [ ] 新增 `Util.SharcCompileTest.comp.slang`，只 include 头并调一两个函数
- [ ] CMake + `ShaderHotReloader.cpp` 加 16-bit/fp16 参数；确认 hot reload 不坏
- [ ] 验收：`./gnb.bat build gkNextRenderer` 通过且生成 `.spv`，`Interlocked*`/`float16_t`/64-bit atomic 编过

### Phase 2 — 布局与资源对齐
- [ ] buffer 结构体改官方布局（§3），更新 C++ 镜像与 `static_assert`
- [ ] `SharcParameters` 改为承载官方 `HashGridParameters`（sceneScale/logarithmBase/levelBias/cameraPosition）
- [ ] CVar 对齐官方语义：`r.sharc.sceneScale`、`r.sharc.levelBias`、`r.sharc.accumulatedFramesMax`、`r.sharc.staleFrameMax` 等
- [ ] 验收：on/off 都能启动，validation layer 无 descriptor/barrier 报错

### Phase 3 — Resolve pass（最独立，先接）
- [ ] `Core.SharcResolve.comp.slang` 改调 `SharcResolveEntry()`，传 accumulatedFramesMax / staleFrameNumMax
- [ ] 核对 Resolve 的 RW 表（hash RW / accum RW / resolved RW）
- [ ] 验收：空 cache 不崩；readback 抽样验证 stale 驱逐

### Phase 4 — Update pass
- [ ] `Core.SharcUpdate.comp.slang` 重写 path loop：`SharcInit` → `SharcUpdateHit/Miss` → `SharcSetThroughput`（每段独立累积）
- [ ] 稀疏采样：每 5×5 block 取 ~1 像素（~4%），帧间抖动保证覆盖
- [ ] 验收：occupancy 随帧增长；关 Query 时画面≈原 path tracer

### Phase 5 — Render/Query pass
- [ ] `Core.SharcQuery.comp.slang` 用 `SharcGetCachedRadiance()`，命中即终止
- [ ] segment 长度 < `GetVoxelSize()` 时继续 trace；specular 用 cone-spread 判定（`2·len·sqrt(0.5·a²/(1−a²))`, a=roughness²）
- [ ] 验收：相同 sample 下噪声更低或更省；无明显漏光/ghosting

### Phase 6 — 质量 / 响应性 / debug
- [ ] `HashGridDebugColoredHash` / `HashGridDebugOccupancy` / bounce-count heatmap 接 debug view
- [ ] 评估开 `SHARC_ENABLE_SH_ENCODING`（含布局切换 32/24B）
- [ ] 评估 `SHARC_ENABLE_RESPONSIVE_LIGHTING`（注意 Resolve 不再清 accumulation，需整体 clear）
- [ ] occupancy 调参：静态相机 10–20%，过高则加 entries 或降 staleFrameMax

## 5. 验证（沿用计划 §5）
- [ ] `./gnb.bat build gkNextRenderer gkNextUnitTests` 通过
- [ ] `./gnb.bat shot --scene assets/models/playground.glb --frames 300`
- [ ] `./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300`
- [ ] 指标：occupancy / collision / hit rate / 各 pass GPU time / on-vs-off 噪声

## 6. 文档收尾
- [ ] `sharc-integration-plan.md` 标注「自研版已废弃，改官方库」并指向本计划
- [ ] 删除自研 `Sharc.slang` 时在 commit 说明保留的脚手架
