#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTemporalUpscaler/NextTemporalUpscalerModule.hpp"

#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"
#include "Modules/NextTemporalUpscaler/SGSR2Upscaler.hpp"
#include "Modules/NextTemporalUpscaler/TemporalUpscaler.hpp"

namespace Modules::NextTemporalUpscaler
{
    void Install(const Runtime::Config::Options&)
    {
        Rendering::Upscaler::RegisterUpscalerFactory(
            [] { return CreateTemporalUpscaler(); });
        Rendering::Upscaler::RegisterUpscalerFactory(
            [] { return CreateSGSR2Upscaler(); });
        SPDLOG_INFO("Native TAAU and Snapdragon GSR 2 compute upscaler providers installed");
    }
}
