import Common;

[[vk::binding(2)]] StructuredBuffer<LightObject> Lights;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(4)]] StructuredBuffer<GPUVertex> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(10)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(11)]] RWStructuredBuffer<VoxelData> Voxels;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint gIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;
    
    // convert to local position
    uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

    FHardwareRayTracerV2 tracer;
    FGpuProbeGenerator probeGen;
    
    uint4 RandomSeed = InitRandomSeed(x + y, y + z, Camera.TotalFrames);
    float3 origin = float3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;

    probeGen.Render(tracer, origin, gIdx, RandomSeed);
}