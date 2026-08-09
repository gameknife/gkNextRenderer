import Shader.Bindless;
import Shader.GpuScene;

// Offline-only, non-reprojected running average. Realtime renderers bypass this pass and
// feed their current sample directly to Process.Compose. The accumulated value is already
// composed linear radiance, so progressive rendering does not need separate diffuse/specular
// histories.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint viewBase = Bindless.GetGpuscene().CustomData0;
    const uint previousSampleCount = Bindless.GetGpuscene().CustomData1;
    const uint targetSampleCount = Bindless.GetGpuscene().CustomData2;
    const uint2 size = Bindless.GetStorageTextureDimensions<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_SINGLE_DIFFUSE));
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const int2 pixel = int2(dispatchThreadId.xy);
    let singleDiffuse = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_SINGLE_DIFFUSE));
    let singleSpecular = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_SINGLE_SPECULAR));
    let albedo = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_ALBEDO));
    let progressiveSceneColor = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_PROGRESSIVE_SCENE_COLOR));

    if (previousSampleCount >= max(targetSampleCount, 1u))
    {
        return;
    }

    const float currentWeight = 1.0 / float(previousSampleCount + 1u);
    const float3 diffuse = singleDiffuse[pixel].rgb;
    const float3 specular = singleSpecular[pixel].rgb;
    const float3 currentSample = Bindless.GetGpuscene().Camera[0].DebugDraw_Lighting
        ? diffuse * 0.5 + specular
        : diffuse * albedo[pixel].rgb + specular;
    if (previousSampleCount == 0u)
    {
        progressiveSceneColor[pixel] = float4(currentSample, 1.0);
    }
    else
    {
        progressiveSceneColor[pixel] = float4(
            lerp(progressiveSceneColor[pixel].rgb, currentSample, currentWeight),
            float(previousSampleCount + 1u));
    }
}
