#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Vulkan
{
    class VulkanBaseRenderer;
    struct FRendererContract;

    class RayTracingSceneBackend final
    {
    public:
        explicit RayTracingSceneBackend(VulkanBaseRenderer& renderer) : renderer_(renderer) {}
        void PrepareSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    private:
        VulkanBaseRenderer& renderer_;
    };

    class AmbientCubeBaker final
    {
    public:
        explicit AmbientCubeBaker(VulkanBaseRenderer& renderer) : renderer_(renderer) {}
        void PrepareSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    private:
        VulkanBaseRenderer& renderer_;
    };

    class GpuDrivenPasses final
    {
    public:
        explicit GpuDrivenPasses(VulkanBaseRenderer& renderer) : renderer_(renderer) {}
        void RenderViewPrepasses(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                 bool isPrimaryView, const FRendererContract& contract);
    private:
        VulkanBaseRenderer& renderer_;
    };
}
