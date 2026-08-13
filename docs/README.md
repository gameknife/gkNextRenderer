# gkNextRenderer 文档索引

本目录只保留仍能解释当前代码、当前操作方式或明确待办工作的文档。已完成的一次性实施计划、被替代的架构草案和重复审计不原样保留；但计划中仍约束现有实现的设计理由、不变量、格式契约和失败教训，必须先提炼进现行 design/guide，不能只以“Git 里还能找到”为由删除。

## 阅读优先级

后续 agent 遇到冲突时按以下顺序判断：

1. 当前代码、CMake、配置和 `AGENTS.md`。
2. 本索引标为“现行”的 guide/design，以及 `AGENT_GUIDE/`。
3. `.spec/TODO.md` 中未完成的任务及其显式链接的当前 design/plan；独立 plan 不构成授权。
4. `.spec/journal/` 与 `.spec/ARCHIVE.md` 仅是历史证据，不能据此恢复已删除架构。

不要因为文档中曾出现过某个类、目录或命令就假定它仍存在；先用 `rg`、`gnb <command> --help` 和 `git log -- <path>` 核对。

## 文档生命周期

- `guide` 解释当前如何使用、扩展或排障；命令、路径和默认值必须能由当前仓库验证。
- `design` 记录仍成立的边界、所有权、数据契约和关键取舍。功能完成后 design 通常应保留并改写成“当前架构”，而不是随 plan 一起删除。
- `plan` 只记录尚未完成且已确认的执行顺序。完成、放弃或被替代后应退出现行文档面；删除前先把耐久知识提炼到 design/guide。
- `.spec/journal/`、Git 提交和旧 plan 是审计证据，不是现行架构入口。它们能回答“当时做了什么”，不能替代一份可发现的当前设计说明。

清理文档时逐项判断“这段内容是否解释了当前代码为何如此、修改时必须守住什么、或哪条看似合理的路线其实未实现”。只删除进度表、过期文件清单、旧行号、一次性验收日志和已被代码否定的方案。

## Guides（现行操作说明）

- [CMake 结构](guides/cmake-structure.md)
- [SDR / HDR10 / EDR 输出模式](guides/display-output-modes.md)
- [NVIDIA Streamline / DLSS / DLSS-G](guides/dlss-streamline.md)
- [AMD FidelityFX FSR 3.1 / Frame Generation](guides/fidelityfx-fsr.md)
- [gnb 架构与代码导览](guides/gnb-architecture.md)
- [gnb CLI](guides/gnb-cli.md)
- [macOS / MoltenVK FIFO 黑屏与闪烁排障](guides/macos-moltenvk-fifo-present-troubleshooting.md)
- [发布流程（打 tag → CI → 验收 → release notes → 回滚）](guides/release-process.md)
- [SCAD 场景生成指引](guides/scad-scene-authoring-guide.md)
- [Windows Dozen Vulkan 后端排障](guides/dozen-vulkan-backend-troubleshooting.md)
- [SCAD 资产目录与 ScadLibrary 场景组装约定](../assets/scad/README.md)
- [SCAD Terrain 使用速查](AGENT_GUIDE/ScadTerrain.md)
- [Soft Mesh Shader GPU-Driven 提交路径](guides/soft-mesh-shader-gpu-driven-submit.md)
- [TUI 终端模式](guides/tui-mode.md)
- [TypeScript 整合](guides/typescript-integration.md)

## Designs（当前架构）

- [Agent 输入驱动验证](designs/agent-validation-input-driver.md)
- [Desktop UI Foundation](designs/desktop-ui-foundation.md)
- [AmbientCube 命中驱动驻留](designs/ambientcube-hit-driven-residency-design.md)
- [大气散射与高度雾架构](designs/atmosphere-and-height-fog-design.md)（现行；程序化天空、大气透视、高度雾、
  昼夜 Demo 与人工/Agent 验证入口均已完成。体积雾光轴与 PathTracing 介质散射按需另立任务）
- [Editor 材质创作架构](designs/editor-material-authoring.md)
- [Editor Settings 与 CVar 架构](designs/editor-settings-and-cvars.md)
- [Editor Sequencer 与动画轨道编辑](designs/editor-sequencer.md)
- [GI 缓存与体素资源架构](designs/gi-cache-architecture.md)
- [Gaussian Splat / SOG v2 格式与集成](designs/gaussian-splatting-sog-design.md)
- [gnb AI Bridge Protocol v2](designs/gnb-ai-bridge-protocol-v2.md)
- [Massive Rendering Mode 与双 uint Visibility Buffer](designs/massive-visibility-buffer-design.md)（提案，未实现；[开发计划](plans/massive-rendering-mode-plan.md)）
- [Tracing Direct Lighting 与 ReSTIR DI 架构](designs/pathtracing-restir-design.md)
- [RenderView 多视图架构](designs/multi-viewport-renderview-design.md)
- [渲染运行时架构与契约](designs/rendering-runtime-architecture.md)
- [场景导出 glTF/GLB 契约](designs/scene-export-gltf-contract.md)
- [NextAI 产品化边界](designs/nextai-product-focused-architecture.md)
- [ScadLibrary AI 融合创作架构](designs/scadlibrary-ai-authoring-integration.md)
- [SCAD Scene Compose](designs/scad-scene-compose-design.md)
- [SCAD Terrain 地形架构](designs/scad-terrain-design.md)（M0–M4 已落地）
- [SoftwareModernNoAmbient 渲染与 GTAO](designs/software-modern-noambient-rendering.md)
- [直接样本后处理与 Upscaler 输入链](designs/direct-sample-post-chain.md)
- [WebRTC Remote Play](designs/webrtc-remoteplay-design.md)

