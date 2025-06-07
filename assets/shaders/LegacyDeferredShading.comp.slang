#include "common/PreProcessor.slang"
import Common;

[[vk::binding(0, 0)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(1, 0)]] RWTexture2D<float4> OutImage;
[[vk::binding(2, 0)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(3)]] StructuredBuffer<float> Vertices;
[[vk::binding(4)]] StructuredBuffer<uint> Indices;
[[vk::binding(5)]] StructuredBuffer<Material> Materials;
[[vk::binding(6)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(7)]] StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(8, 0)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(9, 0)]] StructuredBuffer<VoxelData> Voxels;
[[vk::binding(10, 0)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(11, 0)]] Sampler2D ShadowMapSampler;


// 全局纹理数组
[[vk::binding(0, 1)]] Sampler2D TextureArray[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // compose with renderer and tracer
    FVisibilityBufferRayCaster rayCaster = { Camera, Vertices, NodeProxies, MiniGBuffer };
 
    int2 ipos = int2(DTid.xy);
    int2 size;
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    MiniGBuffer.GetDimensions(size.x, size.y);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    if (!rayCaster.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        OutImage[ipos] = skyColor;
        return;
    }

    Material hitMaterial = Materials[hitVertex.MaterialIndex];

    float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels);
    OutImage[ipos] = hitMaterial.Diffuse * radiance;
}