#pragma once

#include <memory>

namespace Rendering::Upscaler
{
    class IUpscaler;
}

namespace Vulkan
{
    class IDeviceCreationAugmenter;
    class IVulkanSwapchainInterposer;
}

namespace FidelityFXWrapper
{
#if WITH_FIDELITYFX && WIN32
    Vulkan::IVulkanSwapchainInterposer& SwapchainInterposerInstance();
    Vulkan::IDeviceCreationAugmenter& DeviceAugmenterInstance();
#endif

    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateUpscaler();
}
