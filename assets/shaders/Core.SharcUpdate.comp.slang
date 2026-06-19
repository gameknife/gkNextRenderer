import Common;
import Bindless;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FVisibilityBufferRayCaster rayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;

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
