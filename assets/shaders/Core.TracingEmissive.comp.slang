import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/ShadingBucket.slang"
#include "common/TracingTerminalBucket.slang"

// Emissive shading bucket shared by SoftwareModern and SoftwareTracing. Emitted radiance is
// absolute, so like Background this is renderer-independent.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    ShadeTracingEmissivePixel(Gid, GTid);
}
