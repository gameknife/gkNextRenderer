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
[[vk::binding(10)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(11)]] RWStructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(12)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(13)]] Sampler2D ShadowMapSampler;

[[vk::binding(14)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(15)]] RWTexture2D<float4> OutNormalBuffer;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    int2 size;
    MiniGBuffer.GetDimensions(size.x, size.y);

    uint2 vBuffer = MiniGBuffer[ipos].rg;
    float2 uv = float2(ipos) / float2(size) * 2.0 - 1.0;

    float4 albedo = float4(0, 0, 0, 1);
    float4 irrandiaceColor = float4(0, 0, 0, 1);
    float3 normal = float3(0, 0, 0);
    float2 motion = float2(0, 0);

    // compose with renderer and tracer
    FSoftwareRayTracer tracer = { Camera, MiniGBuffer, Vertices, Materials, NodeProxies, Cubes, FarCubes };
    FSoftwareDirectIlluminator dIlluminator = { Camera, ShadowMapSampler };
    FHybridRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, FarCubes, HDRSHs };

    // execute the renderer
    renderer.Render(tracer, dIlluminator, uv, vBuffer, TextureArray, RandomSeed, albedo, normal, motion, irrandiaceColor);

    // compose
    float4 outColor = albedo * irrandiaceColor;

    // write to output buffers
    OutMotionVector[ipos] = float4(motion * float2(size), 0, 0);
    OutAlbedoBuffer[ipos] = albedo;
    OutNormalBuffer[ipos] = float4(normal, 1.0);
    OutImage[ipos] = outColor;
}