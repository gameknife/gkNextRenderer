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
    // compose pass through RT_ATROUS_OUT / RT_ATROUS_SPEC_OUT. The camera UBO routes the
    // compose pass to read the a-trous output whenever DenoiseAtrousIterations > 0 (see
    // Engine.CameraUbo.cpp), so any renderer that wants the denoiser to take effect must
    // run this pass between the reproject and compose passes.
    class AtrousDenoiser final
    {
    public:
        AtrousDenoiser();
        ~AtrousDenoiser();

        void CreateSwapChain(const SwapChain& swapChain);
        void DeleteSwapChain();

        // Runs the diffuse + specular a-trous passes when the denoiser is enabled and the
        // iteration count is positive. A no-op otherwise (compose then falls back to the
        // accumulation buffers, matching Engine.CameraUbo.cpp routing).
        void Run(
            VulkanBaseRenderer& baseRenderer,
            const SwapChain& swapChain,
            VkCommandBuffer commandBuffer,
            const Runtime::Config::UserSettings& settings) const;

    private:
        std::unique_ptr<ZeroBindCustomPushConstantPipeline> pipeline_;
    };
}
