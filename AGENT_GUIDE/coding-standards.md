# 编码规范与代码审查指南 / Coding Standards & Code Review

## 项目概览 / Project Overview

**gkNextEngine** 是一个跨平台 3D 游戏引擎，基于现代 C++20 与 Vulkan，支持硬件/软件光线追踪、实时全局光照等现代渲染技术。

**Key Technologies:**
- C++20/C11, Vulkan API, Slang shader language
- Multi-platform: Windows (x86_64), Linux (x86_64), macOS (arm64), Android (arm64), iOS (arm64)
- Core philosophy: < 50k LOC, prefer mature third-party libraries

---

## Code Review 关注点 / Review Focus Areas

### 1. 命名规范 / Naming Conventions

**强制执行 (.clang-tidy 规则):**

- **类型 (Types)**: `PascalCase`
  - 类、结构体、联合体、枚举、类型别名、命名空间
  - ✅ `class RenderContext`, `struct UniformBufferObject`, `enum ERenderType`
  - ❌ `class render_context`, `struct uniformBufferObject`

- **函数/方法 (Functions/Methods)**: `PascalCase`
  - ✅ `void RenderFrame()`, `bool InitializeDevice()`
  - ❌ `void render_frame()`, `bool initializeDevice()`

- **变量/参数 (Variables/Parameters)**: `camelCase`
  - 局部变量、参数、静态变量
  - ✅ `int frameCounter`, `VkDevice deviceHandle`
  - ❌ `int FrameCounter`, `VkDevice device_handle`

- **全局变量 (Global Variables)**: `PascalCase` (首字母大写)
  - ✅ `Options GOption`, `Logger GLogger`
  - ❌ `Options gOption`, `Logger g_logger`

- **私有成员 (Private Members)**: `camelCase` + 尾随下划线 `_`
  - ✅ `bool supportRayTracing_`, `VkDevice device_`
  - ❌ `bool supportRayTracing`, `VkDevice m_device`

- **常量 (Constants)**: `camelCase`
  - ✅ `constexpr int maxFrames = 3`
  - ❌ `constexpr int MAX_FRAMES = 3`

- **宏 (Macros)**: `UPPER_CASE`
  - ✅ `#define VK_CHECK_RESULT(x)`
  - ❌ `#define vk_check_result(x)`

**审查建议:**
- [ ] 检查命名是否符合上述规范
- [ ] 特别注意 API 符号名称（如 Vulkan 函数封装）保持一致性
- [ ] 私有成员必须有尾随下划线 `_`

---

### 2. 代码风格 / Code Style

**基本规范:**
- 缩进：**4 空格**（禁止 Tab）
- 大括号：函数/方法的大括号另起一行（Allman 风格）
  ```cpp
  void RenderFrame() {  // ✅
      // ...
  }

  void RenderFrame()    // ❌
  {
      // ...
  }
  ```
- 使用现代 C++ 特性：智能指针、range-based for、auto（适度）

**审查建议:**
- [ ] 检查缩进一致性（4 空格）
- [ ] 优先使用 STL 容器和算法，避免重复造轮子

---

### 3. 平台兼容性 / Platform Compatibility

**必须支持的平台:**
- Windows (MSVC 2022)
- Linux (GCC/Clang)
- macOS (arm64, Clang)
- Android (NDK r27, arm64)
- iOS (arm64, Xcode)

**审查建议:**
- [ ] 避免平台特定代码未加 `#ifdef` 保护
- [ ] 文件路径使用跨平台 API (`std::filesystem` 或引擎封装)
- [ ] 着色器代码使用 Slang，避免平台特定的 GLSL/HLSL
- [ ] 检查大小端、对齐、字节序等问题（移动平台）
- [ ] Android 构建需特别注意 SDL3 依赖处理（使用 `.aar`）

---

### 4. 性能与优化 / Performance & Optimization

**核心原则:**
- GPU-Driven 管线：减少 CPU-GPU 同步
- Bindless 资源：避免频繁 descriptor set 更新
- 多线程任务调度：充分利用多核 CPU

**审查建议:**
- [ ] 避免不必要的 CPU-GPU 同步（`vkQueueWaitIdle` 等）
- [ ] 减少内存分配（优先使用对象池、预分配）
- [ ] 检查循环内的重复计算（提取到循环外）
- [ ] 着色器代码避免分支发散（特别是 GPU 光线追踪）
- [ ] 大型资源加载应异步化（纹理、模型）

---

### 5. 资源管理与内存 / Resource & Memory Management

**规范:**
- RAII 原则：资源生命周期绑定对象生命周期
- Vulkan 资源需显式销毁：在析构函数或 `Cleanup()` 方法中
- 避免裸指针：优先 `std::unique_ptr` / `std::shared_ptr`

**审查建议:**
- [ ] 所有 Vulkan 对象（VkImage, VkBuffer 等）必须正确销毁
- [ ] 检查是否存在内存泄漏（使用 Valgrind/ASAN 验证）
- [ ] 避免野指针：指针赋值后检查有效性
- [ ] 纹理/模型卸载逻辑完整（特别是动态加载场景）

---

### 6. 着色器与渲染管线 / Shaders & Rendering Pipeline

**着色器规范:**
- 使用 **Slang** 编写，统一跨平台着色器
- 文件命名与运行时标识对应（如 `ERT_*` 枚举）
- Stage 文件后缀一致：`.vert.slang`, `.frag.slang`, `.rgen.slang` 等

**审查建议:**
- [ ] 着色器代码避免硬编码常量（使用 uniform/push constant）
- [ ] 检查 binding 索引冲突（特别是 bindless 管线）
- [ ] 光线追踪着色器需验证 payload/attribute 对齐
- [ ] 材质编辑器生成的着色器需通过编译测试

