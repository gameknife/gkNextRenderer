# gkNextRenderer 文档索引

本目录只保留能解释当前代码、当前操作方式或明确剩余工作的文档。已完成的一次性实施计划、被取代的
方案和重复审计已退出文档面；历史过程通过 Git 与 `.spec/journal/` 追溯。

## 权威性与生命周期

发生冲突时按以下顺序判断：

1. 当前代码、CMake、配置与 `AGENTS.md`；
2. 本索引中的 guide/design 与 `AGENT_GUIDE/`；
3. `.spec/TODO.md` 的未完成任务及其显式链接；
4. `.spec/ARCHIVE.md`、journal 和 Git 历史只作历史证据。

`guide` 说明现在怎么做，`design` 记录仍成立的边界与契约，`plan` 只保留尚未完成且范围明确的执行
顺序。完成 plan 中仍有价值的内容应并入 design/guide，然后删除 plan；不要长期保留旧文件清单、行号、
进度表和一次性验收日志。

## 当前任务状态

- `.spec/TODO.md` 的下一项是 `#00068` WebRTC Remote Play 后续；开始实现前仍需补充本轮可验证目标。
- `#00011` 持久 TODO 服务仍在“待规划”，不是活动开发任务。
- 下方“剩余计划”是可发现的设计积压，不等于已授权执行；实际调度仍以 `.spec/TODO.md` 为准。

## 操作指南

- 构建与工具：[CMake 结构](guides/cmake-structure.md) · [gnb CLI](guides/gnb-cli.md) ·
  [gnb 架构](guides/gnb-architecture.md) · [发布流程](guides/release-process.md)
- 平台与显示：[SDR/HDR10/EDR](guides/display-output-modes.md) ·
  [macOS/MoltenVK FIFO 排障](guides/macos-moltenvk-fifo-present-troubleshooting.md) ·
  [Windows Dozen 排障](guides/dozen-vulkan-backend-troubleshooting.md) ·
  [Steam Deck 部署](notes/steamdeck-deployment-notes.md)
- 渲染扩展：[DLSS/Streamline](guides/dlss-streamline.md) ·
  [FidelityFX FSR](guides/fidelityfx-fsr.md) · [Tracy](guides/tracy-profiling.md) ·
  [Soft Mesh Shader](guides/soft-mesh-shader-gpu-driven-submit.md)
- 内容与交互：[SCAD 场景创作](guides/scad-scene-authoring-guide.md) ·
  [TUI 模式](guides/tui-mode.md) · [VITURE Carina AR](guides/viture-ar.md)

## 当前架构

### Runtime、渲染与平台

- [渲染运行时架构与契约](designs/rendering-runtime-architecture.md)
- [双平面 Visibility Buffer](designs/massive-visibility-buffer-design.md)
- [Visibility Surface、G-buffer 与 Shading Scheduler](designs/visibility-surface-gbuffer-shading-scheduler.md)
- [直接样本后处理与 Upscaler 输入链](designs/direct-sample-post-chain.md)
- [Tracing Direct Lighting 与 ReSTIR DI](designs/pathtracing-restir-design.md)
- [SoftwareModernNoAmbient 与 GTAO](designs/software-modern-noambient-rendering.md)
- [大气散射与高度雾](designs/atmosphere-and-height-fog-design.md)
- [GI 缓存与体素资源](designs/gi-cache-architecture.md)
- [AmbientCube 命中驱动驻留](designs/ambientcube-hit-driven-residency-design.md)
- [CPU TLAS 快照与后台重建](designs/cpu-tlas-snapshot-architecture.md)
- [RenderView 多视图](designs/multi-viewport-renderview-design.md)
- [Gaussian Splat / SOG v2](designs/gaussian-splatting-sog-design.md)
- [iOS A12X Compatibility Minimal Render](designs/ios-a12x-compatibility-minimal-render-mvp.md)
- [移动端 application 目标](designs/mobile-application-targets.md)
- [Agent 输入驱动验证](designs/agent-validation-input-driver.md)
- [WebRTC Remote Play](designs/webrtc-remoteplay-design.md)

### Editor、脚本与内容管线

- [Desktop UI Foundation](designs/desktop-ui-foundation.md)
- [Editor 材质创作](designs/editor-material-authoring.md) ·
  [Settings/CVar](designs/editor-settings-and-cvars.md) · [Sequencer](designs/editor-sequencer.md)
