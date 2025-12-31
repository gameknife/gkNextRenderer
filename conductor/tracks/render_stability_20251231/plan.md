# Plan: 增强渲染管线的稳定性与性能分析能力

## Phase 1: 基础架构搭建
- [ ] Task: 设计并实现 `GpuProfiler` 类，用于管理 Vulkan Query Pools 和计时戳。
- [ ] Task: 在渲染循环的起始和结束处插入全局计时锚点。
- [ ] Task: Conductor - User Manual Verification 'Phase 1: 基础架构搭建' (Protocol in workflow.md)

## Phase 2: 详细 Pass 测量与 UI 展示
- [ ] Task: 为 `gkNextRenderer` 中的每个主要渲染 Pass（G-Buffer, Shadow, Path Tracing）添加计时器。
- [ ] Task: 在 `gkNextEditor` 中创建一个新的性能分析面板，展示实时耗时曲线。
- [ ] Task: Conductor - User Manual Verification 'Phase 2: 详细 Pass 测量与 UI 展示' (Protocol in workflow.md)

## Phase 3: 稳定性增强与校验
- [ ] Task: 审查并优化当前的 Vulkan 实例创建逻辑，确保在调试模式下启用所有必要的验证特性。
- [ ] Task: 运行集成测试并根据校验层输出修复现有的 API 使用警告。
- [ ] Task: Conductor - User Manual Verification 'Phase 3: 稳定性增强与校验' (Protocol in workflow.md)
