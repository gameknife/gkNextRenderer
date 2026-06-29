#pragma once

#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class RenderView;
    class Sampler;
    class VulkanBaseRenderer;

    class RenderViewResourceFactory final
    {
    public:
        explicit RenderViewResourceFactory(VulkanBaseRenderer& renderer);

        std::unique_ptr<FrameBuffer> RebuildVisibilityFramebuffer(RenderView& view, VkExtent2D extent);
        std::unique_ptr<RenderImage> CreateSampledColorImage(VkExtent2D extent, const char* debugName);
        std::unique_ptr<Sampler> CreateClampSampler();
        void BindSampledColorImage(uint32_t sampleSlot, RenderImage& image, Sampler& sampler);

    private:
        VulkanBaseRenderer& renderer_;
    };
}
