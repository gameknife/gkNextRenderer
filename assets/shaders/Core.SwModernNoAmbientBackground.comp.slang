import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/PreProcessor.slang"
#include "common/NoAmbientShading.slang"
#include "common/ShadingBucket.slang"

// Background shading bucket for SoftwareModernNoAmbient: the pixels the primary ray missed.
// Always full rate - the kernel is a sky sample and a store, so a checkerboard rate would save
// nothing and would force the resolve to reconstruct data that is already exact.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;

    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketBackground, Gid, GTid, pixel))
    {
        return;
    }

    const uint2 size = Surface.Dimensions(viewBase);
    const float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0 - 1.0;
    const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    const float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

    const FNoAmbientShadingOutput lighting = ShadeNoAmbientBackground(normalize(rayDir4.xyz));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, lighting.Diffuse);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, lighting.Ambient);
}
