import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    FVisibilityBufferRayCaster rayCaster;
    FAmbientCubeRayTracer tracer;
    FAmbientCubeIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = true;
    renderer.HitNormalOffset =  max(Bindless.GetGpuscene().Camera->AmbientCubeUnit, 0.0001f) * 0.5;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = true;
    renderer.ForceExitAfterFirst = true;
    renderer.Init(DTid.xy);

    if( !renderer.PrimaryHit(rayCaster) )
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    renderer.Render(tracer, dIlluminator, 1);
    END_SHADERCLOCK(DTid.xy)
}