#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/TemporalPostChain.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::PipelineCommon
{
    namespace
    {
        struct FReprojectPushConstants
        {
            uint32_t ProgressiveRender;
            uint32_t TemporalFrames;
            uint32_t FastReproject;
            float ClampGammaHi;
            float ClampGammaLo;
            float ClampFloor;
        };
    }

    void TemporalPostChain::CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene)
    {
        accumulatePipeline_ = std::make_unique<ZeroBindCustomPushConstantPipeline>(
            swapChain, "assets/shaders/Process.ReProject.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FReprojectPushConstants)));
        composePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.DenoiseJBF.comp.slang.spv", scene);
    }

    void TemporalPostChain::DeleteSwapChain()
    {
        accumulatePipeline_.reset();
        composePipeline_.reset();
    }

    void TemporalPostChain::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (accumulatePipeline_)
        {
            accumulatePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (composePipeline_)
        {
            composePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
    }

    void TemporalPostChain::Run(
        VulkanBaseRenderer& baseRenderer,
        const SwapChain& swapChain,
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const Runtime::Config::UserSettings& settings,
        const FTemporalPostSettings& postSettings) const
    {
        const VkExtent2D extent = baseRenderer.ActiveViewRenderExtent();
        {
            SCOPED_GPU_TIMER("reproject pass");
            baseRenderer.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_SINGLE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_NORMAL, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_MOTIONVECTOR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_MOTIONMOMENT, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ACCUMULATE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_ACCUMULATE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_ACCUMULATE_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderWrite},
            }, "temporal reproject");

            FReprojectPushConstants push{};
            push.ProgressiveRender = postSettings.progressiveRender ? 1u : 0u;
            push.TemporalFrames = postSettings.temporalFrames;
            push.FastReproject = postSettings.fastReproject ? 1u : 0u;
            push.ClampGammaHi = settings.ReprojectClampGammaHi;
            push.ClampGammaLo = settings.ReprojectClampGammaLo;
            push.ClampFloor = settings.ReprojectClampFloor;
            accumulatePipeline_->BindPipeline(commandBuffer, &push, baseRenderer.ActiveViewBankBase());
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }

        if (postSettings.runAtrous)
        {
            SCOPED_GPU_TIMER("atrous pass");
            baseRenderer.ActiveRenderView().AtrousDenoiser().Run(baseRenderer, swapChain, commandBuffer, settings);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            baseRenderer.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_ACCUMULATE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ACCUMULATE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ACCUMULATE_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_DENOISED, ERenderStage::Compute, EResourceAccess::ShaderWrite},
            }, "temporal compose");
            composePipeline_->BindPipeline(commandBuffer,
                baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase()));
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("copy pass");
            baseRenderer.ActiveRenderView().TemporalResolve().CopyToHistory(baseRenderer, commandBuffer, {
                {Assets::Bindless::RT_ACCUMULATE_DIFFUSE, ETemporalChannel::Diffuse},
                {Assets::Bindless::RT_ACCUMULATE_SPECULAR, ETemporalChannel::Specular},
                {Assets::Bindless::RT_ACCUMULATE_ALBEDO, ETemporalChannel::Albedo},
            });
        }
    }
}
