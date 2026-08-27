import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShadingBucket.slang"
#include "common/TracingTerminalBucket.slang"

// Background shading bucket shared by SoftwareModern and SoftwareTracing: the pixels the primary
// ray missed. Renderer-independent, because a sky pixel does not depend on which tracer would have
// shaded the surfaces around it.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    ShadeTracingBackgroundPixel(Gid, GTid);
}
