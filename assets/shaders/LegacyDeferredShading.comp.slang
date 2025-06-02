#include "common/PreProcessor.slang"
import Common;

// 资源绑定
[[vk::binding(0, 0)]] RWTexture2D<float4> GBuffer0Image;
[[vk::binding(1, 0)]] RWTexture2D<float4> GBuffer1Image;
[[vk::binding(2, 0)]] RWTexture2D<float4> GBuffer2Image;
[[vk::binding(3, 0)]] RWTexture2D<float4> OutImage;
[[vk::binding(4, 0)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(5, 0)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(6, 0)]] StructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(7, 0)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 0)]] Sampler2D ShadowMapSampler;

// 全局纹理数组
[[vk::binding(0, 1)]] Sampler2D TextureSamplers[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    int2 ipos = int2(DTid.xy);

    float4 albedo = GBuffer0Image[ipos];
    float4 normalraw = GBuffer1Image[ipos];
    float4 wolrdPosition = GBuffer2Image[ipos];

    float3 normal = normalize(normalraw.rgb);
    float roughness = normalraw.a;

    float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(wolrdPosition.xyz, normal, Cubes, FarCubes);
    float4 outColor = albedo * radiance;

    if(Camera.HDR)
    {
        OutImage[ipos] = float4(LinearToST2084UE(outColor.rgb * Camera.PaperWhiteNit / 230.0), 1.0);
    }
    else
    {
        OutImage[ipos] = float4(Uncharted2_Tonemapping(outColor.rgb * Camera.PaperWhiteNit / 20000.0), 1.0);
    }
}