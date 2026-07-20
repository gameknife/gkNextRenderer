import Shader.Bindless;
import Shader.GpuScene;
import Shader.Tonemap;
import Shader.Types;

struct PushConstants
{
    uint4 header;
    uint64_t cameraAddress;
};

[[vk::push_constant]] PushConstants push;

float3 EncodeSplatForOutput(float3 color, UniformBufferObject camera)
{
    color = saturate(color);
    if (!camera.HDR)
    {
        return color;
    }

    float3 tonemapInputMin = float3(0.0, 0.0, 0.0);
    float3 tonemapInputMax = float3(64.0, 64.0, 64.0);
    for (uint iteration = 0; iteration < 16; ++iteration)
    {
        const float3 mid = (tonemapInputMin + tonemapInputMax) * 0.5;
        const float3 mapped = GT_Tonemapping(mid);
        tonemapInputMin = select(mapped < color, mid, tonemapInputMin);
        tonemapInputMax = select(mapped < color, tonemapInputMax, mid);
    }

    const float3 sceneColor = tonemapInputMax * (40000.0 / max(camera.PaperWhiteNit, 1.0));
    return EncodeHdrOutput(sceneColor, camera.PaperWhiteNit, camera.HDROutputMode);
}

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

    UniformBufferObject* camera = (UniformBufferObject*)push.cameraAddress;
    const float3 splatColor = EncodeSplatForOutput(max(splat.rgb / alpha, 0.0), camera[0]) * alpha;
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel] =
        float4(splatColor + scene.rgb * oneMinusAlpha, scene.a);
}
