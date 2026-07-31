---
title: "Massive Rendering Mode 与双 uint Visibility Buffer"
category: design
status: 提案（未实现）
owner: engine
created: 2026-07-30
last_updated: 2026-07-30
related_plan: ../plans/massive-rendering-mode-plan.md
---

# Massive Rendering Mode 与双 uint Visibility Buffer

## 目标

增加一个默认关闭、启动期选择的 `Massive` 渲染容量模式。在不改变默认渲染路径的前提下：

- 默认模式继续使用当前 `R32_UINT` visibility buffer 和 32-bit packed primitive；
- Massive 模式使用 `R32G32_UINT` visibility buffer，每像素分别保存 render proxy index 与 section-local triangle index；
- Massive 模式的首期工程容量为默认 `MAX_NODES` 的 4 倍，即 `65535 * 4 = 262140` 个 render proxies；
- 新增 `gkNextMassiveBenchmark`，固定开启 Massive 模式并加载包含 `65535 * 2 = 131070` 个 asteroid nodes 的场景；
- 宽格式解除当前 15-bit node / 17-bit triangle 分割。以后提高容量只需要调整资源预算和设备能力检查，不再修改 visibility ID 格式。

这里的容量单位是 **render proxy**，不是逻辑 Scene node。一个带多个 model sections 的 RenderComponent 会在
`Scene::UpdateNodesGpuDriven` 中展开为多个 `NodeProxy`，每个 section 占一个 render proxy。Massive benchmark
使用单-section asteroid model，使 131070 个 Scene nodes 恰好对应 131070 个 render proxies。

## 非目标与“无限容量”的准确含义

双 `uint` 让 visibility 编码不再把 node 与 triangle 竞争性地塞进同一个 32-bit word，因而不再存在 32767
这一格式上限。但它不意味着物理上或 API 上真正无限：

- `uint2.x == 0` 保留为背景，格式最多表达 `UINT32_MAX` 个 non-zero proxy slots；
- GPU-driven 总三角形数、indirect draw 的 `vertexCount` 和 dispatch 维度仍受 32-bit Vulkan 字段限制；
- hardware ray tracing 的 `VkAccelerationStructureInstanceKHR::instanceCustomIndex` 只有 24 bit；
- 当前带 selected/hovered/locked/danger flags 的 object ID 只保留 27 bit instance ID，ReSTIR temporal
  matching 也使用同一 mask；
- `maxStorageBufferRange`、buffer device address、TLAS `maxInstanceCount`、设备内存和主机内存都可能更早成为限制；
- 首期 Massive 工程上限明确设为 262140，而不是尝试分配接近 `UINT32_MAX` 的资源。

因此本文中的“可继续扩展”是指：visibility 格式不再是首要瓶颈，容量可以在设备预算允许时继续上调。

## 当前限制

Soft Mesh Shader 当前把一个 primitive 编成：

```text
bits 31..17: one-based render proxy index（15 bit，0 表示背景）
bits 16..0 : section-local triangle index（17 bit）
```

完整数据流是：

```text
Compact
  → SoftMeshShaderVisibleItem { instanceIdx, triCount, ... }
  → Expand 写 uint primitive stream
  → visibility / wireframe / shadow vertex shader 解码
  → visibility fragment 写 R32_UINT
  → transfer copy 到 storage visibility image
  → renderer compute shader 解码并重建 surface
```

虽然 `BasicTypes.slang::MAX_NODES` 和 `Scene::kMaxIndirectDrawCount` 已是 65535，primitive 中的 instance
字段只有 15 bit，所以第 32768 个 render proxy 会被 mask 截断。另一方面，SceneDynamic 的 Nodes 区域也只为
65535 个 `NodeProxy` 预留；继续上传会侵入紧随其后的 Materials 区域。Massive 模式必须同时解决编码、资源容量和
越界检查，不能只替换 shader mask。

## 模式契约

### 启动期选项

引入不可热切换的容量模式：

```cpp
enum class ERenderCapacityMode : uint8_t
{
    Default,
    Massive,
};
```

