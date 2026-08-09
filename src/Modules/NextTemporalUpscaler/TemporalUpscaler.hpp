#pragma once

#include <memory>

namespace Rendering::Upscaler
{
    class IUpscaler;
}

namespace Modules::NextTemporalUpscaler
{
    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateTemporalUpscaler();
}
