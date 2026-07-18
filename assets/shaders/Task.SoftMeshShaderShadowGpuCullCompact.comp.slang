import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // IsAABBInFrustum (IsOccludedVolume unused here)

// ---------------------------------------------------------------------------
// Block-level aggregation per cascade; see docs/guides/soft-mesh-shader-gpu-driven-submit.md.
//
// The original code had every surviving thread spin on a per-cascade CAS and fire
// up to 4 stats atomics per candidate. The shadow loop iterates all 4 cascades, so
// the contention is even denser than the main pass. Here each active cascade does a
// single LDS exclusive prefix scan in lane 0 + a handful of InterlockedAdd reserves.
// activeCascadeMask is uniform across the workgroup so the cascade loop never
// diverges and every barrier is reached by all 64 lanes.
// ---------------------------------------------------------------------------
static const uint GROUP_SIZE = 64;

groupshared uint g_tri[GROUP_SIZE];   // per-lane emitted triangle count, rewritten in place to an exclusive prefix
groupshared uint g_item[GROUP_SIZE];  // per-lane emit flag (0/1), rewritten in place to an exclusive item index
groupshared uint g_blockTriBase;
groupshared uint g_blockItemBase;
groupshared uint g_proc[GROUP_SIZE];    // stats: candidate flag
groupshared uint g_procTri[GROUP_SIZE]; // stats: candidate triangle count
groupshared uint g_cull[GROUP_SIZE];    // stats: candidate && !shouldDraw (includes frustum cull)
groupshared uint g_cullTri[GROUP_SIZE]; // stats: candidate && !shouldDraw triangle count

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Lid : SV_GroupThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint lid = Lid.x;

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;

    uint activeCascadeMask = scene.CustomData0;
    uint maxSceneTriangles = scene.CustomData2;

    // node-invariant data, computed once (predicate, no early-return before the cascade barriers)
    bool inRange = (DTid.x < scene.CustomData1) && (DTid.x < MAX_NODES);
    NodeProxy node;
    ModelData model;
    uint triangleCount = 0;
    bool isCandidate = false;
    if (inRange)
    {
        node = scene.Nodes[DTid.x];
        model = scene.Offsets[node.modelId];
        triangleCount = model.indexCount / 3;
        isCandidate = (node.visible & RENDER_PARTICIPATION_SHADOW_CASTER) != 0u;
    }

    for (uint cascadeIdx = 0; cascadeIdx < 4; ++cascadeIdx)
    {
        if ((activeCascadeMask & (1u << cascadeIdx)) == 0u)
        {
            continue; // uniform branch: all 64 lanes skip together
        }

        // Sync before overwriting LDS so the previous cascade's emit reads are done.
        GroupMemoryBarrierWithGroupSync();

        bool shouldDraw = false;
        bool emit = false;
        if (inRange)
        {
            float4x4 lightMvp = mul(scene.Camera[0].SunCascadeViewProjection[cascadeIdx], node.worldTS);
            shouldDraw = isCandidate && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, lightMvp);
            emit = shouldDraw && triangleCount > 0;
        }

        // Stats口径与现状逐字对齐：candidate 计 Processed/Triangle；candidate && !shouldDraw 计 Culled（含 frustum 剔除）。
        bool culled = isCandidate && !shouldDraw;
        g_proc[lid]    = isCandidate ? 1u : 0u;
        g_procTri[lid] = isCandidate ? triangleCount : 0u;
        g_cull[lid]    = culled ? 1u : 0u;
        g_cullTri[lid] = culled ? triangleCount : 0u;
        g_tri[lid]  = emit ? triangleCount : 0u;
        g_item[lid] = emit ? 1u : 0u;
        GroupMemoryBarrierWithGroupSync();

        uint slot = cascadeIdx + 1;
        uint counterBase = slot * 2;
        if (lid == 0)
        {
            uint accTri = 0, accItem = 0;
            for (uint i = 0; i < GROUP_SIZE; ++i)
            {
                uint t = g_tri[i];
                uint e = g_item[i];
                g_tri[i]  = accTri;
                g_item[i] = accItem;
                accTri  += t;
                accItem += e;
            }
            uint triBase = 0, itemBase = 0;
            if (accTri  > 0) InterlockedAdd(counters[counterBase],     accTri,  triBase);
            if (accItem > 0) InterlockedAdd(counters[counterBase + 1], accItem, itemBase);
            g_blockTriBase  = triBase;
            g_blockItemBase = itemBase;

            uint sP = 0, sPT = 0, sC = 0, sCT = 0;
            for (uint i = 0; i < GROUP_SIZE; ++i)
            {
                sP  += g_proc[i];
                sPT += g_procTri[i];
                sC  += g_cull[i];
                sCT += g_cullTri[i];
            }
            if (sP  > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].ProcessedCount,      sP);
            if (sPT > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].TriangleCount,       sPT);
            if (sC  > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledCount,         sC);
            if (sCT > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledTriangleCount, sCT);
        }
        GroupMemoryBarrierWithGroupSync();

        if (emit)
        {
            uint primBase = g_blockTriBase  + g_tri[lid];
            uint itemIdx  = g_blockItemBase + g_item[lid];
            if (primBase + triangleCount <= maxSceneTriangles && itemIdx < uint(MAX_NODES))
            {
                SoftMeshShaderVisibleItem item;
                item.primBase = cascadeIdx * maxSceneTriangles + primBase;
                item.instanceIdx = DTid.x + 1;
                item.modelOffsetIdx = node.modelId;
                item.triCount = triangleCount;
                visibleItems[slot * MAX_NODES + itemIdx] = item;
            }
        }
    }
}
