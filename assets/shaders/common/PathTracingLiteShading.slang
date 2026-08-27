#ifndef GK_NEXT_PATH_TRACING_LITE_SHADING_SLANG
#define GK_NEXT_PATH_TRACING_LITE_SHADING_SLANG

// PathTracingLite Core Shading body, used by the scheduler's Standard bucket.
//
// Same shape as the software renderers: the primary hit is replayed from the dense G-buffer
// Core.SurfaceBuild produced, and only the secondary rays are hardware traced. The hardware primary
// caster (aperture / depth of field) is deliberately not used here, which is what makes the surface
// replay legal for this renderer.
//
// Requires: Shader.PathTracing, Shader.SurfaceBuffer and common/ShaderClock.slang.

void ShadePathTracingLitePixel(int2 pixel)
{
    START_SHADERCLOCK()

    FSurfacePrimaryRayCaster surfaceRayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = true;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(uint2(pixel));

    // ReSTIR stays off on this renderer: the scheduler owns GPUScene.ReservedAddress0 and
    // CustomData1, which is exactly where the tracing extras table (ReSTIR / SHARC) would live.
    // Setting dIlluminator.RestirPrimary here would dereference the tile buffer as a reservoir
    // table. See docs/designs/visibility-surface-gbuffer-shading-scheduler.md.

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
    // Moving pixels get more samples. The moment counter is Core.SurfaceBuild's output now, so this
    // is a plain cross-pass read instead of a read-back of what this invocation just wrote.
    const int sampleMultiplier =
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[pixel] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, cache, sampleMultiplier);
    END_SHADERCLOCK(pixel)
}

#endif
