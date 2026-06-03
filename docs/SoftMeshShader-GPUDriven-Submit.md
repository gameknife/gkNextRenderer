# Soft Mesh Shader GPU-Driven 提交路径

本文记录 gkNextEngine 当前唯一的 GPU-driven draw submit 方案：**Soft Mesh Shader single draw**。

它不是 Vulkan/DirectX 的硬件 mesh shader，而是一套 compute + vertex shader 的软件化 meshlet/primitive expansion 路径：GPU culling 先把可见的 model section 压实成逐 primitive 的编码流，随后 graphics pass 通过一次 `vkCmdDrawIndirect(count=1)` 完成主 visibility pass、wireframe overlay 和每个 CSM cascade 的绘制。

## 目标

- 去掉对 `multiDrawIndirect` 和 `drawIndirectFirstInstance` 的运行时依赖。
- 去掉 CPU 侧按 draw batch 组织 MDI command buffer 的路径。
- 保留 GPU-driven 的 culling、统计、visibility buffer 语义。
- 所有平台统一走单 draw 间接提交，减少 Vulkan feature 分叉。

## 核心数据结构

### `SoftMeshShaderResources`

定义位置：`assets/shaders/common/BasicTypes.slang`

```c
struct SoftMeshShaderResources
{
    uint64_t Prim;
    uint64_t ShadowPrim;
    uint64_t VisibleItems;
    uint64_t DrawArgs;
    uint64_t DispatchArgs;
    uint64_t Counters;
};
```

C++ 侧在 `Scene::RebuildMeshBuffer()` 中创建这些 buffer，并把资源表地址写入 `GPUScene::SoftMeshShaderResourcesAddress`。

### Buffer 用途

| Buffer | 作用 | 尺寸策略 |
|---|---|---|
| `SoftMeshShaderPrim` | 主 pass 的逐三角形 primitive 编码流 | `maxSceneTriangles` |
| `SoftMeshShaderShadowPrim` | shadow pass 的逐三角形 primitive 编码流 | `maxSceneTriangles * 4 cascades` |
| `SoftMeshShaderVisibleItems` | compact 阶段输出的可见 section 列表 | `kMaxIndirectDrawCount * slotCount` |
| `SoftMeshShaderDrawArgs` | `VkDrawIndirectCommand`，供 graphics draw 使用 | `1 + 4 cascades` |
| `SoftMeshShaderDispatchArgs` | `VkDispatchIndirectCommand`，供 expand 阶段使用 | `1 + 4 cascades` |
| `SoftMeshShaderCounters` | 每个 slot 两个 counter：triangle count、item count | `slotCount * 2` |

Slot 约定：

| Slot | 用途 |
|---|---|
| `0` | 主 visibility pass 和 wireframe overlay |
| `1` | sun shadow cascade 0 |
| `2` | sun shadow cascade 1 |
| `3` | sun shadow cascade 2 |
| `4` | sun shadow cascade 3 |

## Primitive 编码

Expand 阶段写出的每个 primitive 是一个 `uint`：

```c
prim = ((instanceIdx & 0x7FFF) << 17) | (triangleIdx & 0x1FFFF)
```

含义：

- `instanceIdx`：`NodeProxy` 的索引加 1。0 保留不用，vertex shader 读取时再减 1。
- `triangleIdx`：当前 model section 内的三角形索引。

当前编码把 `instanceIdx` 放在高 15 bit、`triangleIdx` 放在低 17 bit。也就是说，单个 section 的三角形索引必须小于 `131072`，可编码的 non-zero instance 范围是 1..32767。这个限制来自 32-bit visibility/primitive id 的历史编码约束，后续如果要超过该规模，需要改成 64-bit primitive stream 或拆分更多 draw slot。

## 主 Pass 流程

实现入口：`VulkanBaseRenderer::DispatchGpuCulling()`

