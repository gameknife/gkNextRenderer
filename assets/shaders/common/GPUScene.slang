[[vk::push_constant]] GPUScene gpuScene;
namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return gpuScene;
    }

    // Multi-viewport: access a screen-space RT in the active RenderView's bank. Resolves the
    // logical slot through ViewRT(gpuScene.CustomData0, rtSlot); primary view (base 0) is
    // bit-identical to the legacy absolute layout. Use this for per-view / history RTs; keep
    // GetStorageTexture (absolute) for global slots (swapchain / remote-encode / temp).
    public RWTexture2D<T> GetViewStorageTexture<T : ITexelElement>(int rtSlot)
    {
        return GetStorageTexture<T>(ViewRT(gpuScene.CustomData0, rtSlot));
    }

    public uint2 GetViewStorageTextureDimensions<T : ITexelElement>(int rtSlot)
    {
        return GetStorageTextureDimensions<T>(ViewRT(gpuScene.CustomData0, rtSlot));
    }
}
