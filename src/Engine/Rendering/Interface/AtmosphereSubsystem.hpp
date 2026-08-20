#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

namespace Vulkan
{
    class VulkanBaseRenderer;
    struct FRendererContract;
}

namespace Rendering::Atmosphere
{
    class IAtmosphereSubsystem
    {
    public:
        virtual ~IAtmosphereSubsystem() = default;

        virtual void CreateDeviceResources() = 0;
        virtual void CreateSwapChainPipelines() = 0;
        virtual void DestroySwapChainPipelines() = 0;
        virtual void SyncRuntimeResources(bool deviceIsIdle = false) = 0;
        virtual void BeginSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;
        virtual void PrepareView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
                                 const Vulkan::FRendererContract& contract) = 0;
        virtual void ApplyToView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
                                 const Vulkan::FRendererContract& contract) = 0;
        virtual VkDeviceAddress ParamsAddress() const = 0;
        virtual glm::vec3 TransmittanceToSun(float cameraAltitudeKm, float sunZenithCosine) const = 0;
    };

    using AtmosphereSubsystemFactory =
        std::unique_ptr<IAtmosphereSubsystem> (*)(Vulkan::VulkanBaseRenderer&);

    void RegisterAtmosphereSubsystemFactory(AtmosphereSubsystemFactory factory);
    std::unique_ptr<IAtmosphereSubsystem> CreateRegisteredAtmosphereSubsystem(
        Vulkan::VulkanBaseRenderer& renderer);
}
