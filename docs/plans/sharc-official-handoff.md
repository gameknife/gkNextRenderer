---
title: "SHARC 官方库接入 — Agent 交接 Runbook"
category: handoff
status: 官方MVP已接入
owner: engine
created: 2026-06-16
last_updated: 2026-06-16
related:
  - docs/plans/sharc-official-migration-plan.md   # 深度技术方案
  - docs/plans/sharc-integration-remaining-work.md # 剩余工作清单
  - docs/plans/sharc-integration-plan.md           # 原始计划（自研版，已废弃）
---

# SHARC 官方库接入 — Agent 交接 Runbook

> **给接手 agent**：这是唯一入口文档，自包含可直接执行。深度技术细节看 `sharc-official-migration-plan.md`。
> **任务**：把现有的**自研** SHARC 哈希缓存替换为**官方 NVIDIA-RTX/SHARC 库**（RTXGI）。
> **决策已定（2026-06-16）**：走官方库，路线 A（Slang 消费 HLSL 头 + BDA 包装）。

## TL;DR — 一句话现状

引擎侧脚手架（三 pass 调度 / buffer 分配 / barrier / CVar / GPUScene 传址）**已经搭好且保留**；shader 侧已切到官方 NVIDIA-RTX/SHARC adapter，buffer 布局已对齐官方默认 stride，Slang/BDA 包装可编译运行。当前是官方 MVP：Update/Resolve/Query 能启动并写入 occupancy，完整 throughput path-loop、性能收益证明和高级质量项仍在后续清单中。

## 0. 必读约束（护栏，别违反）

- **不改 `GPUScene` 大小**：固定 128B，有 `static_assert(sizeof(Assets::GPUScene) == 128)`。SHARC 地址走二级表 `ReservedAddress0 -> SharcResources`（已有）。
- **默认关闭**：`r.sharc.enable=false`，不得影响现有 renderer 行为。
- **不动 AmbientCube**：SHARC 只进 `ERT_PathTracing`，与 AmbientCube GI 并存，目标不同。
- **保留引擎脚手架**：见 §2「保留」表；只换 shader 算法层与 buffer 布局。
- **官方头精确签名以源码为准**：本 runbook 据官方 `docs/Integration.md` 编写；vendor 头时务必对照 `SharcCommon.h`/`SharcTypes.h` 核对函数签名与结构体字段。

## 1. 构建与验证命令（本仓库约定，见 AGENTS.md）

```bash
# 改了 Engine 层 / shader：只构建受影响目标（别全量）
./gnb.bat build gkNextRenderer gkNextUnitTests

# 新增了 shader 文件（未被 glob 收录）或改了 CMake：加 --reconfigure
./gnb.bat build gkNextRenderer gkNextUnitTests --reconfigure

# 快速看渲染对不对（覆盖式截图到 out/build/<preset>/screenshots/agent_validation.jpg）
./gnb.bat shot --scene assets/models/playground.glb --frames 300
./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300
```

构建产物：`out/build/<platform>/bin/`。Shader 编译 / hot reload 相关：`assets/CMakeLists.txt`、`ShaderHotReloader.cpp`。

## 2. 文件地图（当前真实符号名）

**保留（引擎脚手架，复用）：**

| 作用 | 位置 | 真实符号 |
|---|---|---|
| CVar 定义 | `src/Engine/Runtime/Config/EngineCVars.cpp` | `r.sharc.enable/entriesPow2/updateSampleRatio/debugMode/queryMinBounce/queryRoughnessMin/sceneScale/levelBias/radianceScale/accumulatedFrameMax/responsiveFrameMax/staleFrameMax` |
| CVar 字段 | `src/Engine/Runtime/Config/UserSettings.hpp` | `SharcEnable` 等 |
| 三 pass 调度 / 资源 / barrier | `src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp` | `EnsureSharcPipelines/EnsureSharcResources/UpdateSharcParameters/ClearSharcResources/InsertSharcBarrier/BuildSharcGPUScene` |
| 状态/buffer 成员 | `src/Engine/Rendering/PathTracing/PathTracingRenderer.hpp` | `FSharcState sharc_`（`hashEntries/lockBuffer/accumulation/resolved/parameters/resources`）、`sharcUpdate/Resolve/QueryPipeline_` |
| GPUScene 传址 | `BasicTypes.slang`（含 C++ 镜像 + Slang，两处 `Sharc` property 经 `ReservedAddress0`） | `GPUScene.Sharc` |

