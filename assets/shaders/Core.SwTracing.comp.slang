import Shader.Bindless;
import Shader.PathTracing;
#include "common/ShaderClock.slang"
#include "common/CheckerboardRendering.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const uint2 pixel = ResolveShadingPixel(DTid.xy);
    if (any(pixel >= size))
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
    renderer.Init(pixel);

    const bool restirActive = RestirIsAvailable() && Bindless.GetGpuscene().CustomData0 == 0;
    dIlluminator.RestirPrimary = restirActive;

    if( !renderer.PrimaryHit(rayCaster) )
    {
        if (restirActive)
        {
            RestirStoreEmpty(int2(pixel));
        }
        END_SHADERCLOCK(pixel)
        return;
    }

    dIlluminator.PrimaryInstanceId = renderer.GetPrimaryInstanceId();
    dIlluminator.PrimaryMotionPixels = renderer.GetPrimaryMotionPixels();
    if (restirActive)
    {
        Material primaryMat = Bindless.GetGpuscene().GetMaterial(renderer.GetPrimaryVertex().MaterialIndex);
        if (primaryMat.MaterialModel == MaterialDiffuseLight)
        {
            RestirStoreEmpty(int2(pixel));
        }
    }

    float4 residencyDebugColor;
    if (TryAmbientResidencyDebug(renderer.GetPrimaryVertex().Position, residencyDebugColor))
    {
        renderer.WriteDebugColor(residencyDebugColor);
        END_SHADERCLOCK(pixel)
        return;
    }

    FNullRadianceCache cache;
    renderer.Render(tracer, dIlluminator, cache, 1);
    END_SHADERCLOCK(pixel)
}
