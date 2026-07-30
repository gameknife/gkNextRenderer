---
title: "Massive Rendering Mode 开发计划"
category: plan
status: 已完成
owner: engine
created: 2026-07-30
last_updated: 2026-07-30
design: ../designs/massive-visibility-buffer-design.md
---

# Massive Rendering Mode 开发计划

## 交付范围

本计划实现 [Massive Rendering Mode 与双 uint Visibility Buffer](../designs/massive-visibility-buffer-design.md)，
交付以下内容：

- 默认关闭、启动期选择的 Massive mode；
- default `R32_UINT` 与 massive `R32G32_UINT` 两套一致管线；
- Massive 262140 render-proxy 工程容量；
- 独立 Massive NodeProxy storage、扩容后的 visible items 与 TLAS；
- 131070-node `MassiveAsteroidBelt.proc`；
- `gkNextMassiveBenchmark` 可执行目标；
- 单元、shader、运行、视觉和默认路径回归验证。

计划不包含运行时热切换、分页/streaming node proxies、超过 262140 的产品容量承诺或 benchmark 结果优化。

## 阶段 0：锁定契约与失败测试

### 工作

1. 增加 `ERenderCapacityMode`、`FRenderCapacityLimits` 和容量推导的纯 CPU helper。
2. 为以下条件编写单元测试：
   - Default 容量为 65535、primitive words 为 1；
   - Massive 容量为 262140、primitive words 为 2；
   - checked buffer-size 计算；
   - one-based proxy slot 和背景 0 的约定；
   - default 超过 32767 时明确失败；
   - massive 超过 262140 时明确失败。
3. 给当前 `Scene::UpdateNodesGpuDriven` 增加上传前边界检查，先消除越界写入 Materials 区域的风险。

### 主要文件

- `src/Engine/Options.hpp`
- `src/Engine/Options.cpp`
- `src/Engine/Assets/Core/Scene.hpp`
- `src/Engine/Assets/Core/Scene.Update.cpp`
- `src/Tests/`

### 完成标准

- 默认配置没有行为变化；
- 超限场景稳定返回可诊断错误，不发生 mask 截断或 buffer overwrite；
- 容量与 byte-size helper 有边界测试。

## 阶段 1：Shader 双格式抽象与单 SPIR-V

### 工作

1. 在 shader common 中引入 `VisibilityId` 和集中 load/store helper。
2. Default helper 保持当前 15/17 packed 编码。
3. Massive helper 使用 `uint2(instanceIdx, triangleIdx)`。
4. 替换所有手写 15/17 编解码，包括：
   - SoftMesh Expand；
   - visibility / wireframe / shadow vertex；
   - visibility fragment；
   - PrimaryRayCasters；
   - SceneSampling；
   - SoftwareModernNoAmbient；
   - VisualDebugger。
5. 所有入口只编译一份 SPIR-V，通过 `SoftMeshShaderResources.PrimitiveWordCount` 选择物理 record。
6. visibility vertex/fragment 固定使用 `uint2` 接口，由 `R32_UINT` 或 `R32G32_UINT` attachment
   决定保留的分量。
7. LiveCoding shader hot reload 与 Default/Massive 共用同一个输出。

### 主要文件

- `assets/shaders/common/BasicTypes.slang`
- `assets/shaders/common/PrimaryRayCasters.slang`
- `assets/shaders/common/SceneSampling.slang`
- `assets/shaders/Task.SoftMeshShader*.slang`
- `assets/shaders/Rast.*SoftMeshShader*.slang`
- `assets/shaders/Rast.VisibilityPass.frag.slang`
- `assets/shaders/Core.SwModernNoAmbient.comp.slang`
- `assets/shaders/Util.VisualDebugger.comp.slang`
- `assets/cmake/SlangShaders.cmake`
- `src/Modules/LiveCoding/ShaderHotReloader.cpp`

### 完成标准

- `rg` 不再在业务 shader 中找到 `>> 17`、`0x7FFF`、`0x1FFFF` visibility 编解码；
- 不生成 `.massive.spv` 或容量模式宏；
- 同一 SPIR-V 在 Default 下写/读 packed `uint`；
- 同一 SPIR-V 在 Massive 下写/读 `uint2`。

## 阶段 2：模式化 GPU 资源与 pipeline 选择

### 工作

1. `Options` 增加 `--massive`，默认关闭。
2. renderer 保存 immutable active capacity mode。
3. `CreateRenderTargetBank` 按模式创建：
   - Default：`RT_MINIGBUFFER[_DRAW] = VK_FORMAT_R32_UINT`；
   - Massive：`RT_MINIGBUFFER[_DRAW] = VK_FORMAT_R32G32_UINT`。
