---
title: "SHARC Spatially Hashed Radiance Cache — 可行性评估与开发计划"
category: plan
status: 官方MVP已接入
owner: engine
created: 2026-06-05
last_updated: 2026-06-16
---

# SHARC Spatially Hashed Radiance Cache — 可行性评估与开发计划

> 状态：原自研简化版计划已废弃；2026-06-16 已按 [`sharc-official-handoff.md`](sharc-official-handoff.md) 接入官方 NVIDIA-RTX/SHARC MVP。
> 当前仍需继续验证性能/噪声收益、完整 throughput path-loop 与高级质量项，详见 [`sharc-integration-remaining-work.md`](sharc-integration-remaining-work.md)。
> 目标：在 gkNextEngine 的 `PathTracing` 路径中实验接入 NVIDIA RTX SHARC（Spatially Hashed Radiance Cache），用 world-space radiance cache 降低多 bounce path tracing 成本。
> 非目标：不替换现有 AmbientCube GI；不把 SHARC 作为 `SoftwareModern` / `SoftwareTracing` 的首版默认 GI。

## 0. 结论

SHARC 在 gkNextEngine 中**可行性中高**，但最合适的落点是 `ERT_PathTracing` 专用实验路径。

原因：

- gkNextEngine 当前 `PathTracingRenderer` 已经是 compute shader + Vulkan ray query + TLAS，不使用 ray pipeline，和 SHARC shader-only 的集成方式匹配。
- `FPathTracingRenderer` 的 path loop 已集中在 `assets/shaders/common/PathTracingRenderer.slang`，SHARC 的 update/query 插桩可以集中改这里。
- 引擎已经普遍用 buffer device address（BDA）和 `GPUScene` push constant 传 GPU 资源地址，适合传 SHARC 的 hash / accumulation / resolved buffers。
- 现有 AmbientCube 已经承担 realtime raster / software tracing GI，SHARC 不应该首版替代它；两者目标不同。

首版建议做成：

```text
PathTracing:
  SHARC Update pass   -> 稀疏 path tracing 更新 radiance cache
  SHARC Resolve pass  -> 合并 accumulation 到 resolved cache，清理 stale entry
  SHARC Query pass    -> 主 path tracing，在非 primary hit 查询 cache 并 early-out
  ReProject pass
  Compose pass
```

## 1. 背景

SHARC 官方库是 shader-only radiance cache。典型集成包含三类 render-time pass：

| Pass | 作用 |
|---|---|
| Update | 稀疏追踪一部分 path，把新观测到的 radiance 写入 accumulation cache |
| Resolve | 遍历 cache entry，合并本帧 accumulation 和历史 resolved data，处理 stale eviction |
| Render / Query | 主渲染 pass 正常追踪，但在合适的非 primary hit 调用 cached radiance，命中后 early-out |

官方要求：

- Hash entries、accumulation、resolved buffers，三者 entry 数一致并初始化为 0。
- pass 之间要有 UAV / buffer memory barrier。
- shader 需要 native fp16；HLSL/DXC 文档要求 `-enable-16bit-types`，Slang/SPIR-V 路径需要单独验证等价编译参数。
- 推荐初始容量通常为 `2^22` entries；启用 directional SH encoding 时显存更高。

参考：

- <https://github.com/NVIDIA-RTX/SHARC>
- <https://github.com/NVIDIA-RTX/SHARC/blob/main/docs/Integration.md>
- <https://github.com/NVIDIA-RTX/RTXGI>

## 2. 当前引擎接入点

### 2.1 PathTracing 渲染路径

当前 `PathTracingRenderer` 创建三个 pass：

- `assets/shaders/Core.PathTracing.comp.slang.spv`
- `assets/shaders/Process.ReProject.comp.slang.spv`
- `assets/shaders/Process.DenoiseJBF.comp.slang.spv`

位置：

- `src/Engine/Rendering/PathTracing/PathTracingRenderer.cpp`
- `src/Engine/Rendering/PathTracing/PathTracingRenderer.hpp`

适合改为：

- `Core.SharcUpdate.comp.slang`
- `Core.SharcResolve.comp.slang`
- `Core.SharcQuery.comp.slang` 或复用 `Core.PathTracing.comp.slang` 通过 define 切 permutation
- 维持现有 reproject / compose

### 2.2 Path loop 插桩点

核心逻辑在：

- `assets/shaders/common/PathTracingRenderer.slang`

关键函数：

- `FPathTracingRenderer::PrimaryHit()`
- `FPathTracingRenderer::GetRayColor()`
- `FPathTracingRenderer::Render()`

SHARC 需要插入的位置：

- 每条 sampled path 初始化：`SharcInit()`
- 每个 hit：构造 `SharcHitData`，调用 `SharcUpdateHit()` 或 `SharcGetCachedRadiance()`
- miss：调用 `SharcUpdateMiss()`
- path segment throughput 更新：在选择下一条 ray 后调用 `SharcSetThroughput()`
- direct lighting：当前 `directIllum.DirectIlluminate()` 只在 primary hit 后加一次；SHARC update 需要明确每个缓存 hit 的 direct lighting 定义，避免重复计入 sun / emissive。

