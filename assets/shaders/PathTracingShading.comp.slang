import Common;

[[vk::binding(0)]] RaytracingAccelerationStructure Scene;
[[vk::binding(1)]] StructuredBuffer<LightObject> Lights;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(10)]] RWTexture2D<float4> AccumulationImage;
[[vk::binding(11)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(12)]] RWTexture2D<uint2> VisibilityBuffer;
[[vk::binding(13)]] RWTexture2D<uint2> Visibility1Buffer;
[[vk::binding(14)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(15)]] RWTexture2D<float4> OutNormalBuffer;
[[vk::binding(16)]] RWTexture2D<uint> AdaptiveSampleBuffer;
[[vk::binding(17)]] RWTexture2D<float4> ShaderTimerBuffer;
[[vk::binding(18)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(19)]] StructuredBuffer<AmbientCube> FarCubes;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];


[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // compose with renderer and tracer
    FHardwareRayTracer tracer = { Vertices, Indices, Materials, Offsets, NodeProxies, Scene };
    FHardwareDirectIlluminator dIlluminator = { Camera, Scene };
    FHardwarePrimaryRayCaster rayCaster = { Camera, tracer };
    FPathTracingRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, FarCubes, HDRSHs};

    int2 ipos = int2(DTid.xy);
    int2 isize;
    AccumulationImage.GetDimensions(isize.x, isize.y);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // do a primary ray here
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    if (!rayCaster.TracePrimaryRay(ipos, isize, RandomSeed, hitVertex, hitNode, rayDir))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        AccumulationImage[ipos] = skyColor;
        OutMotionVector[ipos] = float4(0, 0, 0, 0);
        OutAlbedoBuffer[ipos] = skyColor;
        OutNormalBuffer[ipos] = float4(0, 1, 0, 1);
        VisibilityBuffer[ipos] = uint2(0, 0);
        return;
    }
    // MotionVector解析
    float2 motion = Common.CalculateMotionVector(Camera, hitNode, hitVertex);
    OutMotionVector[ipos] = float4(motion * isize, 0, 0);

    // GBuffer解析
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Common.FetchGBuffer(hitVertex, Materials[hitVertex.MaterialIndex], TextureArray, albedo, gbuffer);
    OutAlbedoBuffer[ipos] = albedo;
    OutNormalBuffer[ipos] = gbuffer;
    VisibilityBuffer[ipos] = uint2(hitNode.instanceId, 0);

    // 着色
    renderer.Render(tracer, dIlluminator, hitVertex, gbuffer, TextureArray, RandomSeed, illuminaceColor);

    // 合成
    float4 outColor = albedo * float4(illuminaceColor.rgb, 1);
    outColor.a = 1.0f;

    AccumulationImage[ipos] = outColor;
}