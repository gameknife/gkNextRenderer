implementing Common;

[[vk::binding(0, 1)]]
StructuredBuffer<GPUScene> GPUSceneBuffer;

namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return GPUSceneBuffer[0];
    }
}
