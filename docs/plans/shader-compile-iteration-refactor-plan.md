---
title: "Shader 编译迭代提速重构方案"
category: plan
status: 草案
owner: engine
created: 2026-06-30
last_updated: 2026-06-30
related:
  - AGENT_GUIDE/HotReload.md
  - docs/plans/shading-sharc-decoupling-refactor.md
  - docs/guides/soft-mesh-shader-gpu-driven-submit.md
---

# Shader 编译迭代提速重构方案

> 给接手 agent：本文是交接计划，不是已实施记录。目标是降低 `assets/shaders` 的默认构建时间、减少 common 改动造成的重编面，并提高运行时 Slang hot reload 的反馈速度。优先顺序是：先让依赖图精确，再拆巨型 `Common`，最后做 per-pipeline reload。不要一上来大规模移动 shader 文件。

## 0. 结论摘要

当前慢的根因不是单个 shader 特别大，而是两个“全量化”叠在一起：

1. 构建层：`assets/CMakeLists.txt:158-164` 给每个入口 shader 都加了 `DEPENDS ${shader_common_files}`。`shader_common_files` 包含 `Common.slang`、`common/*.slang`、`third_party/*.h`。因此 common 任意文件变化都会让 45 个入口一起过期。
2. import 层：39 个入口直接 `import Common`。`Common.slang` 又 `__include` 了基础类型、shading、ray tracer、SHARC、path tracing、ambient cube、tonemap 等全部模块。轻量 pass 只要引入 `Common`，也会被迫走完整前端解析。
3. 运行时层：`ShaderHotReloader` 也把 `common/`、`third_party/`、`Common.slang` 统一看成所有入口的公共依赖；编译成功后 `VulkanBaseRenderer::ReloadShaders()` 直接 `RecreateSwapChain()`，也就是全 pipeline 重建。

建议分三步拿收益：

1. **低风险快赢**：接入 `slangc -depfile`，构建层不再把所有 common 文件硬挂到每个入口；同时把 debug/实验入口从默认构建里拿掉。
2. **主体收益**：把 `Common.slang` 从巨型聚合拆成可 import 的功能模块，先迁移 16 个 Basic/Bindless-only shader 和 11 个 post/raster shader。
3. **热重载收益**：hot reload 使用反向依赖图只编译受影响入口；后续再引入 shader/pipeline registry，只重建受影响 pipeline。

## 1. 当前现状数据

### 1.1 文件规模

本次盘点时间：2026-06-30。数据来自仓库当前 `assets/shaders`。

| 项 | 数量/行数 | 说明 |
|---|---:|---|
| 顶层入口 shader | 45 | `*.comp.slang` / `*.vert.slang` / `*.frag.slang` |
| 根模块 | 3 | `Common.slang`、`Bindless.slang`、`SoftMeshShaderCull.slang` |
| `common/` 模块 | 15 | 合计约 4949 行 |
| `third_party/sharc` 头 | 4 | 合计约 1445 行 |
| shader/header 总行数 | 约 10569 | 含入口、common、third_party |

`common/` 中最重的文件：

| 文件 | 行数 | 当前角色 |
|---|---:|---|
| `common/AmbientCube.slang` | 843 | ambient cube 采样、稀疏 brick、体素访问 |
| `common/BasicTypes.slang` | 736 | C++/Slang 共享 ABI 类型 |
| `common/PathTracingRenderer.slang` | 654 | path tracing 主循环、debug 输出 |
| `common/Shading.slang` | 558 | G-buffer、IBL、ray interface、primary ray caster |
| `common/RayTracers.slang` | 531 | HW ray query、software tracer、voxel DDA |
| `common/ConstFunc.slang` | 426 | 数学、随机、pack/unpack、heatmap |
| `common/Sharc.slang` | 395 | SHARC adapter |
| `common/AmbientCubeBaker.slang` | 337 | probe bake helper |

### 1.2 当前 import fanout

直接 import/include 统计：

| 依赖 | 直接使用数 |
|---|---:|
| `Common` | 39 |
| `Bindless` | 33 |
| `common/ShaderClock.slang` | 5 |
| `SoftMeshShaderCull` | 4 |
| `PreProcessor.slang` | 8 |

`Common.slang` 当前内容：

