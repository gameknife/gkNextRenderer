// Cross-platform temporal upscaling accumulation. This pass reconstructs the jittered
// low-resolution input, dilates foreground motion, reprojects display-resolution history,
// rejects depth disocclusions, clips in YCoCg, and maintains luminance moments + lock state.

[[vk::binding(0, 0)]] RWTexture2D<float4> currentColor;
[[vk::binding(1, 0)]] RWTexture2D<float2> motionVectors;
[[vk::binding(2, 0)]] Texture2D<float> currentDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> historyColorRead;
[[vk::binding(4, 0)]] RWTexture2D<float> historyDepthRead;
[[vk::binding(5, 0)]] RWTexture2D<float2> historyMomentsRead;
[[vk::binding(6, 0)]] RWTexture2D<float4> historyColorWrite;
[[vk::binding(7, 0)]] RWTexture2D<float> historyDepthWrite;
[[vk::binding(8, 0)]] RWTexture2D<float2> historyMomentsWrite;
[[vk::binding(9, 0)]] RWTexture2D<float4> outputColor;

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
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> constants;

int2 ClampRenderPixel(int2 pixel)
{
    return clamp(pixel, int2(0), int2(constants.renderSize) - 1);
}

int2 ClampOutputPixel(int2 pixel)
{
    return clamp(pixel, int2(0), int2(constants.outputSize) - 1);
}

float4 LoadCurrent(int2 pixel)
{
    return currentColor.Load(ClampRenderPixel(pixel));
}

float LoadDepth(int2 pixel)
{
    return currentDepth.Load(int3(ClampRenderPixel(pixel), 0));
}

float2 LoadMotion(int2 pixel)
{
    const float2 motion = motionVectors.Load(ClampRenderPixel(pixel));
    const bool valid = !any(isnan(motion)) &&
        all(abs(motion) < float2(constants.renderSize) * 2.0);
    return valid ? motion : 0.0;
}

float CatmullWeight(float distance)
{
    distance = abs(distance);
    if (distance <= 1.0)
    {
        return 1.5 * distance * distance * distance -
               2.5 * distance * distance + 1.0;
    }
    if (distance < 2.0)
    {
        return -0.5 * distance * distance * distance +
               2.5 * distance * distance - 4.0 * distance + 2.0;
    }
    return 0.0;
}

float4 SampleCurrentCatmullRom(float2 position)
{
    const int2 base = int2(floor(position));
    float4 result = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        const float weightY = CatmullWeight(position.y - float(base.y + y));
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            const float weight = weightY * CatmullWeight(position.x - float(base.x + x));
            result += LoadCurrent(base + int2(x, y)) * weight;
            weightSum += weight;
        }
    }
    return result / max(weightSum, 1e-5);
}

float4 SampleHistoryCatmullRom(float2 uv)
{
    const float2 position = uv * float2(constants.outputSize) - 0.5;
    const int2 base = int2(floor(position));
    float4 result = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        const float weightY = CatmullWeight(position.y - float(base.y + y));
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            const float weight = weightY * CatmullWeight(position.x - float(base.x + x));
            result += historyColorRead.Load(ClampOutputPixel(base + int2(x, y))) * weight;
            weightSum += weight;
        }
    }
    return result / max(weightSum, 1e-5);
}

float2 SampleMomentsBilinear(float2 uv)
{
    const float2 position = uv * float2(constants.outputSize) - 0.5;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const float2 top = lerp(
        historyMomentsRead.Load(ClampOutputPixel(base)),
        historyMomentsRead.Load(ClampOutputPixel(base + int2(1, 0))), fraction.x);
    const float2 bottom = lerp(
        historyMomentsRead.Load(ClampOutputPixel(base + int2(0, 1))),
        historyMomentsRead.Load(ClampOutputPixel(base + int2(1, 1))), fraction.x);
    return lerp(top, bottom, fraction.y);
}

float SampleHistoryDepth(float2 uv)
{
    return historyDepthRead.Load(ClampOutputPixel(
        int2(uv * float2(constants.outputSize))));
}

float3 RgbToYCoCg(float3 color)
{
    return float3(
        dot(color, float3(0.25, 0.5, 0.25)),
        color.r - color.b,
        color.g - 0.5 * (color.r + color.b));
}

