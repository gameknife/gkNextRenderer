---
title: "SoftMeshShader GPU Cull 原子竞争优化 —— 海量 drawcall 下的 primBase 分配开销治理"
category: plan
status: 草案
owner: engine
created: 2026-06-19
last_updated: 2026-06-19
---

# SoftMeshShader GPU Cull 原子竞争优化 —— 海量 drawcall 下的 primBase 分配开销治理

> 状态：**草案（待开发）**。本文为交接文档，后续由其他 agent 接手实现。
> 范围：仅改 `Task.SoftMeshShaderGpuCullCompact.comp.slang` 与 `Task.SoftMeshShaderShadowGpuCullCompact.comp.slang` 两个 compute shader 的**原子分配逻辑**；Finalize / Expand / 顶点着色器 / C++ dispatch / 缓冲区布局**默认不动**（仅可选快路径需要少量 C++）。
> 目标：在海量 drawcall（`indirectDrawBatchCount` 数万级，最高到 `MAX_NODES=65535`）下，把 `gpu cull` pass 里逐线程全局原子（尤其是 `counters[0]` 的 CAS 自旋）造成的串行化开销降下来，**不改变可见性输出与统计语义**。

---

## 1. 背景与问题定位

### 1.1 现状管线

GPU-driven 提交走 "Soft Mesh Shader single draw" 路径（详见 `docs/guides/soft-mesh-shader-gpu-driven-submit.md`）。`DispatchGpuCulling()` 串起四个阶段：

1. **GpuCullCompact**（`numthreads(64,1,1)`，`groupCount = (indirectDrawBatchCount + 63) / 64`）：逐 node 做 frustum + occlusion culling，存活者把自己的三角形压实到全局 prim 编码流里。
2. **Finalize**（单线程）：读 `counters[0]`（总三角形数）与 `counters[1]`（item 数），填 `VkDrawIndirectCommand`（`vertexCount = totalTri*3`）与 `VkDispatchIndirectCommand`（`x = itemCount`）。
3. **Expand**（每个可见 item 一个 workgroup）：把每个 item 的三角形展开写入 `prims[item.primBase + t]`。
4. **graphics draw**：一次 `vkCmdDrawIndirect(count=1)` 覆盖 `[0, totalTri*3)` 顶点。

关键不变量：`prims` 缓冲被当作**单一线性数组**，由**一次**大 draw 覆盖 `[0, counters[0])`。因此每个 item 的 `primBase` 分配必须是**从 0 起、连续无空洞（gapless）、互不重叠**，且 `counters[0] ≤ custom_data_2`（= `GetMaxSceneTriangles()`，即 prim 缓冲容量），否则 Expand 会越界、draw 范围会读到未写入的垃圾三角形。

### 1.2 热点代码

`Task.SoftMeshShaderGpuCullCompact.comp.slang`（主 pass）存活线程执行：

```slang
uint primBase = counters[0];
for (;;)
{
    if (primBase >= scene.custom_data_2 || triangleCount > scene.custom_data_2 - primBase)
    {
        return; // 超出三角形预算 -> 丢弃该 item
    }
    uint observed;
    InterlockedCompareExchange(counters[0], primBase, primBase + triangleCount, observed);
    if (observed == primBase) { break; }   // 抢到一段连续区间
    primBase = observed;                    // 失败重试
}

uint itemIdx;
InterlockedAdd(counters[1], 1, itemIdx);   // 再抢一个 item 槽位
// ... 写 visibleItems[itemIdx]
```

`Shadow` 变体在 `for (cascadeIdx 0..3)` 循环里对 `counters[cascadeIdx*2 ... ]` 做同样的事，竞争更密集。

### 1.3 为什么慢