```slang
import Bindless;

module Common;

__include "common/BasicTypes.slang";
__include "common/Shading.slang";
__include "common/RayTracers.slang";
__include "common/RadianceCache.slang";
__include "common/Sharc.slang";
__include "common/PathTracingRenderer.slang";
__include "common/ConstFunc.slang";
__include "common/Tonemap.slang";
__include "common/GeneralFunc.slang";
__include "common/AmbientCube.slang";
__include "common/AmbientCubeBaker.slang";
__include "common/GPUScene.slang";
```

更关键的是，`common/*.slang` 头部普遍写死 `implementing Common;`，`BindlessTexture.slang` 写死 `implementing Bindless;`。因此拆模块前要先做“module-neutral include”spike，不能直接新建模块然后把旧文件 `__include` 进去。

### 1.3 本地 slangc profile

本机使用 `external/VulkanSDK/1.4.341.1/Bin/slangc.exe`，版本 `2026.1-52-gc8ddf20bb`，顺序编译 45 个入口到临时目录，失败数 0，总耗时约 **28.6s**。

最慢的入口：

| Shader | 单次编译耗时 |
|---|---:|
| `Bake.SwAmbientCube.comp.slang` | 1178 ms |
| `Core.SharcQuery.comp.slang` | 962 ms |
| `Core.SharcUpdate.comp.slang` | 955 ms |
| `Core.SwTracing.comp.slang` | 945 ms |
| `Bake.HwAmbientCube.comp.slang` | 882 ms |
| `Core.SwModern.comp.slang` | 853 ms |
| `Core.PathTracing.comp.slang` | 846 ms |

对比几个轻量入口：

| Shader | import 形态 | 单次编译耗时 |
|---|---|---:|
| `UI.ImGui.vert.slang` | 无 `Common` | 137 ms |
| `Splat.SortPrefix.comp.slang` | 无 `Common` | 148 ms |
| `Remote.BgraToNv12.comp.slang` | 只 `Bindless` | 227 ms |
| `Util.BufferClear.comp.slang` | `Bindless + Common` | 636 ms |
| `Rast.VisibilityPass.frag.slang` | `Common` | 606 ms |
| `Core.GTAO.comp.slang` | `Bindless + Common` | 658 ms |

这说明轻量 pass 的主要成本来自 `Common` 固定开销，而不是 shader 自身逻辑。

### 1.4 入口分桶

按当前代码实际依赖粗分：

| 桶 | 数量 | 入口 |
|---|---:|---|
| 不依赖 `Common` | 6 | `UI.ImGui.*`、`Remote.BgraToNv12`、`Splat.Billboard.frag`、`Splat.SortPrefix`、`Rast.Wireframe.frag` |
| Basic/Bindless-only | 16 | visibility/shadow/wireframe vertex/frag、splat sort、soft mesh task、skinning、buffer clear 等 |
| Raster/Post | 11 | GTAO、reproject、compose、denoise、FSR、visual debugger、NoAmbient |
| RayTracing/GI | 7 | path tracing、sw tracing、sw modern、voxel tracing、ambient bake/clear |
| SHARC | 3 | `Core.SharcUpdate/Resolve/Query` |
| Debug/unused | 2 | `Remote.BgraToYuv`、`Util.SharcCompileTest` |

## 2. 发现的清理点

### 2.1 默认构建里疑似不该编的入口

以下入口当前会被 CMake 默认编译，但没有明确运行时引用：

| Shader | 现状 | 建议 |
|---|---|---|
| `Remote.BgraToYuv.comp.slang` | 未找到 `.spv` 运行时引用；当前 remote pipeline 使用 `Remote.BgraToNv12.comp.slang.spv` | 移入 debug/test profile，或默认不编译。保留源码直到确认 I420 fallback 不再需要。 |
| `Util.SharcCompileTest.comp.slang` | 只在历史计划文档和 CMake/hot reload define 里出现，是 SHARC compile spike | 移入 shader test profile；由 `gnb shaders check --tests` 或 `GK_BUILD_SHADER_TESTS=ON` 编译。 |

不要直接删除。先改为默认不参与 runtime 构建，确认 CI 和开发流程没有依赖后再删或归档。

### 2.2 重复代码候选

