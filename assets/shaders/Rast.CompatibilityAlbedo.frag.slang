// Fragment half of the compatibility raster pass. Outputs the material's base-color factor and
// nothing else: no albedo texture sampling (that would need the sampled bindless array), no
// lighting, no G-buffer. Neither Shader.Bindless nor the GPUScene push constant is used -- see the
// vertex shader for why both are unavailable on the target device.
import Shader.Types;

[[vk::binding(4, 0)]] StructuredBuffer<Material> Materials;

struct CompatibilityPushConstants
{
    float4x4 ViewProjection;
    uint ProxyIndex;
};
[[vk::push_constant]] CompatibilityPushConstants pushConstants;

struct FragmentInput
{
    float4 position : SV_Position;
    nointerpolation uint materialId : MATERIAL_ID;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;
    const Material material = Materials[min(MAX_MATERIALS - 1, max(0, int(input.materialId)))];
    output.color = float4(material.Diffuse.rgb, 1.0);
    return output;
}
