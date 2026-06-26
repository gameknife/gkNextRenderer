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
    renderer.ExitProbability = 0.05f;
    renderer.ExitAfterFirst = false;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.UseAmbientCubeTerminal = false;
    renderer.SecondaryDirectMode = 1;
    renderer.Init(DTid.xy);

    uint2 screenSize = renderer.GetSize();

    if (params.DebugMode == 3u)
    {
        renderer.WriteDebugColor(float4(SharcDebugOccupancy(uint2(DTid.xy), screenSize), 1));
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (params.DebugMode == 5u)
    {
        renderer.WriteDebugColor(float4(SharcDebugSampleStale(uint2(DTid.xy), screenSize), 1));
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (!renderer.PrimaryHit(rayCaster))
    {
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    if (params.DebugMode == 4u)
    {
        Vertex primaryVertex = renderer.GetPrimaryVertex();
        float4 cachedRadiance = float4(0, 0, 0, 1);
        bool hit = SharcQuery(primaryVertex.Position, primaryVertex.Normal, cachedRadiance);
        renderer.WriteDebugCachedRadiance(cachedRadiance, hit);
        END_SHADERCLOCK(DTid.xy)
        return;
    }

    FSharcQueryCache cache;
    cache.RoughnessMin = params.QueryRoughnessMin;
    cache.MinBounce = params.QueryMinBounce;
    cache.DebugMode = params.DebugMode;
    int sampleMultiplier = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[DTid.xy] > 0 ? 4 : 1;
    renderer.Render(tracer, dIlluminator, cache, sampleMultiplier);
    END_SHADERCLOCK(DTid.xy)
}