4. Massive 启动时查询 format features 并 fail fast。
5. `VisibilityPipeline` 的 render pass、fragment output 与 shader path同步选择。
6. Expand、visibility、wireframe、shadow 和所有直接读取 visibility 的 renderer pipeline 成套选择 variant。
7. 保持现有 visibility draw → storage copy 与 barrier 状态机，增加 source/destination format 一致 assertion。
8. swapchain recreate、RenderView bank 和 secondary view framebuffer 复用相同 active format。

### 主要文件

- `src/Engine/Rendering/VulkanBaseRenderer.cpp`
- `src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp`
- `src/Engine/Rendering/PipelineCommon/CommonComputePipeline.*`
- `src/Engine/Rendering/Shadow/ShadowMapPass.*`
- `src/Engine/Rendering/RenderView.cpp`
- 各 renderer 的 pipeline 初始化文件

### 完成标准

- 未开启 Massive 时不创建 `R32G32_UINT` visibility images 或 massive pipelines；
- Massive 的所有 RenderView 使用 `R32G32_UINT`；
- image copy、visibility shading、wireframe、CSM 和 secondary RenderView 均无 validation error。

## 阶段 3：Massive buffers、NodeProxy storage 与 TLAS

### 工作

1. 扩展 `SoftMeshShaderResources`，加入 Massive NodeProxy device address、NodeCapacity 和 PrimitiveWordCount。
2. Massive Scene 创建 262140-capacity 独立 NodeProxy buffer；default 继续使用 SceneDynamic Nodes 区域。
3. visible-item buffer 按 active capacity × 5 slots 分配。
4. primitive 与 shadow primitive buffer 按 active record stride 分配和增长。
5. Compact/Finalize/Expand 使用 runtime NodeCapacity/PrimitiveWordCount 计算 slot base、clamp 与 record stride。
6. Finalize 把钳制后的 item count 写入对应的 `GPUDrivenStat::VisibleCount`，定义为实际提交给 Expand 的
   visible-item 数。
7. 所有资源尺寸使用 checked `VkDeviceSize`，在分配前记录预算。
8. TLAS instance capacity 改为 active render-proxy capacity，补齐 device limit、24-bit custom index 和实际数量检查。
9. 增加资源诊断：
   - active mode/capacity；
   - NodeProxy、visible items、main/shadow primitive bytes；
   - TLAS requested/allocated capacity；
   - Vulkan heap budget（可用时）。

### 主要文件

- `assets/shaders/common/BasicTypes.slang`
- `assets/shaders/common/GPUScene.slang`
- `src/Engine/Assets/Core/Scene.*`
- `src/Engine/Assets/Core/Scene.Build.cpp`
- `src/Engine/Assets/Core/Scene.Update.cpp`
- `src/Engine/Rendering/VulkanBaseRenderer.RayTracingAS.cpp`

### 完成标准

- Default 的 SceneDynamic size、visibility images 和 primitive stride 与修改前一致；
- Massive 可上传 262140-capacity NodeProxy buffer，而不移动 Materials/stats/HDRSH；
- 131070 个 proxies 不被 Compact/Finalize 钳制到 65535；
- buffer/TLAS 超限均在写入或 Vulkan build 前失败。

## 阶段 4：Massive Asteroid Belt

### 工作

1. 把现有 AsteroidBelt 构造拆为接受 `asteroidCount` 的内部 helper。
2. 保持 `AsteroidBelt.proc = 30000`，新增 `MassiveAsteroidBelt.proc = 131070`。
3. 继续共享 24 个 faceted asteroid models 和 12000 个 materials。
4. 调整 overview camera/分布，让超过 65535 个 proxies 同时在 frustum 中，并避免极端过绘制使 smoke test
   失去可运行性。
5. 保持 deterministic hash；不落盘巨型 glTF/GLB，不复制 mesh per node。
6. 添加场景构造测试，核对 node、section、proxy 和理论 triangle 数。

### 主要文件

- `src/Application/Common/DemoScenes.cpp`
- `src/Application/Common/DemoScenes.hpp`
- `src/Tests/`

### 完成标准

- 普通 AsteroidBelt 仍是 30000；
- Massive 场景恰好创建 131070 个 asteroid nodes；
- 每 node 一个 render proxy；
- expanded triangle requirement 为 10,485,600；
- 构造结果在相同 seed 下稳定。

## 阶段 5：gkNextMassiveBenchmark

### 工作

