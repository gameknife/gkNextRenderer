import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShaderClock.slang"
#include "common/SwModernShading.slang"
#include "common/ShadingBucket.slang"

// Standard shading bucket for SoftwareModern.
//
// Every lane in this dispatch owns a pixel that the classification proved is a real, non-emissive
// surface, so a wave no longer mixes path-traced pixels with sky pixels that idle through the whole
// trace. That coherence is the point here: the terminal writes themselves are trivial, but a heavy
// kernel pays for every divergent lane in it.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketStandard, Gid, GTid, pixel))
    {
        return;
    }
    ShadeSwModernPixel(pixel);
}
