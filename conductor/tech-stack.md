# 技术栈

本项目采用现代高性能技术栈，旨在构建一个跨平台、高性能的 3D 游戏引擎。

## 核心语言与标准
- **C++20**：利用现代 C++ 特性（如 Concepts, Coroutines, Modules 等，视支持情况而定）来提高代码的表达能力、安全性和性能。

## 图形与渲染
- **Vulkan API**：作为唯一的图形 API，利用其底层控制能力实现高性能渲染，并支持硬件加速的光线追踪、GPU-Driven 管线和全 Bindless 设计。
- **Slang / GLSL**：用于编写着色器代码，利用 Slang 的现代特性进行着色器管理。

## 核心第三方库
- **窗口与系统 (SDL3)**：处理跨平台的窗口创建、输入响应和音频基础。
- **用户界面 (ImGui)**：用于构建引擎编辑器、节点材质编辑器以及各种调试工具。
- **物理引擎 (Jolt Physics)**：集成高性能的物理模拟能力。
- **数学库 (glm)**：提供符合图形编程习惯的数学计算。
- **资产加载 (tinygltf, tinyobjloader)**：支持 glTF 2.0 和 OBJ 格式 the 资产导入。
- **图像处理 (stb, ktx, libavif)**：支持多种图像格式及 KTX 纹理加载。
- **基础工具 (fmt, spdlog, xxHash, nlohmann-json)**：提供日志打印、字符串格式化、快速哈希和 JSON 处理。

## 测试与质量保证
- **Catch2**：作为主要的单元测试和集成测试框架，确保引擎各个模块的稳定性。
- **内置 GPU 分析器 (VulkanGpuTimer)**：提供精确的、分层级的 GPU 时间戳测量，用于实时性能瓶颈诊断。
- **Vulkan 验证集成**：支持通过运行时参数启用 Validation Layers，确保图形 API 使用的正确性与跨平台稳定性。

## 构建与开发环境
- **Modern CMake**：采用基于 Target 的现代构建模式，严格隔离编译属性，并通过 CMake Presets (v3) 统一全平台构建配置。
- **vcpkg**：高效管理第三方库依赖。
- **标准化 CLI 脚本**：统一的 `build`, `run`, `vcpkg` 脚本接口。Windows 平台采用 PowerShell 作为核心脚本语言（提供更强的逻辑处理能力和一致性），Batch 脚本仅作为调用包装器（Shim）。Unix 平台采用 Bash。
- **Ninja / Visual Studio**：推荐的后端构建工具。

## 目标平台
- **桌面端**：Windows (x86_64), Linux (x86_64), macOS (Apple Silicon)。
- **移动端**：Android (arm64-v8a), iOS (arm64)。
