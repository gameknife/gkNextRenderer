import Common;
#include "common/ShaderClock.slang"

[[vk::binding(0, 2)]] Sampler2D TextureArray[];
[[vk::binding(0, 0)]] RWTexture2D<uint> StorageTextureArray_temp[];

[[vk::binding(0, 1)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(1, 1)]] RaytracingAccelerationStructure Scene;

[[vk::binding(0, 3)]] StructuredBuffer<GPUVertex> Vertices;
[[vk::binding(12, 3)]] StructuredBuffer<uint> Indices;
[[vk::binding(2, 3)]] StructuredBuffer<Material> Materials;
[[vk::binding(3, 3)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(4, 3)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(5, 3)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(6, 3)]] RWStructuredBuffer<VoxelData> Voxels;
[[vk::binding(7, 3)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 3)]] StructuredBuffer<LightObject> Lights;


[[vk::push_constant]]
ConstantBuffer<GPUScene> gpuScene;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //START_SHADERCLOCK()
    // compose with renderer and tracer
    FVisibilityBufferRayCaster rayCaster = { Camera, Vertices, NodeProxies, Offsets, StorageTextureArray_temp[Bindless.RT_MINIGBUFFER], Indices };
    FHardwareRayTracerV2 tracer = { Vertices, Indices, Materials, Offsets, gpuScene.Nodes, Scene };
    FHardwareDirectIlluminator dIlluminator = { Camera, Scene };
    //FHardwarePrimaryRayCaster rayCaster = { Camera, tracer };
    FPathTracingRenderer renderer = { Camera, Vertices, Indices, Materials, Offsets, NodeProxies, Cubes, Voxels, HDRSHs};

    int2 ipos = int2(DTid.xy);
    uint2 isize = Bindless.GetStorageTextureDimensions(Bindless.RT_SINGLE_DIFFUSE);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // do a primary ray here
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    float rayLength = 0.0;
    float2 pixelOffset = float2(0, 0);
    if (!rayCaster.TracePrimaryRay(ipos, isize, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        Bindless.SetStorageTexture(Bindless.RT_SINGLE_DIFFUSE, ipos, skyColor);
        Bindless.SetStorageTexture(Bindless.RT_MOTIONVECTOR, ipos, float4(0, 0, 0, 0));
        Bindless.SetStorageTexture(Bindless.RT_ALBEDO, ipos, float4(1, 1, 1, 1));
        Bindless.SetStorageTexture(Bindless.RT_NORMAL, ipos, float4(0, 1, 0, 1));
        Bindless.SetStorageTexture(Bindless.RT_OBJEDCTID_0, ipos, 65535);

        //END_SHADERCLOCK(ipos)
        return;
    }
    // MotionVector解析
    float2 motion = Common.CalculateMotionVector(Camera, hitNode, hitVertex);
    Bindless.SetStorageTexture(Bindless.RT_MOTIONVECTOR, ipos, float4(motion * isize, 0, 0));

    // GBuffer解析
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 reflectColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Material material = Materials[hitVertex.MaterialIndex];
    Common.FetchGBuffer(hitVertex, Materials[hitVertex.MaterialIndex], hitNode, rayDir, TextureArray, albedo, gbuffer);
    Bindless.SetStorageTexture(Bindless.RT_ALBEDO, ipos, albedo);
    Bindless.SetStorageTexture(Bindless.RT_NORMAL, ipos, gbuffer);
    Bindless.SetStorageTexture(Bindless.RT_OBJEDCTID_0, ipos, hitNode.instanceId);


    // write to NDC depth
    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    Bindless.SetStorageTexture(Bindless.RT_PREV_DEPTHBUFFER, ipos, ndcDepth);

    if (material.MaterialModel != MaterialDielectric)
    {
        hitVertex.Position = hitVertex.Position - rayDir * rayLength * 0.0005;
    }  

    // 着色
    renderer.Render(tracer, dIlluminator, hitVertex, albedo, gbuffer, TextureArray, RandomSeed, illuminaceColor, reflectColor);

    // 合成
    float4 outColor = float4(illuminaceColor.rgb, 1);
    outColor.a = length(pixelOffset);
    Bindless.SetStorageTexture(Bindless.RT_SINGLE_DIFFUSE, ipos, outColor);

    float4 outSpec = float4(reflectColor.rgb, 1);
    outSpec.a = length(pixelOffset);
    Bindless.SetStorageTexture(Bindless.RT_SINGLE_SPECULAR, ipos, outSpec);

    //END_SHADERCLOCK(ipos)
}