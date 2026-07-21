#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTemporalUpscaler/NextTemporalUpscalerModule.hpp"

#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"
#include "Modules/NextTemporalUpscaler/TemporalUpscaler.hpp"

namespace Modules::NextTemporalUpscaler
{
    void Install(const Runtime::Config::Options&)
    {
        Rendering::Upscaler::RegisterUpscalerFactory(
            [] { return CreateTemporalUpscaler(); });
        SPDLOG_INFO("Native temporal compute upscaler provider installed");
    }
}
