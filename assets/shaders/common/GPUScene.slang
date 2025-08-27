implementing Common;

[[vk::push_constant]] ConstantBuffer<GPUScene> gpuScene;
namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return gpuScene;
    }
}