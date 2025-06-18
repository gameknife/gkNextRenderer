import Common;

[[vk::binding(0, 0)]] RWStructuredBuffer<VkDrawIndexedIndirectCommand> DrawCommands;
[[vk::binding(1, 0)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(0, 1)]] StructuredBuffer<float> Vertices;
[[vk::binding(1, 1)]] StructuredBuffer<uint> Indices;
[[vk::binding(2, 1)]] StructuredBuffer<Material> Materials;
[[vk::binding(3, 1)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(4, 1)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(5, 1)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(6, 1)]] RWStructuredBuffer<VoxelData> Voxels;
[[vk::binding(7, 1)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 1)]] StructuredBuffer<LightObject> Lights;

[[vk::binding(0, 2)]] RWTexture2D<float4> FinalImage;
[[vk::binding(1, 2)]] RWTexture2D<float4> OutImage;
[[vk::binding(2, 2)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(3, 2)]] RWTexture2D<uint> ObjectId0;
[[vk::binding(4, 2)]] RWTexture2D<uint> ObjectId1;
[[vk::binding(5, 2)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(6, 2)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(7, 2)]] RWTexture2D<float4> OutNormalBuffer;
[[vk::binding(10, 2)]] RWTexture2D<float> OutDepthBuffer;

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
    
    uint2 screenSize;
    OutDepthBuffer.GetDimensions(screenSize.x, screenSize.y);
    
    int occludedCount = 0;
    
    // 检查每个角点
    for (int i = 0; i < 8; i++)
    {
        float4 clipPos = mul(wvp, float4(corners[i], 1.0));
        float3 ndc = clipPos.xyz / clipPos.w;
        
        // 检查是否在视锥体内
        if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
            continue; // 在视锥体外的点不参与遮挡判断
        }
        
        float2 screenUV = ndc.xy * 0.5 + 0.5;
        screenUV.y = 1.0 - screenUV.y; // 翻转Y轴（如果需要）
        
        int2 pixelCoord = int2(screenUV * float2(screenSize));
        
        // 边界检查
        if (pixelCoord.x < 0 || pixelCoord.x >= screenSize.x || 
            pixelCoord.y < 0 || pixelCoord.y >= screenSize.y) {
            continue;
        }
        
        float previousDepth = OutDepthBuffer[pixelCoord];
        float currentDepth = ndc.z;
        
        // 如果当前点被遮挡
        if (previousDepth < currentDepth) {
            occludedCount++;
        }
    }
    
    // 只有当所有在视锥体内的角点都被遮挡时，才认为整个AABB被遮挡
    // 这里需要至少有一些角点在视锥体内，并且这些角点都被遮挡
    return occludedCount >= 4; // 可以调整这个阈值
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
    NodeProxy node = NodeProxies[DTid.x];
    ModelData model = Offsets[node.modelId];
    float4x4 mvp = mul(Camera.ViewProjection, node.worldTS);

    bool shouldDraw = (node.visible > 0) && IsAABBInFrustum(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp);

    if (shouldDraw)
    {
        if (IsOccluded(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp))
        {
            shouldDraw = false;
        }
    }

    // cull all
    DrawCommands[DTid.x].instanceCount = shouldDraw ? 1 : 0;
    DrawCommands[DTid.x].firstInstance = DTid.x;
    DrawCommands[DTid.x].indexCount = model.indexCount;
    DrawCommands[DTid.x].firstIndex = model.indexOffset;
    DrawCommands[DTid.x].vertexOffset = model.vertexOffset;
}