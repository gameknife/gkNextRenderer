// Fixed-descriptor, non-BDA equivalent of the full soft-mesh compact pass. It only performs
// frustum culling: occlusion needs the full renderer's previous-depth resource, which this
// compatibility chain deliberately does not create.
import Shader.Types;

static const uint kRenderParticipationMainVisibility = 1u;

[[vk::binding(0, 0)]] StructuredBuffer<NodeProxy> Nodes;
[[vk::binding(3, 0)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint4> VisibleItems;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> Counters;

struct CompatibilitySoftMeshCullPushConstants
{
    float4x4 ViewProjection;
    uint RenderProxyCount;
    uint TriangleCapacity;
    float2 ViewportSize;
    // Projected pixel extent at or above which a proxy draws at LOD0. Each further level halves it.
    float LodBaseThreshold;
    float Padding;
};
[[vk::push_constant]] CompatibilitySoftMeshCullPushConstants pushConstants;

bool IsAABBInFrustum(float3 minPoint, float3 maxPoint, float4x4 mvp)
{
    float3 corners[8];
    corners[0] = float3(minPoint.x, minPoint.y, minPoint.z);
    corners[1] = float3(maxPoint.x, minPoint.y, minPoint.z);
    corners[2] = float3(minPoint.x, maxPoint.y, minPoint.z);
    corners[3] = float3(maxPoint.x, maxPoint.y, minPoint.z);
    corners[4] = float3(minPoint.x, minPoint.y, maxPoint.z);
    corners[5] = float3(maxPoint.x, minPoint.y, maxPoint.z);
    corners[6] = float3(minPoint.x, maxPoint.y, maxPoint.z);
    corners[7] = float3(maxPoint.x, maxPoint.y, maxPoint.z);

    bool outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.x >= -clipPosition.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.x <= clipPosition.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.y >= -clipPosition.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.y <= clipPosition.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.z >= -clipPosition.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (uint i = 0; i < 8u; ++i)
    {
        const float4 clipPosition = mul(mvp, float4(corners[i], 1.0));
        if (clipPosition.z <= clipPosition.w) { outside = false; break; }
    }
    return !outside;
}

// Longest screen-space edge of the proxy's projected bounds, in pixels. Returns a huge value for
// anything crossing the near plane: a bounds straddling w=0 has no meaningful projected size, and
// forcing full detail there is both cheap (it is close) and safe.
float ComputeScreenExtent(float3 minPoint, float3 maxPoint, float4x4 mvp)
{
    float2 ndcMin = float2(1e30f, 1e30f);
    float2 ndcMax = float2(-1e30f, -1e30f);
    for (uint i = 0; i < 8u; ++i)
    {
        const float3 corner = float3(
            (i & 1u) != 0u ? maxPoint.x : minPoint.x,
            (i & 2u) != 0u ? maxPoint.y : minPoint.y,
            (i & 4u) != 0u ? maxPoint.z : minPoint.z);
        const float4 clipPosition = mul(mvp, float4(corner, 1.0));
        if (clipPosition.w <= 1e-6f)
        {
            return 1e30f;
        }
        const float2 ndc = clipPosition.xy / clipPosition.w;
        ndcMin = min(ndcMin, ndc);
        ndcMax = max(ndcMax, ndc);
    }
    // NDC spans [-1,1], so half the extent maps to the viewport dimension.
    const float2 pixelExtent = (ndcMax - ndcMin) * 0.5f * pushConstants.ViewportSize;
    return max(pixelExtent.x, pixelExtent.y);
}

uint SelectLod(float screenExtent, uint lodCount)
{
    uint lod = 0u;
    float threshold = pushConstants.LodBaseThreshold;
    while (lod + 1u < lodCount && screenExtent < threshold)
    {
        ++lod;
        threshold *= 0.5f;
    }
    return lod;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint proxyIndex = dispatchThreadId.x;
    if (proxyIndex >= pushConstants.RenderProxyCount)
    {
        return;
    }

    const NodeProxy proxy = Nodes[proxyIndex];
    const ModelData model = Offsets[proxy.modelId];
    const float4x4 mvp = mul(pushConstants.ViewProjection, proxy.worldTS);
    const bool visible = (proxy.visible & kRenderParticipationMainVisibility) != 0u &&
        model.indexCount > 0u &&
        IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
    if (!visible)
    {
        return;
    }

    // Pick the level here, once per proxy, and hand the resolved index range downstream. Expansion
    // and the vertex stage then never need to know a LOD exists.
    const float screenExtent = ComputeScreenExtent(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);
    const uint lod = SelectLod(screenExtent, max(model.lodCount, 1u));
    // Indexed off the buffer rather than the local copy: a dynamic index into a struct member array
    // is what pushes the whole ModelData onto the stack in the constrained MSL backend.
    const uint lodIndexOffset = Offsets[proxy.modelId].lodIndexOffset[lod];
    const uint triangleCount = Offsets[proxy.modelId].lodIndexCount[lod] / 3u;
    if (triangleCount == 0u)
    {
        return;
    }

    uint primitiveBase = 0u;
    InterlockedAdd(Counters[0], triangleCount, primitiveBase);
    if (primitiveBase + triangleCount > pushConstants.TriangleCapacity)
    {
        return;
    }

    uint itemIndex = 0u;
    InterlockedAdd(Counters[1], 1u, itemIndex);
    if (itemIndex < uint(MAX_RENDER_PROXIES))
    {
        // { primitiveBase, proxyIndex, triangleCount, index offset of the selected level }
        VisibleItems[itemIndex] = uint4(primitiveBase, proxyIndex, triangleCount, lodIndexOffset);
    }
}
