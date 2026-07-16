#pragma once

#include <memory>

#include <vulkan/vulkan.h>

namespace Vulkan
{
    class SwapChain;
    class VulkanBaseRenderer;
}

namespace Runtime::Config
{
    struct UserSettings;
}

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;

    // Variance-guided a-trous wavelet denoiser, shared by every renderer that feeds the
    // compose pass through RT_ATROUS_OUT / RT_ATROUS_SPEC_OUT. Diffuse and specular are
    // filtered in a fused dispatch, with separate iteration counts and scratch images.
    class AtrousDenoiser final
    {
    public:
        AtrousDenoiser();
        ~AtrousDenoiser();

        void CreateSwapChain(const SwapChain& swapChain);
        void DeleteSwapChain();

        // Runs the fused diffuse/specular a-trous passes when the denoiser is enabled and
        // either iteration count is positive. A no-op otherwise.
        void Run(
            VulkanBaseRenderer& baseRenderer,
            const SwapChain& swapChain,
            VkCommandBuffer commandBuffer,
            const Runtime::Config::UserSettings& settings) const;

    private:
        std::unique_ptr<ZeroBindCustomPushConstantPipeline> pipeline_;
    };
}
