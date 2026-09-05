// Shared culling helpers for the Soft Mesh Shader GPU-driven cull passes.
// Imported by both the LDS and the subgroup (wave) variants of
// Task.SoftMeshShaderGpuCullCompact / Task.SoftMeshShaderShadowGpuCullCompact.
module SoftMeshShaderCull;
import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

public bool IsOccludedVolume(float3 min, float3 max, float4x4 wvp)
{
    const int GRID_SIZE = 6;
    const float GRID_SIZE_MINUS_ONE = GRID_SIZE - 1;

    uint2 screenSize = Bindless.GetViewStorageTextureDimensions<float>(Bindless.RT_PREV_DEPTHBUFFER);

    for (int x = 0; x < GRID_SIZE; x++)
    {
        for (int y = 0; y < GRID_SIZE; y++)
        {
            for (int z = 0; z < GRID_SIZE; z++)
            {
                float3 gridPoint = min + float3(
                    (max.x - min.x) * (float(x) / GRID_SIZE_MINUS_ONE),
                    (max.y - min.y) * (float(y) / GRID_SIZE_MINUS_ONE),
                    (max.z - min.z) * (float(z) / GRID_SIZE_MINUS_ONE));

                float4 clipPos = mul(wvp, float4(gridPoint, 1.0));
                float3 ndc = clipPos.xyz / clipPos.w;
                if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
                {
                    return false;
                }

                float2 screenUV = ndc.xy * 0.5 + 0.5;
                int2 pixelCoord = int2(screenUV * float2(screenSize));
                if (pixelCoord.x < 0 || pixelCoord.x >= screenSize.x ||
                    pixelCoord.y < 0 || pixelCoord.y >= screenSize.y)
                {
                    return false;
                }

                float previousDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixelCoord];
                // Reverse-Z: larger NDC depth is closer. A previous sample that is farther than
                // this volume means the volume may be visible, so it must not be culled.
                float currentDepth = ndc.z + 0.00001;
                if (previousDepth < currentDepth)
                {
                    return false;
                }
            }
        }
    }

    return true;
}

public bool IsAABBInFrustum(float3 min, float3 max, float4x4 wvp)
{
    float3 corners[8];
    corners[0] = float3(min.x, min.y, min.z);
    corners[1] = float3(max.x, min.y, min.z);
    corners[2] = float3(min.x, max.y, min.z);
    corners[3] = float3(max.x, max.y, min.z);
    corners[4] = float3(min.x, min.y, max.z);
    corners[5] = float3(max.x, min.y, max.z);
    corners[6] = float3(min.x, max.y, max.z);
    corners[7] = float3(max.x, max.y, max.z);

    bool outside = true;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.x >= -clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.x <= clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y >= -clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y <= clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z >= -clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z <= clipPos.w) { outside = false; break; }
    }
    if (outside) return false;

    return true;
}

// ---------------------------------------------------------------------------
// Discrete LOD selection, shared by the main and shadow cull passes (and their wave variants) so
// every path resolves a level the same way. The choice is made once per proxy here; expansion and
// the vertex stage only ever see the index range it resolved to.
// ---------------------------------------------------------------------------

// Longest screen-space edge of the proxy's projected bounds, in pixels. Anything crossing the near
// plane returns a huge extent: bounds straddling w=0 have no meaningful projected size, and forcing
// full detail there is both cheap (it is close) and safe.
public float ComputeScreenExtent(float3 minPoint, float3 maxPoint, float4x4 wvp, float2 viewportSize)
{
    float2 ndcMin = float2(1e30f, 1e30f);
    float2 ndcMax = float2(-1e30f, -1e30f);
    for (uint i = 0; i < 8u; ++i)
    {
        const float3 corner = float3(
            (i & 1u) != 0u ? maxPoint.x : minPoint.x,
            (i & 2u) != 0u ? maxPoint.y : minPoint.y,
            (i & 4u) != 0u ? maxPoint.z : minPoint.z);
        const float4 clipPos = mul(wvp, float4(corner, 1.0));
        if (clipPos.w <= 1e-6f)
        {
            return 1e30f;
        }
        const float2 ndc = clipPos.xy / clipPos.w;
        ndcMin = min(ndcMin, ndc);
        ndcMax = max(ndcMax, ndc);
    }
    // NDC spans [-1,1], so half the extent maps onto the viewport dimension.
    const float2 pixelExtent = (ndcMax - ndcMin) * 0.5f * viewportSize;
    return max(pixelExtent.x, pixelExtent.y);
}

// Level 0 at or above baseThreshold pixels, halving the bar for each further level. A
// baseThreshold of 0 pins everything to level 0, which is the A/B baseline for measuring LOD.
public uint SelectLod(float screenExtent, uint lodCount, float baseThreshold)
{
    uint lod = 0u;
    float threshold = baseThreshold;
    while (lod + 1u < lodCount && screenExtent < threshold)
    {
        ++lod;
        threshold *= 0.5f;
    }
    return lod;
}
