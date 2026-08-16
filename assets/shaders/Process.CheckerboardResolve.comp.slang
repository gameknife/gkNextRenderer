import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;

// Checkerboard lighting reconstruction.
//
// Checkerboard rendering only exists on the Primary Surface path: Core.SurfaceBuild writes every
// surface-class RT at full rate, so the only channels this pass ever reconstructs are lighting.
// The old sets that neighbour-copied depth / normal / motion / object id are gone - a copied
// surface value is wrong data rather than approximate data, and it poisoned the temporal upscaler
// at every silhouette. Console checkerboard practice has always kept geometry full rate.

static const uint RESOLVE_TRACING = 1u;
static const uint RESOLVE_SOFTWARE_MODERN_NO_AMBIENT = 2u;

void CopyTracingLighting(int2 target, int2 source)
{
    // RT_ALBEDO is the compose demodulation guide (scene color = diffuse * albedo + specular) and
    // Core overrides it to white at sky and emissive pixels, so it travels with lighting. Every
    // other surface RT is dense from Core.SurfaceBuild and stays out of this pass.
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST)[source];
}

void CopySoftwareModernNoAmbientLighting(int2 target, int2 source)
{
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[source];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const GPUScene scene = Bindless.GetGpuscene();
    const uint totalFrames = scene.Camera[0].TotalFrames;
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    const uint2 target = Scheduler.MissingPixel(dispatchThreadId.xy, totalFrames);
    if (any(target >= size))
    {
        return;
    }

    const int2 targetPixel = int2(target);
    const int2 sourcePixel = int2(Scheduler.ReconstructionSource(target, size, totalFrames));
    const uint resolveSet = scene.CustomData1;

    // The Background and Emissive buckets shaded every pixel at full rate, so those pixels already
    // hold their exact lighting. Reconstructing them from a neighbour would replace a correct value
    // with an approximate one.
    const uint2 bsdf = Surface.LoadBsdf(scene.CustomData0, targetPixel);
    if (bsdf.x == Surface.kInvalidMaterialId ||
        Surface.HasFlag(Surface.UnpackFlags(bsdf.y), Surface.kFlagEmissive))
    {
        return;
    }

    if (resolveSet == RESOLVE_TRACING)
    {
        CopyTracingLighting(targetPixel, sourcePixel);
    }
    else if (resolveSet == RESOLVE_SOFTWARE_MODERN_NO_AMBIENT)
    {
        CopySoftwareModernNoAmbientLighting(targetPixel, sourcePixel);
    }
}
