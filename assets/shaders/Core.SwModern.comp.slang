import Common;
import Bindless;
#include "common/PreProcessor.slang"
#include "common/ShaderClock.slang"

void OutputColor(int2 ipos, float3 Total)
{
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
    if (Camera.HDR)
    {
        Total = Total / 2000;
        Total = GT_Tonemapping(Total);
        Total = Total * 2000;
        float3 st2084 = LinearToST2084UE(Total * Camera.PaperWhiteNit / 230.0);
        Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED).Store(ipos, float4(st2084, 1.0));
    }
    else
    {
        Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED).Store(ipos, float4(GT_Tonemapping(Total * Camera.PaperWhiteNit / 40000.0), 0.0));
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid: SV_DispatchThreadID) {
    //START_SHADERCLOCK()
    // compose with renderer and tracer
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    FVisibilityBufferRayCasterV2 rayCaster = {};
 
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

    Material hitMaterial = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
    float2 motion = Common.CalculateMotionVectorV2(Camera, hitNode, hitVertex, ipos);
    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Common.FetchGBufferV2(hitVertex, hitMaterial, hitNode, rayDir, albedo, gbuffer);

    const float dotValue = dot(rayDir, hitVertex.Normal);
    const float3 outwardNormal = dotValue > 0 ? -hitVertex.Normal : hitVertex.Normal;
    const float cosine = dotValue > 0 ? hitMaterial.RefractionIndex * dotValue : -dotValue;
    const float reflectProb = Schlick(cosine, hitMaterial.RefractionIndex) * pow((1.0 - gbuffer.a), 4.0);
    const float metalProb = hitMaterial.Metalness;

    float3 tracenext = reflect(rayDir, outwardNormal);

    float4 radiance = float4(0, 0, 0, 1);
    const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples);
    const float jitScale = Camera.FastGather ? 0 : 1;

    float3 binormal = normalize(cross(outwardNormal, hitVertex.Tangent.xyz));

    for (int i = 0; i < jitcount; i++)
    {
        float2 offset2d = RandomFloat2(RandomSeed) * 2.0 - 1.0;
        float3 jitoffset = hitVertex.Tangent.xyz * offset2d.x + binormal * offset2d.y;
        float4 indirect = interpolateAmbientCubesV2<FullAmbientCubeSampler>(hitVertex.Position + hitVertex.Normal * 0.05f + jitoffset * CUBE_UNIT * jitScale, hitVertex.Normal);
        float lum = luminance(indirect.rgb);
        radiance += lum < 0.1 ? float4(5,5,5,1) : indirect;
    }
    radiance /= float(jitcount);

    float4 diffuse = albedo * radiance;
    float4 specular = Camera.HasSky ? Common.SampleIBLV2(Camera.SkyIdx, tracenext, Camera.SkyRotation, gbuffer.a) * Camera.SkyIntensity * radiance.a : float4(0, 0, 0, 0);

    float4 specColor = lerp(float4(1,1,1,1), albedo, metalProb);

    float reflectFactor = max(metalProb, reflectProb);

    float4 combined = diffuse * (1.0 - reflectFactor) + specColor * specular * reflectFactor;
    //OutputColor(ipos, combined.rgb);
    Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(ipos, hitNode.instanceId);

    // write to NDC depth
    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(ipos, ndcDepth);

    Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(ipos, float4(motion * size, 0, 0));
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(ipos, radiance * (1.0 - reflectFactor));
    Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(ipos, albedo);
    Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, gbuffer);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(ipos, specColor * specular * reflectFactor);

    //END_SHADERCLOCK(ipos)
}