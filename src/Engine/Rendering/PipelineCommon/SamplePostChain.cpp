#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::PipelineCommon
{
    void SamplePostChain::CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene)
    {
        progressiveAccumulatePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.ProgressiveAccumulate.comp.slang.spv", scene);
        composePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.Compose.comp.slang.spv", scene);
    }

    void SamplePostChain::DeleteSwapChain()
    {
        progressiveAccumulatePipeline_.reset();
        composePipeline_.reset();
    }

    void SamplePostChain::Run(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const FSamplePostSettings& settings) const
    {
        const VkExtent2D extent = baseRenderer.ActiveViewRenderExtent();
        if (settings.progressiveRender)
        {
            baseRenderer.EnsureProgressiveRenderTarget();
            SCOPED_GPU_TIMER("progressive accumulate");
            baseRenderer.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_SINGLE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR, ERenderStage::Compute,
                 EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite},
            }, "progressive accumulate");

            progressiveAccumulatePipeline_->BindPipeline(
                commandBuffer,
                baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase()),
                baseRenderer.ActiveViewBankBase(), settings.progressiveSampleCount,
                settings.progressiveTargetSampleCount);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("sample compose");
            if (settings.progressiveRender)
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "sample compose");
            }
            else
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SINGLE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "sample compose");
            }

            composePipeline_->BindPipeline(
                commandBuffer,
                baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase()),
                baseRenderer.ActiveViewBankBase(), settings.progressiveRender ? 1u : 0u, 0u);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }
    }
}
