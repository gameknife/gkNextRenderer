// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Slang port of Snapdragon Game Super Resolution 2, 2-pass compute Convert.
// The engine supplies full-scene motion in render-pixel units, so the original
// encoded dynamic-only velocity path is converted directly to SGSR2 clip motion.

[[vk::binding(0, 0)]] Texture2D<float4> inputColor;
[[vk::binding(1, 0)]] Texture2D<float> inputDepth;
[[vk::binding(2, 0)]] Texture2D<float2> inputVelocity;
[[vk::binding(3, 0)]] RWTexture2D<float4> motionDepthClipBuffer;
[[vk::binding(4, 0)]] RWTexture2D<uint> yCoCgColor;

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

int2 ClampRenderPixel(int2 pixel)
{
    return clamp(pixel, int2(0), int2(params.renderSize) - 1);
}

float LoadDepth(int2 pixel)
{
    return inputDepth.Load(int3(ClampRenderPixel(pixel), 0));
}

float MaxDepth2x2(int2 topLeft)
{
    const float top = max(LoadDepth(topLeft), LoadDepth(topLeft + int2(1, 0)));
    const float bottom = max(LoadDepth(topLeft + int2(0, 1)), LoadDepth(topLeft + int2(1, 1)));
    return max(top, bottom);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= params.renderSize))
    {
        return;
    }

    const int2 inputPosition = int2(pixel);

    // SGSR2 groups the 3x3 depth neighborhood into four overlapping quadrants.
    // Reverse-Z depth is near=1 and far=0, so the nearest sample is the maximum.
    const float topLeftDepth = MaxDepth2x2(inputPosition - int2(1, 1));
    const float topRightDepth = MaxDepth2x2(inputPosition + int2(0, -1));
    const float bottomLeftDepth = MaxDepth2x2(inputPosition + int2(-1, 0));
    const float bottomRightDepth = MaxDepth2x2(inputPosition);
    const float nearestDepth = max(
        max(topLeftDepth, topRightDepth), max(bottomLeftDepth, bottomRightDepth));

    float depthClip = 0.0f;
    if (nearestDepth > 1.0e-5f)
    {
        const float separation = 1.37e-5f * params.cameraFovAngleHor *
            length(float2(params.renderSize)) * nearestDepth;
        const float epsilon = 1.19e-7f;
        float depthWeight = 0.0f;
        depthWeight += saturate(separation / (abs(nearestDepth - topLeftDepth) + epsilon));
        depthWeight += saturate(separation / (abs(nearestDepth - topRightDepth) + epsilon));
        depthWeight += saturate(separation / (abs(nearestDepth - bottomLeftDepth) + epsilon));
        depthWeight += saturate(separation / (abs(nearestDepth - bottomRightDepth) + epsilon));
        depthClip = saturate(1.0f - depthWeight * 0.25f);
    }

    // Engine contract: velocity=(previousUV-currentUV)*renderSize. SGSR2's Vulkan
    // upscale pass consumes current-minus-previous clip motion, with inverted NDC Y.
    const float2 velocityPixels = inputVelocity.Load(int3(inputPosition, 0));
    const float2 motion = float2(
        -2.0f * velocityPixels.x * params.renderSizeRcp.x,
         2.0f * velocityPixels.y * params.renderSizeRcp.y);

    float3 colorRgb = inputColor.Load(int3(inputPosition, 0)).rgb;
    const float colorMax = max(max(colorRgb.x, colorRgb.y), colorRgb.z) + params.preExposure;
    colorRgb /= colorMax;

    float3 colorYCoCg;
    colorYCoCg.x = 0.25f * (colorRgb.x + 2.0f * colorRgb.y + colorRgb.z);
    colorYCoCg.y = saturate(0.5f * colorRgb.x + 0.5f - 0.5f * colorRgb.z);
    colorYCoCg.z = saturate(colorYCoCg.x + colorYCoCg.y - colorRgb.x);

    const uint x11 = uint(colorYCoCg.x * 2047.5f);
    const uint y11 = uint(colorYCoCg.y * 2047.5f);
    const uint z10 = uint(colorYCoCg.z * 1023.5f);
    yCoCgColor[pixel] = (x11 << 21u) | (y11 << 10u) | z10;
    motionDepthClipBuffer[pixel] = float4(motion, depthClip, colorMax);
}
