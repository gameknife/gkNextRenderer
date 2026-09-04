// Raster half of the compatibility mini G-buffer. This must remain independent of Shader.Bindless
// and GPUScene: the target may support neither full descriptor indexing nor buffer device address.
// Lighting deliberately lives in Core.CompatibilityShade.comp.slang so later compatibility-only
// screen-space work can consume the same two fixed descriptor inputs.
import Shader.Types;

#include "common/PreProcessor.slang"

[[vk::binding(4, 0)]] StructuredBuffer<Material> Materials;

struct FragmentInput
{
    float4 position : SV_Position;
    float3 worldNormal : WORLD_NORMAL;
    nointerpolation uint materialId : MATERIAL_ID;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 albedo : SV_Target0;
    // xyz stores normal * 0.5 + 0.5; alpha is a coverage sentinel so the compute shader can
    // distinguish cleared background from a valid surface without sampling the depth attachment.
    [[vk::location(1)]] float4 normal : SV_Target1;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    FragmentOutput output;
    const Material material = Materials[min(MAX_MATERIALS - 1, max(0, int(input.materialId)))];
    const float3 normal = normalize(input.worldNormal);
    output.albedo = float4(material.Diffuse.rgb, 1.0f);
    output.normal = float4(normal * 0.5f + 0.5f, 1.0f);
    return output;
}
