#include "common/PreProcessor.slang"
import Common;

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

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    int2 ipos = int2(DTid.xy);

    int2 size;
    GBuffer0Image.GetDimensions(size.x, size.y);
    float2 uv = float2(ipos) / float2(size) * 2.0 - 1.0;

    float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
    float3 ray_dir = normalize(dir.xyz);


    float4 albedo = GBuffer0Image[ipos];
    float4 normalraw = GBuffer1Image[ipos];
    float4 pbr_param = GBuffer2Image[ipos];

    float3 normal = normalize(normalraw.rgb);

    float3 reflectDir = reflect(ray_dir, normal);
    float4 iblLight = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, reflectDir, Camera.SkyRotation, 1, HDRSHs, TextureSamplers) * Camera.SkyIntensity : float4(0.f, 0.f, 0.f, 0.f);

    const float3 lightVector = Camera.SunDirection.xyz;
    const float4 d = Camera.SunColor * max(dot(lightVector, normal), 0.0) * M_1_PI;

    float shadow = 0.0f;
    if(Camera.HasSun)
    {
        float3 jit = float3(0, 0, 0);
        shadow += Common.getShadow(pbr_param.xyz, jit, normal, Camera, ShadowMapSampler);
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