**替换 / 删除（自研算法层）：**

| 作用 | 位置 | 处置 |
|---|---|---|
| 自研缓存数学 | `assets/shaders/common/Sharc.slang` | **删除**，换官方 `SharcCommon.h` |
| buffer 结构体 | `BasicTypes.slang`：`SharcHashEntry / SharcAccumulationEntry / SharcResolvedEntry`（均 `ALIGN_16`）+ C++ 镜像 | **替换**为官方布局（§4） |
| path loop 插桩 | `assets/shaders/common/PathTracingRenderer.slang`：`SharcAccumulate / SharcQuery / CanUseSharcAtBounce`、`EnableSharcUpdate/Query` 标志 | **重写**为官方 API |
| 三 pass shader | `assets/shaders/Core.SharcUpdate / Resolve / Query.comp.slang` | **改写**调官方函数（壳保留，内部换） |

## 3. 官方库 API 速查（来自 Integration.md）

头文件：`Shaders/Include/SharcCommon.h`（已 include `HashGridCommon.h`）、`SharcTypes.h`；GLSL 路径需先 include `SharcGlslHelpers.h`。

| Pass | 调用序列 | shader define |
|---|---|---|
| Update（RT） | `SharcInit()` → hit:`SharcUpdateHit()`（false 可提前终止）→ miss:`SharcUpdateMiss()` → 选新方向后 `SharcSetThroughput()`（之后 throughput 重置 1） | `SHARC_UPDATE 1` |
| Resolve（compute） | 每 entry `SharcResolveEntry(accumulatedFramesMax, staleFrameNumMax)` | — |
| Render/Query（RT） | 每个可用 hit（排除 primary）`SharcGetCachedRadiance()`，成功即终止 | `SHARC_QUERY 1` |

HashGrid：`HashGridParameters{cameraPosition, logarithmBase=SHARC_GRID_LOGARITHM_BASE, sceneScale, levelBias=SHARC_GRID_LEVEL_BIAS}`；debug：`HashGridDebugColoredHash()`/`HashGridDebugOccupancy()`/`GetVoxelSize()`。

其他 define：`SHARC_USE_FP16`、`SHARC_ENABLE_64_BIT_ATOMICS`、`SHARC_ENABLE_SH_ENCODING`、`SHARC_ENABLE_RESPONSIVE_LIGHTING`、`SHARC_PROPAGATION_DEPTH`(默认 2)、`SHARC_RADIANCE_SCALE`、`SHARC_STALE_FRAME_NUM_MIN`。

Query 注意：path segment 长度 < `GetVoxelSize()` 时继续 trace；specular 用 cone-spread 判定 `2·len·sqrt(0.5·a²/(1−a²))`（a = roughness²）。

## 4. Buffer 布局（必须对齐官方）

| Buffer | 默认 stride | SH encoding |
|---|---:|---:|
| Hash entries | 8B (uint64) | 8B |
| Accumulation | 16B | 32B |
| Resolved | 16B (`float16_t4`) | 24B |

`2^22` entries：默认 160 MiB / SH 256 MiB。lock buffer 仅在 `SHARC_ENABLE_64_BIT_ATOMICS 0` 时使用（现有 `FSharcState::lockBuffer` 正好接上）。改 `BasicTypes.slang` 结构体 stride，C++ 端 `EnsureSharcResources` 的 `sizeof(...)` 自动跟随。

