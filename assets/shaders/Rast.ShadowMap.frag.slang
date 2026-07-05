import Shader.Types;

struct FragmentInput
{
    [[vk::location(0)]] nointerpolation uint g_material_model : MATERIAL_MODEL;
};

[shader("fragment")]
void main(FragmentInput input)
{
    // 发光面片不投射阴影
    if (input.g_material_model == MaterialDiffuseLight)
    {
        discard;
    }
}
