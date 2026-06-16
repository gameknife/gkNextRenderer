---
title: "SHARC 集成 — 剩余工作清单"
category: plan
status: 进行中
owner: engine
created: 2026-06-16
last_updated: 2026-06-16
related: docs/plans/sharc-integration-plan.md
---

# SHARC 集成 — 剩余工作清单

> 现状：原计划 Phase 1–5 已用**简化自研版本**落地（CVar / 资源 / Update / Resolve / Query 三 pass / barrier / GPU timer / on-off 切换 / 基础 hit-miss debug 均已接入 `PathTracingRenderer`）。
> 本清单只列**尚未完成**的工作，按优先级分组。

## A. 与官方 NVIDIA SHARC 的差距（决策项）

> ✅ **决策已定（2026-06-16）：切官方库 NVIDIA-RTX/SHARC。**
> 详细迁移计划见 [`sharc-official-migration-plan.md`](sharc-official-migration-plan.md)。
> 下面 B / C 两组属于自研版的修补，**改官方库后大多被 §A 迁移取代**，仅留作对照。

- [x] 决策：自研缓存 vs 接入官方 NVIDIA-RTX/SHARC → **选官方**
- [ ] Phase 1：vendor 官方头到 `assets/shaders/third_party/sharc/` + compile spike（见迁移计划 §Phase 1）
- [ ] Phase 1：Slang `RWStructuredBuffer`→BDA 包装 + `Interlocked*`/`float16_t`/64-bit atomic 验证
- [ ] Phase 1：`assets/CMakeLists.txt` 和 `ShaderHotReloader.cpp` 加 16-bit / fp16 参数，确认 hot reload 不受影响

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
- [ ] 扩展 debug view：hash occupancy heatmap、stale / sample-count heatmap（现仅 hit=1 / miss=2）
- [ ] 在 editor / visual debugger 暴露 debug view 切换

## D. 验证与指标（收尾，MVP 必需）

- [ ] 指标 readback：entry occupancy、collision rate、query hit rate
- [ ] GPU timer 性能对比：Update / Resolve / Query 各 pass 耗时，SHARC on vs off
- [ ] 噪声 / 收益证明：相同 frame budget 下噪声对比，或相同画质下 sample/bounce 降幅
- [ ] 画质验证截图：
  - [ ] `./gnb.bat shot --scene assets/models/playground.glb --frames 300`
  - [ ] `./gnb.bat shot --scene assets/models/GIBootcampLarge.proc --frames 300`
  - [ ] 室内薄墙场景漏光 / ghosting 肉眼检查
- [ ] 构建验证：`./gnb.bat build gkNextRenderer gkNextUnitTests` 通过

## E. 文档收尾

- [ ] 更新 `sharc-integration-plan.md` front-matter：`status` 从「待实现」改为实际状态，更新 `last_updated`
- [ ] 在计划文档中记录「自研简化版」与官方 SHARC 的差异，避免后续混淆

## MVP 验收对照（计划 §8）

| # | 条目 | 状态 |
|---|---|---|
| 1 | enable 后 PathTracing 多出 Update/Resolve/Query | ✅ 已完成 |
| 2 | playground 稳定渲染 | ⬜ 未验证 |
| 3 | Debug view 显示 cache hit/miss | ✅ 基础版完成 |
| 4 | off/on 可热切或重启切换 | ✅ 已完成 |
| 5 | GPU timer 证明性能/噪声至少一项收益 | ⬜ timer 在，收益未证明 |
| 6 | `gnb build gkNextRenderer gkNextUnitTests` 通过 | ⬜ 未验证 |
