// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Slang port of Snapdragon Game Super Resolution 2, 2-pass compute Upscale.

[[vk::binding(5, 0)]] Texture2D<float4> motionDepthClipBuffer;
[[vk::binding(6, 0)]] Texture2D<uint> yCoCgColor;
[[vk::binding(7, 0)]] Texture2D<float4> previousHistoryOutput;
[[vk::binding(8, 0)]] RWTexture2D<float4> sceneColorOutput;
[[vk::binding(9, 0)]] RWTexture2D<float4> historyOutput;

struct PushConstants
{
    uint2 renderSize;
    uint2 displaySize;
    float2 renderSizeRcp;
    float2 displaySizeRcp;
    float2 jitterOffset;
    float preExposure;
    float cameraFovAngleHor;
    float cameraNear;
    float minLerpContribution;
    uint sameCamera;
    uint reset;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> params;

float FastLanczos(float base)
{
    const float y = base - 1.0f;
    const float y2 = y * y;
    return (0.75f * y + y2) * y2;
}

float3 DecodeColor(uint packedColor)
{
    const uint x11 = packedColor >> 21u;
    const uint y11 = (packedColor >> 10u) & 2047u;
    const uint z10 = packedColor & 1023u;
    return float3(
        float(x11) * (1.0f / 2047.5f),
        float(y11) * (1.0f / 2047.5f) - 0.5f,
        float(z10) * (1.0f / 1023.5f) - 0.5f);
}

int2 ClampRenderPixel(int2 pixel)
{
    return clamp(pixel, int2(0), int2(params.renderSize) - 1);
}

int2 ClampDisplayPixel(int2 pixel)
{
    return clamp(pixel, int2(0), int2(params.displaySize) - 1);
}

float4 SampleMotionDepthClip(float2 uv)
{
    const float2 position = uv * float2(params.renderSize) - 0.5f;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const float4 top = lerp(
        motionDepthClipBuffer.Load(int3(ClampRenderPixel(base), 0)),
        motionDepthClipBuffer.Load(int3(ClampRenderPixel(base + int2(1, 0)), 0)),
        fraction.x);
    const float4 bottom = lerp(
        motionDepthClipBuffer.Load(int3(ClampRenderPixel(base + int2(0, 1)), 0)),
        motionDepthClipBuffer.Load(int3(ClampRenderPixel(base + int2(1, 1)), 0)),
        fraction.x);
    return lerp(top, bottom, fraction.y);
}

float4 SampleHistory(float2 uv)
{
    const float2 position = uv * float2(params.displaySize) - 0.5f;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const float4 top = lerp(
        previousHistoryOutput.Load(int3(ClampDisplayPixel(base), 0)),
        previousHistoryOutput.Load(int3(ClampDisplayPixel(base + int2(1, 0)), 0)),
        fraction.x);
    const float4 bottom = lerp(
        previousHistoryOutput.Load(int3(ClampDisplayPixel(base + int2(0, 1)), 0)),
        previousHistoryOutput.Load(int3(ClampDisplayPixel(base + int2(1, 1)), 0)),
        fraction.x);
    return lerp(top, bottom, fraction.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= params.displaySize))
    {
        return;
    }

    const float viewportScale = min(
        float(params.displaySize.x) / float(params.renderSize.x), 1.99f);
    const float scaleFactor = min(20.0f, pow(
        (float(params.displaySize.x) / float(params.renderSize.x)) *
        (float(params.displaySize.y) / float(params.renderSize.y)), 3.0f));

    const float2 historyUv = (float2(pixel) + 0.5f) * params.displaySizeRcp;
    const float2 jitterUv = saturate(historyUv + params.jitterOffset * params.renderSizeRcp);
    const int2 inputPosition = ClampRenderPixel(int2(jitterUv * float2(params.renderSize)));

    const float4 motionDepthAlpha = SampleMotionDepthClip(jitterUv);
    const float2 motion = motionDepthAlpha.xy;
    const float2 previousUv = saturate(float2(
        -0.5f * motion.x + historyUv.x,
         0.5f * motion.y + historyUv.y));
    const float depthFactor = motionDepthAlpha.z;
    const float colorMax = motionDepthAlpha.w;

    const float4 history = params.reset != 0 ? float4(0.0f) : SampleHistory(previousUv);
    float3 historyColor = history.xyz;
    const float historyWeight = params.reset != 0 ? 0.0f : saturate(abs(history.w));

    float4 upsampledColorWeight = float4(0.0f);
    const float kernelFactor = saturate(historyWeight + float(params.reset));
    const float biasMax = viewportScale - viewportScale * kernelFactor;
    const float biasMin = max(1.0f, 0.3f + 0.3f * biasMax);
    const float biasFactor = max(0.25f * depthFactor, kernelFactor);
    float kernelBias = lerp(biasMax, biasMin, biasFactor);
    const float motionViewportLength = length(motion * float2(params.displaySize));
    const float curveBias = lerp(-2.0f, -3.0f, saturate(motionViewportLength * 0.02f));

    float3 rectBoxCenter = float3(0.0f);
    float3 rectBoxVariance = float3(0.0f);
    float rectBoxWeight = 0.0f;
    float3 rectBoxMinimum = float3(1.0e20f);
    float3 rectBoxMaximum = float3(-1.0e20f);

