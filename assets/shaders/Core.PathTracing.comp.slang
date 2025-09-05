import Common;
import Bindless;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    FVisibilityBufferRayCasterV2 rayCaster;
    FHardwareRayTracerV2 tracer;
    FHardwareDirectIlluminatorV2 dIlluminator;

    FPathTracingRendererV2 renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = Bindless.GetGpuscene().Camera->FastGather;
    renderer.HitNormalOffset = 0.001f;
    renderer.SampleDownscale = 1;
    renderer.Init(DTid.xy);

    if( !renderer.PrimaryHit(rayCaster) )
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    int sampleMultiplier = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[DTid.xy] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, sampleMultiplier);
    END_SHADERCLOCK(DTid.xy)
}