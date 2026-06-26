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
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = Bindless.GetGpuscene().Camera->FastGather;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(DTid.xy);

    if( !renderer.PrimaryHit(rayCaster) )
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    float4 residencyDebugColor;
    if (TryAmbientResidencyDebug(renderer.GetPrimaryVertex().Position, residencyDebugColor))
    {
        renderer.WriteDebugColor(residencyDebugColor);
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    FNullRadianceCache cache;
    int sampleMultiplier = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[DTid.xy] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, cache, sampleMultiplier);
    END_SHADERCLOCK(DTid.xy)
}