### 2.3 GPU 资源传递

当前 `GPUScene` 已经固定为 128B push constant，并有 `static_assert(sizeof(Assets::GPUScene) == 128)`。不能直接追加多个 SHARC 地址。

推荐仿照 AmbientCube：

```c
struct SharcResources
{
    uint64_t HashEntries;
    uint64_t LockBuffer;
    uint64_t Accumulation;
    uint64_t Resolved;
    uint64_t Parameters;
    uint64_t Reserved0;
    uint64_t Reserved1;
    uint64_t Reserved2;
};
```

然后：

- 优先复用 `GPUScene.ReservedAddress0` 指向 `SharcResources`。
- 或重命名为更通用的 `RendererResources`，但这会扩大改动面，不建议首版做。
- shader 侧提供 `Bindless.GetGpuscene().SharcResources` property。

## 3. 主要风险

### 3.1 Slang 适配风险

SHARC 官方 header 偏 HLSL/DXC 宏风格，包含：

- `RWStructuredBuffer`
- `Interlocked*`
- `float16_t` / fp16 类型
- 可能的 64-bit key / atomic 相关宏

gkNextEngine 的 shader 是 Slang module/import 体系，当前编译命令是：

```text
slangc <shader> -o <out.spv> -entry main -target spirv
```

必须先做 compile spike，确认：

- SHARC headers 能被 Slang include。
- `RWStructuredBuffer` 可替换为 BDA pointer / structured buffer wrapper。
- `Interlocked*` 映射到 Slang/SPIR-V atomics。
- `-enable-16bit-types` 或 Slang 对应选项在 CMake 和 hot reload 中生效。

### 3.2 显存风险

初始建议：

| 配置 | Entry 数 | 预估用途 |
|---|---:|---|
| Low | `2^20` | compile / correctness / Steam Deck 类设备实验 |
| Medium | `2^21` | 默认实验配置 |
| High | `2^22` | 桌面质量对比 |

官方 baseline `2^22` 级别会占用约百 MiB 以上显存。首版必须通过 cvar 控制，不要硬编码高容量。

### 3.3 画质风险

需要重点看：

- cache leaking：薄墙、角落、normal hash 失配导致漏光。
- ghosting：相机快速移动、光照变化、动态物体移动后的历史残留。
- double counting：sun / emissive / sky radiance 被 direct lighting 和 cached radiance 重复计入。
- material detail loss：未启用 material demodulation 时，高频 albedo 可能被 cache 抹平。
- specular path：首版只对 diffuse / rough indirect 使用 SHARC，镜面和 dielectric 应保持 tracing。

## 4. 开发阶段

### Phase 1：Compile Spike

目标：验证 SHARC header 能在 gkNext 的 Slang/SPIR-V 管线里编译。

任务：

1. 将 SHARC shader header 放到 `assets/shaders/third_party/sharc/`。
2. 新增最小 shader：`assets/shaders/Util.SharcCompileTest.comp.slang`。
3. 定义 Slang 兼容宏，映射 `RW_STRUCTURED_BUFFER`、`BUFFER_AT_OFFSET`、`Interlocked*`。
4. 给 `assets/CMakeLists.txt` 和 `ShaderHotReloader.cpp` 增加 SHARC shader 的 16-bit 编译参数。
5. 编译 `gkNextShaders` 或目标构建，确认 `.spv` 生成。

验收：

- `./gnb.bat build gkNextRenderer` 通过。
- `Util.SharcCompileTest.comp.slang.spv` 生成。
- 不影响现有 shader hot reload。

### Phase 2：资源与 CVar

目标：引擎侧分配 SHARC buffers，并传入 shader。

任务：

1. 增加 `SharcResources` GPU-visible struct。
2. 在 `PathTracingRenderer` 或 `VulkanBaseRenderer` 的 path tracing resource group 中创建：
   - hash entries buffer
   - lock buffer
   - accumulation buffer
   - resolved buffer
   - optional parameter buffer
3. 用 cvar 控制：
   - `r.sharc.enable`
   - `r.sharc.entriesPow2`
   - `r.sharc.updateSampleRatio`
   - `r.sharc.debugMode`
4. scene reload / renderer switch / resize 时保证资源生命周期正确。
5. 初次分配和 scene/material/light 大变化时清零。

验收：

- SHARC on/off 都能启动 `gkNextRenderer`。
- RenderDoc / validation layer 无 descriptor / barrier 错误。
- 日志打印容量和显存占用。

### Phase 3：Resolve Pass

目标：实现独立 SHARC resolve compute pass。

任务：

