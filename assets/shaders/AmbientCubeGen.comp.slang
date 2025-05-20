import Common;

[[vk::binding(0)]] RaytracingAccelerationStructure Scene;
[[vk::binding(1)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(2)]] StructuredBuffer<LightObject> Lights;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

struct PushConstant
{
    uint globalOffset;
    uint cascade;
};

[[vk::push_constant]] ConstantBuffer<PushConstant> pushConst;

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint gIdx = DTid.x + pushConst.globalOffset;
    
    // convert to local position
    uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;
    
    uint4 RandomSeed = InitRandomSeed(x + y, y + z, Camera.TotalFrames);
    float3 origin = float3(x, y, z) * (pushConst.cascade == 0 ? CUBE_UNIT : CUBE_UNIT_FAR) + (pushConst.cascade == 0 ? CUBE_OFFSET : CUBE_OFFSET_FAR);

    Cubes[gIdx].ExtInfo2 = 0;//Cubes[gIdx].ExtInfo2 + 1;
    Cubes[gIdx].Active = 0;
    Cubes[gIdx].ExtInfo3 = 0;//Cubes[gIdx].Active;
    Cubes[gIdx].ExtInfo1 = 0;
}