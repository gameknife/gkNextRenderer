#include "common/Const_Func.hlsli"
#include "common/UniformBufferObject.hlsli"

// 资源绑定
[[vk::binding(0, 0)]] Texture2D<float4> GBuffer0Image;
[[vk::binding(1, 0)]] Texture2D<float4> GBuffer1Image;
[[vk::binding(2, 0)]] Texture2D<float4> GBuffer2Image;
[[vk::binding(3, 0)]] RWTexture2D<float4> OutImage;
[[vk::binding(4, 0)]] cbuffer UniformBufferObjectStruct { UniformBufferObject Camera; };
[[vk::binding(7, 0)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 0)]] Texture2D<float> ShadowMapSampler;
[[vk::binding(8, 0)]] SamplerState ShadowMapSamplerState;

// 全局纹理数组
[[vk::binding(0, 1)]] Texture2D<float4> TextureSamplers[];
[[vk::binding(0, 1)]] SamplerState TextureSamplersState;

// 获取阴影值
float getShadow(float3 worldPos, float3 jit, float3 normal, int2 ipos) {
    // 计算光源空间坐标
    float4 posInLightMap = mul(Camera.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));

    // 将光源空间坐标转换到NDC空间 [-1,1] 再转到 [0,1] 的纹理空间
    float3 projCoords = posInLightMap.xyz / posInLightMap.w;
    projCoords = projCoords * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    float currentDepth = projCoords.z;

    float bias = 0.0005;

    float cosTheta = max(dot(normal, normalize(Camera.SunDirection.xyz)), 0.0);
    bias = lerp(0.0001, 0.00005, cosTheta);

    float closestDepth = ShadowMapSampler.Sample(ShadowMapSamplerState, projCoords.xy).x;
    float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

    return shadow;
}

[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    int2 ipos = int2(DTid.xy);

    float4 albedo = GBuffer0Image[ipos];
    float4 normalraw = GBuffer1Image[ipos];
    float4 pbr_param = GBuffer2Image[ipos];

    float3 normal = normalize(normalraw.rgb);

    const float t = 0.5*(normal.y + 1.0);
    float4 iblLight = float4(Camera.SkyIntensity, Camera.SkyIntensity, Camera.SkyIntensity, Camera.SkyIntensity);

    const float3 lightVector = Camera.SunDirection.xyz;
    const float4 d = Camera.SunColor * max(dot(lightVector, normal), 0.0) * M_1_PI;

    float shadow = 0.0f;
    if(Camera.HasSun)
    {
        float3 jit = float3(0, 0, 0);
        shadow += getShadow(pbr_param.xyz, jit, normal, ipos);
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