import Common;

[[vk::binding(0)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(1)]] RWTexture2D<float4> OutImage;
[[vk::binding(2)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(10)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(11)]] StructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(12)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(13)]] Sampler2D ShadowMapSampler;

[[vk::binding(14)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(15)]] RWTexture2D<float4> OutNormalBuffer;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // compose with renderer and tracer
    FVoxelDDARayTracer voxelTracer = { Cubes, FarCubes };
    FVoxelRayCaster rayCasterVoxel = { Camera, voxelTracer };

    int2 ipos = int2(DTid.xy);
    int2 size;
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    MiniGBuffer.GetDimensions(size.x, size.y);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;

    if (rayCasterVoxel.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode))
    {
        float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, FarCubes);
        OutImage[ipos] = Materials[hitVertex.MaterialIndex].Diffuse * radiance;
    }
    else
    {
        OutImage[ipos] = float4(10,10,10,0);
    }
}