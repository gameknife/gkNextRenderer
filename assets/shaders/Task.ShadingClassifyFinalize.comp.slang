import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.ShadingScheduler;

// Turns the shading classification's append cursors into dispatch arguments.
//
// Classification uses one atomic per tile per bucket, and that atomic increments the indirect
// dispatch's groupCountX directly - which is why it can afford to be the only atomic. The tile
// count is therefore only known once every classification workgroup has retired, so publishing it
// and halving the group count for the compacted bucket has to happen in a separate pass. One
// workgroup, one thread per bucket.

[shader("compute")]
[numthreads(Scheduler.kBucketCount, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const GPUScene scene = Bindless.GetGpuscene();
    if (DTid.x >= Scheduler.kBucketCount)
    {
        return;
    }
    Scheduler.FinalizeIndirect(scene.ReservedAddress0, DTid.x, scene.CustomData2);
}
