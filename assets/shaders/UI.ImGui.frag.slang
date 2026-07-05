import Shader.Bindless;

struct PushConsts
{
    float2 scale;
    float2 translate;
    float4 rotation;
    uint hdrOutputMode;
    float hdrReferenceWhiteNit;
    float2 padding;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation float4 clipRect : TEXCOORD1;
    [[vk::location(3)]] nointerpolation uint textureIndex : TEXCOORD2;
    [[vk::location(4)]] nointerpolation uint textureFlags : TEXCOORD3;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};

static const uint UI_TEXTURE_FLAG_RAW_OUTPUT = 1u << 0u;

float3 SrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    bool3 useHigh = color > 0.04045;
    return float3(useHigh.x ? high.x : low.x, useHigh.y ? high.y : low.y, useHigh.z ? high.z : low.z);
}

float3 LinearToST2084UE(float3 linearNits)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float maxNit = 10000.0;

    float3 luminance = linearNits / maxNit;
    float3 luminancePow = pow(max(luminance, 0.0), float3(m1, m1, m1));
    float3 numerator = c1 + c2 * luminancePow;
    float3 denominator = 1.0 + c3 * luminancePow;
    return pow(numerator / denominator, float3(m2, m2, m2));
}

float3 Bt709ToBt2020(float3 color)
{
    return float3(0.6274040 * color.r + 0.3292820 * color.g + 0.0433136 * color.b,
                  0.0690970 * color.r + 0.9195400 * color.g + 0.0113612 * color.b,
                  0.0163916 * color.r + 0.0880132 * color.g + 0.8955950 * color.b);
}

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;

    if (input.position.x < input.clipRect.x || input.position.y < input.clipRect.y ||
        input.position.x >= input.clipRect.z || input.position.y >= input.clipRect.w)
    {
        discard;
    }

    float4 sdrColor = Bindless.GetSampleTexture(input.textureIndex).SampleLevel(input.uv, 0.0) * input.color;
    if ((input.textureFlags & UI_TEXTURE_FLAG_RAW_OUTPUT) != 0u)
    {
        output.color = sdrColor;
        return output;
    }

    if (pushConsts.hdrOutputMode == 1)
    {
        float3 linear709 = SrgbToLinear(saturate(sdrColor.rgb));
        float3 linear2020 = Bt709ToBt2020(linear709);
        output.color = float4(LinearToST2084UE(linear2020 * pushConsts.hdrReferenceWhiteNit), sdrColor.a);
    }
    else if (pushConsts.hdrOutputMode == 2)
    {
        output.color = float4(SrgbToLinear(saturate(sdrColor.rgb)), sdrColor.a);
    }
    else
    {
        output.color = sdrColor;
    }

    return output;
}
