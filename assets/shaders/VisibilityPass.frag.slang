import Common;

struct FragmentInput
{
    [[vk::location(0)]] nointerpolation uint g_primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint g_instance_index : INSTANCE_INDEX;
};

struct FragmentOutput
{
    [[vk::location(0)]] uint2 g_out_visibility : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;

    output.g_out_visibility.x = input.g_instance_index + 1;
    output.g_out_visibility.y = input.g_primitive_index;
   
    return output;
}