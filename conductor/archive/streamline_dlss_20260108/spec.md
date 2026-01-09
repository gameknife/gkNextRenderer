# Specification: NVIDIA Streamline Integration (DLSS SR & RR)

## 1. Overview
本任务旨在将 NVIDIA Streamline SDK 集成到 gkNextRenderer 中，以在 Windows 平台上提供对 DLSS Super Resolution (SR) 和 DLSS Ray Reconstruction (RR) 的支持。这将显著提升高分辨率下的渲染性能，并增强光线追踪效果的视觉质量。

## 2. 范围与约束
- **平台限制**: 仅限 Windows 平台。
- **构建控制**: 通过已有的 `WITH_STREAMLINE` 宏进行编译隔离。
- **SDK 位置**: 使用 `src/ThirdParty` 目录下已有的 Streamline SDK。
- **集成方式**: 直接集成到主渲染器类逻辑中。

## 3. 功能需求
- **SDK 初始化**: 
    - 在渲染器启动时检测硬件支持并初始化 Streamline。
    - 在不支持 DLSS 的硬件上提供优雅的降级处理（关闭 DLSS 选项）。
- **DLSS Super Resolution (SR)**:
    - 支持多种质量模式（Quality, Balanced, Performance, Ultra Performance）。
    - 确保运动矢量（Motion Vectors）、深度图（Depth）等必要 Buffer 的正确生成与映射。
- **DLSS Ray Reconstruction (RR)**:
    - 集成 RR 以替代或增强现有的降噪器（Denoiser）。
    - 针对光追场景优化视觉重建质量。
- **运行时配置 (ImGui)**:
    - 在编辑器设置面板中添加 DLSS 配置项：开关、模式选择、RR 开关。
- **命令行支持 (CLI)**:
    - 支持通过命令行参数设置初始 DLSS 模式。

## 4. 非功能需求
- **视觉完整性**: 解决常见的拉伸、重影或深度不匹配导致的视觉伪影。
- **性能**: 开启 DLSS SR 后，在支持的硬件上应有明显的帧率提升。
- **稳定性**: 确保在不同显卡驱动版本和硬件环境下不发生崩溃。

## 5. 验收标准
- [ ] DLSS SR 能够正常开启，并能在 ImGui 中切换模式。
- [ ] DLSS RR 能够显著提升光追效果（如反射、阴影）的清晰度，减少噪点。
- [ ] 运动矢量处理正确，画面在移动时无明显残影。
- [ ] 在不支持 DLSS 的环境下，引擎能正常运行且不触发 Streamline 相关逻辑。
- [ ] 代码符合项目的 C++20 编码规范和宏隔离要求。
