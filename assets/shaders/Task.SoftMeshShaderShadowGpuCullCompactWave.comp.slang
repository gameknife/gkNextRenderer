import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // IsAABBInFrustum / ComputeScreenExtent / SelectLod
import Shader.VisibilityBuffer;

// ---------------------------------------------------------------------------
// Subgroup (wave) fast path of the soft-mesh shadow GPU cull (plan Phase 3).
//
// Same behaviour as the LDS variant (Task.SoftMeshShaderShadowGpuCullCompact)
// but per-cascade aggregation uses wave intrinsics instead of LDS scan/barriers.
// activeCascadeMask is uniform so the cascade loop never diverges; all lanes run
// the wave ops each active cascade (predicated, no early-return). Only bound on
// devices reporting subgroup arithmetic + ballot for COMPUTE; otherwise the LDS
// variant is used.
// ---------------------------------------------------------------------------
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;

    uint activeCascadeMask = scene.CustomData0;
    uint maxSceneTriangles = scene.CustomData2;

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
            continue; // uniform branch
        }

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
            // Must match the LDS variant exactly, ordering included.
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

        // Same口径 as the LDS variant: proxy counters stay candidate-wide, triangle counters now
        // cover only what the cascade frustum kept.
        bool culled = isCandidate && !shouldDraw;
        uint sP  = WaveActiveSum(isCandidate ? 1u : 0u);
        uint sPT = WaveActiveSum(isCandidate ? triangleCount : 0u);
        uint sC  = WaveActiveSum(culled ? 1u : 0u);
        uint sCT = WaveActiveSum(culled ? triangleCount : 0u);

        uint myTri = emit ? triangleCount : 0u;
        uint laneTriPrefix = WavePrefixSum(myTri);
        uint waveTriTotal  = WaveActiveSum(myTri);
        uint laneItemIndex = WavePrefixCountBits(emit);
        uint waveItemTotal = WaveActiveCountBits(emit);

        uint slot = cascadeIdx + 1;
        uint counterBase = slot * 2;
        uint triBase = 0, itemBase = 0;
        if (WaveIsFirstLane())
        {
            if (waveTriTotal  > 0) InterlockedAdd(counters[counterBase],     waveTriTotal,  triBase);
            if (waveItemTotal > 0) InterlockedAdd(counters[counterBase + 1], waveItemTotal, itemBase);
            if (sP  > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].ProcessedCount,      sP);
            if (sPT > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].TriangleCount,       sPT);
            if (sC  > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledCount,         sC);
            if (sCT > 0) InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledTriangleCount, sCT);
        }
        triBase  = WaveReadLaneFirst(triBase);
        itemBase = WaveReadLaneFirst(itemBase);

        if (emit)
        {
            uint primBase = triBase  + laneTriPrefix;
            uint itemIdx  = itemBase + laneItemIndex;
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
