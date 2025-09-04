import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //START_SHADERCLOCK()
    // compose with renderer and tracer
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    FVisibilityBufferRayCasterV2 rayCaster;
    FHiVoxelDDARayTracerV2 voxelTracer;
    FSoftwareRayTracerV2 tracer;
    FSoftwareDirectIlluminatorV2 dIlluminator;
    FPathTracingRendererV2 renderer;
    renderer.ExitAfterFirst = true;
    renderer.HitNormalOffset = 0.1f;
    
    int2 ipos = int2(DTid.xy);
    int2 size = Bindless.GetStorageTextureDimensions(Bindless.RT_MINIGBUFFER);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    float rayLength = 0.0;
    float2 pixelOffset = float2(0, 0);
    if (!rayCaster.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBLV2(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(ipos, skyColor);
        Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(ipos, float4(0, 0, 0, 0));
        Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(ipos, float4(1, 1, 1, 1));
        Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, float4(0, 1, 0, 1));
        Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(ipos, 65535);

        //END_SHADERCLOCK(ipos)
        return;
    }
    // MotionVector解析
    float2 motion = Common.CalculateMotionVectorV2(Camera, hitNode, hitVertex, ipos);
    Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(ipos, float4(motion * size, 0, 0));

    // GBuffer解析
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 reflectColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Common.FetchGBufferV2(hitVertex, Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex), hitNode, rayDir, albedo, gbuffer);
    Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(ipos, albedo);
    Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, gbuffer);

    int sampleMultiplier = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[ipos] > 0 ? 2 : 1;
    renderer.Render(tracer, dIlluminator, hitVertex, albedo, gbuffer, sampleMultiplier, RandomSeed, illuminaceColor, reflectColor);

    // 合成
    float4 outColor = float4(illuminaceColor.rgb, 1);
    outColor.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(ipos, outColor);

    float4 outSpec = float4(reflectColor.rgb, 1);
    outSpec.a = length(pixelOffset);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(ipos, outSpec);

    Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(ipos, hitNode.instanceId);

    // write to NDC depth
    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(ipos, ndcDepth);

    //END_SHADERCLOCK(ipos)
}