- **单一计数器上的 CAS 自旋**：所有存活线程都对**同一个** `counters[0]` 做 `InterlockedCompareExchange`。这是一个有界版的 "atomic add"（用 CAS 是为了不越过 cap 并保持 gapless）。在海量 drawcall 下，成千上万条线程争抢同一 4 字节地址，CAS 失败会反复重试，退化成近似串行 + 大量重试浪费。`gpu cull` pass 时间随存活 drawcall 数显著上升。
- **被忽视的额外逐线程全局原子**：除了上面 2 个分配原子，主 pass 每个**候选** node（`node.visible>0`，几乎等于全部 drawcall）还做 2~4 次统计原子：
  - `InterlockedAdd(GPUDrivenStats[0].ProcessedCount, 1)`
  - `InterlockedAdd(GPUDrivenStats[0].TriangleCount, triangleCount)`
  - 命中遮挡再加 `CulledCount` / `CulledTriangleCount`。

  这些对**所有候选**（不只是存活者）触发，竞争面更大，是同等量级甚至更大的开销来源。任何彻底的治理都应一并处理。

> 改造前这里只是给 indirect draw 打个标记（每 item 一条 MDI command），没有"全局前缀和式"的紧致分配需求，所以没有这个竞争；切到 single-draw 紧致 prim 流后才引入了 `primBase` 的全局累加，进而暴露竞争。

---

## 2. 设计目标与约束

1. **正确性优先**：在三角形预算内（正常工况，prim 缓冲已按 `max(required, 2×)` 预留，见 `Scene.Build.cpp`）的输出必须与现状**逐位一致**：相同的 `counters[0]/[1]`、相同的 gapless 紧致布局、相同的可见 item 集合、相同的 `GPUDrivenStats`。
2. **大幅削减全局原子流量**：把"每存活线程 1 次 CAS 自旋 + 每候选线程 2~4 次统计原子"收敛为"**每个 workgroup 各计数器 1 次 `InterlockedAdd`**"。
3. **可移植**：Soft Mesh Shader 路径的存在意义之一就是覆盖**无硬件 mesh shader 的设备（含移动端）**。主方案不得依赖可能缺失的 subgroup 特性；subgroup 快路径作为可选项、需运行时能力检测 + 回退。
4. **小改面、易交接**：默认只改 2 个 shader，不动缓冲区布局、Finalize、Expand、C++ dispatch 与 barrier。

---

## 3. 方案总览

核心思想：**两级聚合（block-level aggregation）**，把"逐线程抢全局计数器"换成"组内本地前缀和 + 每组一次全局 `InterlockedAdd` 预订整段区间"。

对每个 64 线程 workgroup：

1. 每条线程算出本线程的 `emit`（是否产出该 item）与 `myTri`（产出时为 `triangleCount`，否则 0）。
2. 用 `groupshared` 做组内**独占前缀和**，得到：
   - `laneTriPrefix`（本线程在组内之前所有产出三角形数之和）
   - `laneItemIndex`（本线程在组内之前的产出 item 个数）
   - `blockTriTotal` / `blockItemTotal`（整组的三角形总数 / item 总数）
3. **组内仅一条线程**（lane 0）做 **2 次**全局 `InterlockedAdd`，把整组的总量一次性预订下来，拿到 `blockTriBase` / `blockItemBase`。
4. 广播 base 后，每条产出线程算：
   - `primBase = blockTriBase + laneTriPrefix`
   - `itemIdx  = blockItemBase + laneItemIndex`
5. **逐线程 cap 检查**取代 CAS：`if (primBase + triangleCount > cap) return;`（外加 `itemIdx >= MAX_NODES` 保护）。
6. 统计原子同理聚合：组内对 `ProcessedCount/TriangleCount/CulledCount/CulledTriangleCount` 各做一次 `groupshared` 求和，lane 0 一次 `InterlockedAdd` 提交。

原子流量：主 pass 从 "≤64 次 CAS 自旋 + ≤64 次 item 原子 + ≤256 次统计原子 / 组" 降到 "**6 次 `InterlockedAdd` / 组**"（2 个分配 + 4 个统计）。在 65535 drawcall 满载时，分配类全局原子从数万次量级降到约 1024 组 × 常数。

### 3.1 为什么 gapless 仍然成立

组内前缀和让组内各 lane 的 prim 区间首尾相接；每组用**一次** `InterlockedAdd(counters[0], blockTriTotal)` 预订正好 `blockTriTotal` 大小的连续段。跨组之间，每组各推进计数器一次，因此 `counters[0]` 始终等于"已预订三角形总数"，全局连续无空洞。✓

### 3.2 cap 边界语义（唯一与现状有细微差异处）

