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

// 点视锥裁剪函数
bool IsPointInFrustum(float3 worldPos, ConstantBuffer<UniformBufferObject> Camera)
{                                                                            
    float4 clipPos = mul(Camera.ViewProjection, float4(worldPos, 1.0));
    float3 ndc = clipPos.xyz / clipPos.w;
    return all(abs(ndc) <= 1.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float4x4 world = NodeProxies[DTid.x].worldTS;
    float3 worldPos = mul(world, float4(0, 0, 0, 1)).xyz;
    bool visible = IsPointInFrustum(worldPos, Camera);
    // cull all
    DrawCommands[DTid.x].instanceCount = visible ? 1 : 0;
}