---

### 7. 错误处理 / Error Handling

**规范:**
- Vulkan 调用必须检查返回值（使用 `VK_CHECK_RESULT` 宏）
- 关键路径（初始化、资源加载）需完整错误处理
- 避免静默失败：至少输出日志

**审查建议:**
- [ ] 所有 Vulkan API 调用需检查 `VkResult`
- [ ] 文件 I/O 操作需检查返回值（`fopen`, `std::ifstream` 等）
- [ ] 异常路径需清理已分配资源（RAII 或手动 cleanup）
- [ ] 错误日志应包含上下文信息（文件名、行号、失败原因）

---

### 8. 构建系统 / Build System

**工具链:**
- CMake 3.26+, Ninja
- vcpkg 管理依赖（`vcpkg.json` / `vcpkg-configuration.json`）
- 平台脚本入口在根：`gnb.sh` / `./gnb.bat`
- 实现位于 `tools/gnb/`
- vcpkg 在首次 `gnb build` 时由 gnb 自动引导，开发者无需手动运行；仅在需要更新时调用 `gnb setup --refresh`

**审查建议:**
- [ ] 新增第三方库需更新 `vcpkg.json`
- [ ] CMakeLists.txt 避免硬编码路径（使用变量）
- [ ] 可选依赖需条件编译（如 AVIF、Superluminal）
- [ ] Android 构建需检查 NDK 版本兼容性（当前 r27）

---

### 9. 测试与验证 / Testing & Validation

**当前测试策略:**
- Catch2 单元测试 `gkNextUnitTests`，直接用可执行文件路径启动（不再要求 CWD 为 bin）
- 渲染改动用 `gkNextRenderer` 目视验证，或跑 `gkNextVisualTest` 生成截图报告

**审查建议:**
- [ ] 触及核心系统时补充/更新对应的 Catch2 测试
- [ ] 渲染改动需附截图对比（性能改动需附 FPS 数据）
- [ ] 跨平台改动需在至少 2 个平台验证
- [ ] 着色器改动需运行 `gkNextRenderer` 目视检查
- [ ] TypeScript 脚本改动需执行 bundled tsc：Windows 用 `tools\tsc\tsc.exe -p assets\typescript\tsconfig.json`，macOS/Linux 用 `tools/tsc/tsc -p assets/typescript/tsconfig.json`；运行时热重载同样使用 bundled tsc，不依赖 Node/npm

---

### 10. Git 提交规范 / Commit Guidelines

**提交消息:**
- 使用短促命题式（imperative mood）
- 单一关注点（single concern per commit）
- 示例：
  - ✅ `fix platform judgement in build script`
  - ✅ `add bilateral denoising for ray tracing`
  - ❌ `fixed some bugs and added new features`

**审查建议:**
- [ ] 提交消息清晰描述改动内容
- [ ] 避免巨型提交（>1000 行建议拆分）
- [ ] 二进制文件（纹理、模型）使用 Git LFS（如适用）

---

## 特别注意事项 / Special Considerations

### ⚠️ 禁止事项 / Prohibited Actions

- ❌ **修改第三方库代码**（ThirdParty/ 目录）
- ❌ **提交编译产物**（build/ 目录、.o/.obj 文件）
- ❌ **硬编码绝对路径**（开发环境路径）
- ❌ **提交敏感信息**（API key、密钥、证书私钥）
- ❌ **绕过 clang-tidy 检查**（除非有充分理由并注释说明）

### 📝 推荐操作 / Recommended Actions

- ✅ **运行 clang-tidy 自动修正命名**：
  ```bash
  python3 tools/clang-tools/run-clang-tidy.py -p out/build/<platform> -fix -j <N>
  ```
- ✅ **生成 compile_commands.json**（禁用 Unity Build）：
  ```bash
  ./gnb.sh build --no-unity
  ```
- ✅ **性能分析使用 Superluminal/Tracy**（不影响默认构建）
- ✅ **提交前检查工作区**：`git status` 确认无遗漏文件

---

## Code Review Checklist / 代码审查清单

在提交 PR 前，请确认以下事项：

### 基础检查 / Basic Checks
- [ ] 代码遵循命名规范（函数 PascalCase，变量 camelCase，私有成员尾随 `_`）
- [ ] 代码风格一致（4 空格缩进，函数大括号另起一行）
- [ ] 无编译警告（warnings-as-errors 已启用）
- [ ] 无 clang-tidy 命名违规（可运行自动修正脚本）

### 功能验证 / Functionality Validation
- [ ] 在至少一个平台成功构建（标注平台：Windows/Linux/macOS/Android）
- [ ] 主程序运行正常（`gkNextRenderer` / `gkNextEditor` / benchmark）
- [ ] 渲染输出正确（附截图对比，如有视觉改动）
- [ ] 性能无明显回退（附 FPS 数据，如有性能影响）

### 跨平台兼容 / Cross-Platform Compatibility
- [ ] 平台特定代码已加 `#ifdef` 保护
- [ ] 文件路径使用跨平台 API
- [ ] 着色器代码使用 Slang（无平台特定语法）

### 资源与内存 / Resources & Memory
- [ ] Vulkan 资源正确销毁（无泄漏）
- [ ] 使用 RAII 或智能指针管理资源
- [ ] 大型资源加载已异步化（如适用）

### 文档与注释 / Documentation & Comments
- [ ] 复杂算法有必要注释（中文或英文）
- [ ] 公共 API 有简要说明（Doxygen 风格，可选）
- [ ] PR 描述清晰（改动内容、构建平台、关键截图）

---

## 反馈 / Feedback

- GitHub Issues: [gameknife/gkNextEngine](https://github.com/gameknife/gkNextEngine/issues)
- 提交 PR 并在描述中说明改动理由