这些重复会让模块拆分更有收益：

| 重复区域 | 证据 | 建议抽出 |
|---|---|---|
| ObjectId flags/outline | `Shading.slang`、`Process.DenoiseJBF`、`Process.ComposeSimple`、`Process.ReProject*`、`Atrous` 都有相同 flag 常量 | `common/ObjectId.slang` 或 `Shader.ObjectId` |
| GTAO depth helpers | `Core.GTAO` 和 `Process.GTAOCompose` 都有 `IsSurfaceDepth` / `ReconstructViewPosition` | `common/PostDepth.slang` |
| ReProject variants | `Process.ReProject` 与 `Process.ReProjectSimple` 非空重复行约 169/237 | 先抽 shared helper，再评估是否合并入口 |
| Compose variants | `Process.DenoiseJBF` 与 `Process.ComposeSimple` 非空重复行约 55/88 | 抽 `Shader.Compose`，保留两个入口 |
| Splat types | `GaussianSplat` / `SplatModelState` 在 3 个 splat shader 重复 | `Splat.Common.slang` |
| SoftMesh wave/LDS variants | 主 pass 和 shadow pass 的 wave/LDS 变体仍有大量共享逻辑 | 保留变体入口，继续向 `SoftMeshShaderCull.slang` 或 `SoftMeshGpuCullCommon.slang` 抽 helper |
| UI push constants | `UI.ImGui.vert` 和 `UI.ImGui.frag` 重复 `PushConsts` | 可选抽 `UI.ImGuiCommon.slang`，收益小 |

注意：重复代码清理是第二优先级。它能减少维护成本，但默认构建变快的核心仍是 import 图和 CMake 依赖图。

## 3. 目标架构

### 3.1 模块层次

目标不是把所有东西拆得很碎，而是建立稳定的层次，避免 post pass 依赖 path tracing/GI。

建议模块边界：

| 层 | 模块 | 内容 |
|---|---|---|
| L0 ABI | `Shader.Types` | `BasicTypes.slang`。C++/Slang 共享类型，保持低层、少改、无高级 helper。 |
| L0 Bindless | `Bindless` | texture/storage/shadow map 数组与 RT slot 常量。保留现有模块名，尽量不动 C++ include 关系。 |
| L1 GPUScene | `Shader.GpuScene` | push constant `GPUScene gpuScene`、`Bindless.GetGpuscene()`、`GetViewStorageTexture*()`。 |
| L1 Utilities | `Shader.Math`、`Shader.Tonemap`、`Shader.ObjectId`、`Shader.PostDepth` | 数学/随机、HDR/tonemap、object id flag、depth reconstruct 等可复用小模块。 |
| L2 Raster | `Shader.GBuffer`、`Shader.ShadowCSM` | `FetchGBuffer`、motion vector、IBL、CSM PCF。不要引入 ray tracer/path tracer。 |
| L2 Ray | `Shader.RayInterfaces`、`Shader.RayTracers` | `IPrimaryRayCaster` / `IRayTracer` / HW ray query / SW tracer / voxel DDA。 |
| L2 GI | `Shader.AmbientCube`、`Shader.AmbientBake`、`Shader.RadianceCache`、`Shader.Sharc` | ambient cube、baker、radiance cache interface、SHARC adapter。 |
| L3 Renderers | `Shader.PathTracing`、`Shader.SwModernTracing` | `FPathTracingRenderer` 等高层渲染 loop，只给 path/sw tracing/SHARC/bake 用。 |
| Domain | `Splat.Common`、`SoftMeshShaderCull`、`UI.ImGuiCommon` | 子系统局部共享，不进全局 Common。 |

`Common.slang` 在迁移期保留为兼容 wrapper，但标记 deprecated。新入口不得再 `import Common`。

### 3.2 Module-neutral include 方案

当前每个 `common/*.slang` 几乎都写死：

```slang
implementing Common;
```

这会阻止同一文件被多个新模块复用。建议第一步做小 spike：

