import Shader.Bindless;

struct PushConsts
{
    // Unified push-constant header used by zero-bind compute pipelines.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint InputIndex;
    uint OutputIndex;
    int OutputOffsetX;
    int OutputOffsetY;
    uint OutputWidth;
    uint OutputHeight;
    float Strength;
    float LumaSigma;
    float FireflySigma;
    uint StepWidth;
    uint ApplyFireflyClamp;
    float Exposure;
    uint FinalPass;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

float Luminance(float3 color)
{
    return dot(max(color, float3(0.0f)), float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ClampLuminance(float3 color, float ceiling)
{
    const float luma = Luminance(color);
    return luma > ceiling ? color * (ceiling / max(luma, 1e-5f)) : color;
}

float3 CompressHdr(float3 color, float exposure)
{
    color = max(color, float3(0.0f)) * exposure;
    return color / (1.0f + Luminance(color));
}

float3 DecompressHdr(float3 color, float exposure)
{
    return max(color, float3(0.0f)) /
        (max(1.0f - Luminance(color), 6.1e-5f) * exposure);
}

float AtrousKernelWeight(int offset)
{
    const int absoluteOffset = abs(offset);
    return absoluteOffset == 0 ? 6.0f : (absoluteOffset == 1 ? 4.0f : 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= pushConsts.OutputWidth || dispatchThreadId.y >= pushConsts.OutputHeight)
    {
        return;
    }

    const int2 pixel = int2(dispatchThreadId.xy);
    const int2 imageMax = int2(pushConsts.OutputWidth, pushConsts.OutputHeight) - int2(1, 1);
    let source = Bindless.GetStorageTexture<float4>(pushConsts.InputIndex);
    let destination = Bindless.GetStorageTexture<float4>(pushConsts.OutputIndex);

    const float exposure = max(pushConsts.Exposure, 1.0e-6f);
    const bool firstPass = pushConsts.ApplyFireflyClamp != 0;
    float3 center = source[pixel].rgb;
    if (firstPass)
    {
        center = CompressHdr(center, exposure);
    }
    float neighborMean = 0.0f;
    float neighborSquareMean = 0.0f;
    uint neighborCount = 0;

    // Local variance makes the color gate tolerant of Monte-Carlo noise while the stable
    // albedo/normal guides below retain material and geometry edges.
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            float3 sampleColor =
                source[clamp(pixel + int2(x, y), int2(0, 0), imageMax)].rgb;
            if (firstPass)
            {
                sampleColor = CompressHdr(sampleColor, exposure);
            }
            const float sampleLuma = Luminance(sampleColor);
            neighborMean += sampleLuma;
            neighborSquareMean += sampleLuma * sampleLuma;
            ++neighborCount;
        }
    }

    const float reciprocalCount = 1.0f / max(float(neighborCount), 1.0f);
    neighborMean *= reciprocalCount;
    neighborSquareMean *= reciprocalCount;
    const float neighborDeviation =
        sqrt(max(0.0f, neighborSquareMean - neighborMean * neighborMean));
    if (pushConsts.ApplyFireflyClamp != 0)
    {
        // The center does not contribute to this threshold, so an isolated bright sample
        // cannot inflate its own acceptance range.
        const float fireflyCeiling =
            neighborMean + max(0.025f, pushConsts.FireflySigma * neighborDeviation);
        center = ClampLuminance(center, fireflyCeiling);
    }

    const float centerLuma = Luminance(center);
    float3 accumulated = float3(0.0f);
    float accumulatedWeight = 0.0f;
    const float sigma = clamp(max(pushConsts.LumaSigma, neighborDeviation * 2.0f), 0.01f, 0.5f);
    const float inverseSigmaSquared = 1.0f / (sigma * sigma);
    const int stepWidth = int(max(pushConsts.StepWidth, 1u));

    const int normalIndex = Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_NORMAL);
    const int albedoIndex = Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ALBEDO);
    let normalTexture = Bindless.GetStorageTexture<float4>(normalIndex);
    let albedoTexture = Bindless.GetStorageTexture<float4>(albedoIndex);
    const uint2 guideDimensions = Bindless.GetStorageTextureDimensions<float4>(normalIndex);
    const float2 outputDimensions = float2(pushConsts.OutputWidth, pushConsts.OutputHeight);
    const int2 guideMax = int2(guideDimensions) - int2(1, 1);
    const int2 centerGuidePixel = clamp(
        int2((float2(pixel) + float2(0.5f)) * float2(guideDimensions) / outputDimensions),
        int2(0, 0),
        guideMax);
    const float3 centerNormal = normalTexture[centerGuidePixel].xyz;
    const float3 centerAlbedo = albedoTexture[centerGuidePixel].rgb;
    const float centerNormalLengthSquared = dot(centerNormal, centerNormal);

    // B3-spline a-trous kernel. Successive passes increase StepWidth (1, 2, 4, ...),
    // growing the denoising support without downsampling the upscaler output.
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 samplePixel =
                clamp(pixel + int2(x, y) * stepWidth, int2(0, 0), imageMax);
            float3 sampleColor = x == 0 && y == 0 ? center : source[samplePixel].rgb;
            if (firstPass && (x != 0 || y != 0))
            {
                sampleColor = CompressHdr(sampleColor, exposure);
            }
            const float sampleLuma = Luminance(sampleColor);
            const float lumaDelta = sampleLuma - centerLuma;
            const float chromaDelta = length(sampleColor - center) * 0.35f;
            const float rangeDelta = max(abs(lumaDelta), chromaDelta);
            const float spatialWeight = AtrousKernelWeight(x) * AtrousKernelWeight(y);
            const float rangeWeight = exp2(-0.7213475f * rangeDelta * rangeDelta * inverseSigmaSquared);
            const int2 sampleGuidePixel = clamp(
                int2((float2(samplePixel) + float2(0.5f)) * float2(guideDimensions) / outputDimensions),
                int2(0, 0),
                guideMax);
            const float3 sampleNormal = normalTexture[sampleGuidePixel].xyz;
            const float sampleNormalLengthSquared = dot(sampleNormal, sampleNormal);
            float normalWeight = 1.0f;
            if (centerNormalLengthSquared > 0.01f || sampleNormalLengthSquared > 0.01f)
            {
                normalWeight = centerNormalLengthSquared > 0.01f && sampleNormalLengthSquared > 0.01f
                    ? pow(saturate(dot(normalize(centerNormal), normalize(sampleNormal))), 32.0f)
                    : 0.0f;
            }
            const float3 albedoDelta = albedoTexture[sampleGuidePixel].rgb - centerAlbedo;
            const float albedoWeight = exp2(-8.0f * dot(albedoDelta, albedoDelta));
            const float weight = spatialWeight * rangeWeight * normalWeight * albedoWeight;
            accumulated += sampleColor * weight;
            accumulatedWeight += weight;
        }
    }

    const float3 filtered = accumulated / max(accumulatedWeight, 1e-5f);
    float3 result = lerp(center, filtered, saturate(pushConsts.Strength));
    if (pushConsts.FinalPass != 0)
    {
        result = DecompressHdr(result, exposure);
    }
    destination[pixel + int2(pushConsts.OutputOffsetX, pushConsts.OutputOffsetY)] =
        float4(result, 1.0f);
}
