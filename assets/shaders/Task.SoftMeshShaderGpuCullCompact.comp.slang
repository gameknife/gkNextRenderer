import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // IsAABBInFrustum / IsOccludedVolume

// ---------------------------------------------------------------------------
// Block-level aggregation (see docs/plans/softmeshshader-gpucull-atomic-contention-plan.md)
//
// Instead of every surviving thread spinning on a single global CAS to reserve
// its prim range (and every candidate firing 2~4 stats atomics), each 64-thread
// workgroup computes a local exclusive prefix sum in LDS and lane 0 reserves the
// whole block's range with a handful of InterlockedAdd calls. This collapses the
// per-thread atomic storm into ~6 atomics per workgroup while keeping the gapless
// prim layout and the exact same stats semantics.
// ---------------------------------------------------------------------------
static const uint GROUP_SIZE = 64;

groupshared uint g_tri[GROUP_SIZE];   // per-lane emitted triangle count, rewritten in place to an exclusive prefix
groupshared uint g_item[GROUP_SIZE];  // per-lane emit flag (0/1), rewritten in place to an exclusive item index
groupshared uint g_blockTriBase;      // global base reserved for this block's triangles
groupshared uint g_blockItemBase;     // global base reserved for this block's items
groupshared uint g_proc[GROUP_SIZE];    // stats: drawn (visible && in-frustum) flag
groupshared uint g_procTri[GROUP_SIZE]; // stats: drawn triangle count
groupshared uint g_cull[GROUP_SIZE];    // stats: occlusion-culled flag
groupshared uint g_cullTri[GROUP_SIZE]; // stats: occlusion-culled triangle count

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Lid : SV_GroupThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint lid = Lid.x;

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;
    uint cap = scene.CustomData2;

    bool inRange = (DTid.x < scene.CustomData1) && (DTid.x < MAX_NODES);

    // ---- per-thread culling, expressed as predicates (no early-return before the barrier) ----
    bool emit = false;
    uint triangleCount = 0;
    NodeProxy node;
    if (inRange)
    {
        node = scene.Nodes[DTid.x];
        ModelData model = scene.Offsets[node.modelId];
        triangleCount = model.indexCount / 3;
        // Frustum culling must use the current camera. The previous camera was only
        // needed by the removed temporal depth test and caused one-frame edge pops.
        float4x4 mvp = mul(scene.Camera[0].PrevViewProjectionUnJit, node.worldTS);

        bool shouldDraw = ((node.visible & RENDER_PARTICIPATION_MAIN_VISIBILITY) != 0u) &&
            IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
        // IsOccludedVolume is expensive (6^3 samples) so it is only evaluated for drawn nodes, as before.
        bool occluded = shouldDraw && IsOccludedVolume(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
        emit = shouldDraw && !occluded && triangleCount > 0;

        // Stats口径与现状逐字对齐：Processed/Triangle = shouldDraw；Culled/CulledTriangle = occlusion 命中。
        g_proc[lid]    = shouldDraw ? 1u : 0u;
        g_procTri[lid] = shouldDraw ? triangleCount : 0u;
        g_cull[lid]    = occluded ? 1u : 0u;
        g_cullTri[lid] = occluded ? triangleCount : 0u;
    }
    else
    {
        g_proc[lid] = g_procTri[lid] = g_cull[lid] = g_cullTri[lid] = 0u;
    }

    g_tri[lid]  = emit ? triangleCount : 0u;
    g_item[lid] = emit ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    // ---- lane 0 serial scan (64 iterations) + one global atomic per counter ----
    if (lid == 0)
    {
        uint accTri = 0, accItem = 0;
        for (uint i = 0; i < GROUP_SIZE; ++i)
        {
            uint t = g_tri[i];
            uint e = g_item[i];
            g_tri[i]  = accTri;   // exclusive prefix of triangles
            g_item[i] = accItem;  // exclusive prefix of items
            accTri  += t;
            accItem += e;
        }
        uint triBase = 0, itemBase = 0;
        if (accTri  > 0) InterlockedAdd(counters[0], accTri,  triBase);
        if (accItem > 0) InterlockedAdd(counters[1], accItem, itemBase);
        g_blockTriBase  = triBase;
        g_blockItemBase = itemBase;

        // Stats aggregation: one global atomic per counter instead of one per candidate thread.
        uint sP = 0, sPT = 0, sC = 0, sCT = 0;
        for (uint i = 0; i < GROUP_SIZE; ++i)
        {
            sP  += g_proc[i];
            sPT += g_procTri[i];
            sC  += g_cull[i];
            sCT += g_cullTri[i];
        }
        if (sP  > 0) InterlockedAdd(scene.GPUDrivenStats[0].ProcessedCount,      sP);
        if (sPT > 0) InterlockedAdd(scene.GPUDrivenStats[0].TriangleCount,       sPT);
        if (sC  > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledCount,         sC);
        if (sCT > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledTriangleCount, sCT);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- emit ----
    if (emit)
    {
        uint primBase = g_blockTriBase  + g_tri[lid];
        uint itemIdx  = g_blockItemBase + g_item[lid];
        // cap check replaces the CAS loop; over-budget lanes are dropped (see plan §3.2).
        if (primBase + triangleCount <= cap && itemIdx < uint(MAX_NODES))
        {
            SoftMeshShaderVisibleItem item;
            item.primBase = primBase;
            item.instanceIdx = DTid.x + 1;
            item.modelOffsetIdx = node.modelId;
            item.triCount = triangleCount;
            visibleItems[itemIdx] = item;
        }
    }
}
