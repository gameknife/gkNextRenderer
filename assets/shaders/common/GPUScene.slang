implementing Common;

[[vk::push_constant]] GPUScene gpuScene;
namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return gpuScene;
    }
}