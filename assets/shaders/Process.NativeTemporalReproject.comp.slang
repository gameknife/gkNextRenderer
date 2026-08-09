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
    float nearPlane;
    float farPlane;
    // Pre-exposure applied before the Reinhard compression this pass works in, so the working
    // point sits on the curve's knee instead of its saturated tail. Undone in the sharpen pass.
    float compressionScale;
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
    const float4 color = currentColor.Load(ClampRenderPixel(pixel));
    const float3 positiveColor = max(color.rgb, 0.0) * constants.compressionScale;
    return float4(positiveColor / (1.0 + Luminance(positiveColor)), color.a);
}

float LoadDepth(int2 pixel)
{
    return currentDepth.Load(int3(ClampRenderPixel(pixel), 0));
}

// Invert the [0,1] perspective depth buffer (GLM_FORCE_DEPTH_ZERO_TO_ONE, finite far plane).
// Disocclusion has to be judged in linear view depth: a fixed NDC epsilon is sub-millimetre near
// the camera and tens of metres in the distance, so it cannot express one usable tolerance.
float LinearizeDepth(float ndcDepth)
{
    const float nearPlane = constants.nearPlane;
    const float farPlane = constants.farPlane;
    return nearPlane * farPlane /
        max(farPlane - ndcDepth * (farPlane - nearPlane), 1.0e-6);
}

