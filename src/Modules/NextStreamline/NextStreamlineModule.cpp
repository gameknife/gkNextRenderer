#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextStreamline/NextStreamlineModule.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Modules/NextStreamline/StreamlineIntegration.hpp"

namespace Modules::NextStreamline
{
    void Install(const Runtime::Config::Options& options)
    {
#if WITH_STREAMLINE && WIN32
        if (options.DisableStreamline)
        {
            SPDLOG_INFO("Streamline DLSS plugins disabled for this application");
            return;
        }
        if (!StreamlineWrapper::ShouldInitialize())
        {
            SPDLOG_INFO("Streamline DLSS plugins disabled because no NVIDIA adapter is present");
            return;
        }

        StreamlineWrapper::Initialize();
        if (!StreamlineWrapper::IsInitialized())
        {
            return;
        }

        Vulkan::SetInterposer(&StreamlineWrapper::InterposerInstance());
        Vulkan::RegisterDeviceCreationAugmenter(&StreamlineWrapper::DeviceAugmenterInstance());
        Rendering::Upscaler::RegisterUpscalerFactory(
            [] { return Rendering::Upscaler::CreateStreamlineUpscaler(); });
#endif
    }
}