`Runtime::Config::Options` 保存该值，对外提供 `--massive` 启动参数，默认是 `Default`。
`gkNextMassiveBenchmark` 在 renderer 和 Scene 资源创建之前强制选择 `Massive`。

不使用运行时 CVar，原因是切换模式需要重建：

- render target bank；
- visibility render pass 与 framebuffer；
- GPU-driven primitive/visible-item buffers；
- Massive NodeProxy buffer；
- 相关 compute/graphics pipelines；
- TLAS instance buffer 和 TLAS。

如果未来需要切换，只能实现为完整 renderer/scene reload，并同时清空 temporal history；首期不承担该复杂度。

### 容量

| 项目 | Default | Massive（首期） |
| --- | ---: | ---: |
| render proxy capacity | 65535 | 262140 |
| visible item capacity / slot | 65535 | 262140 |
| primitive record | 1 × uint32 | 2 × uint32 |
| visibility image format | `VK_FORMAT_R32_UINT` | `VK_FORMAT_R32G32_UINT` |
| section-local triangle ID | 17 bit | 32 bit |
| one-based proxy ID | 15 bit | 32 bit |
| node storage | SceneDynamic Nodes 区域 | 独立 Massive NodeProxy buffer |

建议用一个不可变的 `FRenderCapacityLimits` 从模式推导容量和 stride，Scene、renderer、TLAS 和 benchmark
都读取同一份配置，禁止在 C++ 与 shader 管线中散落 `65535 * 4`。

## 双格式 visibility 契约

定义统一的逻辑 ID：

```slang
struct VisibilityId
{
    uint instanceIdx;  // one-based NodeProxy slot；0 表示背景
    uint triangleIdx;  // section-local triangle index
};
```

两种模式的物理表达：

```text
Default:
    primitive stream: uint packed
    visibility pixel: uint packed

Massive:
    primitive stream: uint2(instanceIdx, triangleIdx)
    visibility pixel: uint2(instanceIdx, triangleIdx)
```

所有 shader 只能通过集中 helper 访问该数据：

```slang
VisibilityId LoadExpandedPrimitive(uint primitiveIndex);
void StoreExpandedPrimitive(uint primitiveIndex, VisibilityId value);
VisibilityId LoadVisibility(int2 pixel);
bool IsVisibilityValid(VisibilityId value);
```

默认 helper 保持当前 15/17 编解码；Massive helper 直接 load/store `uint2`。禁止在 renderer shader、
SceneSampling、visual debugger 中继续出现手写的 `>> 17`、`0x7FFF` 或 `0x1FFFF`。

`instanceIdx` 是 GPU `NodeProxy` 数组中的 one-based slot，不是持久 Scene instance ID。对象选择与 object ID
仍从 `NodeProxy.instanceId` 取得，不改变编辑器、脚本或 Scene selection 的 ID 契约。

## Shader 变体

构建系统为受影响入口额外生成 `.massive.spv`，使用 `GK_MASSIVE_VISIBILITY=1`：

- Compact / CompactWave 及其 shadow 版本；
- Finalize；
- Expand；
- visibility vertex + fragment；
- wireframe vertex；
- shadow-map vertex；
-直接读取 visibility 的 renderer entry：PathTracing、SoftwareTracing、SoftwareModern、
  SoftwareModernNoAmbient、SHARC update/query；
- VisualDebugger。

未读取 visibility 的 pass 继续复用默认 SPIR-V。CMake 的 variant 列表必须是显式清单，不能为所有 shader
盲目构建 massive 副本。

renderer 初始化时按 `ERenderCapacityMode` 选择整套一致的资源格式和 shader。禁止把默认 Expand 与 massive
visibility pipeline 混用；debug build 应用格式/stride assertion 尽早发现组合错误。

Shader hot reload 需要识别同一 source 的 default/massive outputs。若首阶段暂未接入 variant-aware hot reload，
`gkNextMassiveBenchmark` 必须显式关闭 shader hot reload 并打印原因，不能把默认 SPIR-V 热替换进 massive pipeline。

## GPU 资源布局

### Visibility images

每个 RenderView bank 的 `RT_MINIGBUFFER_DRAW` 与 `RT_MINIGBUFFER` 必须使用相同格式：

