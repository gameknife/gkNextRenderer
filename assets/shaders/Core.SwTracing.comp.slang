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
    FSoftwareRayTracer tracer;
    FSoftwareTracingDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = Bindless.GetGpuscene().Camera->ExitAfterFirst;
    renderer.HitNormalOffset = max(Bindless.GetGpuscene().Camera->AmbientCubeUnit, 0.0001f) * 0.5;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = false;
    renderer.ForceExitAfterFirst = false;
    renderer.EnableSoftTracingAdaptiveOffset = true;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(DTid.xy);

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

    dIlluminator.PrimaryInstanceId = renderer.GetPrimaryInstanceId();
    dIlluminator.PrimaryMotionPixels = renderer.GetPrimaryMotionPixels();
    if (restirActive)
    {
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
    renderer.Render(tracer, dIlluminator, cache, 1);
    END_SHADERCLOCK(DTid.xy)
}