- **预算内（正常工况）**：所有存活三角形之和 ≤ cap，没有任何 lane 被 cap 检查丢弃，`counters[0]` 收尾值 == 总数 ≤ cap，Finalize 的 `min(counters[0], cap)` 是 no-op。输出与现状**完全一致**。
- **超预算（退化工况）**：用普通 `InterlockedAdd` 时计数器可能冲过 cap，跨越边界的那条 lane 会被丢弃，从而在 `[primBase, cap)` 留下一小段未写入。这与现状 CAS（把 `counters[0]` 停在最后一个 gapless 适配点）略有不同，可能在**超预算帧**的 draw 尾部出现少量陈旧三角形。
  - 该工况仅在场景三角形数超过已配置预算（缓冲已是所需的 ≥2×）时触发，属于"本来就该扩容"的退化场景。
  - 缓解（按需，默认可不做）：(a) 接受 + 加一个 overflow 统计计数器用于诊断；(b) clear 阶段对 prim 尾部做一次 zero-fill，使陈旧数据退化为无害的退化三角形；(c) 若要严格 gapless，可让"成功提交"的 lane 额外 `InterlockedMax(highWater, primBase+triCount)`，Finalize 用 high-water 而非原始计数器——但这违背"少原子"初衷，不推荐。
  - 推荐：**(a)**，并在文档/调试 HUD 暴露 overflow 标志，提示需要调大 `maxSceneTriangles_`。

### 3.3 主方案选型：workgroup-LDS 还是 subgroup？

| 维度 | A. workgroup-LDS 聚合（**主方案**） | B. subgroup-wave 聚合（**可选快路径**） |
|---|---|---|
| 削减倍率 | 固定 = workgroup 大小（64×） | = subgroup 大小（32/64×，64 线程组 = 1~2 subgroup） |
| 依赖 | 仅 `groupshared` + `GroupMemoryBarrierWithGroupSync`（处处可用） | 需 `VK_SUBGROUP_FEATURE_ARITHMETIC_BIT` + `BALLOT_BIT` + COMPUTE stage；SPIR-V 1.3 / Vulkan 1.1 |
| 移动端 | 安全 | 视设备而定，需能力检测 + 回退 |
| 代码 | 一段固定 scan，易读易测 | 更短，但要处理 divergence/active-mask |
| 结论 | **默认实现，先落地** | 桌面 GPU 上作为 Phase 3 加速项，spec-constant 切换 |

仓库现有 shader 仅用过 `groupshared`（`Process.DenoiseJBF.comp.slang`），尚未用过 `WaveActiveSum/WavePrefixSum`。考虑到 soft-mesh 路径的移动端定位，**主方案选 A（LDS）**，B 作为可选优化另开 Phase。

---

## 4. 详细实现

### 4.1 主 pass：`Task.SoftMeshShaderGpuCullCompact.comp.slang`

保留 `IsAABBInFrustum` / `IsOccludedVolume` 不变。改 `main`：所有 64 线程**全程不提前 return**地走到聚合点（用谓词 `emit`/`candidate` 控制，而非 early-return），以保证 barrier 收敛。参考伪代码：