    const float2 sourcePosition = float2(inputPosition) + 0.5f - params.jitterOffset;
    const float2 sourceOutputPosition = historyUv * float2(params.renderSize);
    kernelBias *= 0.5f;
    const float kernelBiasSquared = kernelBias * kernelBias;
    const float2 sourceDelta = sourcePosition - sourceOutputPosition;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            // The moving-camera fast path uses the official five-sample cross.
            if (params.sameCamera == 0 && x != 0 && y != 0)
            {
                continue;
            }

            const int2 offset = int2(x, y);
            const uint packedColor = yCoCgColor.Load(
                int3(ClampRenderPixel(inputPosition + offset), 0));
            const float3 sampleColor = DecodeColor(packedColor);
            const float2 baseOffset = sourceDelta + float2(offset);
            const float baseOffsetSquared = dot(baseOffset, baseOffset);
            const float base = saturate(baseOffsetSquared * kernelBiasSquared);
            const float weight = FastLanczos(base);
            upsampledColorWeight += float4(sampleColor * weight, weight);

            const float boxWeight = exp(baseOffsetSquared * curveBias);
            rectBoxMinimum = min(rectBoxMinimum, sampleColor);
            rectBoxMaximum = max(rectBoxMaximum, sampleColor);
            const float3 weightedSample = sampleColor * boxWeight;
            rectBoxCenter += weightedSample;
            rectBoxVariance += sampleColor * weightedSample;
            rectBoxWeight += boxWeight;
        }
    }

    rectBoxWeight = 1.0f / max(rectBoxWeight, 1.192e-7f);
    rectBoxCenter *= rectBoxWeight;
    rectBoxVariance *= rectBoxWeight;
    rectBoxVariance = sqrt(abs(rectBoxVariance - rectBoxCenter * rectBoxCenter));
    upsampledColorWeight.xyz = clamp(
        upsampledColorWeight.xyz / max(upsampledColorWeight.w, 1.192e-7f),
        rectBoxMinimum - 0.05f,
        rectBoxMaximum + 0.05f);
    upsampledColorWeight.w *= 1.0f / 3.0f;

    const float oneMinusHistoryWeight = 1.0f - historyWeight;
    float baseUpdate = oneMinusHistoryWeight - oneMinusHistoryWeight * depthFactor;
    baseUpdate = min(baseUpdate, lerp(
        baseUpdate, upsampledColorWeight.w * 10.0f,
        saturate(10.0f * motionViewportLength)));
    baseUpdate = min(baseUpdate, lerp(
        baseUpdate, upsampledColorWeight.w,
        saturate(motionViewportLength * 0.05f)));
    float baseAlpha = baseUpdate;

    const float boxScale = max(depthFactor, saturate(motionViewportLength * 0.05f));
    const float boxSize = lerp(scaleFactor, 1.0f, boxScale);
    const float3 scaledBoxVariance = rectBoxVariance * boxSize;
    rectBoxMaximum = min(rectBoxMaximum, rectBoxCenter + scaledBoxVariance);
    rectBoxMinimum = max(rectBoxMinimum, rectBoxCenter - scaledBoxVariance);

    const float3 clampedColor = clamp(historyColor, rectBoxMinimum, rectBoxMaximum);
    float startLerpValue = params.minLerpContribution;
    if (abs(motionDepthAlpha.x) + abs(motionDepthAlpha.y) > 1.0e-6f)
    {
        startLerpValue = 0.0f;
    }
    const float lerpContribution =
        (any(rectBoxMinimum > historyColor) || any(historyColor > rectBoxMaximum))
            ? startLerpValue
            : 1.0f;
    historyColor = lerp(clampedColor, historyColor, saturate(lerpContribution));
    const float baseMinimum = min(baseAlpha, 0.1f);
    baseAlpha = lerp(baseMinimum, baseAlpha, saturate(lerpContribution));

    const float alphaSum = max(1.192e-7f, baseAlpha + upsampledColorWeight.w);
    const float alpha = saturate(
        upsampledColorWeight.w / alphaSum + float(params.reset));
    upsampledColorWeight.xyz = lerp(historyColor, upsampledColorWeight.xyz, alpha);
    historyOutput[pixel] = float4(upsampledColorWeight.xyz, historyWeight);

    const float xMinusZ = upsampledColorWeight.x - upsampledColorWeight.z;
    upsampledColorWeight.xyz = saturate(float3(
        xMinusZ + upsampledColorWeight.y,
        upsampledColorWeight.x + upsampledColorWeight.z,
        xMinusZ - upsampledColorWeight.y));
    const float componentMaximum = saturate(max(
        max(upsampledColorWeight.x, upsampledColorWeight.y), upsampledColorWeight.z));
    float outputScale = params.preExposure /
        ((1.0f + 600.0f / 65504.0f) - componentMaximum);
    if (colorMax > 4000.0f)
    {
        outputScale = colorMax;
    }
    upsampledColorWeight.xyz *= outputScale;
    sceneColorOutput[pixel] = upsampledColorWeight;
}
