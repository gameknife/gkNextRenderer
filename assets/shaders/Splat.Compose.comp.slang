import Shader.Bindless;
import Shader.GpuScene;

struct PushConstants
{
    uint4 header;
    uint64_t cameraAddress;
};

[[vk::push_constant]] PushConstants push;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(pixel >= size)) return;

    const float4 splat = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPLAT_ACCUM)[pixel];
    const float4 scene = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel];
    const float alpha = saturate(splat.a);
    const float oneMinusAlpha = 1.0 - alpha;
    if (alpha <= 1e-5)
    {
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel] =
            float4(scene.rgb, scene.a);
        return;
    }

    // RT_SCENE_COLOR is linear scene-referred color. Display encoding and tone
    // mapping happen later, after the optional upscaler.
    const float outputAlpha = alpha + scene.a * oneMinusAlpha;
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel] =
        float4(max(splat.rgb, 0.0) + scene.rgb * oneMinusAlpha, outputAlpha);
}