```slang
static const uint GROUP_SIZE = 64;

groupshared uint g_tri[GROUP_SIZE];      // 每 lane 的三角形数（产出才非 0），随后原位改成独占前缀
groupshared uint g_item[GROUP_SIZE];     // 每 lane 是否产出（0/1），随后原位改成独占计数
groupshared uint g_blockTriBase;
groupshared uint g_blockItemBase;
// 统计聚合
groupshared uint g_proc[GROUP_SIZE];     // 候选计数
groupshared uint g_procTri[GROUP_SIZE];  // 候选三角形
groupshared uint g_cull[GROUP_SIZE];     // 被遮挡计数
groupshared uint g_cullTri[GROUP_SIZE];  // 被遮挡三角形

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Lid : SV_GroupThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint lid = Lid.x;

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;
    uint cap = scene.custom_data_2;

    bool inRange = (DTid.x < scene.custom_data_1) && (DTid.x < MAX_NODES);

    // ---- 每线程剔除（与现状逻辑一致），结果落成谓词，不 early-return ----
    bool candidate = false;     // node.visible>0，参与统计
    bool emit = false;          // 最终产出 item
    uint triangleCount = 0;
    NodeProxy node;
    ModelData model;
    if (inRange)
    {
        node = scene.Nodes[DTid.x];
        model = scene.Offsets[node.modelId];
        triangleCount = model.indexCount / 3;
        float4x4 mvp = mul(scene.Camera[0].PrevViewProjectionUnJit, node.worldTS);
        candidate = node.visible > 0;
        bool shouldDraw = candidate && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
        bool occluded = shouldDraw && IsOccludedVolume(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
        emit = shouldDraw && !occluded && triangleCount > 0;

        // 统计：保持现状口径
        g_proc[lid]    = candidate ? 1u : 0u;
        g_procTri[lid] = candidate ? triangleCount : 0u;
        g_cull[lid]    = (candidate && shouldDraw && occluded) ? 1u : 0u;
        g_cullTri[lid] = (candidate && shouldDraw && occluded) ? triangleCount : 0u;
    }
    else
    {
        g_proc[lid] = g_procTri[lid] = g_cull[lid] = g_cullTri[lid] = 0u;
    }

    g_tri[lid]  = emit ? triangleCount : 0u;
    g_item[lid] = emit ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    // ---- lane 0 串行 scan（64 次，足够便宜）+ 每计数器一次全局原子 ----
    if (lid == 0)
    {
        // 分配前缀和
        uint accTri = 0, accItem = 0;
        for (uint i = 0; i < GROUP_SIZE; ++i)
        {
            uint t = g_tri[i];  uint e = g_item[i];
            g_tri[i] = accTri;  g_item[i] = accItem;   // 改成独占前缀
            accTri += t;        accItem += e;
        }
        uint triBase = 0, itemBase = 0;
        if (accTri  > 0) InterlockedAdd(counters[0], accTri,  triBase);
        if (accItem > 0) InterlockedAdd(counters[1], accItem, itemBase);
        g_blockTriBase = triBase;  g_blockItemBase = itemBase;

        // 统计聚合（4 个计数器各一次全局原子）
        uint sP=0,sPT=0,sC=0,sCT=0;
        for (uint i = 0; i < GROUP_SIZE; ++i) { sP+=g_proc[i]; sPT+=g_procTri[i]; sC+=g_cull[i]; sCT+=g_cullTri[i]; }
        if (sP  > 0) InterlockedAdd(scene.GPUDrivenStats[0].ProcessedCount,      sP);
        if (sPT > 0) InterlockedAdd(scene.GPUDrivenStats[0].TriangleCount,       sPT);
        if (sC  > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledCount,         sC);
        if (sCT > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledTriangleCount, sCT);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- 产出 ----
    if (emit)
    {
        uint primBase = g_blockTriBase  + g_tri[lid];
        uint itemIdx  = g_blockItemBase + g_item[lid];
        if (primBase + triangleCount <= cap && itemIdx < uint(MAX_NODES))
        {
            SoftMeshShaderVisibleItem item;
            item.primBase = primBase;
            item.instanceIdx = DTid.x + 1;
            item.modelOffsetIdx = node.modelId;
            item.triCount = triangleCount;
            visibleItems[itemIdx] = item;
        }
        // else: 超预算，丢弃（见 3.2）
    }
}
```

实现注意：
- **谓词化、禁止聚合点前 early-return**：barrier 必须被组内所有线程一致到达。越界线程也要参与到 barrier，并把 `groupshared` 槽位写 0。
- **串行 scan 还是并行 scan**：lane 0 串行 64 次循环逻辑最简、最易验证，单组只跑一次，开销相对被消除的竞争可忽略。若 profiling 显示 scan 本身成为瓶颈，再换 Hillis-Steele 并行 scan（log2(64)=6 步）。
- 统计聚合让 `GPUDrivenStats` 的全局原子从"每候选 2~4 次"降到"每组 4 次"，与分配聚合同等重要。

### 4.2 Shadow pass：`Task.SoftMeshShaderShadowGpuCullCompact.comp.slang`

