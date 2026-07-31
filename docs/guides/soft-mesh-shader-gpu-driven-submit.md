---
title: "Soft Mesh Shader GPU-Driven 提交路径"
category: guide
status: 现行
owner: engine
created: 2026-06-03
last_updated: 2026-07-17
---

# Soft Mesh Shader GPU-Driven 提交路径

当前 raster visibility、wireframe 和 GPU CSM 都使用 Soft Mesh Shader single-draw 路径。它不是硬件 mesh shader：compute 把可见 model section 压实并展开成 primitive stream，graphics pass 再用一次 `vkCmdDrawIndirect(count=1)` 消费该 stream。

## 资源与 slot

`SoftMeshShaderResources` 定义在 `assets/shaders/common/BasicTypes.slang`，保存六个 device address：主 primitive、shadow primitive、visible items、draw args、dispatch args、counters。C++ 对应资源由 `src/Engine/Assets/Core/Scene.Build.cpp` 创建。

| Slot | 用途 |
| ---: | --- |
| 0 | 主 visibility 与 wireframe |
| 1..4 | CSM cascade 0..3 |

primitive buffer 按“实例展开后的三角形需求”定容，不是只看唯一模型 index 数；运行时节点增长超过容量时，Scene 会按需扩容。Visible item 每 slot 上限为 `Scene::kMaxIndirectDrawCount`。

## 每帧流程

实现入口是 `src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp` 的 `DispatchGpuCulling` 与 `DispatchSunShadow`：

```text
clear counters / relevant draw args
  → Compact（section cull + block/wave 聚合 + visible item）
  → Finalize（counter → draw/dispatch indirect args）
  → Expand（visible item → uint primitive stream）
  → vkCmdDrawIndirect(count=1)
```

Compact 有两套等价 shader：

- `Task.SoftMeshShaderGpuCullCompact.comp.slang` / shadow 版本：64-thread workgroup 用 LDS prefix scan，每组少量全局 atomic。
- `*CompactWave.comp.slang`：设备支持 compute subgroup arithmetic + ballot 时使用 wave intrinsics；否则回退 LDS 版本。

不要根据旧 atomic-contention 计划恢复“每个可见 thread CAS 预留区间”的实现。当前 block/wave 聚合保持 gapless layout，并聚合 GPU-driven stats。

Finalize 把每 slot 的 triangle/item counter 转成 `VkDrawIndirectCommand` 和 `VkDispatchIndirectCommand`，并按实际 capacity 钳制。Expand 每个 workgroup 处理一个 visible item，写出 32-bit primitive：

```text
bits 31..17: non-zero instance index（15 bit）
bits 16..0 : section-local triangle index（17 bit）
```

因此单 section 仍不得超过 131072 个三角形，可编码 non-zero instance 为 1..32767。突破该边界必须改编码或拆分数据，不能静默依赖 mask 截断。

Vertex shader 用 `SV_VertexID / 3` 取得 primitive，`% 3` 取得角点，再查 `scene.Nodes`、model offsets、index 与 static/skinned vertex。相关入口：

- `Rast.VisibilityPassSoftMeshShader.vert.slang`
- `Rast.WireframeSoftMeshShader.vert.slang`
- `Rast.ShadowMapSoftMeshShader.vert.slang`

## Shadow 与多视图

Shadow 使用独立 primitive 区间和 slot 1..4。`sunShadowCascadeUpdateMask` 只更新需要的 cascade；同一帧、相同 camera family 的 reference view 可复用已生成的 shadow set，不同 camera family 必须刷新完整集合。

Soft-mesh scratch 当前属于 Scene，全 scheduled view 复用。多视图之间的 transfer/compute/indirect/vertex barrier 是正确性要求；不要把它误当成每个 RenderView 私有资源。画面偶发缺面、shadow 闪烁或次视图污染时，先核对：

- clear → compact 的 counter 可见性；
- compact → finalize/expand 的 counter 与 visible item；
- finalize → indirect read 的 dispatch/draw args；
- expand → vertex read 的 primitive stream；
- 前一个 view 消费完成后 scratch 才能被下一个 view 覆盖。

## 与旧 MDI 的边界

当前路径不创建 `VkDrawIndexedIndirectCommand` 列表，不查询 `multiDrawIndirect` / `drawIndirectFirstInstance`，不绑定旧 indexed-MDI submit，也没有 `r.drawSubmitMode` 分支。不要为单个平台重新引入第二条常驻提交路径。

## 验证

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./gnb.sh shot --scene assets/models/playground.glb
./out/build/<preset>/bin/gkNextUnitTests
```

渲染修改还应检查 visibility、wireframe、四级 CSM、selected/hovered object id，以及至少一个 secondary RenderView；需要多场景 baseline 时运行 `gkNextVisualTest`。
