import Shader.Types;

struct FragmentInput
{
    [[vk::location(0)]] nointerpolation uint2 g_visibility_id : VISIBILITY_ID;
    [[vk::location(1)]] nointerpolation uint g_material_model : MATERIAL_MODEL;
    bool is_front_face : SV_IsFrontFace;
};

struct FragmentOutput
{
    [[vk::location(0)]] uint g_out_instance : SV_Target0;
    [[vk::location(1)]] uint g_out_triangle : SV_Target1;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    if (input.g_material_model == MaterialDiffuseLight && !input.is_front_face)
    {
        discard;
    }

    FragmentOutput output;
    output.g_out_instance = input.g_visibility_id.x;
    output.g_out_triangle = input.g_visibility_id.y;
    return output;
}
