#include "Engine/Rendering/Interface/AtmosphereSubsystem.hpp"

namespace Rendering::Atmosphere
{
    namespace
    {
        AtmosphereSubsystemFactory registeredFactory = nullptr;
    }

    void RegisterAtmosphereSubsystemFactory(const AtmosphereSubsystemFactory factory)
    {
        registeredFactory = factory;
    }

    std::unique_ptr<IAtmosphereSubsystem> CreateRegisteredAtmosphereSubsystem(
        Vulkan::VulkanBaseRenderer& renderer)
    {
        return registeredFactory ? registeredFactory(renderer) : nullptr;
    }
}
