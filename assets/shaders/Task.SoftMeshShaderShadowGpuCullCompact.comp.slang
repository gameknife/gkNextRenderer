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

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint indirectDrawCount = scene.custom_data_1;
    if (DTid.x >= indirectDrawCount || DTid.x >= MAX_NODES)
    {
        return;
    }

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* counters = (uint*)softMeshShader.Counters;

    NodeProxy node = scene.Nodes[DTid.x];
    ModelData model = scene.Offsets[node.modelId];
    uint triangleCount = model.indexCount / 3;
    bool isCandidate = node.visible > 0;
    uint activeCascadeMask = scene.custom_data_0;
    uint maxSceneTriangles = scene.custom_data_2;

    for (uint cascadeIdx = 0; cascadeIdx < 4; ++cascadeIdx)
    {
        if ((activeCascadeMask & (1u << cascadeIdx)) == 0u)
        {
            continue;
        }

        float4x4 lightMvp = mul(scene.Camera[0].SunCascadeViewProjection[cascadeIdx], node.worldTS);
        bool shouldDraw = isCandidate && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, lightMvp);

        if (isCandidate)
        {
            InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].ProcessedCount, 1);
            InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].TriangleCount, triangleCount);

            if (!shouldDraw)
            {
                InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledCount, 1);
                InterlockedAdd(scene.ShadowGPUDrivenStats[cascadeIdx].CulledTriangleCount, triangleCount);
            }
        }

        if (!shouldDraw || triangleCount == 0)
        {
            continue;
        }

        uint slot = cascadeIdx + 1;
        uint counterBase = slot * 2;
        uint primBase = counters[counterBase];
        for (;;)
        {
            if (primBase >= maxSceneTriangles || triangleCount > maxSceneTriangles - primBase)
            {
                break;
            }

            uint observed;
            InterlockedCompareExchange(
                counters[counterBase], primBase, primBase + triangleCount, observed);
            if (observed == primBase)
            {
                uint itemIdx;
                InterlockedAdd(counters[counterBase + 1], 1, itemIdx);

                SoftMeshShaderVisibleItem item;
                item.primBase = cascadeIdx * maxSceneTriangles + primBase;
                item.instanceIdx = DTid.x + 1;
                item.modelOffsetIdx = node.modelId;
                item.triCount = triangleCount;
                visibleItems[slot * MAX_NODES + itemIdx] = item;
                break;
            }
            primBase = observed;
        }
    }
}
