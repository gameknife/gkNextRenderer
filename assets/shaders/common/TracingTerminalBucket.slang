#ifndef GK_NEXT_TRACING_TERMINAL_BUCKET_SLANG
#define GK_NEXT_TRACING_TERMINAL_BUCKET_SLANG

// Background / Emissive shading for the tracing renderers.
//
// These mirror the two terminal branches of FPathTracingRenderer.PrimaryHit, which stays the
// authority on what a sky or emitter pixel writes - change one and change the other. They are
// written out longhand rather than by calling PrimaryHit because that path costs a full
// Surface.Load (eight planes) plus eleven RWTexture handles to produce four stores, and as a
// separate dispatch there is no heavy neighbour work left to hide that behind.
//
// Both buckets are full rate: a handful of stores, so a checkerboard rate would save nothing and
// would force the resolve to reconstruct values that are already exact.
//
// Requires: Shader.PathTracing, Shader.SurfaceBuffer, Shader.ShadingScheduler and
// common/ShadingBucket.slang.

void ShadeTracingBackgroundPixel(uint3 groupId, uint3 groupThreadId)
{
    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketBackground, groupId, groupThreadId, pixel))
    {
        return;
    }

    let Camera = Bindless.GetGpuscene().Camera[0];
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    const float3 rayDir =
        normalize(mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0)).xyz);

    float4 skyColor = Common.SampleVisibleBackground(rayDir);
    if (Camera.BackgroundMode != BACKGROUND_MODE_STUDIO)
    {
        skyColor.rgb += Common.EvaluateAnalyticSunDisk(rayDir);
    }

    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(pixel, float4(0, 0, 0, 0));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST).Store(pixel, 1000);
    Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST).Store(pixel, 1000);
    // RT_ALBEDO is the compose demodulation guide, so the background has to read white even though
    // Core.SurfaceBuild stored a surface albedo for it.
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO).Store(pixel, float4(1, 1, 1, 1));
}

void ShadeTracingEmissivePixel(uint3 groupId, uint3 groupThreadId)
{
    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketEmissive, groupId, groupThreadId, pixel))
    {
        return;
    }

    const float4 emitted = Surface.LoadAlbedo(Bindless.GetGpuscene().CustomData0, pixel);

    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, emitted);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(pixel, float4(0, 0, 0, 0));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST).Store(pixel, 10);
    Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST).Store(pixel, 10);
    // Emitted radiance is absolute, so the demodulation guide must be white or compose would
    // multiply the emitter by its own base color.
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO).Store(pixel, float4(1, 1, 1, 0));
}

#endif
