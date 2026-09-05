import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // IsAABBInFrustum / IsOccludedVolume / ComputeScreenExtent / SelectLod
import Shader.VisibilityBuffer;

// ---------------------------------------------------------------------------
// Subgroup (wave) fast path of the soft-mesh GPU cull (plan Phase 3).
//
// Functionally identical to the LDS variant (Task.SoftMeshShaderGpuCullCompact)
// but the per-block exclusive prefix sum + reservation is done with hardware
// wave intrinsics instead of a groupshared scan + barriers. Aggregation is
// per-subgroup: each subgroup's first lane reserves the whole subgroup's range
// with one InterlockedAdd per counter, so the gapless prim layout still holds.
//
// This .spv declares the GroupNonUniform* capabilities; the renderer only binds
// it on devices that report subgroup arithmetic + ballot for COMPUTE, otherwise
// it falls back to the LDS variant. All lanes execute the wave ops (predicated,
// never early-return before them) so the reductions stay convergent.
// ---------------------------------------------------------------------------
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;
    uint cap = scene.CustomData2;

    bool inRange = (DTid.x < scene.CustomData1) && (DTid.x < MAX_RENDER_PROXIES);

    bool shouldDraw = false;
    bool occluded = false;
    bool emit = false;
    uint triangleCount = 0;
    uint lod0TriangleCount = 0;
    uint lod = 0;
    NodeProxy node;
    if (inRange)
    {
        node = scene.Nodes[DTid.x];
        ModelData model = scene.Offsets[node.modelId];
        // Frustum culling must use the current camera (see LDS variant note).
        float4x4 mvp = mul(scene.Camera[0].PrevViewProjectionUnJit, node.worldTS);

        // Occlusion must project with the SAME matrix the depth buffer was rendered with. The
        // temporal upscaler jitters the projection every frame, so sampling a jittered depth
        // buffer through the unjittered matrix misses by up to half a pixel -- enough to flip the
        // verdict on anything only a few pixels across, and it flips again next frame as the
        // jitter moves. That showed up as distant geometry blinking with a static camera, and as
        // the cull counters oscillating ~8% with nothing in the scene moving.
        // Frustum culling deliberately keeps the unjittered matrix: it has no depth buffer to
        // agree with, and jitter there would pop objects in and out at the screen edge.
        float4x4 occlusionMvp = mul(scene.Camera[0].PrevViewProjection, node.worldTS);

        // Same level policy as the LDS variant; the two must agree or switching paths per device
        // would change what gets drawn.
        lod = SelectLod(
            ComputeScreenExtent(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp,
                                float2(scene.Camera[0].ViewportRect.zw)),
            max(model.lodCount, 1u), scene.Camera[0].LodBaseThreshold);
        triangleCount = scene.Offsets[node.modelId].lodIndexCount[lod] / 3;
        // What this proxy would cost without LOD, for the savings figure in the stat overlay.
        lod0TriangleCount = model.lodIndexCount[0] / 3;

        shouldDraw = ((node.visible & RENDER_PARTICIPATION_MAIN_VISIBILITY) != 0u) &&
            IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
        occluded = shouldDraw && IsOccludedVolume(model.localAabbMin.xyz, model.localAabbMax.xyz, occlusionMvp);
        emit = shouldDraw && !occluded && triangleCount > 0;
    }

    // Stats口径与 LDS / 原 CAS 版逐字对齐：Processed/Triangle = shouldDraw；Culled = occlusion 命中。
    uint sP  = WaveActiveSum(shouldDraw ? 1u : 0u);
    uint sPT = WaveActiveSum(shouldDraw ? triangleCount : 0u);
    uint sC  = WaveActiveSum(occluded ? 1u : 0u);
    uint sCT = WaveActiveSum(occluded ? triangleCount : 0u);
    uint sL0 = WaveActiveSum(shouldDraw ? lod0TriangleCount : 0u);
    uint maxVisibleProxyIndex = WaveActiveMax(emit ? DTid.x + 1u : 0u);

    uint myTri = emit ? triangleCount : 0u;
    uint laneTriPrefix = WavePrefixSum(myTri);
    uint waveTriTotal  = WaveActiveSum(myTri);
    uint laneItemIndex = WavePrefixCountBits(emit);
    uint waveItemTotal = WaveActiveCountBits(emit);

    uint triBase = 0, itemBase = 0;
    if (WaveIsFirstLane())
    {
        if (waveTriTotal  > 0) InterlockedAdd(counters[0], waveTriTotal,  triBase);
        if (waveItemTotal > 0) InterlockedAdd(counters[1], waveItemTotal, itemBase);
        if (sP  > 0) InterlockedAdd(scene.GPUDrivenStats[0].ProcessedCount,      sP);
        if (sPT > 0) InterlockedAdd(scene.GPUDrivenStats[0].TriangleCount,       sPT);
        if (sC  > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledCount,         sC);
        if (sCT > 0) InterlockedAdd(scene.GPUDrivenStats[0].CulledTriangleCount, sCT);
        if (sL0 > 0) InterlockedAdd(scene.GPUDrivenStats[0].Lod0TriangleCount,   sL0);
        if (maxVisibleProxyIndex > 0u)
            InterlockedMax(scene.GPUDrivenStats[0].MaxVisibleProxyIndex, maxVisibleProxyIndex);
    }
    triBase  = WaveReadLaneFirst(triBase);
    itemBase = WaveReadLaneFirst(itemBase);

    if (emit)
    {
        uint primBase = triBase  + laneTriPrefix;
        uint itemIdx  = itemBase + laneItemIndex;
        if (primBase + triangleCount <= cap && itemIdx < uint(MAX_RENDER_PROXIES))
        {
            SoftMeshShaderVisibleItem item;
            item.primBase = primBase;
            item.instanceIdx = Visibility.MakeIdWithLod(DTid.x + 1, 0, lod).x;
            item.modelOffsetIdx = node.modelId;
            item.triCount = triangleCount;
            visibleItems[itemIdx] = item;
        }
    }
}
