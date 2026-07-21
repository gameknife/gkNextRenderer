// NativeTemporal's explicit-descriptor variant of the temporal upscaler post-filter.
// It intentionally matches Process.FsrPostFilter: neighbor-only firefly rejection on
// pass zero followed by normal/albedo-guided B3-spline a-trous filtering.

[[vk::binding(9, 0)]] RWTexture2D<float4> outputColor;
[[vk::binding(10, 0)]] RWTexture2D<float4> filterPing;
[[vk::binding(11, 0)]] RWTexture2D<float4> filterPong;
[[vk::binding(12, 0)]] RWTexture2D<float4> albedoTexture;
[[vk::binding(13, 0)]] RWTexture2D<float4> normalTexture;

struct PushConstants
{
    uint2 renderSize;
    uint2 outputSize;
    float2 jitter;
    float2 previousJitter;
    uint reset;
    float sharpness;
    float historyWeight;
    float padding;
    uint filterPassIndex;
    uint filterPassCount;
    uint filterStepWidth;
    uint applyFireflyClamp;
    float filterStrength;
    float filterLumaSigma;
    float fireflySigma;
    float filterPadding;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> constants;

float Luminance(float3 color)
{
    return dot(max(color, float3(0.0)), float3(0.2126, 0.7152, 0.0722));
}

float3 ClampLuminance(float3 color, float ceiling)
{
    const float luma = Luminance(color);
    return luma > ceiling ? color * (ceiling / max(luma, 1e-5)) : color;
}

float AtrousKernelWeight(int offset)
{
    const int absoluteOffset = abs(offset);
    return absoluteOffset == 0 ? 6.0 : (absoluteOffset == 1 ? 4.0 : 1.0);
}

float4 LoadSource(int2 pixel)
{
    return (constants.filterPassIndex & 1u) == 0u
        ? filterPing.Load(pixel)
        : filterPong.Load(pixel);
}

void StoreDestination(int2 pixel, float4 value)
{
    if (constants.filterPassIndex + 1u == constants.filterPassCount)
    {
        outputColor[pixel] = value;
    }
    else if ((constants.filterPassIndex & 1u) == 0u)
    {
        filterPong[pixel] = value;
    }
    else
    {
        filterPing[pixel] = value;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= constants.outputSize))
    {
        return;
    }

    const int2 pixel = int2(dispatchThreadId.xy);
    const int2 imageMax = int2(constants.outputSize) - int2(1);
    const float4 original = LoadSource(pixel);
    float3 center = original.rgb;
    float neighborMean = 0.0;
    float neighborSquareMean = 0.0;
    uint neighborCount = 0;

    // Excluding the center prevents an isolated firefly from raising its own threshold.
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            const float sampleLuma = Luminance(
                LoadSource(clamp(pixel + int2(x, y), int2(0), imageMax)).rgb);
            neighborMean += sampleLuma;
            neighborSquareMean += sampleLuma * sampleLuma;
            ++neighborCount;
        }
    }

    const float reciprocalCount = 1.0 / max(float(neighborCount), 1.0);
    neighborMean *= reciprocalCount;
    neighborSquareMean *= reciprocalCount;
    const float neighborDeviation =
        sqrt(max(0.0, neighborSquareMean - neighborMean * neighborMean));
    if (constants.applyFireflyClamp != 0u)
    {
        const float fireflyCeiling =
            neighborMean + max(0.025, constants.fireflySigma * neighborDeviation);
        center = ClampLuminance(center, fireflyCeiling);
    }

    const float centerLuma = Luminance(center);
    float3 accumulated = float3(0.0);
    float accumulatedWeight = 0.0;
    const float sigma = clamp(
        max(constants.filterLumaSigma, neighborDeviation * 2.0), 0.01, 0.5);
    const float inverseSigmaSquared = 1.0 / (sigma * sigma);
    const int stepWidth = int(max(constants.filterStepWidth, 1u));

    const int2 guideMax = int2(constants.renderSize) - int2(1);
    const int2 centerGuidePixel = clamp(
        int2((float2(pixel) + float2(0.5)) *
             float2(constants.renderSize) / float2(constants.outputSize)),
        int2(0), guideMax);
    const float3 centerNormal = normalTexture.Load(centerGuidePixel).xyz;
    const float3 centerAlbedo = albedoTexture.Load(centerGuidePixel).rgb;
    const float centerNormalLengthSquared = dot(centerNormal, centerNormal);

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 samplePixel =
                clamp(pixel + int2(x, y) * stepWidth, int2(0), imageMax);
            const float3 sampleColor = x == 0 && y == 0
                ? center
                : LoadSource(samplePixel).rgb;
            const float sampleLuma = Luminance(sampleColor);
            const float lumaDelta = sampleLuma - centerLuma;
            const float chromaDelta = length(sampleColor - center) * 0.35;
            const float rangeDelta = max(abs(lumaDelta), chromaDelta);
            const float spatialWeight = AtrousKernelWeight(x) * AtrousKernelWeight(y);
            const float rangeWeight = exp2(
                -0.7213475 * rangeDelta * rangeDelta * inverseSigmaSquared);

            const int2 sampleGuidePixel = clamp(
                int2((float2(samplePixel) + float2(0.5)) *
                     float2(constants.renderSize) / float2(constants.outputSize)),
                int2(0), guideMax);
            const float3 sampleNormal = normalTexture.Load(sampleGuidePixel).xyz;
            const float sampleNormalLengthSquared = dot(sampleNormal, sampleNormal);
            float normalWeight = 1.0;
            if (centerNormalLengthSquared > 0.01 || sampleNormalLengthSquared > 0.01)
            {
                normalWeight = centerNormalLengthSquared > 0.01 &&
                               sampleNormalLengthSquared > 0.01
                    ? pow(saturate(dot(normalize(centerNormal), normalize(sampleNormal))), 32.0)
                    : 0.0;
            }
            const float3 albedoDelta =
                albedoTexture.Load(sampleGuidePixel).rgb - centerAlbedo;
            const float albedoWeight = exp2(-8.0 * dot(albedoDelta, albedoDelta));
            const float weight =
                spatialWeight * rangeWeight * normalWeight * albedoWeight;
            accumulated += sampleColor * weight;
            accumulatedWeight += weight;
        }
    }

    const float3 filtered = accumulated / max(accumulatedWeight, 1e-5);
    const float3 result = lerp(center, filtered, saturate(constants.filterStrength));
    StoreDestination(pixel, float4(result, original.a));
}