float LoadLinearDepth(int2 pixel)
{
    return LinearizeDepth(LoadDepth(pixel));
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

// History depth is display resolution while the current depth is render resolution, so a
// reprojected sample can land up to half a render pixel across a silhouette. Picking the closest
// of the four bilinear-footprint candidates keeps that fixed misalignment from reading as a
// disocclusion on every contour pixel, every frame.
float SampleHistoryLinearDepth(float2 uv, float reference)
{
    const float2 position = uv * float2(constants.outputSize) - 0.5;
    const int2 base = int2(floor(position));
    float best = historyDepthRead.Load(ClampOutputPixel(base));
    float bestDelta = abs(best - reference);
    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            const float candidate = historyDepthRead.Load(
                ClampOutputPixel(base + int2(x, y)));
            const float delta = abs(candidate - reference);
            if (delta < bestDelta)
            {
                bestDelta = delta;
                best = candidate;
            }
        }
    }
    return best;
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
    // The jitter belongs to the reconstruction tap position above and nowhere else. Rounding the
    // jittered position would move this pixel's sampling centre between two neighbouring render
    // pixels every frame; on a silhouette those two are foreground and background, so the depth
    // test, the motion lookup and the neighbourhood statistics would all flip sides at the jitter
    // frequency. That is edge flicker by construction.
    const int2 centerPixel = ClampRenderPixel(int2(round(unjitteredRenderPosition)));

    const float centerLinearDepth = LoadLinearDepth(centerPixel);

    // Dilation feeds the motion lookup only: the nearest 3x3 sample keeps foreground silhouettes
    // attached to their own velocity. It must not reach the depth written to history, or every
    // contour gets a one-pixel ring of foreground depth that the next frame reads back as a
    // disocclusion.
    int2 motionPixel = centerPixel;
    float nearestDepth = LoadDepth(centerPixel);
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 candidatePixel = ClampRenderPixel(centerPixel + int2(x, y));
            const float candidateDepth = LoadDepth(candidatePixel);
            if (candidateDepth < nearestDepth)
            {
                nearestDepth = candidateDepth;
                motionPixel = candidatePixel;
            }
        }
    }

    // Surface slope in linear view depth. Taking the smaller of the two one-sided differences per
    // axis drops the silhouette step and leaves the real slope, so a grazing surface still gets a
    // generous tolerance while an edge no longer inherits the whole foreground/background gap.
    const float slopeX = min(
        abs(centerLinearDepth - LoadLinearDepth(centerPixel + int2(-1, 0))),
        abs(LoadLinearDepth(centerPixel + int2(1, 0)) - centerLinearDepth));
    const float slopeY = min(
        abs(centerLinearDepth - LoadLinearDepth(centerPixel + int2(0, -1))),
        abs(LoadLinearDepth(centerPixel + int2(0, 1)) - centerLinearDepth));
    const float surfaceSlope = slopeX + slopeY;

    const float2 motion = LoadMotion(motionPixel);
    const float2 previousUv = outputUv + motion / float2(constants.renderSize);
    const bool historyOnScreen = all(previousUv > 0.0) && all(previousUv < 1.0);
    const float previousLinearDepth = historyOnScreen
        ? SampleHistoryLinearDepth(previousUv, centerLinearDepth)
        : centerLinearDepth;
    const float depthTolerance = centerLinearDepth * 0.025 + surfaceSlope * 3.0 +
        constants.nearPlane * 0.5;
    const bool disoccluded = !historyOnScreen ||
        isnan(previousLinearDepth) ||
        abs(previousLinearDepth - centerLinearDepth) > depthTolerance;

    // Current-frame spatial statistics over the eight neighbours only. Including the centre lets
    // a single outlier widen the very box that is supposed to reject it: with one bright sample
    // in nine, mean + 2.75*sigma lands almost exactly on that sample's luminance, so a firefly
    // passes through completely untouched. Excluding it also turns the reconstruction clamp below
    // into a first line of outlier rejection.
    // The extremes track the second largest/smallest instead of the true min/max, so one stray
    // neighbour cannot prise the box open either. YCoCg keeps a bright channel from expanding the
    // whole RGB box and retains far more chroma detail than a plain min/max clamp.
    float3 neighborhoodMinimum = float3(1e20);
    float3 secondMinimum = float3(1e20);
    float3 neighborhoodMaximum = float3(-1e20);
    float3 secondMaximum = float3(-1e20);
    float3 neighborhoodMean = 0.0;
    float3 neighborhoodSquared = 0.0;
    float maximumLuminance = -1e20;
    float secondMaximumLuminance = -1e20;
    float neighborLuminanceMean = 0.0;
    float neighborLuminanceSquared = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x != 0 || y != 0)
            {
                const float3 sampleRgb = LoadCurrent(centerPixel + int2(x, y)).rgb;
                const float3 sampleColor = RgbToYCoCg(sampleRgb);
                secondMaximum = max(secondMaximum, min(neighborhoodMaximum, sampleColor));
                neighborhoodMaximum = max(neighborhoodMaximum, sampleColor);
                secondMinimum = min(secondMinimum, max(neighborhoodMinimum, sampleColor));
                neighborhoodMinimum = min(neighborhoodMinimum, sampleColor);
                neighborhoodMean += sampleColor;
                neighborhoodSquared += sampleColor * sampleColor;

                const float sampleLuminance = Luminance(sampleRgb);
                secondMaximumLuminance = max(
                    secondMaximumLuminance, min(maximumLuminance, sampleLuminance));
                maximumLuminance = max(maximumLuminance, sampleLuminance);
                neighborLuminanceMean += sampleLuminance;
                neighborLuminanceSquared += sampleLuminance * sampleLuminance;
            }
        }
    }
    neighborhoodMean /= 8.0;
    neighborLuminanceMean /= 8.0;
    const float3 spatialSigma = sqrt(max(
        neighborhoodSquared / 8.0 - neighborhoodMean * neighborhoodMean, 0.0));
    const float neighborLuminanceSigma = sqrt(max(
        neighborLuminanceSquared / 8.0 - neighborLuminanceMean * neighborLuminanceMean, 0.0));
    const float3 currentYCoCg = clamp(
        RgbToYCoCg(reconstructedCurrent.rgb),
        secondMinimum - spatialSigma * 0.2,
        secondMaximum + spatialSigma * 0.2);
    float3 currentRgb = max(YCoCgToRgb(currentYCoCg), 0.0);

    float4 history = SampleHistoryCatmullRom(previousUv);
    history.a = saturate(history.a);
    const float2 previousMoments = SampleMomentsBilinear(previousUv);
    const float temporalVariance = max(
        previousMoments.y - previousMoments.x * previousMoments.x, 0.0);
    const float temporalSigma = sqrt(temporalVariance);
    const float noiseRatio = temporalSigma / max(abs(previousMoments.x), 0.05);
    const float clipGamma = lerp(1.25, 2.75, saturate(noiseRatio * 2.0));
    const float learnedLock = saturate((history.a - 0.2) * 1.5);
    const bool invalidHistory = constants.reset != 0 || disoccluded ||
                                any(isnan(history)) || any(isnan(previousMoments));
    // Firefly rejection matters most on the frame a pixel disoccludes, which is exactly when a
    // hard `learnedLock > 0.5` gate used to switch it off: confidence needs five clean frames to
    // clear that bar, and an edge pixel never gets them. Weight the two ceilings continuously on
    // the lock instead. The spatial ceiling needs only this frame, so it is always available and
    // is the sole estimate a fresh pixel has; the temporal one is allowed to relax it once the
    // lock has actually learned the local variance and a bright pixel may be genuine.
    {
        const float currentLuminance = Luminance(currentRgb);
        // Second-largest neighbour rather than mean + k*sigma alone: on a silhouette the neighbour
        // deviation is geometric contrast, not noise, and would set the ceiling far too high.
        const float spatialCeiling = min(
            neighborLuminanceMean + max(0.02, neighborLuminanceSigma * 2.0),
            secondMaximumLuminance * 1.25 + 0.02);
        // Hand over to the temporal ceiling as the lock matures, without ever taking the looser of
        // the two. The spatial estimate rests on a single noisy frame, so its sigma is an order of
        // magnitude above the converged temporal one; letting it raise the ceiling on settled
        // pixels relaxes firefly rejection across the whole image. At full lock this is exactly
        // the original pure-temporal ceiling, only now reached continuously instead of through a
        // hard gate that edge pixels could never clear.
        const float temporalCeiling = previousMoments.x + temporalSigma * 2.5;
        const float fireflyCeiling = invalidHistory
            ? spatialCeiling
            : lerp(spatialCeiling, temporalCeiling, learnedLock);
        if (currentLuminance > fireflyCeiling)
        {
            currentRgb *= fireflyCeiling / max(currentLuminance, 1.0e-5);
        }
    }
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
        secondMinimum - spatialSigma * 0.5);
    const float3 clipMaximum = min(
        neighborhoodMean + clipExtent,
        secondMaximum + spatialSigma * 0.5);
    const float3 historyYCoCg = RgbToYCoCg(history.rgb);
    const float3 clippedHistoryYCoCg = clamp(historyYCoCg, clipMinimum, clipMaximum);
    const float3 clippedHistory = max(YCoCgToRgb(clippedHistoryYCoCg), 0.0);

    // Distinguish real changes from Monte-Carlo noise using temporal moments. Reactivity only
    // becomes authoritative after the lock has had time to learn the local variance.
    const float currentLuminance = Luminance(currentRgb);
    const float innovation = abs(currentLuminance - previousMoments.x);
    const float unexpectedChange = max(innovation - temporalSigma * 2.5, 0.0) /
        max(max(currentLuminance, abs(previousMoments.x)) + temporalSigma, 0.05);
    // Measure how far outside the clip box history sat, in units of the box itself. Normalising
    // by luminance instead makes a narrower box read as higher reactivity for the same actual
    // disagreement: tightening the box (centre-excluded statistics, second-largest extremes)
    // clips more by construction, which would silently collapse temporal reuse everywhere.
    const float clipBoxHalfWidth = max((clipMaximum.x - clipMinimum.x) * 0.5, 1.0e-4);
    const float clipDistance = saturate(
        abs(historyYCoCg.x - clippedHistoryYCoCg.x) / clipBoxHalfWidth);
    const float reactive = saturate(unexpectedChange * 1.25 + clipDistance * 0.75) * learnedLock;
    const float outputMotionLength = length(
        motion * (float2(constants.outputSize) / float2(constants.renderSize)));
    const float motionStability = exp2(-outputMotionLength * 0.012);

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
    historyDepthWrite[pixel] = centerLinearDepth;
    historyMomentsWrite[pixel] = updatedMoments;
}
