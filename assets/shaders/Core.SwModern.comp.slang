import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    FVisibilityBufferRayCasterV2 rayCaster;
    FAmbientCubeRayTracerV2 tracer;
    FAmbientCubeIlluminatorV2 dIlluminator;

    FPathTracingRendererV2 renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = true;
    renderer.HitNormalOffset = 0.1f;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = true;
    renderer.Init(DTid.xy);

    if( !renderer.PrimaryHit(rayCaster) )
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    renderer.Render(tracer, dIlluminator, 1);
    END_SHADERCLOCK(DTid.xy)
}