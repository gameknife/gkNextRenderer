#pragma once

#include "Engine/Rendering/Upscaler/IUpscaler.hpp"

#include <functional>
#include <memory>

namespace Rendering::Upscaler
{
    using FUpscalerFactory = std::function<std::unique_ptr<IUpscaler>()>;

    // Registered by an upscaler module (e.g. NextStreamline) before renderer start.
    void RegisterUpscalerFactory(FUpscalerFactory factory);
    bool HasRegisteredUpscalerFactory();
    // Creates the registered upscaler; nullptr when no module registered one
    // (or the factory declined, e.g. hardware unsupported).
    std::unique_ptr<IUpscaler> CreateRegisteredUpscaler();
}
