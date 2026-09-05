// Pure passthrough: the vertex stage already resolved the edge colour, so nothing here depends on
// push constants or on the LOD level.
struct FragmentInput
{
    [[vk::location(0)]] nointerpolation float3 color : WIRE_COLOR;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 OutColor : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;
    output.OutColor = float4(input.color, 0.55f);
    return output;
}
