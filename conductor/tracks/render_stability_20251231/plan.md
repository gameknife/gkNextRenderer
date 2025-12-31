# Plan: 增强渲染管线的稳定性与性能分析能力

## Phase 1: 现有分析器审计与重构 [checkpoint: 616c910]
- [x] Task: 审查 `VulkanGpuTimer` 代码，特别是 `ScopedGpuTimer` 中的静态 `folderName_`，将其改为成员变量或上下文相关存储以解决潜在风险。 [6be5855]
- [x] Task: 验证当前 `GpuTimer` 在 Android 平台上的可用性，确保 `vkCmdWriteTimestamp` 正确工作。 [03c1a8b]
- [x] Task: Conductor - User Manual Verification 'Phase 1: 现有分析器审计与重构' (Protocol in workflow.md)

## Phase 2: 全面打点与 UI 升级 [checkpoint: c9ad0b9]
- [x] Task: 扫描所有渲染 Pass（`gkNextRenderer`, `PostProcess`, `RayTracing` 等），补充缺失的 `SCOPED_GPU_TIMER` 宏。 [20869e0]
- [x] Task: 优化 ImGui 的性能面板展示，支持按层级缩进显示，并考虑添加简单的柱状图或百分比占比。 [4bcd83f]
- [x] Task: Conductor - User Manual Verification 'Phase 2: 全面打点与 UI 升级' (Protocol in workflow.md)

## Phase 3: 稳定性增强与校验
- [ ] Task: 确保调试模式下正确启用了 Vulkan Validation Layers。
- [ ] Task: 运行主要场景（如 CornellBox, Sponza），收集并修复 Validation Layers 报告的 Error 和 Warning。
- [ ] Task: Conductor - User Manual Verification 'Phase 3: 稳定性增强与校验' (Protocol in workflow.md)
