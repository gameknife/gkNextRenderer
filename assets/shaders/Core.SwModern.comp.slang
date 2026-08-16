import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShaderClock.slang"
#include "common/SwModernShading.slang"

// SoftwareModern Core Shading, analytic allocation: one full-screen dispatch, with checkerboard
// expressed as a compacted dispatch width. The scheduler path uses the same kernel body through
// Core.SwModernStandard.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const uint2 pixel = Scheduler.ResolveAnalyticPixel(
        DTid.xy, Scheduler.IsCheckerboardActive(Bindless.GetGpuscene().CustomData2),
        Bindless.GetGpuscene().Camera[0].TotalFrames);
    if (any(pixel >= size))
    {
        return;
    }
    ShadeSwModernPixel(int2(pixel));
}
