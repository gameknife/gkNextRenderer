#pragma once

#include "Engine/Rendering/Upscaler/IUpscaler.hpp"

#include <functional>
#include <memory>

namespace Rendering::Upscaler
{
    using FUpscalerFactory = std::function<std::unique_ptr<IUpscaler>()>;

    // Registered by upscaler modules before renderer start. Multiple independent
    // providers may coexist (for example an SDK provider plus NativeTemporal).
    void RegisterUpscalerFactory(FUpscalerFactory factory);
    // Creates a composite over every provider that accepted creation.
    std::unique_ptr<IUpscaler> CreateRegisteredUpscaler();
}
