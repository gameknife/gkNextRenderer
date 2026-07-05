---
title: "SHARC 集成 — 剩余工作清单"
category: plan
status: 官方 stateful Update 已接入，剩余质量/指标收尾
owner: engine
created: 2026-06-16
last_updated: 2026-06-16
related: docs/plans/sharc-integration-plan.md
---

# SHARC 集成 — 剩余工作清单

> 现状：原计划 Phase 1–5 曾用**简化自研版本**落地；2026-06-16 已切到官方 NVIDIA-RTX/SHARC adapter（官方头 vendored、Slang/BDA 包装、官方默认 buffer stride、Update/Resolve/Query、occupancy debug 均可编译运行）。
> 本清单只列**尚未完成**的工作，按优先级分组。

## 0. 代码审查发现（2026-06-16，按严重度）

> 审查范围：`Sharc.slang` adapter、三个 pass shader、`PathTracingRenderer.slang` path loop、`PathTracingRenderer.cpp` 资源/调度、官方 vendored 头、设备特性。
> **已接对的部分**（无需改）：官方头完整 vendored 且 `GK_ENABLE_OFFICIAL_SHARC` 在 CMake + 热重载两处定义；buffer 布局与官方对齐（8/16/16/48，有 `static_assert`）；`kSharcProbePadding=16` 正确覆盖 resolve 线性探测越界；lock-buffer 路径 + 清零；设备 `shaderFloat16` + `storageBuffer16BitAccess` 已按支持开启；pass 顺序 Update→Resolve→Query 带 buffer barrier；debug mode 3/4 已接。

### P0 — 主缺陷：Update 没有真正跑 SHARC 算法

- [x] **path loop 贯穿单个 `SharcState`**（解锁 SHARC 真正收益的关键）。当前 `Sharc.slang` 的 `SharcAccumulate` 是**无状态**的：每次 `SharcInit` 新建 state、只 `SharcUpdateHit` 一次就丢弃，`pathLength` 恒为 0，backprop 循环永不执行。后果：`SharcSetThroughput` / `SharcUpdateMiss` / 多顶点反向传播全部未用，缓存只存"各 voxel 处直接光"，**多 bounce 间接光不被传播**——SHARC 降多 bounce 成本的核心价值没拿到，只借用了哈希网格+打包+resolve。
  - 正确做法：在 `PathTracingRenderer.slang` path loop 里——路径起点 `SharcInit`，每个 diffuse bounce `SharcUpdateHit`，每次采样方向后 `SharcSetThroughput`（随后 throughput 重置 1），miss 时 `SharcUpdateMiss`；把 `SharcAccumulate` 这个无状态 helper 废弃。
  - 2026-06-16：已改为每条 path 持有单个 `SharcState`，在 bounce 间调用 `SharcUpdateHit` / `SharcSetThroughput` / `SharcUpdateMiss`；`SharcAccumulate` 仅保留 compile-test 兼容壳。

### P1 — 具体 bug

- [x] **Resolve 的 `cameraPositionPrev` 传了当前帧相机**（`Sharc.slang:110-113` 用当前 `eye`）。官方 `SHARC_BLEND_ADJACENT_LEVELS` 靠 `cameraOffset = 当前 - 上一帧 > 0` 触发，现在恒为 0 → **相邻层混合永不触发**，相机移动时缓存复用失效、popping/滞后。应缓存并传上一帧相机位置。
  - 复审确认（二轮）：改用 `runtime.CameraPositionPrev`，C++ `UpdateSharcParameters` 经 `lastCameraPosition` 跟踪上一帧（`:191-197`），`static_assert(...==64)` 同步更新。✓
- [x] **Update pass 写输出图像且与 Query 之间无 image barrier**。`Render()` 末尾 `OutSingleDiffuse.Store` 等（`PathTracingRenderer.slang:494-497`）、`PrimaryHit` 写 G-buffer；Update 与 Query 两 dispatch 写同一批图像，中间只有 SHARC **buffer** barrier → 重叠像素 **WAW 竞争**（sync validation 会报警）+ 纯浪费。Update pass 应只填缓存、不写可见输出。
  - 复审确认（二轮）：新增 `WriteOutputs` 标志，Render() 与 PrimaryHit 的**所有** image store 均已包住（`:366/581/599/621/635`），`Core.SharcUpdate` 设 `WriteOutputs=false`，update pass 不再碰任何图像。✓