## Projects

- Brotato3D：[介绍](projects/brotato-3d/introduction.md) · [配置与玩法开发指南](projects/brotato-3d/developer-guide.md)
- Flappy C++ / TypeScript parity：[介绍与验证方式](projects/flappy-bird-parity/introduction.md)
- AirportSim：[架构与确定性边界](projects/airport-sim/architecture.md)
- NextRA：[架构不变量](projects/nextra/architecture.md) · [现状与后续方向](projects/nextra/roadmap.md)
- ScadStudio：[会话、生成与预览架构](projects/scad-studio/architecture.md)
- ScadLibrary：[AI 创作当前实现](projects/scadlibrary/ai-authoring.md) · [架构契约](designs/scadlibrary-ai-authoring-integration.md) · [后续收口计划](plans/scadlibrary-ai-authoring-plan.md)
- StudioSim：[架构与 AI 边界](projects/studio-sim/architecture.md)
- NextDayz：[MVP 基线设计](projects/nextdayz/nextdayz-mvp-design.md) ·
  [复杂 3C 与 ScadRig 分层动画设计](projects/nextdayz/nextdayz-3c-scadrig-design.md) ·
  [PVE 生存循环现行架构](projects/nextdayz/nextdayz-productization-design.md)
- NextTotalwar：[行军 MVP 设计与历史实测](projects/nexttotalwar/nexttotalwar-mvp-design.md) ·
  [战斗 MVP 设计（C0～C2 部分实现）](projects/nexttotalwar/nexttotalwar-battle-mvp-design.md) ·
  [基础战斗循环现行架构](projects/nexttotalwar/nexttotalwar-productization-design.md) ·
  [代码导览](AGENT_GUIDE/NextTotalwar.md)
- TruckerDemo（迭代计划）：[SnowRunner 风格越野运输 Demo 迭代计划](projects/trucker-demo/trucker-demo-iteration-plan.md)

## Plans（待实施）

  （待实施；统一 Engine UI 主题、语义控件、Toolbar/Combo、应用 chrome、领域选项目录与 DevTools 边界）
- [Android 纯 CMake 驱动构建重构方案](plans/android-cmake-build-refactor-plan.md)（实施中；源码迁移已完成，待兼容 NDK 环境完成 APK/AVD 验收）
- [iOS 纯 CMake 驱动构建重构方案](plans/ios-pure-cmake-build-refactor-plan.md)（device-only 实施；复用 macOS Vulkan SDK 解析，保留 `gnb ios build` 薄入口）
- [阶段性 Release 准备计划](plans/release-readiness-plan.md)（现行；发布流水线、崩溃兜底、UI 收口、
  合规与文档一致性的问题清单与分批任务，覆盖 gkNextRenderer / gkNextEditor / gkNextMotionBenchmark）
- [CPU TLAS 异步与并行更新计划](plans/cpu-tlas-parallel-update-plan.md)
- [PathTracing Slang Shader 库重构计划](plans/shader-library-refactor-plan.md)（计划中）
- [PathTracing 材质模型统一（Stage 3 设计增量）](plans/shader-material-model-unification-plan.md)（待审阅）
- CitySolSim 经营循环：[架构设计](projects/citysolsim/management-loop-design.md) ·
  [开发计划](projects/citysolsim/management-loop-plan.md)（待实现）
- NextRA 经济与建造：[架构设计](projects/nextra/economy-build-design.md) ·
  [开发计划](projects/nextra/economy-build-plan.md)（待实现）

## 已完成计划（保留作为实现依据，待下次文档清理时归档）

这些 plan 的 `status` 已是“已完成 / 已实施”。按本页的生命周期规则它们应退出现行文档面；
在把其中仍成立的契约提炼进对应 design 之前先在此列出，避免成为无人索引的游离文档。

- [大气散射与高度雾开发计划](plans/atmosphere-and-height-fog-plan.md)（已完成 → [design](designs/atmosphere-and-height-fog-design.md)）
- [Bindless 3D 纹理支持计划](plans/bindless-3d-texture-support-plan.md)（已完成）
- [BSDF-aware Direct Lighting 计划](plans/bsdf-aware-direct-lighting-plan.md)（已实施 → [design](designs/pathtracing-restir-design.md)）
- [SoftwareTracing Direct Lighting 与 ReSTIR DI 同步计划](plans/softwaretracing-direct-lighting-restir-plan.md)（已实施）
- [SCAD Terrain 开发计划](plans/scad-terrain-plan.md)（M0–M4 已完成 → [design](designs/scad-terrain-design.md)）
- [NextDayz 复杂 3C 与 ScadRig 分层动画开发计划](projects/nextdayz/nextdayz-3c-scadrig-development-plan.md)（已完成）
- [NextDayz PVE 生存循环产品化开发计划](projects/nextdayz/nextdayz-productization-development-plan.md)（已完成）
- [NextTotalwar 基础战斗循环产品化开发计划](projects/nexttotalwar/nexttotalwar-productization-development-plan.md)（已完成）

## 环境记录

- [Steam Deck / Arch Linux 部署注意事项](notes/steamdeck-deployment-notes.md)

## 更稳定的专题入口

反射、QuickJS、热重载、各 loader 和具体游戏代码结构优先阅读 `AGENT_GUIDE/`；它们由 `AGENTS.md` 的 Key References 维护，不在这里复制一份容易漂移的版本。