1. 从 `common/*.slang` 去掉顶层 `implementing Common;`，依赖“被 `__include` 到某个 `module X;` 后自然进入当前模块”的行为。
2. `Common.slang` 仍然按旧顺序 include 所有文件，验证 45 个入口输出正常。
3. `BindlessTexture.slang` 同理，去掉 `implementing Bindless;` 后由 `Bindless.slang` 包住。
4. 如果 Slang 不接受这种写法，再退回 body include 模式：
   - `common/BasicTypes.body.slang` 放实际内容。
   - `common/BasicTypes.slang` 作为旧兼容 wrapper。
   - 新模块 include `.body.slang`。

验收：只改 module 声明/包装，不改函数内容，`./gnb.bat build gkNextRenderer gkNextUnitTests` 通过。

### 3.3 入口 manifest

建议引入显式 manifest，取代“顶层 glob 决定一切”的规则。短期可以是 CMake list，长期用 TOML/JSON 供 CMake、hot reload、文档工具共用。

推荐字段：

```toml
[[shader]]
source = "Core.PathTracing.comp.slang"
stage = "compute"
profile = "runtime"
owner = "PathTracingRenderer"
reload_group = "path_tracing"
defines = ["SHADER_CLOCK?"]

[[shader]]
source = "Util.SharcCompileTest.comp.slang"
stage = "compute"
profile = "test"
owner = "SHARC compile spike"
defines = ["GK_ENABLE_OFFICIAL_SHARC", "SHARC_UPDATE=1", "SHARC_QUERY=1"]
```

收益：

1. 默认 runtime 构建不再自动吃进 test/debug shader。
2. 每个 shader 的 owner 和 reload group 明确，后续 per-pipeline reload 有登记点。
3. 新增入口不依赖 CMake glob 重扫。

如果短期不做 manifest，至少把 `shader_files` 改为 `file(GLOB CONFIGURE_DEPENDS ...)`，避免新增 shader 必须人工 `--reconfigure` 的问题。

## 4. 构建系统重构

### 4.1 接入 slangc depfile

当前 `slangc` 支持：

```powershell
slangc.exe <entry> -target spirv -entry main -o <out.spv> -depfile <out.d>
```

本地验证 `Core.GTAO.comp.slang` 的 depfile 会列出真实 import/include 文件。当前它仍列出全部 `Common.slang` 子依赖，因为 `Common` 本身 include 了全部模块；但等模块拆分后，depfile 会自然变窄。

CMake 目标改造建议：

```cmake
set(dep_file "${output_file}.d")
add_custom_command(
    OUTPUT ${output_file}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
    COMMAND ${Vulkan_SLANGC}
            ${full_path}
            -o ${output_file}
            -entry main
            -target spirv
            -depfile ${dep_file}
            ${slangc_args}
    DEPENDS ${full_path}
    DEPFILE ${dep_file}
    COMMENT "slangc ${file_name} -> ${file_name}.spv"
    VERBATIM
)
```

注意点：

1. `shader_common_files` 可以继续放在 `SOURCES` 里供 IDE 展示，但不要继续放进每个入口的 `DEPENDS`。
2. Windows Ninja depfile 路径 escaping 已由 slangc 输出为 `P\:\\...` 形态，落地前要在当前 generator 上做一次 clean build spike。
3. MSBuild/Visual Studio generator 对 `DEPFILE` 支持有限时，可保留保守 fallback：非 Ninja 仍用旧的 `shader_common_files`，Ninja 使用精确 depfile。
4. `-I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"` 可以显式加上，避免移动模块后 import resolution 依赖工作目录。

### 4.2 并行与 profile

目前 shader entry 是独立 custom command，Ninja/MSBuild 本来可以并行。真正的问题是过期面太大。不要先花时间手写并行编译器。

优化等级建议作为后续开关：

| 选项 | 本地观测 | 建议 |
|---|---|---|
| 默认 `-O1` | 当前默认 | 保持 release/runtime 构建默认 |
| `-O0` | 对 `Core.PathTracing` 从约 903 ms 降到约 799 ms；对轻 pass 帮助很小 | 可做 Debug/hot reload 可选项 |
| `-minimum-slang-optimization` | 单独收益不明显；`-O0 + minimum` 对重 shader 有小幅帮助 | 不作为主线，后续单独 spike |

## 5. Hot Reload 重构

### 5.1 Phase A：只减少编译数量，仍全量 pipeline reload

先改 `ShaderHotReloader::GatherCompileRequests()`，不要一开始碰 renderer 生命周期。

