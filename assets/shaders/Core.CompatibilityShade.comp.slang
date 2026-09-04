// Minimal fixed-descriptor compute shading for ERT_Compatibility. Do not import Shader.Bindless
// or Shader.GpuScene: both pull in resource contracts the constrained profile cannot create.

[[vk::binding(0, 0)]] Texture2D<float4> GBufferAlbedo;
[[vk::binding(1, 0)]] Texture2D<float4> GBufferNormal;
// A known format avoids requiring shaderStorageImage*WithoutFormat on an A12X-class device.
[[vk::binding(2, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> SceneColor;

struct CompatibilityShadePushConstants
{
    // .xyz points toward the sun. w in the color fields says whether that source exists.
    float4 SunDirection;
    float4 SunColor;
    float4 SkyColor;
};
[[vk::push_constant]] CompatibilityShadePushConstants pushConstants;

static const float3 kBackgroundColor = float3(0.05f, 0.05f, 0.06f);
static const float kSunWeight = 0.75f;
static const float kSkyWeight = 0.55f;

float3 Tint(float3 color)
{
    return color / max(max(color.r, color.g), max(color.b, 1.0e-4f));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    SceneColor.GetDimensions(width, height);
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= uint2(width, height)))
    {
        return;
    }

    const float4 packedNormal = GBufferNormal.Load(int3(pixel, 0));
    if (packedNormal.a < 0.5f)
    {
        SceneColor[pixel] = float4(kBackgroundColor, 1.0f);
        return;
    }

    const float3 albedo = GBufferAlbedo.Load(int3(pixel, 0)).rgb;
    const float3 normal = normalize(packedNormal.xyz * 2.0f - 1.0f);
    const float3 lightDirection = normalize(pushConstants.SunDirection.xyz);
    const float noL = max(dot(normal, lightDirection), 0.0f);
    const float hemisphere = 0.25f + 0.75f * (0.5f + 0.5f * normal.y);

    const float sunPresent = pushConstants.SunColor.w;
    const float skyPresent = pushConstants.SkyColor.w;
    const float3 sunTerm = Tint(pushConstants.SunColor.rgb) * (noL * kSunWeight * sunPresent);
    const float3 skyTerm = Tint(pushConstants.SkyColor.rgb) * (hemisphere * kSkyWeight * skyPresent);
    const float weightSum = kSunWeight * sunPresent + kSkyWeight * skyPresent;
    const float3 lighting = weightSum > 1.0e-4f
        ? (sunTerm + skyTerm) / weightSum
        : float3(1.0f, 1.0f, 1.0f);
    SceneColor[pixel] = float4(albedo * lighting, 1.0f);
}
