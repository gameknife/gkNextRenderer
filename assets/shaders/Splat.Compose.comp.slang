import Bindless;
import Common;

struct PushConstants
{
    uint reserved;
};

[[vk::push_constant]] PushConstants push;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_DENOISED);
    if (any(pixel >= size)) return;

    const float4 splat = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPLAT_ACCUM)[pixel];
    const float4 scene = Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED)[pixel];
    const float oneMinusAlpha = 1.0 - saturate(splat.a);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED)[pixel] =
        float4(splat.rgb + scene.rgb * oneMinusAlpha, scene.a);
}