目标行为：

1. 启动时扫描 `assets/shaders`，建立 entry -> dependencies 和 dependency -> entries 的反向图。
2. 依赖图来源优先级：
   - 读取 output 目录现有 `.spv.d` depfile。
   - depfile 缺失时，用项目内轻量 parser 解析 `import X`、`__include "..."`、`#include "..."`，递归解析。
   - parser 失败时回退当前保守策略。
3. 检测到某个 source 变化，只编译依赖它的入口。
4. 编译成功后更新该入口的 depfile/内存图。
5. 编译失败时保留旧 SPIR-V 和旧 pipeline，沿用现有失败节流逻辑。

这一阶段即使编译 1 个 shader，仍调用现有 `renderer_->ReloadShaders()`。收益来自少编译，风险低。

### 5.2 Phase B：按 changed SPV 传递 reload 信息

扩展接口：

```cpp
struct FShaderReloadResult
{
    std::vector<std::filesystem::path> compiledSpv;
    bool anyDependencyGraphChanged = false;
    bool requiresFullRendererReload = false;
};

renderer_->ReloadShaders(compiledSpv);
```

Phase B 可以先内部仍然 `RecreateSwapChain()`，但日志里打印 affected shaders，为 Phase C 铺路。

### 5.3 Phase C：Shader/Pipeline Registry

目标是只重建受影响 pipeline，而不是重建 swapchain。建议从 compute pipeline 开始，因为它们只有一个 `.spv`。

登记信息：

| 字段 | 用途 |
|---|---|
| `shaderSpvPaths` | 一个 pipeline 对应一个 compute shader，或 graphics 的 vert+frag |
| `ownerName` | 日志与 debug UI |
| `reloadGroup` | 例如 `post`, `path_tracing`, `soft_mesh`, `shadow`, `ui`, `splat` |
| `recreateCallback` | WaitIdle 后重建该 pipeline |
| `layoutStable` | false 时回退 full reload |

落地顺序：

1. `PipelineCommon::ZeroBindPipeline` / `ZeroBindCustomPushConstantPipeline` / `ZeroBindWithTLASPipeline` 存 shader path，并注册 recreate callback。
2. 先覆盖 post/compute：`Process.*`、`Util.BufferClear`、`Core.GTAO`、`Splat.Compose`。
3. 再覆盖 graphics pair：visibility/wireframe/shadow/UI/splat billboard。任一 vert/frag 变化时重建 pair。
4. 涉及 render target 格式、push constant 尺寸、descriptor set layout 的变化先标记 `requiresFullRendererReload`。

## 6. 分阶段执行计划

### Phase 0：基线和护栏

任务：

1. 写一个临时或正式的 shader profile 命令，记录 45 个入口单次 slangc 耗时。
2. 记录当前 `gnb build gkNextRenderer gkNextUnitTests` 的 shader 重编行为。
3. 记录 `gnb shot --scene assets/models/playground.glb` 的基线截图。

验收：

1. 有 profile 表，至少包含 top 10 慢 shader。
2. 没有源码行为变更。

### Phase 1：构建 depfile + debug/test shader 默认剥离

任务：

1. CMake custom command 加 `-depfile` 和 `DEPFILE`。
2. 从每个入口的 `DEPENDS` 移除全量 `shader_common_files`。
3. `Remote.BgraToYuv`、`Util.SharcCompileTest` 改为 test/debug profile，不进默认 runtime shader target。
4. CMake 与 `ShaderHotReloader.cpp` 中 SHARC define 规则保持一致，避免 test profile 漏宏。

验收：

1. Clean build 生成 `.spv` 和 `.spv.d`。
2. 修改 `Remote.BgraToNv12.comp.slang` 只重编自身。
3. 修改 `common/Tonemap.slang` 在旧 `Common` 未拆前仍会影响大部分 Common 入口，这是预期。
4. `./gnb.bat build gkNextRenderer gkNextUnitTests` 通过。

### Phase 2：module-neutral include spike

任务：

1. 让 `common/*.slang` 不再写死 `implementing Common;`，或引入 `.body.slang` 模式。
2. `Common.slang` 保持兼容 wrapper，旧入口不改。
3. `BindlessTexture.slang` 同理从 `implementing Bindless;` 解耦。

