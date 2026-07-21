#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"

namespace Rendering::Upscaler
{
    namespace
    {
        FUpscalerFactory& Factory()
        {
            static FUpscalerFactory factory;
            return factory;
        }
    }

    void RegisterUpscalerFactory(FUpscalerFactory factory)
    {
        Factory() = std::move(factory);
    }

    bool HasRegisteredUpscalerFactory()
    {
        return static_cast<bool>(Factory());
    }

    std::unique_ptr<IUpscaler> CreateRegisteredUpscaler()
    {
        return Factory() ? Factory()() : nullptr;
    }
}