```mermaid
flowchart LR
    A["Clear counters"] --> B["Task.SoftMeshShaderGpuCullCompact"]
    B --> C["Barrier: counters + visible items"]
    C --> D["Task.SoftMeshShaderFinalize(slot 0)"]
    D --> E["Barrier: dispatch args + visible items"]
    E --> F["Task.SoftMeshShaderExpand(slot 0)"]
    F --> G["Barrier: prim stream + draw args"]
    G --> H["vkCmdDrawIndirect(slot 0, count=1)"]
```

### 1. Compact

Shader：`assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang`

每个 thread 处理一个 `NodeProxy` / model section：

- 读取 `scene.Nodes[DTid.x]`
- 读取 `scene.Offsets[node.modelId]`
- 做可见性判断：
  - `node.visible > 0`
  - AABB frustum culling
  - 基于上一帧 depth 的粗粒度 occlusion culling
- 通过 atomic counter 分配：
  - `primBase`：当前 section 在 primitive stream 中的起始位置
  - `itemIdx`：当前 visible item 在 compact list 中的位置
- 写出 `SoftMeshShaderVisibleItem`

`SoftMeshShaderVisibleItem` 不存每个三角形，只存 section 粒度的元数据：

```c
struct SoftMeshShaderVisibleItem
{
    uint primBase;
    uint instanceIdx;
    uint modelOffsetIdx;
    uint triCount;
};
```

### 2. Finalize

Shader：`assets/shaders/Task.SoftMeshShaderFinalize.comp.slang`

Finalize 只有 1 个 thread。它读取当前 slot 的两个 counter：

- `totalTri = counters[slot * 2]`
- `itemCount = counters[slot * 2 + 1]`

然后生成：

- `drawArgs[slot].vertexCount = totalTri * 3`
- `drawArgs[slot].instanceCount = 1`
- `dispatchArgs[slot].x = itemCount`

这一步把 compact 结果转成后续 expand 和 graphics draw 可以消费的 indirect 参数。

### 3. Expand

Shader：`assets/shaders/Task.SoftMeshShaderExpand.comp.slang`

Expand 使用 `vkCmdDispatchIndirect()`。每个 workgroup 处理一个 visible item，64 个 thread 并行展开该 item 的三角形：

```c
for (uint t = Lid.x; t < item.triCount; t += 64)
{
    prims[item.primBase + t] = encBase | (t & 0x1FFFF);
}
```

输出结果是连续的 `Prim[]`，后续 vertex shader 用 `SV_VertexID / 3` 反查 primitive，再用 `SV_VertexID % 3` 得到三角形角点。

### 4. Draw

主 visibility pass：`Rast.VisibilityPassSoftMeshShader.vert.slang`

Wireframe overlay：`Rast.WireframeSoftMeshShader.vert.slang`

两者都从 slot 0 的 `Prim` 读取 primitive 编码：

```c
uint prim = prims[vertexIndex / 3];
uint corner = vertexIndex % 3;
uint instanceIdx = (prim >> 17) & 0x7FFF;
uint triangleIdx = prim & 0x1FFFF;
```

然后：

1. `proxy = scene.Nodes[instanceIdx - 1]`
2. `model = scene.Offsets[proxy.modelId]`
3. `localVertex = scene.Indices[model.indexOffset + triangleIdx * 3 + corner]`
4. 读取 static/skinned vertex buffer
5. 输出 clip-space position 和 visibility/material 信息

graphics 侧只提交：

```cpp
vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(),
                  0, 1, sizeof(VkDrawIndirectCommand));
```

## Shadow Pass 流程

实现入口：`VulkanBaseRenderer::DispatchSunShadow()`

Shadow pass 复用同一套 visible item、counter、finalize、expand pipeline，但使用 slot 1..4。每个 active cascade 都有独立 counter、draw arg、dispatch arg 和 primitive stream 区间。

流程：