float3 YCoCgToRgb(float3 color)
{
    const float temporary = color.x - color.z * 0.5;
    return float3(temporary + color.y * 0.5, color.x + color.z * 0.5,
                  temporary - color.y * 0.5);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= constants.outputSize))
    {
        return;
    }

    const float2 outputUv = (float2(pixel) + 0.5) / float2(constants.outputSize);
    const float2 unjitteredRenderPosition = outputUv * float2(constants.renderSize) - 0.5;
    const float2 currentPosition = unjitteredRenderPosition + constants.jitter;
    const float4 reconstructedCurrent = SampleCurrentCatmullRom(currentPosition);
    const int2 centerPixel = ClampRenderPixel(int2(round(currentPosition)));

    // Dilate the motion vector from the closest 3x3 depth sample. This keeps foreground
    // silhouettes attached to their velocity instead of pulling stale background history in.
    int2 motionPixel = centerPixel;
    float depth = LoadDepth(centerPixel);
    float localDepthMinimum = depth;
    float localDepthMaximum = depth;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 candidatePixel = ClampRenderPixel(centerPixel + int2(x, y));
            const float candidateDepth = LoadDepth(candidatePixel);
            localDepthMinimum = min(localDepthMinimum, candidateDepth);
            localDepthMaximum = max(localDepthMaximum, candidateDepth);
            if (candidateDepth < depth)
            {
                depth = candidateDepth;
                motionPixel = candidatePixel;
            }
        }
    }

    const float2 motion = LoadMotion(motionPixel);
    const float2 previousUv = outputUv + motion / float2(constants.renderSize);
    const bool historyOnScreen = all(previousUv > 0.0) && all(previousUv < 1.0);
    const float previousDepth = historyOnScreen ? SampleHistoryDepth(previousUv) : depth;
    const float depthThreshold = max(
        0.00035 + abs(depth) * 0.0005,
        (localDepthMaximum - localDepthMinimum) * 1.1);
    const bool disoccluded = !historyOnScreen || abs(previousDepth - depth) > depthThreshold;

    // Current-frame spatial statistics. YCoCg prevents a bright channel from expanding the
    // entire RGB clip box and retains much more chroma detail than a plain min/max clamp.
    float3 neighborhoodMinimum = float3(1e20);
    float3 neighborhoodMaximum = float3(-1e20);
    float3 neighborhoodMean = 0.0;
    float3 neighborhoodSquared = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float3 sampleColor = RgbToYCoCg(LoadCurrent(centerPixel + int2(x, y)).rgb);
            neighborhoodMinimum = min(neighborhoodMinimum, sampleColor);
            neighborhoodMaximum = max(neighborhoodMaximum, sampleColor);
            neighborhoodMean += sampleColor;
            neighborhoodSquared += sampleColor * sampleColor;
        }
    }
    neighborhoodMean /= 9.0;
    const float3 spatialSigma = sqrt(max(
        neighborhoodSquared / 9.0 - neighborhoodMean * neighborhoodMean, 0.0));
    const float3 currentYCoCg = clamp(
        RgbToYCoCg(reconstructedCurrent.rgb),
        neighborhoodMinimum - spatialSigma * 0.2,
        neighborhoodMaximum + spatialSigma * 0.2);
    const float3 currentRgb = max(YCoCgToRgb(currentYCoCg), 0.0);

    float4 history = SampleHistoryCatmullRom(previousUv);
    history.a = saturate(history.a);
    const float2 previousMoments = SampleMomentsBilinear(previousUv);
    const float temporalVariance = max(
        previousMoments.y - previousMoments.x * previousMoments.x, 0.0);
    const float temporalSigma = sqrt(temporalVariance);
    const float noiseRatio = temporalSigma / max(abs(previousMoments.x), 0.05);
    const float clipGamma = lerp(1.25, 2.75, saturate(noiseRatio * 2.0));
    // A locked history is allowed slightly outside this frame's noisy min/max range. Clamping
    // strictly to a single noisy frame defeats temporal convergence and was the main source of
    // the weak reuse in the first implementation.
    const float3 clipExtent = max(
        spatialSigma * clipGamma,
        float3(temporalSigma * 1.5 + abs(neighborhoodMean.x) * 0.005,
               spatialSigma.y * 1.5 + 0.002,
               spatialSigma.z * 1.5 + 0.002));
    const float3 clipMinimum = max(
        neighborhoodMean - clipExtent,
        neighborhoodMinimum - spatialSigma * 0.5);
    const float3 clipMaximum = min(
        neighborhoodMean + clipExtent,
        neighborhoodMaximum + spatialSigma * 0.5);
    const float3 historyYCoCg = RgbToYCoCg(history.rgb);
    const float3 clippedHistoryYCoCg = clamp(historyYCoCg, clipMinimum, clipMaximum);
    const float3 clippedHistory = max(YCoCgToRgb(clippedHistoryYCoCg), 0.0);

    // Distinguish real changes from Monte-Carlo noise using temporal moments. Reactivity only
    // becomes authoritative after the lock has had time to learn the local variance.
    const float currentLuminance = Luminance(currentRgb);
    const float innovation = abs(currentLuminance - previousMoments.x);
    const float unexpectedChange = max(innovation - temporalSigma * 2.5, 0.0) /
        max(max(currentLuminance, abs(previousMoments.x)) + temporalSigma, 0.05);
    const float clipDistance = abs(historyYCoCg.x - clippedHistoryYCoCg.x) /
        max(max(abs(historyYCoCg.x), abs(currentYCoCg.x)), 0.05);
    const float learnedLock = saturate((history.a - 0.2) * 1.5);
    const float reactive = saturate(unexpectedChange * 1.25 + clipDistance * 0.75) * learnedLock;
    const float outputMotionLength = length(
        motion * (float2(constants.outputSize) / float2(constants.renderSize)));
    const float motionStability = exp2(-outputMotionLength * 0.012);

    const bool invalidHistory = constants.reset != 0 || disoccluded ||
                                any(isnan(history)) || any(isnan(previousMoments));
    const float confidence = invalidHistory ? 0.0 : min(history.a + 0.12, 1.0);
    const float historyBlend = invalidHistory ? 0.0 : min(
        constants.historyWeight * confidence * motionStability * (1.0 - reactive * 0.85),
        0.98);
    const float3 resolved = max(lerp(currentRgb, clippedHistory, historyBlend), 0.0);

    const float momentsRate = invalidHistory ? 1.0 : lerp(0.08, 0.22, reactive);
    const float2 currentMoments = float2(currentLuminance, currentLuminance * currentLuminance);
    const float2 updatedMoments = invalidHistory
        ? currentMoments
        : lerp(previousMoments, currentMoments, momentsRate);

    historyColorWrite[pixel] = float4(resolved, confidence);
    historyDepthWrite[pixel] = depth;
    historyMomentsWrite[pixel] = updatedMoments;
}
