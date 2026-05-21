import Common;
import Bindless;

bool IsAABBInFrustum(float3 min, float3 max, float4x4 wvp)
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
        if (clipPos.x >= -clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.x <= clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y >= -clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y <= clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z >= -clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    outside = true;
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z <= clipPos.w)
        {
            outside = false;
            break;
        }
    }
    if (outside) return false;

    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Bindless.GetGpuscene().custom_data_1)
    {
        return;
    }

    NodeProxy node = Bindless.GetGpuscene().Nodes[DTid.x];
    ModelData model = Bindless.GetGpuscene().Offsets[node.modelId];
    uint cascadeIdx = min(Bindless.GetGpuscene().custom_data_0, 3u);
    float4x4 lightMvp = mul(Bindless.GetGpuscene().Camera[0].SunCascadeViewProjection[cascadeIdx], node.worldTS);
    uint triangleCount = model.indexCount / 3;

    bool isCandidate = node.visible > 0;
    bool shouldDraw = isCandidate && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, lightMvp);

    if (isCandidate)
    {
        InterlockedAdd(Bindless.GetGpuscene().ShadowGPUDrivenStats[cascadeIdx].ProcessedCount, 1);
        InterlockedAdd(Bindless.GetGpuscene().ShadowGPUDrivenStats[cascadeIdx].TriangleCount, triangleCount);

        if (!shouldDraw)
        {
            InterlockedAdd(Bindless.GetGpuscene().ShadowGPUDrivenStats[cascadeIdx].CulledCount, 1);
            InterlockedAdd(Bindless.GetGpuscene().ShadowGPUDrivenStats[cascadeIdx].CulledTriangleCount, triangleCount);
        }
    }

    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].instanceCount = shouldDraw ? 1 : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].firstInstance = shouldDraw ? DTid.x : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].indexCount = shouldDraw ? model.indexCount : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].firstIndex = shouldDraw ? model.indexOffset : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].vertexOffset = shouldDraw ? model.reorderOffset : 0;
}
