---
title: "Visibility Buffer 高实例容量改造计划"
category: plan
status: 已实施
owner: engine
created: 2026-07-30
last_updated: 2026-08-02
design: ../designs/massive-visibility-buffer-design.md
---

# Visibility Buffer 高实例容量改造计划

## 实施原则

采用一条全局路径，不保留旧 packed 格式，也不增加 Massive/default 模式：

1. 建立共享 `VisibilityId` 和唯一 load/store helper；
2. primitive stream 改为两个 `uint`，instance 与 triangle 不再竞争位宽；
3. visibility pass 输出 `R32_UINT + R16_UINT` 两个 attachment；
4. render target、framebuffer、render pass 与 copy 由同一 plane 表驱动；
5. 工程 render-proxy 容量统一提高为 131072，并在 CPU upload 前做显式上限检查；
6. 增加 70000-instance 场景以及超过 65535 可见 proxy 的 GPU 诊断断言。

## 影响范围

- Shader：Expand、visibility、wireframe、shadow、surface reconstruction、scene sampling、visual debugger；
- Vulkan：多 color attachment render pass/framebuffer、blend attachment 数组、双 plane 创建和 copy；
- Scene：NodeProxy/visible-item/TLAS 容量、primitive allocation、model section 三角形边界；
- Demo 与验证：`MassiveAsteroidBelt.proc`、agent query、70k agentscript；
- 文档与 NextTotalwar 的 render-proxy 预算说明。

## 验收标准

- shader 源码中不再存在 visibility 的 `15/17` bit 编解码；
- `gkNextRenderer` 与 `gkNextUnitTests` 构建通过，单元测试通过；
- 原有轻量场景可正常渲染；
- 70k 场景成功上传并稳定运行；
- 自动脚本证明 `scene.renderProxyCount == 70000` 且 `scene.maxVisibleProxyIndex >= 65536`；
- 超过 131072 render proxies 时在 CPU 侧明确失败，不发生 buffer overwrite。
