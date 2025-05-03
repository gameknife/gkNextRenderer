implementing Common;

namespace Common
{
    // 获取阴影值
    public float getShadow(float3 worldPos, float3 jit, float3 normal, int2 ipos, in UniformBufferObject ubo, in Sampler2D shadowmap) {
        // 计算光源空间坐标
        float4 posInLightMap = mul(ubo.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));
    
        // 将光源空间坐标转换到NDC空间 [-1,1] 再转到 [0,1] 的纹理空间
        float3 projCoords = posInLightMap.xyz / posInLightMap.w;
        projCoords = projCoords * 0.5 + 0.5;
        projCoords.y = 1.0 - projCoords.y;
    
        float currentDepth = projCoords.z;
    
        float bias = 0.0005;
    
        float cosTheta = max(dot(normal, normalize(ubo.SunDirection.xyz)), 0.0);
        bias = lerp(0.0001, 0.00005, cosTheta);
    
        float closestDepth = shadowmap.Sample(projCoords.xy).x;
        float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;
    
        return shadow;
    }
}