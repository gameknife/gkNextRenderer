import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // IsAABBInFrustum / ComputeScreenExtent / SelectLod
import Shader.VisibilityBuffer;

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
    bool inRange = (DTid.x < scene.CustomData1) && (DTid.x < MAX_RENDER_PROXIES);
    NodeProxy node;
    ModelData model;
    bool isCandidate = false;
    if (inRange)
    {
        node = scene.Nodes[DTid.x];
        model = scene.Offsets[node.modelId];
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
        uint triangleCount = 0;
        uint lod = 0;
        if (inRange)
        {
            float4x4 lightMvp = mul(scene.Camera[0].SunCascadeViewProjection[cascadeIdx], node.worldTS);
            shouldDraw = isCandidate && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, lightMvp);

            // Frustum first, level second: selecting a level costs eight corner projections plus
            // a bounds reduction, and a proxy outside this cascade never uses the result. In the
            // asteroid belt 99.5% of shadow candidates are rejected here, so that work was almost
            // entirely wasted.
            //
            // This also fixes the stats. triangleCount is now zero for anything the cascade
            // frustum dropped, so TriangleCount stops counting the candidate pool and starts
            // counting what is actually rasterised, and CulledTriangleCount falls to zero. The
            // panel's `visible = TriangleCount - CulledTriangleCount` therefore reads the real
            // shadow load. The proxy counters are untouched, so how many casters were considered
            // and how many the frustum rejected is still visible.
            // Per-cascade, not per-node: each cascade projects the same proxy at a different
            // scale, so a distant cascade legitimately shadows with coarser geometry than cascade 0.
            if (shouldDraw)
            {
                lod = SelectLod(
                    ComputeScreenExtent(model.localAabbMin.xyz, model.localAabbMax.xyz, lightMvp,
                                        float2(SUN_SHADOW_RESOLUTION, SUN_SHADOW_RESOLUTION)),
                    max(model.lodCount, 1u), scene.Camera[0].LodShadowThreshold);
                triangleCount = scene.Offsets[node.modelId].lodIndexCount[lod] / 3;
            }
            emit = shouldDraw && triangleCount > 0;
        }

        // Proxy counters keep the candidate口径: Processed = candidate, Culled = candidate &&
        // !shouldDraw. The triangle counters no longer follow it -- triangleCount is zero outside
        // the cascade frustum -- so TriangleCount is the rasterised total and CulledTriangleCount
        // is zero. That is deliberate: the old candidate-wide triangle total looked like a render
        // cost and was not one.
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
            if (primBase + triangleCount <= maxSceneTriangles && itemIdx < uint(MAX_RENDER_PROXIES))
            {
                SoftMeshShaderVisibleItem item;
                item.primBase = cascadeIdx * maxSceneTriangles + primBase;
                item.instanceIdx = Visibility.MakeIdWithLod(DTid.x + 1, 0, lod).x;
                item.modelOffsetIdx = node.modelId;
                item.triCount = triangleCount;
                visibleItems[slot * MAX_RENDER_PROXIES + itemIdx] = item;
            }
        }
    }
}
