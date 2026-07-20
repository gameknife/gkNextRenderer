import Shader.Bindless;

struct PushConsts
{
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint PreviousSampleCount;
    uint TargetSampleCount;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

// Offline-only, non-reprojected running average. Realtime renderers bypass this pass and
// feed their current sample directly to Process.Compose.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint viewBase = pushConsts._ViewHdr_CustomData0;
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
    let progressiveDiffuse = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_PROGRESSIVE_DIFFUSE));
    let progressiveSpecular = Bindless.GetStorageTexture<float4>(
        Bindless.ViewRT(viewBase, Bindless.RT_PROGRESSIVE_SPECULAR));

    if (pushConsts.PreviousSampleCount >= max(pushConsts.TargetSampleCount, 1u))
    {
        return;
    }

    const float currentWeight = 1.0 / float(pushConsts.PreviousSampleCount + 1u);
    if (pushConsts.PreviousSampleCount == 0u)
    {
        progressiveDiffuse[pixel] = singleDiffuse[pixel];
        progressiveSpecular[pixel] = singleSpecular[pixel];
    }
    else
    {
        progressiveDiffuse[pixel] = float4(
            lerp(progressiveDiffuse[pixel].rgb, singleDiffuse[pixel].rgb, currentWeight),
            float(pushConsts.PreviousSampleCount + 1u));
        progressiveSpecular[pixel] = float4(
            lerp(progressiveSpecular[pixel].rgb, singleSpecular[pixel].rgb, currentWeight),
            float(pushConsts.PreviousSampleCount + 1u));
    }
}
