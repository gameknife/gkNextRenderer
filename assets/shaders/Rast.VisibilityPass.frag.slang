import Common;

struct FragmentInput
{
    [[vk::location(0)]] nointerpolation uint g_primitive_index : PRIMITIVE_INDEX;
};

struct FragmentOutput
{
    [[vk::location(0)]] uint g_out_visibility : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;
    output.g_out_visibility = input.g_primitive_index;
    return output;
}