差异：外层 `for (cascadeIdx 0..3)`，计数器按 `counterBase = (cascadeIdx+1)*2`，`visibleItems[(cascadeIdx+1)*MAX_NODES + itemIdx]`，`item.primBase = cascadeIdx*maxSceneTriangles + primBase`。

改造原则：**让整组 64 线程对每个 active cascade 统一迭代**（`activeCascadeMask` 是 uniform 的，所有 lane 看法一致，循环不发散），在每个 cascade 迭代体内做一次"4.1 式"的 LDS 聚合 + lane 0 全局原子，并在迭代之间用 barrier 复位 `groupshared`：

```slang
for (uint cascadeIdx = 0; cascadeIdx < 4; ++cascadeIdx)
{
    if ((activeCascadeMask & (1u << cascadeIdx)) == 0u) continue; // uniform 分支，全组一致
    GroupMemoryBarrierWithGroupSync();   // 复位上一个 cascade 的 LDS 之前同步

    // 每 lane 算该 cascade 的 candidate/emit/triangleCount，填 g_tri/g_item/g_stats
    // ... 同 4.1 ...
    GroupMemoryBarrierWithGroupSync();

    if (lid == 0) {
        // scan + InterlockedAdd(counters[counterBase], ...) / counters[counterBase+1]
        // 统计写 scene.ShadowGPUDrivenStats[cascadeIdx].*
    }
    GroupMemoryBarrierWithGroupSync();

    if (emit) {
        uint primBase = blockTriBase + g_tri[lid];
        uint itemIdx  = blockItemBase + g_item[lid];
        if (primBase + triangleCount <= maxSceneTriangles && itemIdx < uint(MAX_NODES)) {
            // item.primBase = cascadeIdx * maxSceneTriangles + primBase;
            // visibleItems[(cascadeIdx+1)*MAX_NODES + itemIdx] = item;
        }
    }
}
```

注意 shadow 的 cap 是每 cascade 各 `maxSceneTriangles`（shadow prim 缓冲是 `maxSceneTriangles * 4`，按 cascade 分段），与主 pass 同构。shadow 统计口径见现状：`candidate` 时累加 `Processed/Triangle`，`!shouldDraw` 时累加 `Culled/CulledTriangle`（含 frustum 剔除），需逐字对齐。

### 4.3 可选 Phase 3：subgroup 快路径（桌面）

在支持 `subgroupArithmetic + subgroupBallot` 的设备上，用 subgroup intrinsic 省掉 LDS 与 barrier：

```slang
uint myTri = emit ? triangleCount : 0u;
uint laneTriPrefix  = WavePrefixSum(myTri);
uint waveTriTotal   = WaveActiveSum(myTri);
uint4 ballot        = WaveActiveBallot(emit);
uint laneItemIndex  = WavePrefixCountBits(ballot);   // 之前产出 lane 数（独占）
uint waveItemTotal  = WaveActiveCountBits(ballot);
uint triBase = 0, itemBase = 0;
if (WaveIsFirstLane()) {
    if (waveTriTotal  > 0) InterlockedAdd(counters[0], waveTriTotal,  triBase);
    if (waveItemTotal > 0) InterlockedAdd(counters[1], waveItemTotal, itemBase);
}
triBase  = WaveReadLaneFirst(triBase);
itemBase = WaveReadLaneFirst(itemBase);
uint primBase = triBase + laneTriPrefix;
uint itemIdx  = itemBase + laneItemIndex;
```

落地要点：
- C++ 侧在 `Device` 能力里查询 `VkPhysicalDeviceSubgroupProperties`（`supportedOperations` 含 `ARITHMETIC|BALLOT|BASIC`，`supportedStages` 含 `COMPUTE`），存入 `caps_`。
- 用 spec constant 或两套编译变体（`*_wave` / `*_lds`）在 `gpuCullCompactPipeline` 创建时择一绑定；不支持则回退 LDS 变体。
- subgroup 聚合是 per-subgroup 而非 per-workgroup，64 线程组在 32-lane 硬件上 = 2 次原子/组，仍远优于现状。
- divergence：用谓词 `emit` + 全 lane 一致执行 wave op（不要在 wave op 前 early-return），与 4.1 同样的"谓词化"纪律。

---

