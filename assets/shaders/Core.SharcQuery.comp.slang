import Shader.Bindless;
import Shader.PathTracing;
#include "common/ShaderClock.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    if (any(DTid.xy >= size))
    {
        return;
    }
    START_SHADERCLOCK()

    FHardwarePrimaryRayCaster rayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    SharcRuntimeParameters params = SharcGetRuntimeParameters();
    renderer.ExitProbability = 0.01f;
    renderer.ExitAfterFirst = false;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.UseAmbientCubeTerminal = false;
    renderer.SecondaryDirectMode = 1;
    renderer.Init(DTid.xy);

    // ReSTIR is primary-view-only (see Core.PathTracing.comp.slang).
    const bool restirActive = RestirIsAvailable() && Bindless.GetGpuscene().CustomData0 == 0;
    dIlluminator.RestirPrimary = restirActive;

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
        if (restirActive)
        {
            RestirStoreEmpty(int2(DTid.xy));
        }
        END_SHADERCLOCK(DTid.xy)
        return;
    }
    // Primary-hit data for the ReSTIR temporal gates (see Core.PathTracing.comp.slang).
    dIlluminator.PrimaryInstanceId = renderer.GetPrimaryInstanceId();
    dIlluminator.PrimaryMotionPixels = renderer.GetPrimaryMotionPixels();
    if (restirActive)
    {
        // Emitter primaries early-out of Render before the gather step (see Core.PathTracing).
        Material primaryMat = Bindless.GetGpuscene().GetMaterial(renderer.GetPrimaryVertex().MaterialIndex);
        if (primaryMat.MaterialModel == MaterialDiffuseLight)
        {
            RestirStoreEmpty(int2(DTid.xy));
        }
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
