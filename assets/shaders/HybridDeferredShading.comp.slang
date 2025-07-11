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

[[vk::binding(16)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(17)]] StructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(18)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(19)]] StructuredBuffer<LightObject> Lights;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];


[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // compose with renderer and tracer
    FVisibilityBufferRayCaster rayCaster = { Camera, Vertices, NodeProxies, MiniGBuffer };
    FHardwareRayTracer tracer = { Vertices, Indices, Materials, Offsets, NodeProxies, Scene };
    FHardwareDirectIlluminator dIlluminator = { Camera, Scene };
    FHybridRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, FarCubes, HDRSHs };
    FPathTracingRenderer PTRenderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, FarCubes, HDRSHs };

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
        OutMotionVector[ipos] = float4(0, 0, 0, 0);
        OutAlbedoBuffer[ipos] = skyColor;
        OutNormalBuffer[ipos] = float4(0, 1, 0, 1);
        return;
    }

    // MotionVector解析
    float2 motion = Common.CalculateMotionVector(Camera, hitNode, hitVertex);
    OutMotionVector[ipos] = float4(motion * size, 0, 0);

    // GBuffer解析
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Material material = Materials[hitVertex.MaterialIndex];
    Common.FetchGBuffer(hitVertex, material, TextureArray, albedo, gbuffer);
    OutAlbedoBuffer[ipos] = albedo;
    OutNormalBuffer[ipos] = gbuffer;

    if (material.MaterialModel == MaterialDielectric)
    {
        PTRenderer.Render(tracer, dIlluminator, hitVertex, gbuffer, TextureArray, RandomSeed, illuminaceColor);
    }
    else
    {
        renderer.Render(tracer, dIlluminator, hitVertex, gbuffer, TextureArray, RandomSeed, illuminaceColor);
    }
    
    // 合成
    float4 outColor = albedo * float4(illuminaceColor.rgb, 1);
    outColor.a = 1.0f;

    OutImage[ipos] = outColor;
}