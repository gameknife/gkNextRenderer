import Common;

[[vk::binding(2)]] StructuredBuffer<LightObject> Lights;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(10)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(11)]] RWStructuredBuffer<VoxelData> Voxels;

[[vk::binding(12)]] StructuredBuffer<AmbientCube> RoCubes;
[[vk::binding(13)]] StructuredBuffer<VoxelData> RoVoxels;

// 全局纹理数组
[[vk::binding(0, 1)]] Sampler2D TextureSamplers[];

struct PushConstant
{
    uint globalOffset;
    uint cascade;
};

[[vk::push_constant]] ConstantBuffer<PushConstant> pushConst;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FVoxelDDARayTracer tracer = { RoCubes, RoVoxels };
    FGpuProbeGenerator probeGen = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, Voxels, HDRSHs, Lights };

    uint gIdx = DTid.x + pushConst.globalOffset;
    
    // convert to local position
    uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;
    
    uint4 RandomSeed = InitRandomSeed(x + y, y + z, Camera.TotalFrames);
    float3 origin = float3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;

    probeGen.RenderAmbientCubeOnly(tracer, origin, gIdx, RandomSeed, TextureSamplers);
}