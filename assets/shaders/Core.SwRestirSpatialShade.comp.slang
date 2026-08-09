import Shader.Bindless;
import Shader.PathTracing;
#include "common/PreProcessor.slang"

// Software ReSTIR DI pass 2. No TLAS descriptor is required; visibility uses finite
// voxel-DDA segments through FSoftwareRayTracer.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    FSoftwareRayTracer tracer;
    RestirSpatialShade(dispatchThreadId, tracer);
}
