#ifndef GK_NEXT_GTAO_UPSAMPLE_SLANG
#define GK_NEXT_GTAO_UPSAMPLE_SLANG

// Half-resolution GTAO -> full-resolution sky occlusion.
//
// Shared by Core Shading (which consumes AO directly and applies it to the ambient term) and by
// Process.GTAOCompose (AO debug view only). Keeping one copy is what guarantees the "user strength
// is applied exactly once" rule: GTAOSkyOcclusion is the only function allowed to raise visibility
// to the strength power.
//
// Requires: Shader.Bindless and a gpuScene push constant carrying UniformBufferObject.

bool GTAOIsSurfaceDepth(float depth)
{
    return depth >= 0.0f && depth <= 1.0f;
}

float3 GTAOReconstructViewPosition(UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 view = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
    return view.xyz / safeW;
}

// A 3x3 joint bilateral footprint is still cheap and finds valid geometry when a temporally
// jittered half-resolution representative landed on sky.
float GTAOUpsample(UniformBufferObject camera, int2 pixel, uint2 fullSize, float centerDepth,
                   float3 centerNormal)
{
    const uint2 aoSize = Bindless.GetViewStorageTextureDimensions<float>(Bindless.RT_GTAO);
    const int2 aoMax = int2(aoSize) - int2(1, 1);
    const int2 fullMax = int2(fullSize) - int2(1, 1);
    const float3 centerPosition =
        GTAOReconstructViewPosition(camera, pixel, fullSize, centerDepth);
    const float2 halfPosition = (float2(pixel) + 0.5f) * 0.5f - 0.5f;
    const int2 halfBase = int2(floor(halfPosition));
    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 aoPixel = clamp(halfBase + int2(x, y), int2(0, 0), aoMax);
            const int2 representative = min(aoPixel * 2 + int2(1, 1), fullMax);
            const float sampleDepth =
                Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[representative];
            const float3 sampleNormal =
                Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[representative].xyz;
            if (!GTAOIsSurfaceDepth(sampleDepth) || dot(sampleNormal, sampleNormal) < 0.25f)
            {
                continue;
            }

            const float3 samplePosition =
                GTAOReconstructViewPosition(camera, representative, fullSize, sampleDepth);
            const float normalWeight = pow(saturate(dot(centerNormal, normalize(sampleNormal))), 32.0f);
            const float depthScale = max(0.02f, abs(centerPosition.z) * 0.02f);
            const float depthWeight = exp2(-abs(samplePosition.z - centerPosition.z) / depthScale);
            const float2 spatialDelta = float2(aoPixel) - halfPosition;
            const float spatialWeight = exp2(-dot(spatialDelta, spatialDelta));
            const float weight = normalWeight * depthWeight * spatialWeight;
            weightedAO += Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO)[aoPixel] * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1e-5f)
    {
        return Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO)[clamp(pixel / 2, int2(0, 0), aoMax)];
    }
    return weightedAO / weightSum;
}

// Occlusion multiplier for the sky diffuse term. Gamma-style master strength: monotone and never
// hard-clipping to pure black (unlike a linear 1 - strength*(1-vis), which zeroes everything below
// 1-1/strength), so GTAO keeps a visible gradient in deep corners instead of a dead-black patch.
float GTAOSkyOcclusion(UniformBufferObject camera, int2 pixel, uint2 fullSize, float depth,
                       float3 worldNormal)
{
    if (!camera.GTAOEnable || !GTAOIsSurfaceDepth(depth) || dot(worldNormal, worldNormal) < 0.25f)
    {
        return 1.0f;
    }
    const float ao = GTAOUpsample(camera, pixel, fullSize, depth, normalize(worldNormal));
    return pow(saturate(ao), max(camera.GTAOStrength, 0.0f));
}

#endif
