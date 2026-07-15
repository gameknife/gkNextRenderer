# gkNextEngine 文档索引

> 本目录按类别组织：`guides/` 说明文档、`designs/` 设计方案、`plans/` 开发计划、`notes/` 随笔与复盘、`projects/` 子项目文档。
>
> 每篇文档头部带统一 frontmatter（`title / category / status / owner / created / last_updated`）。文中对源码的引用统一采用 `文件:行号` 可点击链接，配合 `gnb dashboard` 的 Docs 标签页可直接跳转到对应源码行。

**状态图例**：🟢 现行 · ✅ 已完成 · 🔵 进行中 · ⚪ 待实现 · 📝 草案 · 🗄 已归档

## 说明文档 · guides/

| 文档 | 状态 | 更新 |
| --- | --- | --- |
| [CLI Spec](guides/cli-spec.md) | 🟢 现行 | 2026-06-07 |
| [CMake 结构](guides/cmake-structure.md) | 🟢 现行 | 2026-05-29 |
| [SDR / HDR10 / EDR 输出模式](guides/display-output-modes.md) | 🟢 现行 | 2026-06-21 |
| [gnb 架构与代码导览](guides/gnb-architecture.md) | 🟢 现行 | 2026-06-12 |
| [gnb CLI](guides/gnb-cli.md) | 🟢 现行 | 2026-06-12 |
| [gnb 技术栈说明](guides/gnb-tech-stack.md) | 🟢 现行 | 2026-06-12 |
| [高 Token 效率的 Agent 工程开发方式](guides/token-efficient-agent-development.md) | 🟢 现行 | 2026-07-13 |
| [Gaussian Splat / SOG](../AGENT_GUIDE/GaussianSplat.md) | 🟢 现行 | 2026-06-18 |
| [SCAD 场景生成指引（AGENT 向）](guides/scad-scene-authoring-guide.md) | 🟢 现行 | 2026-06-12 |
| [Soft Mesh Shader GPU-Driven 提交路径](guides/soft-mesh-shader-gpu-driven-submit.md) | 🟢 现行 | 2026-06-07 |
| [TUI 终端模式](guides/tui-mode.md) | 🟢 现行 | 2026-06-25 |
| [TypeScript 整合说明](guides/typescript-integration.md) | 🟢 现行 | 2026-06-12 |

## 设计方案 · designs/

| 文档 | 状态 | 更新 |
| --- | --- | --- |
| [Agent 自动验证（输入驱动 + 断言）系统 — 设计与开发计划](designs/agent-validation-input-driver.md) | ⚪ 待实现 | 2026-06-09 |
| [AmbientCube 命中驱动探针残留（SHARC 式 insert/evict）— 设计方案与开发计划](designs/ambientcube-hit-driven-residency-design.md) | ✅ 已完成 | 2026-06-24 |
| [高斯溅射（SOG 加载 + GS 渲染模式）设计与开发计划](designs/gaussian-splatting-sog-design.md) | ✅ 已完成 | 2026-06-18 |
| [多视口渲染（RenderView：单窗口分区 + 离屏到纹理 + 多相机/缩略图）设计与开发计划](designs/multi-viewport-renderview-design.md) | 📝 草案 | 2026-06-26 |
| [NextAI 面向具体产品能力的目标架构](designs/nextai-product-focused-architecture.md) | 📝 草案 | 2026-07-15 |
| [NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 架构设计）](designs/nextra-rts-mvp-design.md) | 📝 草案 | 2026-06-26 |
| [SCAD 加载器（SCADLoader）设计与开发计划](designs/scad-loader-design.md) | ✅ 已完成 | 2026-05-30 |
| [SCAD Model Generator（SCAD Studio）设计与开发计划](designs/scad-model-generator-design.md) | ✅ 已完成 | 2026-06-07 |
| [SCAD 刚体骨骼角色（ScadRig）设计与开发计划](designs/scad-rig-design.md) | ✅ 已完成 | 2026-06-13 |
| [SwModernNoAmbient 天光遮蔽（屏幕空间 GTAO）设计与开发计划](designs/swmodern-noambient-sky-occlusion-design.md) | ✅ 已完成 | 2026-06-21 |
| [VoxelData 体素天光可见度（GPU Soft Tracing）设计与开发计划](designs/voxel-skyvisibility-soft-tracing-design.md) | ⚪ 待实现 | 2026-06-22 |
| [WebRTC 远程游玩（Remote Play）设计与开发计划](designs/webrtc-remoteplay-design.md) | 🔵 进行中 | 2026-06-08 |

## 开发计划 · plans/