- Default：`VK_FORMAT_R32_UINT`；
- Massive：`VK_FORMAT_R32G32_UINT`。

二者之间仍用现有 transfer copy，barrier 状态机不改变。Massive 启动时必须查询并验证
`VK_FORMAT_R32G32_UINT` 同时支持 color attachment、storage image、transfer source 和 transfer destination。

Visibility render pass 的 attachment format、fragment output 类型以及 framebuffer image view 必须一起选择，
不可只修改 storage image。

### Primitive streams

`SoftMeshShaderPrim` 和四级 `SoftMeshShaderShadowPrim` 的容量仍按“所有 render proxies 展开后的总三角形数”
计算，但 record stride 由模式决定：

```cpp
primitiveBytes = expandedTriangleCapacity * visibilityWordCount * sizeof(uint32_t);
shadowPrimitiveBytes = primitiveBytes * kSunShadowCascadeCount;
```

所有乘法先提升为 `uint64_t`/`VkDeviceSize` 并做 checked arithmetic。现有
`requiredGpuDrivenTriangleCapacity_` 可继续表示 record 数量，但 buffer byte size 不能再假设每 record 4 bytes。

### NodeProxy storage

不能简单把 `BasicTypes.slang::MAX_NODES` 改成 262140。当前
`GPU_SCENE_DYNAMIC_MATERIALS_OFFSET` 是由 `GPU_SCENE_NODE_PROXY_SIZE * MAX_NODES` 编译期计算的；直接扩大将让默认
模式额外常驻约 42 MiB，运行时采用不同常量还会让 C++/Slang 对 Materials、stats 和 HDRSH 的地址理解不一致。

Massive 模式应创建独立的 `MassiveNodeProxies` buffer，并在 `SoftMeshShaderResources` 增加：

```cpp
uint64_t NodeProxies;
uint32_t NodeCapacity;
uint32_t PrimitiveWordCount;
uint32_t Reserved0;
uint32_t Reserved1;
```

Massive shader 的 `GPUScene.Nodes` 从该 device address 读取；default shader 继续从 SceneDynamic 固定 Nodes
区域读取。Materials、GPU-driven stats 与 HDRSH 仍保持当前 SceneDynamic offsets，从而不改变默认布局。

`Scene::UpdateNodesGpuDriven` 在构建完 `nodeProxies` 后必须先校验容量再 map/copy。禁止截断，因为截断会使
visibility/TLAS/object ID 之间失去对应关系。

### Visible items 与 counters

Visible-item buffer 按 `nodeCapacity * kSoftMeshShaderDrawSlotCount` 分配。Massive shader 的 Compact、
Finalize、Expand 都使用资源表中的 `NodeCapacity`，slot offset 也用该值，不能继续用编译期默认 `MAX_NODES`。

Finalize 同时把钳制后的 `itemCount` 写入当前 slot 对应的 `GPUDrivenStat::VisibleCount`。该字段目前存在但没有
生产者；把它定义为“实际提交给 Expand 的 visible-item 数”后，benchmark 和运行时诊断可以直接证明是否跨过
旧的 65535 容量，而不需要从 Processed/Culled 间接推导。

Default 变体保留当前常量路径，保证默认性能与资源占用不变。

### TLAS

当前 TLAS instance buffer 以 `Scene::kMaxIndirectDrawCount` 为固定申请容量。Massive 模式改为使用选择后的
render proxy capacity，并保留：

- `VkPhysicalDeviceAccelerationStructurePropertiesKHR::maxInstanceCount` 启动检查；
- `std::bit_ceil` 后不超过 device limit 的检查；
- `VkAccelerationStructureInstanceKHR::instanceCustomIndex` 24-bit 检查；
- 实际 instance count 不超过已分配 capacity 的检查。

Massive benchmark 首选 `SoftwareModernNoAmbient`，用于隔离验证 visibility/GPU-driven 宽索引路径；另设一条
支持 RT 的设备验证，覆盖 TLAS 扩容但不作为低配机器的基础 smoke test。

## 内存预算

131070 个 asteroid nodes、每个共享单-section 80-triangle model 时：