1. 清空 shadow GPU-driven stats。
2. 清空 `SoftMeshShaderCounters`。
3. 清空 `SoftMeshShaderDrawArgs[1..4]`，避免未更新 cascade 使用旧 draw 参数。
4. `Task.SoftMeshShaderShadowGpuCullCompact` 按 active cascade mask 循环每个 cascade。
5. 对每个 active cascade 执行 finalize。
6. 对每个 active cascade 执行 expand。
7. `ShadowMapPass::DrawCascade()` 对每个 active cascade 调一次 `vkCmdDrawIndirect(count=1)`。

Shadow primitive 的地址布局：

```c
item.primBase = cascadeIdx * maxSceneTriangles + primBase;
```

Shadow vertex shader 通过 `scene.custom_data_2` 加上 cascade 偏移读取：

```c
uint prim = prims[scene.custom_data_2 + vertexIndex / 3];
```

`scene.custom_data_2` 在 `ShadowMapPass::DrawCascade()` 中设置为：

```cpp
gpuScene.custom_data_2 = cascade * scene.GetMaxSceneTriangles();
```

## Synchronization

SoftMeshShader 路径依赖几个关键 barrier：

- host 写 node matrix -> compute 读。
- transfer 清 counter/draw arg -> compute/indirect 读写。
- compact 写 counter/visible item -> finalize 读。
- finalize 写 dispatch arg -> expand 的 `vkCmdDispatchIndirect` 读。
- expand 写 primitive stream、finalize 写 draw arg -> vertex shader / draw indirect 读。

如果画面出现随机缺三角形、shadow 闪烁、wireframe 和 visibility 不一致，优先检查这些 barrier 的 src/dst access mask、stage mask、offset/size 是否覆盖对应 slot。

## 与旧 MDI 路径的差异

旧路径为每个 visible section 写一条 `VkDrawIndexedIndirectCommand`，再一次 `vkCmdDrawIndexedIndirect(drawCount=batchCount)`。

当前路径改为：

- 不创建 `VkDrawIndexedIndirectCommand` buffer。
- 不启用或查询 `multiDrawIndirect` / `drawIndirectFirstInstance`。
- 不暴露 `r.drawSubmitMode`。
- 不绑定 index buffer 做 indexed indirect draw。
- vertex shader 自行根据 primitive stream 取 index 和 vertex。

这使得 submit 路径对 Vulkan 基础 indirect draw 的依赖更小，尤其适合移动端、MoltenVK、以及 MDI 支持/性能不稳定的平台。

## 相关文件

- C++ submit：
  - `src/Engine/Rendering/VulkanBaseRenderer.cpp`
  - `src/Engine/Rendering/Shadow/ShadowMapPass.cpp`
- Scene 资源：
  - `src/Engine/Assets/Core/Scene.hpp`
  - `src/Engine/Assets/Core/Scene.cpp`
- Shared layout：
  - `assets/shaders/common/BasicTypes.slang`
- Compute shaders：
  - `assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang`
  - `assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact.comp.slang`
  - `assets/shaders/Task.SoftMeshShaderFinalize.comp.slang`
  - `assets/shaders/Task.SoftMeshShaderExpand.comp.slang`
- Vertex shaders：
  - `assets/shaders/Rast.VisibilityPassSoftMeshShader.vert.slang`
  - `assets/shaders/Rast.WireframeSoftMeshShader.vert.slang`
  - `assets/shaders/Rast.ShadowMapSoftMeshShader.vert.slang`

## 验证建议

- 编译：`gnb.bat build gkNextRenderer`
- 渲染冒烟：`gnb.bat run gkNextRenderer`，确认日志出现 `uploaded scene [...] to gpu`
- 单元测试：`out/build/windows/bin/gkNextUnitTests.exe`
- 渲染改动：运行 `gkNextVisualTest`，重点检查 visibility buffer、wireframe overlay、CSM shadow、selected/hovered object id 是否一致。