1. 新建 `src/Application/Render/gkNextBenchmark/gkNextMassiveBenchmark/`。
2. 复用 benchmark common 的计时、统计和 report 能力。
3. 构造阶段强制 Massive mode、1280×720、immediate present、1 sample。
4. 只加载 `MassiveAsteroidBelt.proc`，默认选择 SoftwareModernNoAmbient。
5. warm-up 后执行硬断言：
   - node/proxy count = 131070；
   - active capacity = 262140；
   - format = `R32G32_UINT`；
   - primitive stride = 8 bytes；
   - `GPUDrivenStat::VisibleCount > 65535`；
   - 无 primitive、visible-item、NodeProxy 或 TLAS overflow。
6. 输出 frame time、GPU time、FPS、VRAM、visible nodes、triangles 和各 massive buffer bytes。
7. 失败时返回非零退出码。
8. 将目标加入 benchmark CMake；新增文件需要 `--reconfigure`。

### 主要文件

- `src/Application/Render/gkNextBenchmark/CMakeLists.txt`
- `src/Application/Render/gkNextBenchmark/gkNextMassiveBenchmark/CMakeLists.txt`
- `src/Application/Render/gkNextBenchmark/gkNextMassiveBenchmark/*.hpp`
- `src/Application/Render/gkNextBenchmark/gkNextMassiveBenchmark/*.cpp`
- `src/Application/Render/gkNextBenchmark/Common/`

### 完成标准

- `gkNextMassiveBenchmark` 只在 Massive mode 下运行；
- benchmark 的 `GPUDrivenStat::VisibleCount` 实际跨越 65535；
- 正常完成返回 0，任何 contract 不满足返回非零。

## 阶段 6：验证与文档收口

### 构建

新增 target/CMake/shader ABI 后执行：

```powershell
gnb.bat build gkNextMassiveBenchmark gkNextUnitTests --reconfigure
```

Engine header、GPUScene ABI 和通用 renderer pipeline 影响面较广，最终再执行：

```powershell
gnb.bat build --all --reconfigure
```

构建必须串行等待 `gnb` 返回，不能并发启动第二个 CMake/Ninja/gnb。

### 自动验证

```powershell
out\build\windows\bin\gkNextUnitTests.exe
gnb.bat shot --target gkNextMassiveBenchmark --scene MassiveAsteroidBelt.proc --frames 60
```

增加专用 agent-validation script，至少断言：

- `scene.nodeCount == 131071`（131070 个 asteroid nodes + 1 个引擎环境节点）；
- 新增的 `engine.renderProxyCount == 131070`；
- 新增的 `engine.renderCapacity == 262140`；
- `engine.visibilityWords == 2`；
- `engine.gpuDrivenVisibleCount > 65535`；
- 截图成功且程序可受控退出。

### 默认路径回归

```powershell
gnb.bat shot --scene assets/models/playground.glb
gnb.bat shot --scene AsteroidBelt.proc --frames 60
out\build\windows\bin\gkNextVisualTest.exe
```

至少人工检查：

- default visibility、材质重建和 object ID；
- selected/hovered outline；
- wireframe；
- 四级 CSM；
- PathTracing、SoftwareTracing、SoftwareModern、SoftwareModernNoAmbient；
- secondary RenderView；
- Massive overview 中没有 32767 周期性的重复 geometry/material 或错 node transform。

### 设备覆盖

- 一台支持 ray query 的 Windows Vulkan GPU：Massive + TLAS；
- 一台 `ForceNoRT`/SoftwareModernNoAmbient 路径：Massive 基础 smoke；
- 若 CI 覆盖 macOS/Android，至少编译统一 shader 并验证不支持的 format/容量能清晰失败。

### 文档

实现完成后：

1. 将 design 状态改为“现行”，更新实际类名和最终资源字段；
2. 更新 `docs/guides/soft-mesh-shader-gpu-driven-submit.md`，记录 default/massive 两种 record；
3. 更新 `docs/README.md` 的状态文字；
4. 本 plan 完成后退出“现行计划”索引；耐久契约保留在 design/guide。

## 最终验收清单

- [x] Massive 默认关闭，普通应用无需修改参数。
- [x] Default 仍是 15/17 packed uint + `R32_UINT`。
- [x] Massive 是 `uint2` primitive + `R32G32_UINT`。
- [x] Massive active proxy capacity 为 262140。
- [x] SceneDynamic 默认布局未因 massive 扩容。
- [x] 131070-node MassiveAsteroidBelt 可加载。
- [x] GPU 实际处理/显示超过 65535 个 proxies。
- [x] visibility、wireframe、CSM、object ID 和支持的 renderer 正确。
- [x] TLAS 容量与 24-bit custom index 有显式检查。
- [x] 所有 byte-size 计算和资源分配有溢出/预算诊断。
- [x] `gkNextMassiveBenchmark` 成功返回 0，contract 失败返回非零。
- [x] targeted build、unit tests、agent validation、visual regression 和最终 full build 通过。
