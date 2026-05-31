import Common;
import Bindless;

bool IsOccluded(float3 min, float3 max, float4x4 wvp)
{
    // 计算AABB的8个角点
    float3 corners[8];
    corners[0] = float3(min.x, min.y, min.z);
    corners[1] = float3(max.x, min.y, min.z);
    corners[2] = float3(min.x, max.y, min.z);
    corners[3] = float3(max.x, max.y, min.z);
    corners[4] = float3(min.x, min.y, max.z);
    corners[5] = float3(max.x, min.y, max.z);
    corners[6] = float3(min.x, max.y, max.z);
    corners[7] = float3(max.x, max.y, max.z);
    
    uint2 screenSize = Bindless.GetStorageTextureDimensions<float>(Bindless.RT_PREV_DEPTHBUFFER);
    
    int occludedCount = 0;
    
    // 检查每个角点
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        float3 ndc = clipPos.xyz / clipPos.w;
        
        // 检查是否在视锥体内
        if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
            return false; // 在视锥体外的点不参与遮挡判断
        }
        
        float2 screenUV = ndc.xy * 0.5 + 0.5;
        //screenUV.y = 1.0 - screenUV.y; // 翻转Y轴（如果需要）
        
        int2 pixelCoord = int2(screenUV * float2(screenSize));
        
        // 边界检查
        if (pixelCoord.x < 0 || pixelCoord.x >= screenSize.x ||
            pixelCoord.y < 0 || pixelCoord.y >= screenSize.y) {
            return false;
        }
        
        float previousDepth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixelCoord];
        float currentDepth = ndc.z- 0.00001;
        
        // 如果当前点被遮挡
        if (previousDepth > currentDepth) {
            return false;
        }
    }
    return true;
}

bool IsOccludedVolume(float3 min, float3 max, float4x4 wvp)
{
    const int GRID_SIZE = 6;
    const float GRID_SIZE_MINUS_ONE = GRID_SIZE - 1;

    uint2 screenSize = Bindless.GetStorageTextureDimensions<float>(Bindless.RT_PREV_DEPTHBUFFER);

    // 在AABB内均匀分布4x4x4的检测点
    for (int x = 0; x < GRID_SIZE; x++)
    {
        for (int y = 0; y < GRID_SIZE; y++)
        {
            for (int z = 0; z < GRID_SIZE; z++)
            {
                // 计算当前网格点的世界坐标
                float3 gridPoint = min + float3(
                    (max.x - min.x) * (float(x) / GRID_SIZE_MINUS_ONE),
                    (max.y - min.y) * (float(y) / GRID_SIZE_MINUS_ONE),
                    (max.z - min.z) * (float(z) / GRID_SIZE_MINUS_ONE)
                );

                // 变换到剪裁空间
                float4 clipPos = mul(wvp, float4(gridPoint, 1.0));

                // 透视除法得到NDC坐标
                // if (clipPos.w <= 0.0) continue; // 跳过在相机后面的点

                float3 ndc = clipPos.xyz / clipPos.w;

                // 检查是否在视锥体内
                if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
                    return false; // 跳过视锥体外的点
                }

                // 转换到屏幕坐标
                float2 screenUV = ndc.xy * 0.5 + 0.5;
                int2 pixelCoord = int2(screenUV * float2(screenSize));

                // 边界检查
                if (pixelCoord.x < 0 || pixelCoord.x >= screenSize.x ||
                    pixelCoord.y < 0 || pixelCoord.y >= screenSize.y) {
                    return false;
                }

                // 深度测试
                float previousDepth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixelCoord];
                float currentDepth = ndc.z - 0.0001; // 添加小的偏移避免Z-fighting

                // 如果当前点被遮挡
                if (previousDepth > currentDepth) {
                    return false;
                }
            }
        }
    }

    return true;
}



bool IsPointInFrustum(float3 pos, float4x4 wvp)
{
    float4 clipPos = mul(wvp, float4(pos, 1.0));
    float3 ndc = clipPos.xyz / clipPos.w;
    return all(abs(ndc) <= 1.0);
}

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
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.x >= -clipPos.w) {
            outside = false;
            break;
        }
    }
    if (outside) return false;
    
    outside = true;
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.x <= clipPos.w) {
            outside = false;
            break;
        }
    }
    if (outside) return false;
    
    outside = true;
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y >= -clipPos.w) {
            outside = false;
            break;
        }
    }
    if (outside) return false;
    
    outside = true;
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.y <= clipPos.w) {
            outside = false;
            break;
        }
    }
    if (outside) return false;
    
    outside = true;
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z >= -clipPos.w) {
            outside = false;
            break;
        }
    }
    if (outside) return false;
    
    outside = true;
    for (int i = 0; i < 8; i++) {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        if (clipPos.z <= clipPos.w) {
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
    float4x4 mvp = mul(Bindless.GetGpuscene().Camera[0].PrevViewProjectionUnJit, node.worldTS);

    bool shouldDraw = (node.visible > 0) && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);

    if (shouldDraw)
    {
        InterlockedAdd(Bindless.GetGpuscene().GPUDrivenStats[0].ProcessedCount, 1);
        InterlockedAdd(Bindless.GetGpuscene().GPUDrivenStats[0].TriangleCount, model.indexCount / 3);

        if (IsOccludedVolume(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp))
        {
            InterlockedAdd(Bindless.GetGpuscene().GPUDrivenStats[0].CulledCount, 1);
            InterlockedAdd(Bindless.GetGpuscene().GPUDrivenStats[0].CulledTriangleCount, model.indexCount / 3);
            shouldDraw = false;
        }
    }

    // cull all
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].instanceCount = shouldDraw ? 1 : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].firstInstance = shouldDraw ? DTid.x : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].indexCount = shouldDraw ? model.indexCount : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].firstIndex = shouldDraw ? model.indexOffset : 0;
    Bindless.GetGpuscene().IndirectDrawCommands[DTid.x].vertexOffset = shouldDraw ? model.reorderOffset : 0;
}
