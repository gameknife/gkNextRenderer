import Shader.Tonemap;
import Shader.Types;

struct PushConstants
{
    uint64_t cameraAddress;
    uint64_t primitiveAddress;
    uint primitiveCount;
    uint outputWidth;
    uint outputHeight;
    uint padding;
};

[[vk::push_constant]] PushConstants push;

static const uint AUX_TYPE_LINE = 0;
static const uint AUX_TYPE_POINT = 1;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation float4 lineStartRadius : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float4 lineEndType : TEXCOORD1;
    [[vk::location(3)]] nointerpolation uint flags : TEXCOORD2;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};

float3 EncodeAuxColor(float3 color, UniformBufferObject camera)
{
    color = saturate(color);
    if (!camera.HDR)
    {
        return color;
    }

    float3 tonemapInputMin = float3(0.0, 0.0, 0.0);
    float3 tonemapInputMax = float3(64.0, 64.0, 64.0);
    for (uint iteration = 0; iteration < 16; ++iteration)
    {
        const float3 mid = (tonemapInputMin + tonemapInputMax) * 0.5;
        const float3 mapped = GT_Tonemapping(mid);
        tonemapInputMin = select(mapped < color, mid, tonemapInputMin);
        tonemapInputMax = select(mapped < color, tonemapInputMax, mid);
    }

    const float3 sceneColor = tonemapInputMax * (40000.0 / max(camera.PaperWhiteNit, 1.0));
    return EncodeHdrOutput(sceneColor, camera.PaperWhiteNit, camera.HDROutputMode);
}

float LineCoverage(float2 pixelCenter, float2 start, float2 end, float radius)
{
    const float2 segment = end - start;
    const float lenSq = dot(segment, segment);
    if (lenSq <= 1e-6)
    {
        const float dist = length(pixelCenter - start);
        return saturate(radius + 0.5 - dist);
    }

    const float t = saturate(dot(pixelCenter - start, segment) / lenSq);
    const float2 closest = start + segment * t;
    const float dist = length(pixelCenter - closest);
    return saturate(radius + 0.5 - dist);
}

float PointCoverage(float2 pixelCenter, float2 center, float radius)
{
    const float dist = length(pixelCenter - center);
    return saturate(radius + 0.5 - dist);
}

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    const uint type = uint(input.lineEndType.w + 0.5);
    const float2 pixelCenter = input.position.xy;
    float coverage = 0.0;
    if (type == AUX_TYPE_LINE)
    {
        coverage = LineCoverage(
            pixelCenter,
            input.lineStartRadius.xy,
            input.lineEndType.xy,
            input.lineStartRadius.w);
    }
    else if (type == AUX_TYPE_POINT)
    {
        coverage = PointCoverage(pixelCenter, input.lineStartRadius.xy, input.lineStartRadius.w);
    }

    const float alpha = saturate(input.color.a * coverage);
    if (alpha <= 0.0)
    {
        discard;
    }

    UniformBufferObject* cameraPtr = (UniformBufferObject*)push.cameraAddress;
    const UniformBufferObject camera = cameraPtr[0];
    FragmentOutput output;
    output.color = float4(EncodeAuxColor(input.color.rgb, camera), alpha);
    return output;
}
