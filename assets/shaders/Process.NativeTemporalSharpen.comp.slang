// Display-resolution edge-adaptive sharpening. Kept separate from accumulation so all taps
// observe a completed temporal image and the result is deterministic across GPU wave sizes.

[[vk::binding(0, 0)]] RWTexture2D<float4> currentColor;
[[vk::binding(1, 0)]] RWTexture2D<float2> motionVectors;
[[vk::binding(2, 0)]] Texture2D<float> currentDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> historyColorRead;
[[vk::binding(4, 0)]] RWTexture2D<float> historyDepthRead;
[[vk::binding(5, 0)]] RWTexture2D<float2> historyMomentsRead;
[[vk::binding(6, 0)]] RWTexture2D<float4> resolvedColor;
[[vk::binding(7, 0)]] RWTexture2D<float> historyDepthWrite;
[[vk::binding(8, 0)]] RWTexture2D<float2> historyMomentsWrite;
// Binding 9 is the external final output. Sharpen writes binding 10, which aliases the
// external output when post-filtering is disabled and otherwise points at filter ping.
[[vk::binding(10, 0)]] RWTexture2D<float4> outputColor;

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

float3 LoadResolved(int2 pixel)
{
    pixel = clamp(pixel, int2(0), int2(constants.outputSize) - 1);
    return resolvedColor.Load(pixel).rgb;
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

    const int2 position = int2(pixel);
    const float3 center = LoadResolved(position);
    const float3 north = LoadResolved(position + int2(0, -1));
    const float3 south = LoadResolved(position + int2(0, 1));
    const float3 west = LoadResolved(position + int2(-1, 0));
    const float3 east = LoadResolved(position + int2(1, 0));
    const float3 localMinimum = min(center, min(min(north, south), min(west, east)));
    const float3 localMaximum = max(center, max(max(north, south), max(west, east)));
    const float3 blur = (north + south + west + east) * 0.25;
    const float contrast = (Luminance(localMaximum) - Luminance(localMinimum)) /
                           max(Luminance(localMaximum), 0.05);
    const float adaptiveStrength = constants.sharpness * saturate(1.0 - contrast * 0.65);
    const float3 sharpened = max(center + (center - blur) * adaptiveStrength, 0.0);
    outputColor[pixel] = float4(sharpened, 1.0);
}