- expanded primitive records：约 10,485,600；
- Massive main primitive stream：约 80 MiB；
- 四级 shadow primitive stream：约 320 MiB；
- 262140-capacity NodeProxy buffer：约 56 MiB；
- 262140-capacity、5 slots visible items：约 20 MiB；
- 262144-capacity TLAS instance input：约 16 MiB；
- 1280×720 的两张 `R32G32_UINT` visibility images：合计约 14 MiB。

仅上述核心资源已接近 0.5 GiB，尚未计入 vertices、materials、BLAS/TLAS、ambient resources、render targets
和驱动分配。Benchmark 应记录实际 buffer byte sizes 和 Vulkan memory budget；设备预算不足时必须在创建大
buffer 前给出所需/可用字节并退出，不能依赖 `VK_ERROR_OUT_OF_DEVICE_MEMORY` 才说明原因。

## Massive Asteroid Belt

保留现有 `AsteroidBelt.proc` 的 30000 nodes，不改变普通 demo 和 still benchmark 的负载。将场景构造重构为
可传入数量的共享 helper，并新增：

```text
MassiveAsteroidBelt.proc
asteroid node count = 65535 * 2 = 131070
model count = 24（共享）
material count = 12000（共享）
model sections per node = 1
triangles per section = 80
```

Massive 场景使用确定性 hash，不增加 131070 份 mesh。Overview camera 和 belt 分布需要保证超过 65535 个
proxies 同时处于 frustum；benchmark 以 `GPUDrivenStat::VisibleCount` 证明宽索引路径实际跨过旧上限，
不能只检查 CPU `Scene::GetNodeCount()`。

## gkNextMassiveBenchmark

新增目标：

```text
src/Application/Render/gkNextBenchmark/gkNextMassiveBenchmark/
```

它复用 `gkNextBenchmark/Common`，但不遍历全部 DemoScenes：

1. 构造阶段强制 `ERenderCapacityMode::Massive`；
2. 默认 1280×720、present immediate、1 sample；
3. 默认 renderer 为 `SoftwareModernNoAmbient`；
4. 只加载 `MassiveAsteroidBelt.proc`；
5. warm-up 后验证并报告：
   - Scene node count = 131070；
   - render proxy count = 131070；
   - configured node capacity = 262140；
   - visibility format = `VK_FORMAT_R32G32_UINT`；
   - primitive word count = 2；
   - `GPUDrivenStat::VisibleCount > 65535`；
   - expanded triangle count、CPU/GPU frame time 与 buffer memory；
6. 任一容量、格式或计数条件不满足时返回非零退出码。

Benchmark 应允许命令行覆盖 frame/warm-up 配置，但不允许关闭 Massive 模式，否则目标失去验证意义。

## 失败策略与不变量

- Default 是默认值；未传 `--massive` 的程序不创建 massive image、node buffer 或 shader pipeline。
- 模式一旦 renderer 初始化即不可改变。
- `nodeProxies.size() > activeNodeCapacity`：场景加载失败并报告 logical nodes、render proxies 与 capacity。
- 任何 model section 的 triangle count 不能超过 active visibility ID 的表达范围。
- Massive 下 `VK_FORMAT_R32G32_UINT` capability 不满足：启动失败，不能回退到会截断的 default format。
- primitive/visible/shadow/TLAS byte-size 计算全部使用 checked 64-bit arithmetic。
- `uint2.x == 0` 始终是背景；有效 proxy slot 始终 one-based。
- default 与 massive shader、image format、primitive stride 必须成套选择。
- 默认模式继续拒绝超过 32767 个可参与 visibility 的 render proxies，避免保留当前静默 mask 行为。

## 与现有架构的关系

本设计扩展而不替换 [Soft Mesh Shader GPU-Driven 提交路径](../guides/soft-mesh-shader-gpu-driven-submit.md)：
Compact → Finalize → Expand → single indirect draw 的结构保持不变。变化仅发生在 active capacity、expanded
primitive record、visibility attachment 和 NodeProxy storage 的物理表示。

具体实施顺序与验收门槛见 [Massive Rendering Mode 开发计划](../plans/massive-rendering-mode-plan.md)。
