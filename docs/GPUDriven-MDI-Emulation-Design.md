# MDI 模拟方案设计（替代 MultiDrawIndirect 的 GPU-Driven 提交路径）

> 状态：设计草案 / 待实现
> 作者：AI 调研（Claude）
> 目标读者：后续负责实现的 AI agent 与引擎开发者
> 关联文章：[Tellusim — Mesh Shader Emulation](https://tellusim.com/mesh-shader-emulation/)

## 0. TL;DR（给实现者的一句话）

新增一条**与 MDI 等价但只用单次 draw 调用**的 GPU-driven 提交路径：culling compute 把所有可见三角形压实进一个 `EmuPrim`（每三角形 1×`uint`，值 = 现有 visibility 编码 `(instanceIdx<<17)|triangleIdx`）缓冲，配套写出 `VkDrawIndirectCommand`，渲染时用 `vkCmdDrawIndirect(count=1)` 的**非索引**单次 draw，顶点着色器从 `EmuPrim[SV_VertexID/3]` 反查 node/三角形并自取顶点。通过 CVar `r.drawSubmitMode` 在 `MDI`（默认）与 `Emulated` 间切换，并在设备缺少 `multiDrawIndirect` / `drawIndirectFirstInstance` 时自动回退到 `Emulated`。该方案**输出与现有 MDI 路径逐像素一致**（复用 visibility buffer 语义），无需重新组织几何数据。

---

## 1. 背景与动机

### 1.1 现状：引擎依赖 MultiDrawIndirect

gkNextEngine 的 SoftwareModern（光栅 + GPU CSM）渲染路径是 GPU-driven 的：每个「可绘制 model section」对应一条 `VkDrawIndexedIndirectCommand`，由 culling compute shader 逐条填充（剔除时把 `instanceCount` 置 0），最后用**一次** `vkCmdDrawIndexedIndirect(count = batchCount)` 提交全部批次。

这要求设备无条件支持两个特性（[`VulkanBaseRenderer.cpp:597`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）：

```cpp
deviceFeatures.multiDrawIndirect = true;        // drawCount > 1 的间接绘制
deviceFeatures.drawIndirectFirstInstance = true; // 间接绘制里 firstInstance != 0
```

### 1.2 问题

参考 Tellusim 的系列实测（见 §2），MDI 存在两类问题：

1. **兼容性**：部分设备/驱动不支持 `multiDrawIndirect` 或 `drawIndirectFirstInstance`（尤其移动端、部分 Vulkan-on-Metal 路径）。本引擎当前把它们设为**强制**，在这些设备上直接无法创建逻辑设备。
2. **效率**：在 TBR（Tile-Based，移动 GPU）和 Metal（MoltenVK 把 MDI 退化为 CPU 循环）上，MDI 提交效率很低；当单批 primitive 数较少而批次很多时尤其糟糕。Tellusim 的结论是：**把大量小 DIP 打包成单次 draw，在 AMD / Qualcomm 上反而最快**，即使要额外生成索引/图元缓冲。

### 1.3 目标

- 新增一条**单次 draw** 的 GPU-driven 提交路径（下文称 **Emulated 模式**），作为 MDI 的等价替代。
- 通过**选项开关**在 MDI / Emulated 间切换；设备不支持 MDI 时**自动**用 Emulated。
- **不改变渲染结果**：visibility buffer 编码、resolve、时序复用（ReProject/Denoise）必须逐像素一致。
- **不要求重新组织几何数据**：复用已有的 `Vertices / Reorders / primAddress(Indices) / Offsets` 缓冲。
- 覆盖三处当前用 MDI 的 draw：主 visibility pass、wireframe overlay、4 级 CSM shadow。

非目标（本期不做，列入 §11 备选）：完整 meshlet 化、硬件 Mesh Shader 后端、compute 软光栅化。

---

## 2. 文章方法摘要（Tellusim）

> 来源：[Mesh Shader Emulation](https://tellusim.com/mesh-shader-emulation/)、[Mesh Shader vs MultiDrawIndirect](https://tellusim.com/mesh-shader/)、[MultiDrawIndirect and Metal](https://tellusim.com/metal-mdi/)、[Compute vs Hardware](https://tellusim.com/compute-raster/)、[Dispatch, Dispatch, Dispatch](https://tellusim.com/dispatch/)。

**核心技巧（"Mesh Shader emulation trick"）**：

- 不用硬件 Mesh Shader，而是用 **compute shader 生成一个索引缓冲**，再用**单次 draw** 渲染全部几何。
- 关键编码：顶点索引被拆解——`meshlet = gl_VertexIndex >> 8`，`vertex = gl_VertexIndex & 0xff`（meshlet ≤ 256 顶点，故低 8 位够用），顶点数据从 SSBO 里 pull。
- **免费的逐三角形剔除**：因为索引由 compute 生成，可以在生成时顺手做背面/视锥/遮挡剔除，被剔除的三角形不进入索引缓冲，省带宽。

**对比维度（性能实测里的术语）**：

| 术语 | 含义 |
|---|---|
| **MDI** | MultiDrawIndirect，一次提交多条间接命令（本引擎当前方案） |
| **ICB** | Metal Indirect Command Buffer |
| **Loop** | CPU 循环多次 `drawIndirect()`（每次 count=1） |
| **Single DIP / Emulation** | compute 压实成**单个** draw call |

**关键结论**：

- 需要画大量小 DIP 时，**在 AMD / Qualcomm 上打包成单次 draw 最快**，单 DIP 甚至优于 Mesh Shader 和 MDI（含索引生成开销）。
- primitive 数 > 128 时，MDI 才开始超过 Mesh Shader。
- **MDI 在移动端（TBR）表现差**；Metal 上 MDI 被退化处理。
- 任何 Mesh Shader 配置都会把硬件三角形吞吐打到约 1/3 ～ 1/4（Intel Arc 上 Mesh Shader 损失约 4× 吞吐，emulation trick 反而更快）。

**对本引擎的启示**：我们不需要照搬「meshlet + 低 8 位编码」（那需要把几何切成 ≤256 顶点的 meshlet）。本引擎已经是 **programmable vertex pulling**（光栅管线 `vertexBindingDescriptionCount = 0`，顶点从 BDA 取），所以可以用「单次非索引 draw + 逐图元 pull 缓冲」更直接地落地同样的收益。详见 §5。

---

## 3. 现有 MDI 实现剖析

### 3.1 数据准备（CPU 侧）

`Scene::UpdateNodesGpuDriven()`（[`Scene.cpp:1193`](../src/Engine/Assets/Core/Scene.cpp)）：

- 遍历节点，对每个可绘制 `RenderComponent` 的每个 model section 生成一个 `NodeProxy`，push 进 `nodeProxys`，并令 `indirectDrawBatchCount_++`。
- `NodeProxy`（[`BasicTypes.slang:145`](../assets/shaders/common/BasicTypes.slang)）携带 `worldTS`、`modelId`、`skinId`、`matId[16]`、`visible`、选中/hover/lock 状态位等。
- `nodeProxys` 上传到 scene dynamic buffer 的 `GPU_SCENE_DYNAMIC_NODES_OFFSET` 段。
- `indirectDrawBatchCount_` = 当前帧批次数（draw 调用的 `drawCount`）。

`ModelData`（[`BasicTypes.slang:160`](../assets/shaders/common/BasicTypes.slang)）：`indexOffset / indexCount / vertexOffset / vertexCount / localAabbMin/Max / reorderOffset`。

### 3.2 几何缓冲布局（关键，决定编码可行性）

`Scene::RebuildMeshBuffer()`（[`Scene.cpp:540`](../src/Engine/Assets/Core/Scene.cpp)）对每个 section：

- 用 `meshopt_generateProvokingIndexBuffer` 生成 **provoking 索引缓冲** `provoke[]` 与 `reorder[]`。
- **provoking 特性**：三角形 T 的第一个（provoking）顶点的局部索引 **恰好等于 T**。这正是 visibility buffer 把 `triangleIdx = SV_VertexID` 当作三角形号的前提。
- `reorder[i] += vertexOffset` → `Reorders[]`（reorder 索引 → 绝对顶点索引）。
- `primIndices[i] = reorder[provoke[i]]`（offset 前，section 局部顶点索引）→ `primAddress[]`。
- 三块缓冲经 BDA 暴露给 shader（[`Scene.cpp:900`](../src/Engine/Assets/Core/Scene.cpp)）：
  - `gpuScene.Vertices` = 顶点
  - `gpuScene.Reorders` = reorder 缓冲
  - `gpuScene.Indices` = **primAddress**（局部顶点索引；resolve / RT 用）
  - 另有静态 `IndexBuffer`（= `provoke[]`，光栅 `vkCmdBindIndexBuffer` 用）

**等价关系（实现者务必理解）**：
```
Vertices[ Reorders[provoke_local + reorderOffset] ]   // 光栅路径取顶点
        == Vertices[ vertexOffset + primAddress[i] ]  // resolve 路径取顶点
```
两条路径取到**同一个顶点**。这意味着 Emulated 模式可以任选其一，结果一致。

### 3.3 Culling compute

`Task.GpuCull.comp.slang`（[整文件](../assets/shaders/Task.GpuCull.comp.slang)，`numthreads(64,1,1)`，1 线程/节点）：

- AABB 视锥剔除 `IsAABBInFrustum` + 基于上一帧深度的遮挡剔除 `IsOccludedVolume`。
- 对每个节点写一条 `IndirectDrawCommands[DTid.x]`：
  ```
  instanceCount = shouldDraw ? 1 : 0;     // 剔除 = instanceCount 0
  firstInstance = shouldDraw ? DTid.x : 0; // ★ 把 node 索引塞进 firstInstance
  indexCount    = model.indexCount;
  firstIndex    = model.indexOffset;
  vertexOffset  = model.reorderOffset;     // ★ 顶点偏移
  ```
- 统计写入 `GPUDrivenStats[0]`（processed / culled / triangle 计数）。

`Task.ShadowGpuCull.comp.slang`（[整文件](../assets/shaders/Task.ShadowGpuCull.comp.slang)）：对 active cascade mask 中的每级，按 `cascadeIdx * MAX_NODES + DTid.x` 写命令，逻辑同上但只做视锥剔除。

派发点：`DispatchGpuCulling`（[`VulkanBaseRenderer.cpp:1214`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)），shadow 派发在 [`1385`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) 附近。barrier 为 `COMPUTE_SHADER_BIT → DRAW_INDIRECT_BIT` + `SHADER_WRITE → INDIRECT_COMMAND_READ`。

### 3.4 Draw 调用（3 处）

| 用途 | 位置 | 调用 |
|---|---|---|
| 主 visibility pass | [`VulkanBaseRenderer.cpp:1311`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) | `vkCmdDrawIndexedIndirect(IndirectDrawBuffer, 0, batchCount, stride)` |
| wireframe overlay | [`VulkanBaseRenderer.cpp:1874`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) | 同上 |
| CSM shadow（每 cascade） | [`ShadowMapPass.cpp:254`](../src/Engine/Rendering/Shadow/ShadowMapPass.cpp) | `vkCmdDrawIndexedIndirect(ShadowIndirectDrawBuffer, cascadeOffset, batchCount, stride)` |

帧顺序（[`VulkanBaseRenderer.cpp:1053`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）：`DispatchGpuCulling → DispatchClearPass → DispatchVisibilityPass`。

### 3.5 顶点着色器如何用 per-draw 参数

`Rast.VisibilityPass.vert.slang`（[整文件](../assets/shaders/Rast.VisibilityPass.vert.slang)）：

```hlsl
uint absInstanceIdx = instanceIndex + instanceBase;        // SV_InstanceID + SV_StartInstanceLocation(=firstInstance)
NodeProxy proxy = Nodes[absInstanceIdx];                    // ★ 靠 firstInstance 取 node
GPUVertex v = Vertices[ Reorders[vertexIndex + baseVertex] ]; // SV_VertexID + SV_StartVertexLocation(=vertexOffset)
output.position = mul(mul(VP, proxy.worldTS), v.pos);
uint instanceIdx = absInstanceIdx + 1;                      // 0 = 背景/miss
uint triangleIdx = vertexIndex;                             // ★ provoking 顶点 → 三角形号 T
output.primitive_index = ((instanceIdx & 0x7FFF) << 17) | (triangleIdx & 0x1FFFF);
```

`Rast.ShadowMap.vert.slang` 同构（[整文件](../assets/shaders/Rast.ShadowMap.vert.slang)），只是用 `SunCascadeViewProjection[cascade]`，不写 primitive_index。

### 3.6 Visibility buffer 的下游消费（硬约束）

主 pass 把 `primitive_index` 写进 `RT_MINIGBUFFER`。下游：

- `Core.SwModernNoAmbient.comp.slang`（[`:55`](../assets/shaders/Core.SwModernNoAmbient.comp.slang)）解码 `vBuffer = ((p>>17)&0x7FFF, p&0x1FFFF)` = `(instanceIdx, triangleIdx)`，然后：
  ```
  NodeProxy hitNode = Nodes[vBuffer.x - 1];
  ModelData model   = Offsets[hitNode.modelId];
  uint indexBase = model.indexOffset + vBuffer.y * 3;        // ★ triangleIdx*3
  v0/v1/v2 = Vertices[model.vertexOffset + Indices[indexBase + {0,1,2}]];  // Indices = primAddress
  ```
- `Process.ReProject.comp.slang` / `Process.DenoiseJBF.comp.slang` 用 `primitive_index` 做时序匹配。

> **硬约束**：Emulated 模式的顶点着色器**必须**产出与 MDI 路径**完全相同**的 `(instanceIdx, triangleIdx)`，且顶点位置一致。否则 visibility buffer / 时序复用全部错乱。

### 3.7 资源与上限

- `kMaxIndirectDrawCount = 65535`（[`Scene.hpp:28`](../src/Engine/Assets/Core/Scene.hpp)），`MAX_NODES = 65535`（[`BasicTypes.slang:10`](../assets/shaders/common/BasicTypes.slang)）。
- `indirectDrawBuffer_`（65535 条）、`shadowIndirectDrawBuffer_`（65535 × 4）在 [`Scene.cpp:113`](../src/Engine/Assets/Core/Scene.cpp) 分配。
- visibility 编码上限：instanceIdx 15 bit（≤32767 节点），triangleIdx 17 bit（≤131071 三角形/section）。section 切片上限 `65535*3` 索引/section（[`Scene.cpp:577`](../src/Engine/Assets/Core/Scene.cpp)），即 ≤65535 三角形/section，落在 17 bit 内。

---

## 4. 设计总览

### 4.1 思路

把「N 条间接命令 + 一次 MDI」替换为「compute 压实出 1 个图元缓冲 + 一次单 draw」。难点在于单 draw 丢失了 per-draw 的 `firstInstance`（node 索引）与 `vertexOffset`（reorder 偏移），必须把它们编进 compute 生成的逐图元数据流。

**关键洞察**：visibility 编码 `(instanceIdx<<17)|triangleIdx` 本身就同时含 node 索引（高 15 bit）和三角形号（低 17 bit）——正好是 Emulated 模式每个三角形需要的全部信息。于是 compute 只需为每个可见三角形写 **1 个 `uint`**（就是这个编码值），顶点着色器即可反查一切并自取顶点（§3.2 的等价关系保证位置一致）。

### 4.2 推荐方案：Design B-thin（单次非索引 draw + 逐图元 EmuPrim）

数据流：

```
[CPU] UpdateNodesGpuDriven → nodeProxys 上传（不变）
   │
   ▼
[GPU] Pass-1 CullCompact (1 线程/section)
   │   · 视锥/遮挡剔除（复用现有逻辑）
   │   · 幸存者：atomicAdd 预留 triCount 个 EmuPrim 槽位 → base
   │   · 写 VisibleItem{ primBase, instanceIdx(=node+1), modelOffsetIdx, triCount }
   │   · atomicAdd 累加 totalTri、visibleItemCount
   │   · 末尾（或单独 1 线程）写 DrawArg.vertexCount = totalTri*3，写 DispatchArg
   ▼ (barrier: compute→compute)
[GPU] Pass-2 Expand (1 workgroup/VisibleItem, indirect dispatch)
   │   · 读 VisibleItem
   │   · for t in [0, triCount): EmuPrim[primBase + t] = (instanceIdx<<17) | t
   ▼ (barrier: compute→ DRAW_INDIRECT + VERTEX_SHADER read)
[GPU] Draw: vkCmdDrawIndirect(DrawArgBuffer, 0, /*count*/1, stride)   ← 单次！非索引
   │   顶点着色器（Emu 变体）:
   │     uint i = SV_VertexID; uint tri = i/3, corner = i%3;
   │     uint prim = EmuPrim[tri];
   │     uint instanceIdx = (prim>>17)&0x7FFF; uint T = prim & 0x1FFFF;
   │     NodeProxy proxy = Nodes[instanceIdx-1];
   │     ModelData model = Offsets[proxy.modelId];
   │     uint localVert = Indices[model.indexOffset + T*3 + corner];   // primAddress
   │     GPUVertex v = (proxy.skinId!=~0 ? SkinnedVertices : Vertices)[model.vertexOffset + localVert];
   │     output.position = mul(mul(VP, proxy.worldTS), v.pos);
   │     output.primitive_index = prim;                                // 直接透传
   ▼
[GPU] RT_MINIGBUFFER（与 MDI 路径逐像素一致）→ 下游不变
```

**为什么单 draw 在所有设备可用**：`vkCmdDrawIndirect(count = 1)` 不触发 `multiDrawIndirect`；`firstInstance = 0` 不触发 `drawIndirectFirstInstance`；非索引免去 index buffer 绑定。仅需基础 `vkCmdDrawIndirect` + BDA（引擎已普遍依赖）。

**为什么结果一致**：顶点位置走 §3.2 的「primAddress 路径」，与光栅「Reorders 路径」等价；`primitive_index` 直接由 `(instanceIdx<<17)|T` 给出，与 §3.5 完全相同。

**内存**：`EmuPrim` 每可见三角形 4 字节。分配需按最坏情况（全可见）= 场景总三角形数 × 4。Shadow 见 §6.3 的复用策略。

### 4.3 与 MDI 路径的对应关系

| MDI 路径 | Emulated 路径 |
|---|---|
| `nodeProxys` 上传（CPU） | 不变 |
| `Task.GpuCull.comp` 写 `VkDrawIndexedIndirectCommand[node]` | `Task.GpuCullCompact.comp` 写 `VisibleItem` + 预留 EmuPrim + DrawArg |
| —（无） | `Task.EmuExpand.comp` 填 `EmuPrim` |
| `vkCmdDrawIndexedIndirect(count=batchCount)` | `vkCmdDrawIndirect(count=1)` |
| `Rast.VisibilityPass.vert`（firstInstance/baseVertex） | `Rast.VisibilityPassEmu.vert`（SV_VertexID → EmuPrim） |
| `firstInstance` → node | EmuPrim 高 15 bit → node |
| `SV_VertexID`（provoking）→ T | EmuPrim 低 17 bit → T |

---

## 5. 数据结构与 buffer 布局

### 5.1 新增 GPU 缓冲（Scene 持有）

> 命名与现有风格对齐；放入 `Assets::Scene` 私有成员（参考 [`Scene.hpp:258`](../src/Engine/Assets/Core/Scene.hpp) 的 `indirectDrawBuffer_`），并在 `RebuildMeshBuffer` 后按场景三角形数分配，`CleanUp` 释放。

| 缓冲 | 元素 | 数量（上限） | 用途 | usage flags |
|---|---|---|---|---|
| `emuPrimBuffer_` | `uint` | `maxSceneTris`（主视图）| 逐图元 visibility 编码 | STORAGE + SHADER_DEVICE_ADDRESS |
| `emuShadowPrimBuffer_` | `uint` | `maxSceneTris`（cascade 复用，见 §6.3）| shadow 图元 | STORAGE + BDA |
| `emuVisibleItemBuffer_` | `VisibleItem`（见下）| `kMaxIndirectDrawCount` | Pass-1→Pass-2 工作表 | STORAGE + BDA |
| `emuDrawArgBuffer_` | `VkDrawIndirectCommand` | `1 + kSunShadowCascadeCount` | 单 draw 参数 | INDIRECT + STORAGE + BDA |
| `emuDispatchArgBuffer_` | `VkDispatchIndirectCommand` | `1 + kSunShadowCascadeCount` | Expand 间接派发 | INDIRECT + STORAGE + BDA |
| `emuCounterBuffer_` | `uint`（若干）| 小 | 原子计数器（totalTri / itemCount，逐 pass / cascade）| STORAGE + BDA + TRANSFER_DST（fill 清零）|

> `maxSceneTris` = `RebuildMeshBuffer` 里 `indices.size() / 3`（所有 section 三角形总数）。建议加 `kMaxEmuTriangles` 上限常量并 clamp，超限打 WARN。

### 5.2 新增 struct（C++ ↔ Slang 共享，放入 `BasicTypes.slang`）

```hlsl
// 与 §3.6 的 visibility 编码一一对应；Pass-1 写、Pass-2 读
public struct ALIGN_16 EmuVisibleItem
{
    public uint primBase;     // EmuPrim 中本 section 的起始槽
    public uint instanceIdx;  // = nodeProxyIndex + 1（高 15 bit 源）
    public uint modelOffsetIdx; // = NodeProxy.modelId（供调试/校验，可选）
    public uint triCount;     // 本 section 三角形数 = indexCount/3
};

// 非索引 draw 参数（Vulkan 标准布局）
public struct VkDrawIndirectCommand
{
    public uint vertexCount;   // = totalVisibleTri * 3
    public uint instanceCount; // = 1
    public uint firstVertex;   // = 0
    public uint firstInstance; // = 0  ← 不依赖 drawIndirectFirstInstance
};

public struct VkDispatchIndirectCommand { public uint x, y, z; };
```

> C++ 侧已有等价 `VkDrawIndirectCommand`（Vulkan header），slang 侧补 `VkDrawIndirectCommand` / `VkDispatchIndirectCommand` 定义即可（仿照已有 `VkDrawIndexedIndirectCommand`，[`BasicTypes.slang:273`](../assets/shaders/common/BasicTypes.slang)）。

### 5.3 GPUScene 地址槽

`GPUScene`（[`BasicTypes.slang:323/429`](../assets/shaders/common/BasicTypes.slang)）目前地址槽已较满（push_constant 128 字节对齐）。新增缓冲地址的暴露方式二选一：

- **方案 i（推荐）**：复用现有 `ReservedAddress0` 槽指向一个「Emu 资源描述符」小缓冲（内含上述若干 BDA），shader 解引用。改动最小，不动 push_constant 布局。
- **方案 ii**：把 Emu 缓冲地址通过 `custom_data_*` / 新增 push_constant 传入对应 pipeline（compute 与 vertex 各自需要的子集）。

> 注意 Apple（MoltenVK）分支 `PLATFORM_APPLE` 的 `GPUScene` 是裸指针成员（[`:350`](../assets/shaders/common/BasicTypes.slang)），非 Apple 是 `uint64_t2` 打包（[`:429`](../assets/shaders/common/BasicTypes.slang)）。两个分支都要同步加字段/属性。Apple 恰恰是最可能走 Emulated 模式的平台，务必测试。

---

## 6. 着色器改动

### 6.1 Pass-1：`assets/shaders/Task.GpuCullCompact.comp.slang`（新增）

以 `Task.GpuCull.comp.slang` 为蓝本，复用 `IsAABBInFrustum` / `IsOccludedVolume`：

```hlsl
[numthreads(64,1,1)]
void main(uint3 DTid) {
    let scene = Bindless.GetGpuscene();
    if (DTid.x >= scene.custom_data_1) return;          // batchCount
    NodeProxy node = scene.Nodes[DTid.x];
    ModelData model = scene.Offsets[node.modelId];
    float4x4 mvp = mul(scene.Camera[0].PrevViewProjectionUnJit, node.worldTS);

    bool shouldDraw = (node.visible > 0) && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
    // 统计（同现有）...
    if (shouldDraw && IsOccludedVolume(...)) shouldDraw = false;
    if (!shouldDraw || model.indexCount == 0) return;

    uint triCount = model.indexCount / 3;
    uint primBase; InterlockedAdd(EmuCounter.totalTri, triCount, primBase);
    uint itemIdx;  InterlockedAdd(EmuCounter.itemCount, 1, itemIdx);
    EmuVisibleItems[itemIdx] = { primBase, DTid.x + 1, node.modelId, triCount };
}
```

DrawArg / DispatchArg 的填写（避免 host 回读）二选一：

- **A**：单独一个极小的「finalize」compute（1 线程）在 Pass-1 之后读 `EmuCounter` 写 `DrawArg.vertexCount = totalTri*3` 与 `DispatchArg.x = itemCount`。
- **B**：Pass-1 里每个线程 `InterlockedMax`/在 `itemIdx==0` 时预写，复杂易错，**不推荐**。

> 推荐 A：清晰、无回读、barrier 简单。

### 6.2 Pass-2：`assets/shaders/Task.EmuExpand.comp.slang`（新增）

1 workgroup 处理 1 个 VisibleItem（`vkCmdDispatchIndirect`，groupCount.x = itemCount）：

```hlsl
[numthreads(64,1,1)]
void main(uint3 gid : SV_GroupID, uint3 lid : SV_GroupThreadID) {
    EmuVisibleItem it = EmuVisibleItems[gid.x];
    uint enc = (it.instanceIdx & 0x7FFF) << 17;
    for (uint t = lid.x; t < it.triCount; t += 64)       // strided，覆盖大 section
        EmuPrim[it.primBase + t] = enc | (t & 0x1FFFF);  // ★ 就是 visibility 编码
}
```

> （Phase-2 可选）在此处做**逐三角形剔除**：取 `Indices[model.indexOffset + t*3 + {0,1,2}]` 算屏幕面积/背面，被剔除则不写并从压实计数中扣除（需改成「每三角形 atomicAdd 槽位」的两段式，或写 sentinel 让顶点着色器退化为退化三角形）。本期先不做，保持 `EmuPrim` 与三角形一一对应、`T` = 原始局部三角形号。

### 6.3 Shadow 版本

- `Task.ShadowGpuCullCompact.comp.slang`：对 active cascade，每级独立计数器与 `VisibleItem` 段、独立 DrawArg/DispatchArg slot、独立 `EmuShadowPrim` 段。
- **内存优化（推荐）**：cascade **串行复用**单个 `emuShadowPrimBuffer_`（大小 = `maxSceneTris`）：`expand(cascade i) → DrawCascade(i) → expand(cascade i+1)`，避免 ×4 内存。代价是 cascade 间串行（shadow 本就分帧更新，影响小）。备选：×4 内存并行。
- 顶点着色器 `Rast.ShadowMapEmu.vert.slang`：同 §6.4，但用 `SunCascadeViewProjection[custom_data_0]`，不写 primitive_index。

### 6.4 顶点着色器 Emu 变体

新增 `Rast.VisibilityPassEmu.vert.slang`、`Rast.ShadowMapEmu.vert.slang`、`Rast.WireframeEmu.vert.slang`（或给现有 vert 加 specialization constant / push_constant flag 分支，见 §7.4 取舍）。核心：

```hlsl
[shader("vertex")]
VertexOutput main(uint i : SV_VertexID) {
    let scene = Bindless.GetGpuscene();
    uint tri = i / 3, corner = i % 3;
    uint prim = scene.EmuPrim[tri];
    uint instanceIdx = (prim >> 17) & 0x7FFF;            // node+1
    uint T = prim & 0x1FFFF;
    NodeProxy proxy = scene.Nodes[instanceIdx - 1];
    ModelData model = scene.Offsets[proxy.modelId];
    GPUVertex* verts = (proxy.skinId != 0xFFFFFFFF) ? scene.SkinnedVertices : scene.Vertices;
    uint localVert = scene.Indices[model.indexOffset + T * 3 + corner];   // primAddress
    GPUVertex pv = verts[model.vertexOffset + localVert];
    output.position = mul(mul(scene.Camera[0].ViewProjection, proxy.worldTS), float4(pv.Position_Tx.xyz, 1));
    // material_model 同现有；
    output.primitive_index = prim;                       // 直接透传，免去重算
    return output;
}
```

> **校验点**：`vkCmdDraw` 用 triangle-list，`i=0,1,2` 为第 0 个三角形的三个角。provoking 顶点（corner 0）的 `nointerpolation` 取值即 `prim`，与 MDI 路径一致。确认引擎 provoking 约定为「first vertex」（Vulkan 默认；若启用了 `VK_EXT_provoking_vertex` 改 last，需相应调整 corner 选择）。

---

## 7. C++ 集成

### 7.1 开关：CVar `r.drawSubmitMode`

在 `EngineCVars.cpp`（[`RegisterEngineCVars`](../src/Engine/Runtime/Config/EngineCVars.cpp)）新增（仿照 `r.rendererType`）：

```cpp
GK_CVAR_INT_CB("r.drawSubmitMode", settings, DrawSubmitMode, 0, ECVarFlags::Archive,
               "GPU-driven 提交方式 (0=MDI, 1=Emulated SingleDraw)",
               std::bind(RequestSwapChainIfPossible, engine));  // 切换时重建管线
```

- 在 `UserSettings` 加 `int DrawSubmitMode{0};`（[`UserSettings`](../src/Engine/Runtime/Config/UserSettings.hpp)）。
- `_CB` 回调触发 `RequestRecreateSwapChain`，借助现有重建路径切换 pipeline 集合。
- 可选：在 GraphicsDebugPanel（`debug.graphics.panel`）加下拉，方便运行时切换对比。

### 7.2 设备特性可选化 + 自动回退

`VulkanBaseRenderer.cpp:597`：把强制改为「可用则开，并记录」：

```cpp
const bool hasMDI = supportedFeatures.multiDrawIndirect && supportedFeatures.drawIndirectFirstInstance;
deviceFeatures.multiDrawIndirect = supportedFeatures.multiDrawIndirect;
deviceFeatures.drawIndirectFirstInstance = supportedFeatures.drawIndirectFirstInstance;
// 存入 renderer 能力位
caps_.supportMDI = hasMDI;
```

提交前决定有效模式：`effectiveMode = (DrawSubmitMode == 1 || !caps_.supportMDI) ? Emulated : MDI;`。`!supportMDI` 时强制 Emulated 并 INFO 日志说明。

### 7.3 渲染流程接线

- **Culling 派发**：`DispatchGpuCulling`（[`:1214`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）按 `effectiveMode` 分流：
  - Emulated：`vkCmdFillBuffer` 清 `emuCounterBuffer_` → Pass-1 `gpuCullCompactPipeline` dispatch → finalize(1 线程) → barrier → Pass-2 `emuExpandPipeline` `vkCmdDispatchIndirect(emuDispatchArgBuffer_)` → barrier(`COMPUTE→DRAW_INDIRECT|VERTEX_SHADER read`)。
  - MDI：保持现状。
- **Draw**：`DispatchVisibilityPass`（[`:1281`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）/ wireframe（[`:1839`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）/ `ShadowMapPass::DrawCascade`（[`:220`](../src/Engine/Rendering/Shadow/ShadowMapPass.cpp)）按模式：
  - Emulated：绑定 Emu pipeline（无 index buffer），`vkCmdDrawIndirect(emuDrawArgBuffer_, slotOffset, 1, sizeof(VkDrawIndirectCommand))`。
  - MDI：保持现状。
- **Pipeline 集合**：新增 `visibilityEmuPipeline` / `wireframeEmuPipeline` / shadow Emu pipeline（用 Emu 顶点着色器、无 vertex input、无 index）。在 `CreateSwapChain`（[`:865`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）按模式创建对应一套（或都建好按需绑定）。`ShadowMapPass` 需支持双 pipeline 或参数化。

### 7.4 顶点着色器变体：独立文件 vs specialization

- **独立文件（推荐起步）**：实现直观、互不影响、便于 A/B 与回滚；代价是多几个 `.slang` + 编译产物。
- **specialization constant / push flag 分支**：单文件双分支，少维护文件，但 vertex 阶段拿 `SV_StartInstanceLocation` 在非索引 draw 下语义不同，分支逻辑较绕。建议先独立文件，稳定后再考虑合并。

### 7.5 Scene 改动

- `Scene.hpp/cpp`：加上述缓冲成员 + getter（仿 [`IndirectDrawBuffer()`](../src/Engine/Assets/Core/Scene.hpp)）；`RebuildMeshBuffer` 末尾按 `maxSceneTris` 分配；`CleanUp`/析构释放；`BuildGPUScene` 填入新地址（§5.3）。
- 暴露 `GetMaxSceneTriangles()` 供分配与统计。

---

## 8. 分阶段实施计划

> 每阶段都应能独立编译（`./gnb build --reconfigure`）并通过既有验收（日志出现 `uploaded scene [...] to gpu`）。

### Phase 0：脚手架与开关（不改渲染结果）
- 加 `UserSettings.DrawSubmitMode` + CVar `r.drawSubmitMode`。
- 设备特性可选化 + `caps_.supportMDI` + `effectiveMode` 计算（此时 Emulated 分支留空/回退 MDI）。
- **验收**：编译通过；CVar 可读写；缺 MDI 的设备能创建 device（用 validation/headless 验证逻辑分支）。

### Phase 1：主 visibility pass 的 Emulated 路径
- slang：`VkDrawIndirectCommand`/`VkDispatchIndirectCommand`/`EmuVisibleItem` 定义；`Task.GpuCullCompact.comp` + finalize + `Task.EmuExpand.comp` + `Rast.VisibilityPassEmu.vert`。
- C++：Scene 新缓冲；`gpuCullCompactPipeline`/`emuExpandPipeline`/`visibilityEmuPipeline`；`DispatchGpuCulling`/`DispatchVisibilityPass` 接线 + barrier。
- **验收**：`SwModernNoAmbient` 下 `r.drawSubmitMode 1` 与 `0` **逐像素一致**（截图 diff ≈ 0）；gkNextVisualTest 通过；选中/hover 高亮、ObjectId 正确。

### Phase 2：wireframe + shadow
- wireframe overlay Emu 路径（[`:1839`](../src/Engine/Rendering/VulkanBaseRenderer.cpp)）。
- shadow：`Task.ShadowGpuCullCompact.comp` + `Rast.ShadowMapEmu.vert` + cascade 串行复用（§6.3）；`ShadowMapPass` 双 pipeline / 参数化。
- **验收**：CSM 阴影两模式一致；wireframe 正常；4 cascade 正确。

### Phase 3：健壮性、统计、回退、文档
- `GPUDrivenStats` 在 Emulated 下同样填充（processed/culled/triangle），保证 overlay 数据可比。
- `maxSceneTris` 上限 clamp + WARN；空场景/0 三角形/超 65535 节点边界。
- 自动回退路径端到端验证（强制关 MDI 跑全套）。
- 更新 `AGENTS.md` 渲染章节与本文档「已实现」状态；GraphicsDebugPanel 下拉。
- **验收**：benchmark 三模式（MDI / Emulated / 强制回退）均稳定；多平台冒烟。

---

## 9. 测试与验证

1. **一致性（最重要）**：同一场景同相机，`r.drawSubmitMode` 0 vs 1 截图 diff。利用 `gkNextVisualTest`（`assets/configs/visual_test.json`）+ `UpdateVisualTestBaseline`。主 pass、shadow、wireframe、ObjectId、选中态、时序复用（移动相机看有无 ghosting 差异）全覆盖。
2. **功能**：skinned mesh（`skinId != ~0` 分支）、多 section 大模型（>65535 三角形分片）、空场景、全剔除场景。
3. **性能**：`gkNextBenchmark` 对比两模式 GPU 计时（`SCOPED_GPU_TIMER` 已覆盖 "gpu cull" / "visibility pass"）；重点看「多小批次」场景（MagicaLego/BrickPlayer 大量小积木）——预期 Emulated 在移动/Metal 上更快。
4. **兼容性/回退**：人为令 `caps_.supportMDI=false`，确认强制 Emulated 全套正常。
5. **平台**：macOS arm64（MoltenVK，重点）、Windows、Linux；条件允许测 Android arm64（TBR，Emulated 收益最大）。
6. **validation layer**：`--validation` 跑通，无 barrier/同步报错（间接缓冲的 `INDIRECT_COMMAND_READ` 与 vertex `SHADER_READ` 可见性）。

---

## 10. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| `EmuPrim` 最坏内存（场景总三角形 × 4B，shadow 复用后仍 ×1） | 大场景显存压力 | 串行复用 shadow 缓冲；`kMaxEmuTriangles` clamp + WARN；后续做逐三角形剔除压缩 |
| 顶点着色器多级依赖取数（EmuPrim→Nodes→Offsets→Indices→Vertices） | 顶点吞吐略降 | 数据已在缓存友好的 BDA；实测对比；必要时 Pass-2 预解析部分字段进更宽的 EmuPrim |
| provoking 顶点约定不符（last vs first） | primitive_index / 三角形号错位 | 确认未启用 `VK_EXT_provoking_vertex`；在 vert 里按约定选 corner；加单测/截图校验 |
| Apple `GPUScene` 双布局漏改 | MoltenVK 上崩溃/花屏 | §5.3 两分支同步；MoltenVK 优先测 |
| 两段式 compute 的 barrier 漏挂 | 数据竞争/闪烁 | 明确 compute→compute、compute→draw_indirect/vertex 两道 barrier；validation 验证 |
| `vkCmdDrawIndirect` count=1 的 `firstInstance` 仍被某些驱动校验 | 理论无（=0） | 保持 firstInstance=0；非索引免 index 绑定 |
| 时序复用对 `primitive_index` 极敏感 | ghosting/闪烁 | 保证编码逐位一致（同 §3.6）；diff 验证移动场景 |

**回退**：整套受 `r.drawSubmitMode` 控制，默认 0（MDI）。Emulated 出问题随时切回，不影响既有路径。

---

## 11. 备选 / 后续方案（本期不实现）

- **A. 单次索引 draw**：`EmuPrim`（逐图元）+ 组合索引缓冲，`vkCmdDrawIndexedIndirect(count=1)`。索引 draw 下 `SV_VertexID` = 索引值而非序号，顶点着色器拿不到「组合三角形序号」去查 EmuPrim，需额外 side 机制，反而更绕。**非索引（B-thin）更优**。
- **B. Loop 模式**：CPU 循环 `vkCmdDrawIndexedIndirect(count=1)` N 次（Tellusim 的 "Loop"）。零 shader 改动、最大兼容，但 N 次 CPU 提交开销大。可作为「连 `vkCmdDrawIndirect` 间接都想避开」的保底，优先级低。
- **C. 完整 meshlet 化 + Tellusim 原版 `(meshlet<<8)|vertex` 编码**：把几何切成 ≤256 顶点 meshlet，支持逐 meshlet/逐三角形剔除与硬件 Mesh Shader 后端统一抽象。收益大但需重构几何 pipeline（`RebuildMeshBuffer`、RT 的 primAddress 等），是独立大项目。
- **D. Compute 软光栅化**：Tellusim [compute-raster](https://tellusim.com/compute-raster/) 路线，与本引擎已有的 SoftwareTracing/SoftwareModern 体系另作权衡。
- **E. 逐三角形剔除压缩**：在 §6.2 Expand 阶段加背面/小三角形剔除并压实 `EmuPrim`（`T` 仍存原始局部号以保持 visibility 一致），进一步省顶点/带宽——B-thin 的自然延伸，建议作为 Phase 4。

---

## 12. 关键文件索引（实现时对照）

| 主题 | 文件 |
|---|---|
| 设备特性强制 | [`VulkanBaseRenderer.cpp:597`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) |
| Cull 派发 / 帧顺序 | [`VulkanBaseRenderer.cpp:1053`](../src/Engine/Rendering/VulkanBaseRenderer.cpp), [`:1214`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) |
| 主 draw / wireframe draw | [`VulkanBaseRenderer.cpp:1281`](../src/Engine/Rendering/VulkanBaseRenderer.cpp), [`:1839`](../src/Engine/Rendering/VulkanBaseRenderer.cpp) |
| shadow cull / draw | [`VulkanBaseRenderer.cpp:1360`](../src/Engine/Rendering/VulkanBaseRenderer.cpp), [`ShadowMapPass.cpp:220`](../src/Engine/Rendering/Shadow/ShadowMapPass.cpp) |
| 间接缓冲分配 / batchCount | [`Scene.cpp:113`](../src/Engine/Assets/Core/Scene.cpp), [`Scene.cpp:1193`](../src/Engine/Assets/Core/Scene.cpp) |
| 几何构建（provoking/reorder/primAddress）| [`Scene.cpp:540`](../src/Engine/Assets/Core/Scene.cpp), [`:900`](../src/Engine/Assets/Core/Scene.cpp) |
| GPUScene / NodeProxy / ModelData | [`BasicTypes.slang:145`](../assets/shaders/common/BasicTypes.slang), [`:160`](../assets/shaders/common/BasicTypes.slang), [`:323`](../assets/shaders/common/BasicTypes.slang) |
| Cull shader | [`Task.GpuCull.comp.slang`](../assets/shaders/Task.GpuCull.comp.slang), [`Task.ShadowGpuCull.comp.slang`](../assets/shaders/Task.ShadowGpuCull.comp.slang) |
| 顶点着色器（含 visibility 编码）| [`Rast.VisibilityPass.vert.slang`](../assets/shaders/Rast.VisibilityPass.vert.slang), [`Rast.ShadowMap.vert.slang`](../assets/shaders/Rast.ShadowMap.vert.slang) |
| visibility 解码 / 重建 | [`Core.SwModernNoAmbient.comp.slang:55`](../assets/shaders/Core.SwModernNoAmbient.comp.slang) |
| CVar 注册 | [`EngineCVars.cpp`](../src/Engine/Runtime/Config/EngineCVars.cpp) |
| Pipeline 基类 | [`CommonComputePipeline.hpp`](../src/Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp) |

---

### 参考链接

- [Tellusim — Mesh Shader Emulation](https://tellusim.com/mesh-shader-emulation/)
- [Tellusim — Mesh Shader versus MultiDrawIndirect](https://tellusim.com/mesh-shader/)
- [Tellusim — MultiDrawIndirect and Metal](https://tellusim.com/metal-mdi/)
- [Tellusim — Compute versus Hardware](https://tellusim.com/compute-raster/)
- [Tellusim — Dispatch, Dispatch, Dispatch](https://tellusim.com/dispatch/)
</content>
</invoke>