验收：

1. 所有 43 个 runtime shader 仍能编译。
2. `BasicTypes.slang` 的 C++ include 仍可用，`Scene.cpp` 的 static_assert 不变。
3. 不引入运行时行为变化。

### Phase 3：迁移 Basic/Bindless-only 入口

优先迁移这 16 个入口，因为它们当前被 `Common` 拖累最明显：

```text
Rast.ShadowMap.frag.slang
Rast.ShadowMapSoftMeshShader.vert.slang
Rast.VisibilityPass.frag.slang
Rast.VisibilityPassSoftMeshShader.vert.slang
Rast.WireframeSoftMeshShader.vert.slang
Splat.Billboard.vert.slang
Splat.SortHistogram.comp.slang
Splat.SortScatter.comp.slang
Task.Skinning.comp.slang
Task.SoftMeshShaderExpand.comp.slang
Task.SoftMeshShaderFinalize.comp.slang
Task.SoftMeshShaderGpuCullCompact.comp.slang
Task.SoftMeshShaderGpuCullCompactWave.comp.slang
Task.SoftMeshShaderShadowGpuCullCompact.comp.slang
Task.SoftMeshShaderShadowGpuCullCompactWave.comp.slang
Util.BufferClear.comp.slang
```

任务：

1. 新建 `Shader.Types` / `Shader.GpuScene` 级别模块。
2. 入口从 `import Common` 改为只 import 所需模块。
3. Splat 类型重复可同步抽 `Splat.Common`。
4. SoftMesh 已有 `SoftMeshShaderCull.slang`，继续保留，不要强行合并 wave/LDS 入口。

验收：

1. `Util.BufferClear`、visibility/shadow 这类入口单次编译应接近 150-250 ms，而不是 600 ms 级。
2. 修改 `common/RayTracers.slang` 不再触发这些入口重编。
3. visibility、shadow、soft mesh 路径截图正常。

### Phase 4：迁移 Raster/Post 入口

迁移目标：

```text
Core.GTAO.comp.slang
Core.SwModernNoAmbient.comp.slang
Process.AtrousWavelet.comp.slang
Process.ComposeSimple.comp.slang
Process.DenoiseJBF.comp.slang
Process.GTAOCompose.comp.slang
Process.ReProject.comp.slang
Process.ReProjectSimple.comp.slang
Process.UpScaleFSR.comp.slang
Splat.Compose.comp.slang
Util.VisualDebugger.comp.slang
```

任务：

1. 抽 `Shader.Tonemap`，让 compose/JBF/Voxel 之外的入口不依赖 tonemap。
2. 抽 `Shader.ObjectId`，消除 object flag 常量复制。
3. 抽 `Shader.PostDepth`，服务 GTAO 与 GTAOCompose。
4. `Core.SwModernNoAmbient` 依赖 GBuffer/IBL/CSM，但不应依赖 `FPathTracingRenderer` 和 ray tracer。
5. `Process.ReProject` 与 `Process.ReProjectSimple` 先抽 helper，再评估合并入口。不要在同一阶段改变 temporal 行为。

验收：

1. 修改 `Shader.Tonemap` 只重编 compose/JBF/需要 tonemap 的少数入口。
2. 修改 `Shader.RayTracers` 不影响 post pass。
3. `gnb shot --scene assets/models/playground.glb` 在 SwModernNoAmbient 下无明显回归。

### Phase 5：迁移 RayTracing/GI/SHARC 重入口

迁移目标：

```text
Bake.ClearAmbientCubeCache.comp.slang
Bake.HwAmbientCube.comp.slang
Bake.SwAmbientCube.comp.slang
Core.PathTracing.comp.slang
Core.SwModern.comp.slang
Core.SwTracing.comp.slang
Core.VoxelTracing.comp.slang
Core.SharcUpdate.comp.slang
Core.SharcResolve.comp.slang
Core.SharcQuery.comp.slang
```

任务：

1. 把 ray interface 从 `Shading.slang` 中独立出来，避免 raster-only helper 和 ray tracer 互相拖拽。
2. `FPathTracingRenderer` 只依赖 ray interface、ray tracer、radiance cache、必要的 ambient/tonemap。
3. `Sharc.slang` 只被 SHARC 三 pass 和必要的 compile test profile 引入。
4. `AmbientCubeBaker.slang` 只被 bake pass 引入，不进入 path tracing 常规入口，除非确实需要。

