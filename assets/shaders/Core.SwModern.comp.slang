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
    FAmbientCubeRayTracerIndirect tracer;
    FShadowMapDirectIlluminator dIlluminator;

    FPathTracingRenderer renderer;
    renderer.ExitProbability = 0.5f;
    renderer.ExitAfterFirst = true;
    renderer.HitNormalOffset =  max(Bindless.GetGpuscene().Camera->AmbientCubeUnit, 0.0001f) * 0.5;
    renderer.SampleDownscale = 2;
    renderer.FuzzyTracing = true;
    renderer.ForceExitAfterFirst = true;
    renderer.SecondaryDirectMode = 0;
    renderer.Init(pixel);

    if( !renderer.PrimaryHit(rayCaster) )
    {
        END_SHADERCLOCK(pixel)
        return;
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
