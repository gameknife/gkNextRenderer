import Shader.Bindless;
import Shader.PathTracing;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    if (any(DTid.xy >= size))
    {
        return;
    }
    FVisibilityBufferRayCaster rayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;
    // The update pass must keep the classic single-sample estimator: reservoir samples are
    // correlated and would corrupt SHARC temporal accumulation (and reservoirs may not exist).
    dIlluminator.RestirPrimary = false;

    FPathTracingRenderer renderer;
    SharcRuntimeParameters params = SharcGetRuntimeParameters();
    renderer.ExitProbability = 0.05f;
    renderer.ExitAfterFirst = false;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.UseAmbientCubeTerminal = false;
    renderer.WriteOutputs = false;
    renderer.SecondaryDirectMode = 2;
    renderer.Init(DTid.xy);

    if (!renderer.PrimaryHit(rayCaster))
    {
        return;
    }

    FSharcUpdateCache cache;
    cache.UpdateSampleRatio = params.UpdateSampleRatio;
    renderer.Render(tracer, dIlluminator, cache, 1);
}