- [ ] **缓存仅在 `entriesPow2` 变化时清零**（`EnsureSharcResources` 设 `pendingClear`）。场景重载 / 材质 / 光照大变化不清 → 残留旧 GI（鬼影、错误亮度）。需在 scene reload / 光照突变时 invalidate。
  - 2026-06-16：已覆盖 scene reload / frame counter 回退，以及 sun / sky 参数变化时的强制 clear；材质 / 自发光突变仍需要接入明确 dirty 信号。

### P2 — 次要 / 待确认

- [x] `Sharc.slang:16-17` 把 `SHARC_UPDATE` 与 `SHARC_QUERY` 恒定义为 1 → 三个 pass 都编译 update 路径，query pass 的 `SharcState` 带无用数组。无害但不规范、增大编译。
  - 2026-06-16：已改为 per-pass 编译宏；`Core.SharcUpdate` 只开 update，`Core.SharcQuery` 只开 query，`Core.SharcResolve` 两者都关，hot reload 与 CMake 同步。
- [x] 稀疏采样用逐像素 `RandomFloat > ratio`（`Render():338`），非官方建议的 5×5 block 分层抖动 → 覆盖不均、收敛慢。
  - 2026-06-16：已改为 5×5 tile slot 分层采样，并按帧号旋转覆盖。
- [ ] host-visible `SharcParameters` 每帧 memcpy 重写，无 frames-in-flight 多缓冲 → in-flight>1 时 CPU 可能覆盖 GPU 在读的上一帧（影响轻，`FrameIndex` 会错位）。
- [ ] slangc 命令未显式传 `-enable-16bit-types` 类 flag（`CMakeLists:153`）；看似靠 Slang 自动 capability + 设备特性可跑，确认 validation layer 对 16-bit storage 无报警。

### 复审二轮新发现（2026-06-16，stateful Update 改写引入）

> P0/P1 经复审确认修复正确（`SharcState` 贯穿，`UpdateHit→SetThroughput→UpdateHit` 顺序与官方 loop 一致）。以下为改写新引入项。

- [x] **N1（中）— 直接光模型不一致，命中/未命中边界亮度跳变。** Update pass 中间顶点用 `EvaluateDirectLighting → directIllum.DirectIlluminate`（全光源）写入缓存；Query/Render pass 未命中、继续 trace 的中间 bounce 用另一套显式 sun-cone 模型（`PathTracingRenderer.slang:518-532`，不受 `EnableSharcUpdate` 约束）。两套光照喂同一张图 → 缓存命中区 vs 未命中区 GI 不一致。且 sun-cone 块在 update pass 里冗余执行（改 RayColor 后 `break`，既不入缓存也不写图像，纯浪费）。**修法：update 与 render 的中间顶点直接光统一为同一函数。** 开缓存前优先解决，否则边界有可见接缝。
  - 2026-06-16：SHARC Query 的未命中中间顶点改用 `EvaluateDirectLighting`；Update pass 跳过旧 sun-cone 终结块，普通非 SHARC path tracing 保持原行为。
- [ ] **N2（低，需验证）— 自发光终结的吞吐记账。** 自发光命中走 `SharcUpdatePathHit(lightVertex, terminalRadiance)`（`:421-424`），终结分支前无 `SetThroughput`，且 `terminalRadiance=outAlbedo` 同时被 `GetRayColor` 乘进 RayColor（`:314`）。天空 miss 在 albedo-on-arrival 约定下正确；自发光这条建议做 update-only vs 参考路径 A/B，确认沿途 voxel 无偏亮/偏暗。
- [ ] **N3（低）— Update 成本。** 每个 update 顶点都调 `DirectIlluminate`（投影阴影射线），update dispatch 仍是全分辨率 8×8，靠 5×5 分层采样早退。GPU timer 实测时重点看 update pass 占比。

## A. 与官方 NVIDIA SHARC 的差距（决策项）

> ✅ **决策已定（2026-06-16）：切官方库 NVIDIA-RTX/SHARC。**
> 详细迁移计划见 [`sharc-official-migration-plan.md`](sharc-official-migration-plan.md)。
> 下面 B / C 两组属于自研版的修补，**改官方库后大多被 §A 迁移取代**，仅留作对照。

