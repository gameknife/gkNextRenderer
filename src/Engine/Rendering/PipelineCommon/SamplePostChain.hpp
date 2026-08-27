#pragma once

// Shared only by concrete renderer implementations in this module.

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

namespace Assets
{
    class Scene;
}

namespace Vulkan
{
    class SwapChain;
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;
    class ZeroBindPipeline;

    struct FSamplePostSettings
    {
        bool progressiveRender = false;
        uint32_t progressiveSampleCount = 0;
        uint32_t progressiveTargetSampleCount = 1;
    };

    FSamplePostSettings MakeSamplePostSettings(const VulkanBaseRenderer& baseRenderer);

    // Converts the current renderer sample directly into scene color. The history paths retained
    // here are explicit non-reprojected progressive accumulations of linear radiance;
    // realtime rendering never reprojects or spatially filters the sample before a temporal
    // upscaler consumes it. Compose pre-exposes the result so display white is near 1.0;
    // display encoding is applied by the final output pass.
    class SamplePostChain final
    {
    public:
        void CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene);
        void DeleteSwapChain();
        void Run(VulkanBaseRenderer& baseRenderer, VkCommandBuffer commandBuffer,
                 uint32_t imageIndex, const FSamplePostSettings& settings) const;

    private:
        std::unique_ptr<ZeroBindPipeline> progressiveAccumulatePipeline_;
        std::unique_ptr<ZeroBindPipeline> composePipeline_;
    };
}
