import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    FVisibilityBufferRayCaster rayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    SharcRuntimeParameters params = SharcGetRuntimeParameters();
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = false;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.UseAmbientCubeTerminal = false;
    renderer.EnableSharcQuery = true;
    renderer.SharcDebugMode = params.DebugMode;
    renderer.SharcQueryRoughnessMin = params.QueryRoughnessMin;
    renderer.SharcQueryMinBounce = params.QueryMinBounce;
    renderer.Init(DTid.xy);

    if (renderer.SharcDebugMode == 3u)
    {
        renderer.WriteSharcDebugOccupancy();
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (renderer.SharcDebugMode == 5u)
    {
        renderer.WriteSharcDebugSampleStale();
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (!renderer.PrimaryHit(rayCaster))
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (renderer.SharcDebugMode == 4u)
    {
        renderer.WriteSharcDebugRadiance();
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    int sampleMultiplier = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[DTid.xy] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, sampleMultiplier);
    END_SHADERCLOCK(DTid.xy)
}
