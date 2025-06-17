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

    // cull all
    DrawCommands[DTid.x].instanceCount = shouldDraw ? 1 : 0;
    DrawCommands[DTid.x].firstInstance = DTid.x;
    DrawCommands[DTid.x].indexCount = model.indexCount;
    DrawCommands[DTid.x].firstIndex = model.indexOffset;
    DrawCommands[DTid.x].vertexOffset = model.vertexOffset;
}