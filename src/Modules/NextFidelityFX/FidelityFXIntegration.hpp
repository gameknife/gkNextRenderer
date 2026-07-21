#pragma once

#include <memory>

namespace Rendering::Upscaler
{
    class IUpscaler;
}

namespace Vulkan
{
    class IDeviceCreationAugmenter;
    class IVulkanInterposer;
}

namespace FidelityFXWrapper
{
#if WITH_FIDELITYFX && WIN32
    Vulkan::IVulkanInterposer& InterposerInstance();
    Vulkan::IDeviceCreationAugmenter& DeviceAugmenterInstance();
#endif

    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateUpscaler();
}