1. 新增 `assets/shaders/Core.SharcResolve.comp.slang`。
2. 每个 dispatch thread 处理一个 cache entry，调用 `SharcResolveEntry()`。
3. 在 Update -> Resolve -> Query 之间插入 buffer memory barriers。
4. 支持 stale eviction 参数。

验收：

- 空 cache resolve 不崩溃。
- hash entry 清空 / stale 逻辑可通过 debug counter 或 readback 抽样验证。

### Phase 4：Update Pass

目标：稀疏追踪路径并填充 SHARC accumulation。

任务：

1. 基于当前 `Core.PathTracing.comp.slang` 创建 `Core.SharcUpdate.comp.slang`。
2. 在 `FPathTracingRenderer` 增加 update mode：
   - primary hit 后进入 path loop
   - 对 diffuse / non-specular bounce 调用 `SharcUpdateHit()`
   - miss 调用 `SharcUpdateMiss()`
   - 每段 bounce 后处理 throughput
3. 用 blue noise / frame index 控制稀疏采样比例。
4. 暂不启用 responsive lighting / material demodulation，先做基本 radiance cache。

验收：

- cache occupancy 随帧数增长。
- 关闭 Query 时画面和原 path tracing 基本一致。
- GPU timer 中 update pass 成本可控。

### Phase 5：Query Pass

目标：主渲染 pass 查询 resolved radiance 并 early-out。

任务：

1. 增加 query mode：非 primary、diffuse/rough bounce 才允许 `SharcGetCachedRadiance()`。
2. 命中 cache 时：
   - `RayColor *= cachedRadiance`
   - path terminate
3. 对 specular / dielectric / low confidence cache 保持原 tracing。
4. 加 debug mode：
   - cache hit mask
   - cache miss mask
   - hash occupancy color
   - stale / sample count heatmap

验收：

- SHARC on 比 off 在相同 sample 下噪声更低或成本更低。
- 无明显漏光 / 大面积 ghosting。
- `gnb shot --scene assets/models/playground.glb --frames 300` 可稳定输出。

### Phase 6：质量与响应性

目标：解决实际场景中的稳定性和动态响应问题。

任务：

1. 调整 hash grid voxel scale / level bias。
2. 引入 normal threshold / roughness threshold。
3. 对 sun / sky / emissive 变化做 cache invalidation 或加速 stale。
4. 评估是否启用：
   - `SHARC_ENABLE_SH_ENCODING`
   - `SHARC_MATERIAL_DEMODULATION`
   - `SHARC_ENABLE_RESPONSIVE_LIGHTING`
5. 在 editor / visual debugger 中暴露 debug view。

验收：

- playground / GIBootcampLarge / 室内薄墙场景通过肉眼检查。
- 运动镜头下 ghosting 可接受。
- 性能收益明确，有 GPU timer 数据。

## 5. 验证计划

### 构建

按 AGENTS.md 的 targeted build：

```bash
./gnb.bat build gkNextRenderer gkNextUnitTests
```

若新增 shader 文件未被 glob 收录或改了 CMake：

```bash
./gnb.bat build gkNextRenderer gkNextUnitTests --reconfigure
```

### 快速截图

```bash
./gnb.bat shot --scene assets/models/playground.glb --frames 300
./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300
```

需要对 AmbientCube 交互影响做验证时，仍按 AmbientCube 文档使用更高帧数（例如 3000），因为 CPU voxelization + bake 需要时间收敛。

### 指标

记录：

- SHARC entry capacity / occupancy / collision rate。
- Update / Resolve / Query pass GPU time。
- Query hit rate。
- Same frame budget 下的噪声对比。
- Same visual quality 下的 sample count / bounce count 降幅。
- 显存占用。

## 6. 推荐默认实验配置

```text
r.sharc.enable = false
r.sharc.entriesPow2 = 21
r.sharc.updateSampleRatio = 0.25
r.sharc.queryMinBounce = 1
r.sharc.queryRoughnessMin = 0.35
r.sharc.enableSHEncoding = false
r.sharc.materialDemodulation = false
r.sharc.responsiveLighting = false
```

首版必须默认关闭，避免影响现有 renderer 行为。

## 7. 不建议首版做的事

- 不要替换 AmbientCube。
- 不要直接打开官方所有高级宏。
- 不要默认使用 `2^22` entries。
- 不要把 SHARC 插进 `SoftwareModern`，它没有真实 path segment 信息，收益和正确性都不如 PathTracing 明确。
- 不要改动 `GPUScene` 大小；保持 128B，使用二级资源表。

## 8. 最小可交付定义

一个可接受的 MVP：

1. `r.sharc.enable=true` 后 `PathTracing` 路径多出 Update / Resolve / Query。
2. playground 场景能稳定渲染。
3. Debug view 能显示 cache hit / miss。
4. SHARC off/on 可热切或重启后切换。
5. GPU timer 能证明性能/噪声至少一项有明确收益。
6. `./gnb.bat build gkNextRenderer gkNextUnitTests` 通过。
