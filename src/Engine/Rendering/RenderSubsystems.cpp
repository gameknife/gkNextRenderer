#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/RenderSubsystems.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

namespace Vulkan
{
    void RayTracingSceneBackend::PrepareSceneFrame(
        const VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        renderer_.UpdateSkinningBuffers();
        renderer_.DispatchSkinning(commandBuffer, imageIndex);
        renderer_.UpdateAccelerationStructuresBottom(commandBuffer);
        renderer_.UpdateAccelerationStructuresTop(commandBuffer);
    }

    void AmbientCubeBaker::PrepareSceneFrame(
        const VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        renderer_.HandleAmbientCubeCacheInvalidation(commandBuffer, imageIndex);
    }

    void GpuDrivenPasses::RenderViewPrepasses(
        const VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const bool isPrimaryView,
        const FRendererContract& contract)
    {
        if (HasAny(contract.prepasses, EViewPrepass::Cull))
            renderer_.DispatchGpuCulling(commandBuffer, imageIndex);
        if (HasAny(contract.prepasses, EViewPrepass::Clear))
            renderer_.DispatchClearPass(commandBuffer, imageIndex, isPrimaryView);
        if (HasAny(contract.prepasses, EViewPrepass::Visibility))
            renderer_.DispatchVisibilityPass(commandBuffer, imageIndex);
        if (HasAny(contract.prepasses, EViewPrepass::CSM))
            renderer_.DispatchSunShadow(commandBuffer, imageIndex);
    }
}
