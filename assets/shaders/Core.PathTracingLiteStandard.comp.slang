import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShaderClock.slang"
#include "common/PathTracingLiteShading.slang"
#include "common/ShadingBucket.slang"

// Standard shading bucket for PathTracingLite. Background and Emissive pixels are served by the
// shared Core.TracingBackground / Core.TracingEmissive kernels, so this one carries no miss and no
// emissive branch.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketStandard, Gid, GTid, pixel))
    {
        return;
    }
    ShadePathTracingLitePixel(pixel);
}