| 文档 | 状态 | 更新 |
| --- | --- | --- |
| [AirportSim —— Jumbo Airport Story 风格机场生态观察 Demo（MVP 设计与开发计划）](plans/airport-sim-mvp-plan.md) | 🔵 进行中 | 2026-06-13 |
| [AmbientCube 显存占用降低 — 可行性评估与开发计划](plans/ambient-cube-memory-reduction.md) | ✅ 已完成 | 2026-06-07 |
| [DLSS 超分无抗锯齿 / scale 无收益 —— 根因定位与修复开发计划](plans/dlss-superres-no-aa-fix.md) | 📝 草案 | 2026-06-20 |
| [gkNextEditor MaterialEditor 扩展设计与开发计划](plans/gknexteditor-material-editor-expansion-plan.md) | ⚪ 待实现 | 2026-06-25 |
| [NextAI 面向具体产品能力的轻量化重构计划](plans/nextai-product-focused-refactor-plan.md) | ✅ 已完成 | 2026-07-15 |
| [gnb AI / Agent 统一控制面重构计划](plans/gnb-ai-agent-unification-refactor-plan.md) | 🗄 已取代 | 2026-07-15 |
| [Git 历史资产瘦身方案](plans/repository-history-asset-slimming-plan.md) | 📝 草案 | 2026-07-07 |
| [Release 前代码修缮：书写规范、结构卫生与明显错误清理](plans/release-code-polish-plan.md) | 📝 草案 | 2026-07-13 |
| [src/Engine 核心层精炼 Round 2：god class 拆解 + include 卫生](plans/engine-core-refactor-round2.md) | 📝 草案 | 2026-06-11 |
| [src/Engine 核心层精炼 Round 3：模块归位 + SkinnedMesh 瘦身](plans/engine-core-refactor-round3.md) | 📝 草案 | 2026-06-14 |
| [src/Engine 核心层精炼 Round 4：重回 30k —— 模块外移 + 写法压缩](plans/engine-core-refactor-round4.md) | 📝 草案 | 2026-07-09 |
| [src/Engine 核心层精简重构：分析与执行计划](plans/engine-core-refactor.md) | ✅ 已完成 | 2026-06-10 |
| [Engine 层精简重构计划](plans/engine-refactor-plan.md) | 📝 草案 | 2026-06-08 |
| [NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 开发计划）](plans/nextra-rts-mvp-plan.md) | 📝 草案 | 2026-06-26 |
| [NoAmbientDeferred TAA 抖动 + 过曝 —— 问题定位与修复计划（方案 A：实现真 TAA）](plans/noambient-deferred-taa-fix.md) | ✅ 已完成 | 2026-06-13 |
| [QuickJS 模块化迁移计划](plans/quickjs-module-migration.md) | ✅ 已完成 | 2026-06-12 |
| [ReProject 历史钳制黑点问题 — 根因分析与改进方案](plans/reproject-history-clamp-blackdot-fix.md) | 🚧 Phase A 已实现 | 2026-06-20 |
| [Remote MultiView 独立 ImGui 界面开发计划](plans/remote-multiview-imgui-isolation-plan.md) | 📝 草案 | 2026-07-03 |
| [Shader 编译迭代提速重构方案](plans/shader-compile-iteration-refactor-plan.md) | 📝 草案 | 2026-06-30 |
| [SHARC Spatially Hashed Radiance Cache — 可行性评估与开发计划](plans/sharc-integration-plan.md) | ⚪ 待实现 | 2026-06-07 |
| [SOG 高斯溅射与普通场景深度融合开发计划](plans/sog-scene-integration-plan.md) | 📝 草案 | 2026-07-04 |
| [StudioSim —— 游戏项目化迭代计划（向《游戏发展国》再进一步）](plans/studiosim-gameproject-iteration-plan.md) | 🔵 进行中 | 2026-06-08 |
| [StudioSim —— LLM 驱动的游戏工作室办公室模拟（MVP 开发计划）](plans/studiosim-mvp-plan.md) | ✅ 已完成 | 2026-06-07 |
| [StudioSim —— 产出与进度系统改进方案（打磨前结构性调整）](plans/studiosim-production-model-refinement.md) | ✅ 已完成 | 2026-06-08 |
| [StudioSim 重构 + 公共仿真层（Sim Kit）抽取与开发计划](plans/studiosim-refactor-simkit-plan.md) | 📝 草案 | 2026-06-13 |
| [TUI 终端渲染运行模式（隐藏窗口 + 终端逐帧刷新）设计与开发计划](plans/tui-terminal-rendering-plan.md) | ⚪ 待实现 | 2026-06-25 |
| [Remote Play 硬件编码改造计划（HW Texture → Vulkan Video → WebRTC）](plans/webrtc-remoteplay-hwencode-plan.md) | 🔵 进行中 | 2026-06-10 |
| [Vulkan + Renderer 专项精炼：命名归一 + 渲染器去重](plans/vulkan-renderer-refinement-plan.md) | 📝 草案 | 2026-06-15 |
| [Vulkan 后端精简与第三方包装评估（引入 vk-bootstrap）](plans/vulkan-backend-thirdparty-wrapper-plan.md) | 📝 草案 | 2026-06-15 |

## 随笔与复盘 · notes/

| 文档 | 状态 | 更新 |
| --- | --- | --- |
| [BrickPlayer / LDraw 技术总结（2026-03）](notes/brickplayer-ldraw-technical-summary.md) | 🗄 已归档 | 2026-03-21 |
| [Steam Deck 首次部署与编译复盘](notes/steamdeck-deployment-notes.md) | 🟢 现行 | 2026-05-29 |
| [thoughts](notes/thoughts.md) | 🟢 现行 | 2026-04-06 |
| [VulkanBaseRenderer 架构、LogicRenderer 与渲染正确性审计](notes/vulkan-base-renderer-architecture-audit.md) | 📝 待核对 | 2026-07-11 |

## 项目文档 · projects/

| 文档 | 状态 | 更新 |
| --- | --- | --- |
| [Brotato3D 开发者指南](projects/brotato-3d/developer-guide.md) | 🟢 现行 | 2026-05-10 |
| [Brotato3D 项目介绍](projects/brotato-3d/introduction.md) | 🟢 现行 | 2026-05-18 |
| [Flappy Bird Parity 项目介绍](projects/flappy-bird-parity/introduction.md) | 🟢 现行 | 2026-05-18 |
| [Flappy Bird Parity Report](projects/flappy-bird-parity/parity-report.md) | 🟢 现行 | 2026-05-06 |

---

_本索引由文档 frontmatter 生成；新增文档后请保持 frontmatter 字段完整，并可重跑生成脚本刷新本表。_
