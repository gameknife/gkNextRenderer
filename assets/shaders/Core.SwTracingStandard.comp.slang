import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShaderClock.slang"
#include "common/SwTracingShading.slang"
#include "common/ShadingBucket.slang"

// Standard shading bucket for SoftwareTracing. See Core.SwModernStandard for why the coherence
// matters more than the branch removal on these renderers.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketStandard, Gid, GTid, pixel))
    {
        return;
    }
    ShadeSwTracingPixel(pixel);
}
