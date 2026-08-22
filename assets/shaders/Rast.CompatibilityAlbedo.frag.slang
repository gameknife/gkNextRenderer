// Fragment half of the compatibility raster pass: material base colour lit by a single Lambert sun
// term plus a hemisphere sky fill. No albedo texture sampling (that would need the sampled bindless
// array), no shadows, no specular. Shader.Bindless is deliberately not imported and there is no
// GPUScene push constant -- see the vertex shader for why both are unavailable on the target device.
import Shader.Types;

#include "common/PreProcessor.slang"

[[vk::binding(4, 0)]] StructuredBuffer<Material> Materials;

struct CompatibilityPushConstants
{
    float4x4 ViewProjection;
    // .xyz points toward the sun, matching UniformBufferObject::SunDirection.
    float4 SunDirection;
    // rgb is the light colour; w is 1 when the scene actually has that light, 0 otherwise, so
    // neither shader needs a branch or a separate flag.
    float4 SunColor;
    float4 SkyColor;
    uint ProxyIndex;
};
[[vk::push_constant]] CompatibilityPushConstants pushConstants;

// Preview weights, deliberately not radiance. SunIntensity / SkyIntensity are scaled against the
// sky IBL (a typical scene ships SkyIntensity = 100 to compensate for the small values in the HDR
// map), and this profile cannot sample that map -- so their magnitudes carry no meaning here. Only
// the light colours are used, and the result is normalised to land near 1 for a fully lit surface
// whichever lights the scene has. That keeps the preview readable and exposure-stable instead of
// pretending to match the full renderer.
static const float kSunWeight = 0.75f;
static const float kSkyWeight = 0.55f;

// Hue without magnitude.
float3 Tint(float3 color)
{
    return color / max(max(color.r, color.g), max(color.b, 1.0e-4f));
}

struct FragmentInput
{
    float4 position : SV_Position;
    float3 worldNormal : WORLD_NORMAL;
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
    const float3 albedo = material.Diffuse.rgb;
    const float3 N = normalize(input.worldNormal);

    // Same convention as NoAmbientShading: SunDirection points at the sun.
    const float3 L = normalize(pushConstants.SunDirection.xyz);
    const float NoL = max(dot(N, L), 0.0f);

    // Stand-in for the sky IBL: brightest for upward normals, floored so downward-facing surfaces
    // stay readable rather than going black.
    const float hemisphere = 0.25f + 0.75f * (0.5f + 0.5f * N.y);

    const float sunPresent = pushConstants.SunColor.w;
    const float skyPresent = pushConstants.SkyColor.w;
    const float3 sunTerm = Tint(pushConstants.SunColor.rgb) * (NoL * kSunWeight * sunPresent);
    const float3 skyTerm = Tint(pushConstants.SkyColor.rgb) * (hemisphere * kSkyWeight * skyPresent);

    const float weightSum = kSunWeight * sunPresent + kSkyWeight * skyPresent;
    // A scene with neither light falls back to flat base colour, which is what this pass drew
    // before lighting existed -- better than a black frame.
    const float3 lighting =
        (weightSum > 1.0e-4f) ? (sunTerm + skyTerm) / weightSum : float3(1.0f, 1.0f, 1.0f);

    output.color = float4(albedo * lighting, 1.0f);
    return output;
}
