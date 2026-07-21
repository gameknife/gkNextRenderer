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

    FVisibilityBufferRayCaster rayCaster;
    FHardwareRayTracer tracer;
    FHardwareDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = Bindless.GetGpuscene().Camera->ExitAfterFirst;
    renderer.HitNormalOffset = 0.001f * Bindless.GetGpuscene().Camera->SceneEpsilonScale;
    renderer.SampleDownscale = 1;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(DTid.xy);

    // ReSTIR is primary-view-only: reservoirs are sized for the primary extent, so the
    // recorded view bank base gates every non-primary view off the buffers.
    const bool restirActive = RestirIsAvailable() && Bindless.GetGpuscene().CustomData0 == 0;
    dIlluminator.RestirPrimary = restirActive;

    if( !renderer.PrimaryHit(rayCaster) )
    {
        if (restirActive)
        {
            RestirStoreEmpty(int2(DTid.xy));
        }
        END_SHADERCLOCK(DTid.xy)
        return;
    }
    // Primary-hit data for the ReSTIR temporal gates must come from renderer state, not
    // from reading back the just-written G-buffer (not coherent within the invocation).
    dIlluminator.PrimaryInstanceId = renderer.GetPrimaryInstanceId();
    dIlluminator.PrimaryMotionPixels = renderer.GetPrimaryMotionPixels();
    if (restirActive)
    {
        // Emitter primaries early-out of Render before the gather step runs, so their
        // intermediate reservoir must be cleared here or the spatial pass reads stale data.
        Material primaryMat = Bindless.GetGpuscene().GetMaterial(renderer.GetPrimaryVertex().MaterialIndex);
        if (primaryMat.MaterialModel == MaterialDiffuseLight)
        {
            RestirStoreEmpty(int2(DTid.xy));
        }
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
