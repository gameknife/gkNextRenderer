---
title: "双平面 Visibility Buffer"
category: design
status: 已实现
owner: engine
created: 2026-07-30
last_updated: 2026-08-02
related_plan: ../plans/massive-rendering-mode-plan.md
---

# 双平面 Visibility Buffer

## 结论

Visibility ID 采用全局唯一格式，不引入 Massive 模式、启动选项或 shader variant：

- instance plane：`R32_UINT`，保存 one-based render proxy index，`0` 表示背景；
- triangle plane：`R16_UINT`，保存 section-local triangle index；
- shader 逻辑统一使用 `VisibilityId { uint instanceIdx; uint triangleIdx; }`；
- primitive stream 仍是连续的两个 `uint`，避免扩大为另一种条件格式；
- 当前工程容量为 131072 render proxies，容量单位不是逻辑 scene node；
- 单 section 最多 65535 个三角形，model loader 继续按此边界切 section。

像素存储共 6 bytes/pixel。instance 与 triangle 分开，可让需要实例信息的调试或后续 pass 单独访问
R32 plane，同时避免 `R32G32_UINT` 的 8 bytes/pixel 成本。

## 统一访问契约

`assets/shaders/Shader/VisibilityBuffer.slang` 是唯一的编解码入口，负责：

- primitive stream 的 `VisibilityId` load/store；
- 两张 visibility image 的像素 load；
- 背景有效性判断与 `uint2` 适配。

业务 shader 不允许再手写 bit mask 或 attachment slot。attachment 格式与 storage/draw slot 则集中在
`VisibilityBufferLayout.hpp`；render pass、framebuffer、render-target 创建和 copy 都遍历同一描述表。

## 容量与失败语义

`MAX_RENDER_PROXIES` 与 `Scene::kRenderProxyCapacity` 固定为 131072。Scene 在任何 GPU upload 前检查：

- 最终 `NodeProxy` 数不能超过容量；
- 每个 visible-item slot 的累计数量不能超过容量；
- 超限时抛出带实际值和容量值的异常，不允许覆盖相邻 GPU scene 区域。

Visibility ID 的 32-bit instance 字段不再是当前瓶颈，但硬件 RT 的 custom instance index 仍只有 24 bit；
若未来把工程容量继续提高到该边界以上，需要同步设计 RT 映射或分页。

## 验证场景

- `AsteroidBelt.proc` 保持 30000 个实例，作为日常轻量回归；
- `MassiveAsteroidBelt.proc` 创建 70000 个单-section asteroid 实例；
- `visibility-buffer-70k.agentscript.json` 断言 render proxy 数为 70000，并要求本帧最大可见 proxy index
  达到 65536 以上，从而证明测试覆盖了旧 15-bit 上限，而不只是成功创建节点。
