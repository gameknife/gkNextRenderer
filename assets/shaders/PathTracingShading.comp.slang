import Common;

[[vk::binding(0, 0)]] Sampler2D TextureArray[];

[[vk::binding(0, 1)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(1, 1)]] RaytracingAccelerationStructure Scene;

[[vk::binding(0, 2)]] RWTexture2D<float4> FinalImage;
[[vk::binding(1, 2)]] RWTexture2D<float4> OutImage;
[[vk::binding(2, 2)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(3, 2)]] RWTexture2D<uint> ObjectId0;
[[vk::binding(4, 2)]] RWTexture2D<uint> ObjectId1;
[[vk::binding(5, 2)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(6, 2)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(7, 2)]] RWTexture2D<float4> OutNormalBuffer;
[[vk::binding(8, 2)]] RWTexture2D<float4> ShaderTimerBuffer;

[[vk::binding(0, 3)]] StructuredBuffer<float> Vertices;
[[vk::binding(1, 3)]] StructuredBuffer<uint> Indices;
[[vk::binding(2, 3)]] StructuredBuffer<Material> Materials;
[[vk::binding(3, 3)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(4, 3)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(5, 3)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(6, 3)]] RWStructuredBuffer<VoxelData> Voxels;
[[vk::binding(7, 3)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 3)]] StructuredBuffer<LightObject> Lights;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint64_t clock = Camera.ShowHeatmap ? clockARB() : 0;

    // compose with renderer and tracer
    FHardwareRayTracer tracer = { Vertices, Indices, Materials, Offsets, NodeProxies, Scene };
    FHardwareDirectIlluminator dIlluminator = { Camera, Scene };
    FHardwarePrimaryRayCaster rayCaster = { Camera, tracer };
    FPathTracingRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, Voxels, HDRSHs};

    int2 ipos = int2(DTid.xy);
    int2 isize;
    OutImage.GetDimensions(isize.x, isize.y);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // do a primary ray here
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    if (!rayCaster.TracePrimaryRay(ipos, isize, RandomSeed, hitVertex, hitNode, rayDir))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        OutImage[ipos] = skyColor;
        OutMotionVector[ipos] = float4(0, 0, 0, 0);
        OutAlbedoBuffer[ipos] = float4(1, 1, 1, 1);
        OutNormalBuffer[ipos] = float4(0, 1, 0, 1);
        ObjectId0[ipos] = 0;
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
    ObjectId0[ipos] = hitNode.instanceId;

    // 着色
    renderer.Render(tracer, dIlluminator, hitVertex, gbuffer, TextureArray, RandomSeed, illuminaceColor);

    // 合成
    float4 outColor = float4(illuminaceColor.rgb, 1);
    outColor.a = 1.0f;

    OutImage[ipos] = outColor;

    if (Camera.ShowHeatmap)
    {
        const uint64_t deltaTime = clockARB() - clock;
        const float heatmapScale = 1000000.0f * Camera.HeatmapScale * Camera.HeatmapScale;
        const float deltaTimeScaled = clamp(float(deltaTime) / heatmapScale, 0.0f, 1.0f);
        const float4 shaderHeatColor = float4(heatmap(deltaTimeScaled), 1.0);
        ShaderTimerBuffer[ipos] = shaderHeatColor;
    }
}