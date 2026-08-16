#ifndef GK_NEXT_SW_TRACING_SHADING_SLANG
#define GK_NEXT_SW_TRACING_SHADING_SLANG

// SoftwareTracing Core Shading body, shared by the analytic full-screen entry and the scheduler's
// Standard bucket. Keeping one copy is what stops the two allocations from drifting apart.
//
// Requires: Shader.PathTracing, Shader.SurfaceBuffer and common/ShaderClock.slang.

void ShadeSwTracingPixel(int2 pixel)
{
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
    renderer.Init(uint2(pixel));

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
        END_SHADERCLOCK(pixel)
        return;
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

#endif
