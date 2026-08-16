import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const uint2 pixel = Scheduler.ResolveAnalyticPixel(
        DTid.xy, Scheduler.IsCheckerboardActive(Bindless.GetGpuscene().CustomData2),
        Bindless.GetGpuscene().Camera[0].TotalFrames);
    if (any(pixel >= size))
    {
        return;
    }
    START_SHADERCLOCK()

    // On the Primary Surface path the primary hit is replayed from the dense G-buffer
    // Core.SurfaceBuild produced; otherwise the visibility buffer is decoded inline.
    const bool surfacePath = Surface.IsPathActive(Bindless.GetGpuscene().CustomData2);
    FVisibilityBufferRayCaster visibilityRayCaster;
    FSurfacePrimaryRayCaster surfaceRayCaster;
    FSoftwareRayTracer tracer;
    FSoftwareTracingDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = true;//Bindless.GetGpuscene().Camera->ExitAfterFirst;
    renderer.HitNormalOffset = max(Bindless.GetGpuscene().Camera->AmbientCubeUnit, 0.0001f) * 0.5;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.EnableSoftTracingAdaptiveOffset = true;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(pixel);

    const bool restirActive = RestirIsAvailable() && Bindless.GetGpuscene().CustomData0 == 0;
    dIlluminator.RestirPrimary = restirActive;

    bool primaryHit;
    if (surfacePath)
    {
        primaryHit = renderer.PrimaryHit(surfaceRayCaster);
    }
    else
    {
        primaryHit = renderer.PrimaryHit(visibilityRayCaster);
    }

    if( !primaryHit )
    {
        if (restirActive)
        {
            RestirStoreEmpty(int2(pixel));
        }
        END_SHADERCLOCK(pixel)
        return;
    }

    dIlluminator.PrimaryInstanceId = renderer.GetPrimaryInstanceId();
    dIlluminator.PrimaryMotionPixels = renderer.GetPrimaryMotionPixels();
    if (restirActive)
    {
        Material primaryMat = Bindless.GetGpuscene().GetMaterial(renderer.GetPrimaryVertex().MaterialIndex);
        if (primaryMat.MaterialModel == MaterialDiffuseLight)
        {
            RestirStoreEmpty(int2(pixel));
        }
    }

    float4 residencyDebugColor;
    if (TryAmbientResidencyDebug(renderer.GetPrimaryVertex().Position, residencyDebugColor))
    {
        renderer.WriteDebugColor(residencyDebugColor);
        END_SHADERCLOCK(pixel)
        return;
    }

    FNullRadianceCache cache;
    renderer.Render(tracer, dIlluminator, cache, 1);
    END_SHADERCLOCK(pixel)
}