- [.NET 脚本运行时](designs/dotnet-scripting-design.md) ·
  [托管游戏 Launcher 与 PIE](designs/managed-game-launcher-design.md)
- [NextAI 产品化边界](designs/nextai-product-focused-architecture.md) ·
  [gnb AI Bridge v2](designs/gnb-ai-bridge-protocol-v2.md)
- [SCAD Scene Compose](designs/scad-scene-compose-design.md) ·
  [统一场景文档](designs/scad-unified-scene-document.md) ·
  [Terrain](designs/scad-terrain-design.md) ·
  [ScadLibrary AI 创作实现](projects/scadlibrary/ai-authoring.md)
- [真实地理数据生成城市关卡](designs/geo-city-generation-design.md)
- [场景导出 glTF/GLB 契约](designs/scene-export-gltf-contract.md)

## AGENT 专题指南

- C#：[开发应用](AGENT_GUIDE/CSharpGameDevelopment.md) ·
  [.NET Bindings](AGENT_GUIDE/DotNetBindings.md) · [反射](AGENT_GUIDE/ReflectionSystem.md) ·
  [Hot Reload](AGENT_GUIDE/HotReload.md)
- SCAD：[Loader](AGENT_GUIDE/SCADLoader.md) · [资产 Playbook](AGENT_GUIDE/ScadAssetPlaybook.md) ·
  [Terrain](AGENT_GUIDE/ScadTerrain.md) · [ScadRig](AGENT_GUIDE/ScadRig.md)
- Loader/渲染：[LDraw](AGENT_GUIDE/LDrawLoader.md) · [Gaussian Splat](AGENT_GUIDE/GaussianSplat.md) ·
  [Native Temporal Upscaler](AGENT_GUIDE/NativeTemporalUpscaler.md)
- Gameplay：[CharacterDemo](AGENT_GUIDE/CharacterDemo.md) · [SimKit](AGENT_GUIDE/SimKit.md) ·
  [Brotato3D](AGENT_GUIDE/Brotato3D.md) · [MagicaLego](AGENT_GUIDE/MagicaLego.md) ·
  [NextTotalwar](AGENT_GUIDE/NextTotalwar.md) · [NextWorldTravel](AGENT_GUIDE/NextWorldTravel.md)
- 通用代码审查：[编码规范](AGENT_GUIDE/coding-standards.md)

## 项目文档

- [AirportSim](projects/airport-sim/architecture.md)
- Brotato3D：[项目介绍](projects/brotato-3d/introduction.md) ·
  [配置与玩法开发](projects/brotato-3d/developer-guide.md)
- CitySolSim：[经营循环设计](projects/citysolsim/management-loop-design.md) ·
  [剩余计划](projects/citysolsim/management-loop-plan.md)
- [Flappy C++/C# parity](projects/flappy-bird-parity/introduction.md)
- NextDayz：[3C 与 ScadRig](projects/nextdayz/nextdayz-3c-scadrig-design.md) ·
  [PVE 生存循环](projects/nextdayz/nextdayz-productization-design.md)
- NextRA：[架构不变量](projects/nextra/architecture.md) ·
  [经济与建造设计](projects/nextra/economy-build-design.md) ·
  [剩余计划](projects/nextra/economy-build-plan.md)
- NextTotalwar：[行军/地图约束](projects/nexttotalwar/nexttotalwar-mvp-design.md) ·
  [基础战斗循环](projects/nexttotalwar/nexttotalwar-productization-design.md)
- [ScadStudio](projects/scad-studio/architecture.md) ·
  [ScadLibrary AI 实现](projects/scadlibrary/ai-authoring.md) ·
  [StudioSim](projects/studio-sim/architecture.md) ·
  [TruckerDemo](projects/trucker-demo/architecture.md)

## 剩余计划

- [GI Bake 磁盘缓存](plans/gi-bake-disk-cache-plan.md)（待实施）
- [PathTracing 材质模型统一](plans/shader-material-model-unification-plan.md)（待审阅）
- [地理城市生成剩余项](plans/geo-city-generation-plan.md)（P0–P5 已完成；只保留未完成项）
- [ScadLibrary AI 创作收口](plans/scadlibrary-ai-authoring-plan.md)（M0–M4 已完成；只剩 ScadStudio 退役门槛）

未在本页重复列出的具体 loader、游戏代码入口和验证命令，以对应 `AGENT_GUIDE/`、代码旁注释与
`gnb <command> --help` 为准。
