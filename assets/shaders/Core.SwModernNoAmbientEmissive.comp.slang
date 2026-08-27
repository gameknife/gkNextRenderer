import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/PreProcessor.slang"
#include "common/NoAmbientShading.slang"
#include "common/ShadingBucket.slang"

// Emissive shading bucket for SoftwareModernNoAmbient. Emitted radiance is absolute, so this
// kernel reads one plane of the surface and stores. Full rate, for the same reason as Background.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint viewBase = Bindless.GetGpuscene().CustomData0;

    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketEmissive, Gid, GTid, pixel))
    {
        return;
    }

    const FNoAmbientShadingOutput lighting =
        ShadeNoAmbientEmissive(Surface.LoadAlbedo(viewBase, pixel));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, lighting.Diffuse);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, lighting.Ambient);
}
