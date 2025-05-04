#ifndef SAMPLEIBL_H_

#ifdef __cplusplus
float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness)
{
    float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0f - rotate);
    return float4(rayColor, 1.0);
}
#else
float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness)
{
     if(roughness > 0.6f)
     {
          float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
          return float4(rayColor * 1.0f, 1.0);
     }
     float3 d = normalize(direction);
     float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);

     // Sample with explicit LOD based on roughness
     return min(float4(10,10,10,1), TextureSamplers[NonUniformResourceIndex(skyIdx)].SampleLevel(t, roughness * 10.0f));
}
#endif

#define SAMPLEIBL_H_
#endif