#pragma once

#include "Engine/Vulkan/VulkanFwd.hpp"

#include <cstdint>
#include <limits>
#include <memory>

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class Sampler;

    struct FRenderViewTargetResources
    {
        FRenderViewTargetResources() = default;
        ~FRenderViewTargetResources();

        FRenderViewTargetResources(const FRenderViewTargetResources&) = delete;
        FRenderViewTargetResources& operator=(const FRenderViewTargetResources&) = delete;
        FRenderViewTargetResources(FRenderViewTargetResources&&) noexcept;
        FRenderViewTargetResources& operator=(FRenderViewTargetResources&&) noexcept;

        void ResetSwapChainResources(bool releaseSampledOutput);

        std::unique_ptr<FrameBuffer> visibilityFramebuffer;
        std::unique_ptr<RenderImage> offscreenImage;
        std::unique_ptr<Sampler> offscreenSampler;
        uint32_t outputSampleSlot = std::numeric_limits<uint32_t>::max();
    };
}
