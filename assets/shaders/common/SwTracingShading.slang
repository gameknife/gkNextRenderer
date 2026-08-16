#ifndef GK_NEXT_SW_TRACING_SHADING_SLANG
#define GK_NEXT_SW_TRACING_SHADING_SLANG

// SoftwareTracing Core Shading body, used by the scheduler's Standard bucket.
//
// Requires: Shader.PathTracing, Shader.SurfaceBuffer and common/ShaderClock.slang.

void ShadeSwTracingPixel(int2 pixel)
{
    START_SHADERCLOCK()

    // The primary hit is replayed from the dense G-buffer Core.SurfaceBuild produced; nothing on
    // this renderer decodes the visibility buffer per pixel any more.
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

    if (!renderer.PrimaryHit(surfaceRayCaster))
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
