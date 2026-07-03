struct FragmentInput
{
    [[vk::location(0)]] float2 local : LOCAL;
    [[vk::location(1)]] nointerpolation float4 colorOpacity : COLOR_OPACITY;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    const float a = dot(input.local, input.local);
    if (a > 1.0) discard;
    const float expEdge = exp(-4.0);
    const float weight = (exp(-4.0 * a) - expEdge) / (1.0 - expEdge);
    const float alpha = min(0.99, input.colorOpacity.a * weight);
    if (alpha < (1.0 / 255.0)) discard;
    FragmentOutput output;
    output.color = float4(input.colorOpacity.rgb, alpha);
    return output;
}
