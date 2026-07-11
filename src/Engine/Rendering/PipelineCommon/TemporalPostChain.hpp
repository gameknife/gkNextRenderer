#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Assets
{
    class Scene;
}

namespace Runtime::Config
{
    struct UserSettings;
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

    struct FTemporalPostSettings
    {
        bool progressiveRender = false;
        bool fastReproject = false;
        bool runAtrous = true;
        uint32_t temporalFrames = 1;
    };

    class TemporalPostChain final
    {
    public:
        void CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene);
        void DeleteSwapChain();
        void ReloadShaders(const std::set<std::string>& changedShaderFiles,
                           std::set<std::string>& handledShaderFiles);
        void Run(VulkanBaseRenderer& baseRenderer, const SwapChain& swapChain,
                 VkCommandBuffer commandBuffer, uint32_t imageIndex,
                 const Runtime::Config::UserSettings& settings,
                 const FTemporalPostSettings& postSettings) const;

    private:
        std::unique_ptr<ZeroBindCustomPushConstantPipeline> accumulatePipeline_;
        std::unique_ptr<ZeroBindPipeline> composePipeline_;
    };
}
