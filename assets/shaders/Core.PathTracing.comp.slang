import Common;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    UniformBufferObject CameraDirect = Bindless.GetGpuscene().Camera[0];
    //START_SHADERCLOCK()
    // compose with renderer and tracer
    FVisibilityBufferRayCasterV2 rayCaster = {};
    FHardwareRayTracerV2 tracer = {};
    FHardwareDirectIlluminatorV2 dIlluminator = {tracer};
    FPathTracingRendererV2 renderer = {};

    int2 ipos = int2(DTid.xy);
    uint2 isize = Bindless.GetStorageTextureDimensions(Bindless.RT_SINGLE_DIFFUSE);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, CameraDirect.TotalFrames);

    // do a primary ray here
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    float rayLength = 0.0;
    float2 pixelOffset = float2(0, 0);
    if (!rayCaster.TracePrimaryRay(ipos, isize, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset))
    {
        float4 skyColor = CameraDirect.HasSky ? Common.SampleIBLV2(CameraDirect.SkyIdx, rayDir, CameraDirect.SkyRotation, 0) * CameraDirect.SkyIntensity : float4(0, 0, 0, 0);
        
        Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[ipos] = skyColor;
        Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[ipos] = float4(0, 0, 0, 0);
        Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO)[ipos] = float4(1, 1, 1, 1);
        Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL)[ipos] = float4(0, 1, 0, 1);
        Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0)[ipos] = 65535;

        //END_SHADERCLOCK(ipos)
        return;
    }
    // MotionVector解析
    float2 motion = Common.CalculateMotionVectorV2(CameraDirect, hitNode, hitVertex);
    Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[ipos] = float4(motion * isize, 0, 0);

    // GBuffer解析
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 reflectColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
    Common.FetchGBufferV2(hitVertex, material, hitNode, rayDir, albedo, gbuffer);
    Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO)[ipos] = albedo;
    Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL)[ipos] = gbuffer;
    Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0)[ipos] = hitNode.instanceId;

    // write to NDC depth
    float4 clipPos = mul(CameraDirect.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[ipos] = ndcDepth;

    if (material.MaterialModel != MaterialDielectric)
    {
        hitVertex.Position = hitVertex.Position - rayDir * rayLength * 0.0005;
    }  

    // 着色
    renderer.Render(tracer, dIlluminator, hitVertex, albedo, gbuffer, RandomSeed, illuminaceColor, reflectColor);

    // 合成
    float4 outColor = float4(illuminaceColor.rgb, 1);
    outColor.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[ipos] = outColor;

    float4 outSpec = float4(reflectColor.rgb, 1);
    outSpec.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[ipos] = outSpec;

    //END_SHADERCLOCK(ipos)
}