- [x] 决策：自研缓存 vs 接入官方 NVIDIA-RTX/SHARC → **选官方**
- [x] Phase 1：vendor 官方头到 `assets/shaders/third_party/sharc/` + compile spike（见迁移计划 §Phase 1）
- [x] Phase 1：Slang `RWStructuredBuffer`→BDA 包装 + `Interlocked*`/`float16_t`/64-bit atomic 验证（当前默认走 lock buffer，未启用 64-bit atomic）
- [x] Phase 1：`assets/CMakeLists.txt` 和 `ShaderHotReloader.cpp` 加官方 SHARC shader define，确认 hot reload 依赖覆盖 third_party

## B. （已被官方库取代）自研实现的正确性 / 健壮性缺口

> 改官方库后由 SharcCommon.h 自带解决，无需单独修复；保留仅供回溯。

- [ ] ~~哈希冲突处理~~ → 官方多级哈希网格自带
- [ ] ~~throughput 传播~~ → 官方 `SharcSetThroughput` / `SharcUpdateHit` 自带（计划 §2.2）
- [ ] double-counting 验证：确认 sun/emissive 在 update 累加与 query early-out 之间不重复计入（计划 §3.3）
- [ ] 多分辨率体素：按相机距离做 level bias / voxel scale，替换单一 `VoxelSize`
- [ ] normal 阈值改 CVar：`Sharc.slang:136` 的 `0.35f` 硬编码改为 `r.sharc.queryNormalThreshold`
- [ ] 辐射编码范围：当前 clamp [0,64] + 固定 1024 缩放，亮 GI 会截断；评估更大范围或 RGB9E5/对数编码
- [ ] 清理死资源：`LockBuffer` 已分配但未使用 —— 要么接入锁逻辑，要么移除

## C. Phase 6 质量与响应性（中优先级）

- [ ] 光照变化的 cache invalidation / 加速 stale（sun / sky / emissive 变化）
- [ ] 评估并接入 `SHARC_ENABLE_SH_ENCODING`（方向性编码）
- [ ] 评估并接入 `SHARC_MATERIAL_DEMODULATION`（保留高频 albedo）
- [ ] 评估 `SHARC_ENABLE_RESPONSIVE_LIGHTING`
- [x] 扩展 debug view：hash occupancy heatmap（`r.sharc.debugMode=3`，官方 `HashGridDebugOccupancy`）
- [x] 扩展 debug view：radiance cache 实色马赛克（`r.sharc.debugMode=4`）
- [x] 扩展 debug view：stale / sample-count heatmap（`r.sharc.debugMode=5`，cache table：R=stale，G=sample count，B=accumulated frames）
- [ ] 在 editor / visual debugger 暴露 debug view 切换

## D. 验证与指标（收尾，MVP 必需）

- [ ] 指标 readback：entry occupancy、collision rate、query hit rate
- [ ] GPU timer 性能对比：Update / Resolve / Query 各 pass 耗时，SHARC on vs off
- [ ] 噪声 / 收益证明：相同 frame budget 下噪声对比，或相同画质下 sample/bounce 降幅
- [ ] 画质验证截图：
  - [x] `./gnb.bat shot --scene assets/models/playground.glb --frames 120/180`（off/on 均通过）
  - [x] `./gnb.bat shot --scene assets/models/playground.glb --frames 120`（`r.sharc.debugMode=5` stale/sample/frame heatmap 可读）
  - [ ] `./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300`
  - [ ] 室内薄墙场景漏光 / ghosting 肉眼检查
- [x] 构建验证：`./gnb.bat build gkNextRenderer --reconfigure` 与 `./gnb.bat build gkNextUnitTests` 通过

## E. 文档收尾

- [x] 更新 `sharc-integration-plan.md` front-matter：`status` 从「待实现」改为实际状态，更新 `last_updated`
- [x] 在计划文档中记录「自研简化版」与官方 SHARC 的差异，避免后续混淆

## MVP 验收对照（计划 §8）

| # | 条目 | 状态 |
|---|---|---|
| 1 | enable 后 PathTracing 多出 Update/Resolve/Query | ✅ 已完成 |
| 2 | playground 稳定渲染 | ✅ off/on 截图验证通过 |
| 3 | Debug view 显示 cache hit/occupancy | ✅ occupancy debug 完成 |
| 4 | off/on 可热切或重启切换 | ✅ 已完成 |
| 5 | GPU timer 证明性能/噪声至少一项收益 | ⬜ timer 在，stateful Update 已接入；仍需补正式 on/off 指标与噪声对比 |
| 6 | `gnb build gkNextRenderer gkNextUnitTests` 通过 | ✅ 分目标验证通过 |
