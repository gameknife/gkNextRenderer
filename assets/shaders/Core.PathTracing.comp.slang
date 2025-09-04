import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    // compose with renderer and tracer
    UniformBufferObject CameraDirect = Bindless.GetGpuscene().Camera[0];
    FVisibilityBufferRayCasterV2 rayCaster;
    FHardwareRayTracerV2 tracer;
    FHardwareDirectIlluminatorV2 dIlluminator;
    FPathTracingRendererV2 renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = CameraDirect.FastGather; // set true is hybrid tracing, delete it will be
    renderer.HitNormalOffset = 0.001f;

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
        
        Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(ipos, skyColor);
        Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(ipos, float4(0, 0, 0, 0));
        Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(ipos, float4(1, 1, 1, 1));
        Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, float4(0, 1, 0, 1));
        Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(ipos, 65535);

        END_SHADERCLOCK(ipos)
        return;
    }

    float4 clipPos = mul(CameraDirect.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    float2 motion = Common.CalculateMotionVectorV2(CameraDirect, hitNode, hitVertex, ipos);
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 reflectColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
    Common.FetchGBufferV2(hitVertex, material, hitNode, rayDir, albedo, gbuffer);

    Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(ipos, albedo);
    Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, gbuffer);
    Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(ipos, hitNode.instanceId);
    Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(ipos, float4(motion * isize, 0, 0));
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(ipos, ndcDepth);

    if (material.MaterialModel != MaterialDielectric)
    {
        hitVertex.Position = hitVertex.Position - rayDir * 0.03f;// - rayDir * rayLength * 0.005;
    }  

    // 着色
    int sampleMultiplier = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[ipos] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, hitVertex, albedo, gbuffer, sampleMultiplier, RandomSeed, illuminaceColor, reflectColor);

    // 合成
    float4 outColor = float4(illuminaceColor.rgb, 1);
    outColor.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(ipos, outColor);

    float4 outSpec = float4(reflectColor.rgb, 1);
    outSpec.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(ipos, outSpec);

    END_SHADERCLOCK(ipos)
}