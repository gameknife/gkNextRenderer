#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextFidelityFX/NextFidelityFXModule.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Modules/NextFidelityFX/FidelityFXIntegration.hpp"

namespace Modules::NextFidelityFX
{
    void Install(const Runtime::Config::Options& options)
    {
#if WITH_FIDELITYFX && WIN32
        if (options.DisableFidelityFX)
        {
            SPDLOG_INFO("FidelityFX FSR plugins disabled for this application");
            return;
        }
        Vulkan::RegisterSwapchainInterposer(
            &FidelityFXWrapper::SwapchainInterposerInstance());
        Vulkan::RegisterDeviceCreationAugmenter(&FidelityFXWrapper::DeviceAugmenterInstance());
        Rendering::Upscaler::RegisterUpscalerFactory(
            [] { return FidelityFXWrapper::CreateUpscaler(); });
        SPDLOG_INFO("FidelityFX FSR 3.1 Vulkan provider installed alongside other upscalers");
#else
        (void)options;
#endif
    }
}