## 5. 核心风险（Phase 1 必须先解决）

官方头是 HLSL `RWStructuredBuffer` + `SharcParameters` 句柄风格；本引擎 shader 是 **Slang + BDA 指针**，不走 descriptor 绑定。

**路线 A（已选）**：Slang include HLSL 头，把官方 `SharcParameters` 里的 `RWStructuredBuffer` 成员换成 Slang 指针（`uint64_t*` / 结构体指针），用 `Bindless.GetGpuscene().Sharc` 的 BDA 喂进去。

Phase 1 compile spike 必须验证：Slang 能 include 官方头、`RWStructuredBuffer`→BDA 包装可行、`Interlocked*` 映射到 SPIR-V atomics、`float16_t` 与 16-bit storage/arithmetic capability 在 SPIR-V 下可用、64-bit atomic（或退回 lock buffer）。**这关不过，后面全部阻塞——所以第一步只攻这个。**

## 6. 执行顺序（按此推进，每步有验收门）

> 详细任务展开见 `sharc-official-migration-plan.md` §Phase 1–6。每个 Phase 编译/截图通过再进下一个。

1. **Phase 1 — Vendor + Compile Spike**（先做）：头入 `assets/shaders/third_party/sharc/`；写 Slang→BDA 包装；新增 `Util.SharcCompileTest.comp.slang` 只 include + 调一两个函数；CMake/hot reload 加 16-bit/fp16 参数。验收：`./gnb.bat build gkNextRenderer --reconfigure` 通过且生成 `.spv`。
2. **Phase 2 — 布局与资源对齐**：buffer 结构体改官方 stride（§4）；`SharcParameters` 承载官方 `HashGridParameters`；CVar 对齐官方语义（`sceneScale/levelBias/accumulatedFramesMax/staleFrameMax`）。验收：on/off 都能启动，validation layer 干净。
3. **Phase 3 — Resolve**：`Core.SharcResolve.comp.slang` 调 `SharcResolveEntry()`。验收：空 cache 不崩，stale 驱逐可 readback 抽验。
4. **Phase 4 — Update**：`Core.SharcUpdate.comp.slang` 重写 path loop（`SharcInit/UpdateHit/Miss/SetThroughput`）；稀疏采样每 5×5 取 ~1 像素并帧间抖动。验收：occupancy 随帧增长，关 Query 时画面≈原 path tracer。
5. **Phase 5 — Query**：`Core.SharcQuery.comp.slang` 调 `SharcGetCachedRadiance()`，命中终止；segment/specular 判定见 §3。验收：相同 sample 噪声更低或更省，无明显漏光/ghosting。
6. **Phase 6 — 质量/debug**：接 `HashGridDebugColoredHash`/`HashGridDebugOccupancy`/bounce heatmap；评估 SH encoding / responsive lighting；occupancy 调参（静态相机 10–20%）。

## 7. 完成定义（DoD）

- [x] targeted build 通过：`./gnb.bat build gkNextRenderer --reconfigure`、`./gnb.bat build gkNextUnitTests`
- [x] `r.sharc.enable=true` 后 PathTracing 跑官方 Update/Resolve/Query，playground 稳定渲染
- [x] debug view 显示 occupancy（`r.sharc.debugMode=3`，官方 `HashGridDebugOccupancy`）
- [x] off/on 可重启切换，默认 off
- [ ] GPU timer 证明性能或噪声至少一项收益
- [x] 自研 `Sharc.slang` 算法已替换为官方 adapter；`sharc-integration-plan.md` 标注废弃并指向本 runbook
- [x] 更新本文件 front-matter `status`

## 8. 参考

- 官方库：https://github.com/NVIDIA-RTX/SHARC
- 集成指南：https://github.com/NVIDIA-RTX/SHARC/blob/main/docs/Integration.md
- RTXGI：https://github.com/NVIDIA-RTX/RTXGI
