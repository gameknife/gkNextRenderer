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
    SharcParameters params = SharcGetParameters();
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = false;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.EnableSharcUpdate = true;
    renderer.SharcUpdateSampleRatio = params.UpdateSampleRatio;
    renderer.Init(DTid.xy);

    if (!renderer.PrimaryHit(rayCaster))
    {
        return;
    }

    renderer.Render(tracer, dIlluminator, 1);
}

