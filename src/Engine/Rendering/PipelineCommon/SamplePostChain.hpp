#pragma once

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

    // Converts the current renderer sample directly into scene color. The only history path
    // retained here is explicit offline progressive accumulation of composed linear radiance;
    // realtime rendering never reprojects or spatially filters the sample before a temporal
    // upscaler consumes it.
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