## 5. 改动清单（交接用）

默认（Phase 1+2，零 C++ 改动）：

- `assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang` —— 重写 `main` 的分配与统计为 LDS 聚合。
- `assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact.comp.slang` —— 同上，逐 cascade。
- 重新编译 shader（`gnb` 构建脚本会跑 slang→spv；确认两个 `.spv` 重新生成）。

不需要改动（确认即可）：

- `Task.SoftMeshShaderFinalize.comp.slang`：仍读 `counters[slot*2]/[slot*2+1]`，语义不变。
- `Task.SoftMeshShaderExpand.comp.slang`：仍按 `item.primBase` 展开，语义不变。
- `VulkanBaseRenderer.GpuDriven.cpp`：counter `vkCmdFillBuffer` 清零、barrier、dispatch、indirect 参数全部不变。
- `Scene.Build.cpp` 缓冲区布局、`Counters` 尺寸（`slotCount*2`）、`BasicTypes.slang` 结构体不变。

可选（Phase 3，需少量 C++）：

- `Engine/Vulkan/Device.*`：查询并缓存 subgroup 能力。
- 管线创建处：按能力选 `*_wave` / `*_lds` 变体或设 spec constant。
- 可加一个 overflow 诊断计数器（见 3.2）。

---

## 6. 验证方案

1. **功能等价（预算内）**：
   - 准备一个海量 drawcall 压力场景（数万 node，确保总三角形 < `maxSceneTriangles`）。
   - 对比改造前后：`GPUDrivenStats`（Processed/Triangle/Culled/CulledTriangle）逐字段相等；`counters[0]/[1]` 相等；visibility pass / 最终图像逐像素一致（可用 object-id buffer 或截图 diff）。
   - shadow：逐 cascade 重复上述对比（`ShadowGPUDrivenStats` 与各 cascade draw 结果）。
2. **性能**：用已有 `SCOPED_GPU_TIMER("gpu cull")` 与 `"shadow pass"` 计时，在压力场景下对比改造前后该 pass 的 GPU 时间，应随 drawcall 数显著下降且斜率变缓。记录基线/改后数据回填本文。
3. **边界 / 超预算**：构造一个故意超 `maxSceneTriangles` 的场景，确认不崩溃、不越界（开 Vulkan validation + GPU-AV），并验证 overflow 行为符合 3.2 的预期（仅尾部少量退化，且诊断计数器置位）。
4. **gapless 不变量**：调试构建里加一段校验（CPU readback 或 debug shader）确认 `[0, counters[0])` 内 prim 全部被写入、无空洞、item 区间互不重叠。
5. **多平台**：至少在一台桌面 GPU + 一台移动 / 集显（soft-mesh 的目标设备）上各跑一遍 LDS 主方案；Phase 3 的 wave 变体额外验证能力检测与回退。

---

## 7. 风险与回退

- **Barrier 收敛**：聚合点前若残留 early-return 会导致 barrier 死锁 / UB。务必谓词化。这是本改造最大坑点，code review 重点检查。
- **统计口径漂移**：shadow 的 `Culled` 含 frustum 剔除（`!shouldDraw`），主 pass 的 `Culled` 仅含 occlusion。聚合时要逐字对齐现状条件，否则统计对不上。
- **超预算尾部差异**：见 3.2，属可接受退化；若产品上不可接受，启用缓解 (b) 或 (c)。
- **回退**：改动局限在 2 个 shader 的 `main`，`git revert` 即可恢复 CAS 版本；缓冲区与 C++ 不变，无迁移成本。建议分两个 commit（主 pass / shadow pass）便于二分。

---

## 8. 分阶段交付

- **Phase 1 — 主 pass LDS 聚合**：改 `Task.SoftMeshShaderGpuCullCompact`，跑通验证 1/2/4。预期拿下主 pass 的大头收益。
- **Phase 2 — Shadow pass LDS 聚合**：改 shadow 变体，逐 cascade 验证。
- **Phase 3（可选）— subgroup 快路径 + 能力检测**：桌面加速，spec-constant/变体切换 + 回退；含 overflow 诊断计数器。

建议先做 Phase 1 并测得收益数据，再决定是否继续 Phase 3。
