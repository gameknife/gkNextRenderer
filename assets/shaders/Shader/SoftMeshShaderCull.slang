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
                float currentDepth = ndc.z - 0.0001;
                if (previousDepth > currentDepth)
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