验收：

1. 修改 `Sharc.slang` 只重编 SHARC 入口和 test profile。
2. 修改 `AmbientCubeBaker.slang` 只重编 bake 入口。
3. PathTracing、SwTracing、SwModern 三模式截图正常。

### Phase 6：Hot reload 依赖图精确化

任务：

1. `ShaderHotReloader` 读取 `.spv.d` 或自建 parser，建立 reverse dependency map。
2. common 文件变化只编译受影响入口。
3. 日志输出 changed source、affected entries、compile count、elapsed。
4. Editor hot reload panel 增加最近一次 affected shader 数量。

验收：

1. 修改 `Process.ComposeSimple.comp.slang`，日志显示编译 1 个入口。
2. 修改 `Shader.Tonemap`，日志显示只编译 tonemap consumers。
3. 修改 `Shader.Types` / `BindlessTexture` 仍可触发大范围重编，这是合理的 ABI 变化。

### Phase 7：Per-pipeline reload

任务：

1. 建立 shader/pipeline registry。
2. compute pipeline 先支持局部重建。
3. graphics pipeline pair 再支持局部重建。
4. 布局变化、RT 格式变化、push constant size 变化回退 full reload。

验收：

1. 修改 `Process.ComposeSimple` 不再 `RecreateSwapChain()`。
2. 修改 visibility vert/frag 只重建 visibility pipeline pair。
3. 修改 `BasicTypes` 等 ABI 文件仍 full reload。

## 7. 风险和注意事项

1. **`BasicTypes.slang` 是 ABI 文件**：`src/Engine/Assets/GPU/UniformBuffer.hpp` 直接 include 它，`Scene.cpp` 有多处 static_assert。拆模块时不得改变结构布局、常量值、对齐宏。
2. **Slang module 语义先 spike**：当前 `implementing Common;` 写死较深。先小范围验证，再批量迁移。
3. **不要一次删除 `Common.slang`**：迁移期它是兼容 wrapper。只有 `rg "import Common" assets/shaders` 为空后，才考虑删除或改成仅用于 legacy/test。
4. **不要改第三方 SHARC 头**：`assets/shaders/third_party/sharc` 保持原样，adapter 层在 `common/Sharc.slang` 或新 `Shader.Sharc` 处理。
5. **不要把 shader 合并作为第一收益来源**：很多入口是 pipeline ABI 或运行时能力分支造成的合理变体。先抽 shared helper，再决定是否合并。
6. **depfile 不是 import 设计的替代品**：如果入口仍 `import Common`，depfile 会忠实记录全部 `Common` 子依赖，构建层无法凭空变窄。
7. **pipeline reload 要保守**：shader layout 变化时局部重建可能不够，必须有 full reload fallback。

## 8. 建议验收命令

按改动范围使用 targeted build：

```powershell
./gnb.bat build gkNextRenderer gkNextUnitTests
```

渲染肉眼验证：

```powershell
./gnb.bat shot --scene assets/models/playground.glb
```

建议新增或临时使用的 shader profile 命令：

```powershell
# 建议后续实现到 gnb：输出每个入口 slangc 耗时、dep 数量、import Common 状态
./gnb.bat shaders profile

# 建议后续实现到 gnb：检查 manifest、depfile、未引用入口、重复模块候选
./gnb.bat shaders audit
```

## 9. 完成定义

重构完成不要求 shader 数量大幅下降，真正的完成标准是：

1. 默认 runtime shader target 不再编译 debug/test 入口。
2. `shader_common_files` 不再作为每个入口的硬依赖。
3. `import Common` 从 runtime 入口中清空，或只保留在明确 legacy/test 入口中。
4. 修改 `Tonemap/ObjectId/PostDepth/Sharc/AmbientBake` 这类中层模块时，重编入口数符合实际消费者，而不是接近全量。
5. hot reload 日志能清楚说明 affected source、affected entries、compile count、pipeline reload 策略。
6. `gkNextRenderer`、`gkNextUnitTests`、至少一张 `gnb shot` 通过。
