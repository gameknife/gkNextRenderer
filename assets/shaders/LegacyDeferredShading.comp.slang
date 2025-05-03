import Common;

#include "common/Const_Func.slang"

// 资源绑定
[[vk::binding(0, 0)]] RWTexture2D<float4> GBuffer0Image;
[[vk::binding(1, 0)]] RWTexture2D<float4> GBuffer1Image;
[[vk::binding(2, 0)]] RWTexture2D<float4> GBuffer2Image;
[[vk::binding(3, 0)]] RWTexture2D<float4> OutImage;
[[vk::binding(4, 0)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(7, 0)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 0)]] Sampler2D ShadowMapSampler;

// 全局纹理数组
[[vk::binding(0, 1)]] Sampler2D TextureSamplers[];
#include "common/SampleIBL.hlsli"

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    int2 ipos = int2(DTid.xy);

    float4 albedo = GBuffer0Image[ipos];
    float4 normalraw = GBuffer1Image[ipos];
    float4 pbr_param = GBuffer2Image[ipos];

    float3 normal = normalize(normalraw.rgb);

    const float t = 0.5*(normal.y + 1.0);
    float4 iblLight = Camera.HasSky ? SampleIBL(Camera.SkyIdx, normal, Camera.SkyRotation, 1) * Camera.SkyIntensity : float4(0.f, 0.f, 0.f, 0.f);

    const float3 lightVector = Camera.SunDirection.xyz;
    const float4 d = Camera.SunColor * max(dot(lightVector, normal), 0.0) * M_1_PI;

    float shadow = 0.0f;
    if(Camera.HasSun)
    {
        float3 jit = float3(0, 0, 0);
        shadow += Common.getShadow(pbr_param.xyz, jit, normal, ipos, Camera, ShadowMapSampler);
    }

    if(Camera.DebugDraw_Lighting)
    {
        albedo = float4(0.5, 0.5, 0.5, 1);
    }

    float4 outColor = albedo * d * shadow + iblLight * albedo;

    if(Camera.HDR)
    {
        OutImage[ipos] = float4(LinearToST2084UE(outColor.rgb * Camera.PaperWhiteNit / 230.0), 1.0);
    }
    else
    {
        OutImage[ipos] = float4(Uncharted2_Tonemapping(outColor.rgb * Camera.PaperWhiteNit / 20000.0), 1.0);
    }
}