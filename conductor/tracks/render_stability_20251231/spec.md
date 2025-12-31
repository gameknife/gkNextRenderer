# Spec: 增强渲染管线的稳定性与性能分析能力

## 目标
基于现有的 `VulkanGpuTimer` 基础，优化其健壮性，覆盖更多的渲染路径，并提供更直观的性能分析可视化。同时强化 Vulkan 层的校验以提升引擎稳定性。

## 范围
1. **GPU Timestamp Queries**: 审查并重构现有的 `VulkanGpuTimer`，解决潜在的线程安全问题（如静态成员变量），并确保跨平台（尤其是 Android）的兼容性。
2. **Coverage & Instrumentation**: 确保所有关键渲染 Pass（包括 G-Buffer, Shadow, Ray Tracing, Post-processing, Compute Shaders）都被 `SCOPED_GPU_TIMER` 正确覆盖。
3. **Advanced UI**: 在 ImGui 中升级性能展示，支持层级折叠或更清晰的时间线视图，而不仅仅是简单的文本列表。
4. **Pipeline Validation**: 启用并修复 Vulkan Validation Layers 报告的高优先级错误。

## 成功标准
- `VulkanGpuTimer` 无静态状态依赖，支持嵌套调用且线程安全。
- 编辑器 UI 能清晰展示渲染管线的完整层级耗时。
- 在开启 Validation Layers 的情况下运行 benchmark 场景无严重报错。
