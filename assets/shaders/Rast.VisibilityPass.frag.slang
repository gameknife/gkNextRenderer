import Shader.Types;

struct FragmentInput
{
    [[vk::location(0)]] nointerpolation uint g_primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint g_material_model : MATERIAL_MODEL;
    bool is_front_face : SV_IsFrontFace;
};

struct FragmentOutput
{
    [[vk::location(0)]] uint g_out_visibility : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    if (input.g_material_model == MaterialDiffuseLight && !input.is_front_face)
    {
        discard;
    }

    FragmentOutput output;
    output.g_out_visibility = input.g_primitive_index;
    return output;
}
