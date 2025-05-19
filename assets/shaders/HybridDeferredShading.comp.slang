import Common;

[[vk::binding(0)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(2)]] RWTexture2D<float4> OutImage;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(9)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(10)]] RaytracingAccelerationStructure Scene;

[[vk::binding(13)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(14)]] RWTexture2D<float4> OutNormalBuffer;

[[vk::binding(16)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(17)]] RWStructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(18)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(19)]] StructuredBuffer<LightObject> Lights;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];


[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // compose with renderer and tracer
    FHardwareRayTracer tracer = { Vertices, Indices, Materials, Offsets, NodeProxies, Scene };
    FHardwareDirectIlluminator dIlluminator = { Camera, Scene };
    FHybridRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, FarCubes, HDRSHs};
    
    int2 ipos = int2(DTid.xy);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    int2 size;
    MiniGBuffer.GetDimensions(size.x, size.y);

    uint2 vBuffer = MiniGBuffer[ipos].rg;
    float2 uv = float2(ipos) / float2(size) * 2.0 - 1.0;

    float4 albedo = float4(0, 0, 0, 1);
    float4 indirectColor = float4(0, 0, 0, 1);
    float3 normal = float3(0, 0, 0);
    float2 motion = float2(0, 0);
    renderer.Render(tracer, dIlluminator, uv, vBuffer, TextureArray, RandomSeed, albedo, normal, motion, indirectColor);
    OutMotionVector[ipos] = float4(motion * float2(size),0,0);

    float4 outColor = albedo * float4(indirectColor.rgb, 1);
    outColor.a = 1.0f;

    OutAlbedoBuffer[ipos] = albedo;
    OutNormalBuffer[ipos] = float4(normal, 1.0);
    OutImage[ipos] = outColor;
}