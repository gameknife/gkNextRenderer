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
    uint FinalPass;
    // Render-pixel projection jitter of the frame that produced the normal/albedo guides.
    float JitterX;
    float JitterY;
    // Pre-exposure for the Reinhard round trip below, matching the temporal upscaler's.
    float CompressionScale;
    // Not every renderer contract writes RT_ALBEDO; SoftwareModernNoAmbient does not.
    uint UseAlbedoGuide;
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

// The pre-exposure keeps this round trip on the compression curve's knee. Without it the inverse's
// 1 / (1 - L) gain turns the additive firefly ceiling below into a multiplicative jump on any
// pixel near paper white, which is where the surviving outliers actually sit.
float3 CompressHdr(float3 color)
{
    color = max(color, float3(0.0f)) * pushConsts.CompressionScale;
    return color / (1.0f + Luminance(color));
}

float3 DecompressHdr(float3 color)
{
    const float3 decompressed = max(color, float3(0.0f)) /
        max(1.0f - Luminance(color), 6.1e-5f);
    return decompressed / max(pushConsts.CompressionScale, 1.0e-6f);
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

    const bool firstPass = pushConsts.ApplyFireflyClamp != 0;
    float3 center = source[pixel].rgb;
    if (firstPass)
    {
        center = CompressHdr(center);
    }
    float neighborMean = 0.0f;
    float neighborSquareMean = 0.0f;
    float neighborMaxLuma = -1e20f;
    float neighborSecondMaxLuma = -1e20f;
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
                sampleColor = CompressHdr(sampleColor);
            }
            const float sampleLuma = Luminance(sampleColor);
            neighborSecondMaxLuma = max(neighborSecondMaxLuma, min(neighborMaxLuma, sampleLuma));
            neighborMaxLuma = max(neighborMaxLuma, sampleLuma);
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
        const float statisticalCeiling =
            neighborMean + max(0.025f, pushConsts.FireflySigma * neighborDeviation);
        // On a silhouette the neighbour deviation is geometric contrast rather than noise, and
        // this ceiling goes slack exactly where the temporal pass leaves the most residual noise.
        // The second-largest neighbour is immune to that: across an edge it is still a legitimate
        // foreground luminance, and in a flat region it tracks the local level.
        const float orderCeiling = neighborSecondMaxLuma * 1.25f + 0.025f;
        center = ClampLuminance(center, min(statisticalCeiling, orderCeiling));
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
    const float2 guideScale = float2(guideDimensions) / outputDimensions;
    // The guides are rendered jittered at render resolution while this pass runs on the resolved
    // display image, so their edges sit up to half a render pixel away from the colour edges.
    // Shifting the lookup by the frame's jitter puts the guide edge back onto the colour edge;
    // without it the normal/albedo terms below reject the wrong pixels along every contour.
    const float2 guideJitter = float2(pushConsts.JitterX, pushConsts.JitterY);
    const int2 centerGuidePixel = clamp(
        int2(floor((float2(pixel) + float2(0.5f)) * guideScale + guideJitter)),
        int2(0, 0),
        guideMax);
    const bool useAlbedoGuide = pushConsts.UseAlbedoGuide != 0u;
    const float3 centerNormal = normalTexture[centerGuidePixel].xyz;
    const float3 centerAlbedo = useAlbedoGuide
        ? albedoTexture[centerGuidePixel].rgb
        : float3(0.0f);
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
                sampleColor = CompressHdr(sampleColor);
            }
            const float sampleLuma = Luminance(sampleColor);
            const float lumaDelta = sampleLuma - centerLuma;
            const float chromaDelta = length(sampleColor - center) * 0.35f;
            const float rangeDelta = max(abs(lumaDelta), chromaDelta);
            const float spatialWeight = AtrousKernelWeight(x) * AtrousKernelWeight(y);
            const float rangeWeight = exp2(-0.7213475f * rangeDelta * rangeDelta * inverseSigmaSquared);
            const int2 sampleGuidePixel = clamp(
                int2(floor((float2(samplePixel) + float2(0.5f)) * guideScale + guideJitter)),
                int2(0, 0),
                guideMax);
            const float3 sampleNormal = normalTexture[sampleGuidePixel].xyz;
            const float sampleNormalLengthSquared = dot(sampleNormal, sampleNormal);
            float normalWeight = 1.0f;
            if (centerNormalLengthSquared > 0.01f || sampleNormalLengthSquared > 0.01f)
            {
                normalWeight = centerNormalLengthSquared > 0.01f && sampleNormalLengthSquared > 0.01f
                    ? pow(saturate(dot(normalize(centerNormal), normalize(sampleNormal))), 12.0f)
                    : 0.0f;
            }
            const float3 albedoDelta = useAlbedoGuide
                ? albedoTexture[sampleGuidePixel].rgb - centerAlbedo
                : float3(0.0f);
            const float albedoWeight = exp2(-8.0f * dot(albedoDelta, albedoDelta));
            // A hard geometric cutoff collapses the kernel onto its center tap along every
            // silhouette, which is where the residual fireflies actually survive. Keep a floor so
            // a little energy still crosses the edge. The range weight above stays the sharp
            // edge-preserving term, so this does not smear geometry on its own.
            const float guideWeight = max(normalWeight * albedoWeight, 0.05f);
            const float weight = spatialWeight * rangeWeight * guideWeight;
            accumulated += sampleColor * weight;
            accumulatedWeight += weight;
        }
    }

    const float3 filtered = accumulated / max(accumulatedWeight, 1e-5f);
    float3 result = lerp(center, filtered, saturate(pushConsts.Strength));
    if (pushConsts.FinalPass != 0)
    {
        result = DecompressHdr(result);
    }
    destination[pixel + int2(pushConsts.OutputOffsetX, pushConsts.OutputOffsetY)] =
        float4(result, 1.0f);
}
