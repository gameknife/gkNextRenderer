import Shader.Bindless;
import Shader.PathTracing;
#include "common/PreProcessor.slang"

// Hardware ReSTIR DI pass 2. The shared algorithm lives in
// common/RestirSpatialShade.slang; this entry only selects ray-query visibility.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    FHardwareRayTracer tracer;
    RestirSpatialShade(dispatchThreadId, tracer);
}
