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
- [SCAD 场景生成指引](guides/scad-scene-authoring-guide.md)
- [SCAD 资产目录与 ScadLibrary 场景组装约定](../assets/scad/README.md)
- [SCAD Terrain 使用速查](../AGENT_GUIDE/ScadTerrain.md)
- [Soft Mesh Shader GPU-Driven 提交路径](guides/soft-mesh-shader-gpu-driven-submit.md)
- [TUI 终端模式](guides/tui-mode.md)
- [TypeScript 整合](guides/typescript-integration.md)

## Designs（当前架构）

- [Agent 输入驱动验证](designs/agent-validation-input-driver.md)
- [AmbientCube 命中驱动驻留](designs/ambientcube-hit-driven-residency-design.md)
- [大气散射与高度雾架构](designs/atmosphere-and-height-fog-design.md)（现行；程序化天空、大气透视、高度雾、
  昼夜 Demo 与人工/Agent 验证入口均已完成。体积雾光轴与 PathTracing 介质散射按需另立任务）
- [Editor 材质创作架构](designs/editor-material-authoring.md)
- [Editor Settings 与 CVar 架构](designs/editor-settings-and-cvars.md)
- [GI 缓存与体素资源架构](designs/gi-cache-architecture.md)
- [Gaussian Splat / SOG v2 格式与集成](designs/gaussian-splatting-sog-design.md)
- [gnb AI Bridge Protocol v2](designs/gnb-ai-bridge-protocol-v2.md)
- [Massive Rendering Mode 与双 uint Visibility Buffer](designs/massive-visibility-buffer-design.md)（现行；默认/宽索引双管线与容量契约）
- [Tracing Direct Lighting 与 ReSTIR DI 架构](designs/pathtracing-restir-design.md)
- [RenderView 多视图架构](designs/multi-viewport-renderview-design.md)
- [渲染运行时架构与契约](designs/rendering-runtime-architecture.md)
- [场景导出 glTF/GLB 契约](designs/scene-export-gltf-contract.md)
- [NextAI 产品化边界](designs/nextai-product-focused-architecture.md)
- [ScadLibrary AI 融合创作架构](designs/scadlibrary-ai-authoring-integration.md)
- [SCAD Scene Compose](designs/scad-scene-compose-design.md)
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
- NextDayz：[MVP 基线设计](projects/nextdayz/nextdayz-mvp-design.md) · [复杂 3C 与 ScadRig 分层动画设计](projects/nextdayz/nextdayz-3c-scadrig-design.md)
- NextTotalwar：[MVP 设计、当前架构与实测记录](projects/nexttotalwar/nexttotalwar-mvp-design.md) · [代码导览](../AGENT_GUIDE/NextTotalwar.md)
- TruckerDemo（迭代计划）：[SnowRunner 风格越野运输 Demo 迭代计划](projects/trucker-demo/trucker-demo-iteration-plan.md)

## 环境记录

- [Steam Deck / Arch Linux 部署注意事项](notes/steamdeck-deployment-notes.md)

## 更稳定的专题入口

反射、QuickJS、热重载、各 loader 和具体游戏代码结构优先阅读 `AGENT_GUIDE/`；它们由 `AGENTS.md` 的 Key References 维护，不在这里复制一份容易漂移的版本。
