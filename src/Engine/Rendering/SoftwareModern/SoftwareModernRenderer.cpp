#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::SoftwareModern
{
    SoftwareModernRenderer::SoftwareModernRenderer(VulkanBaseRenderer& baseRender) : LogicRendererBase(baseRender) {}
    SoftwareModernRenderer::~SoftwareModernRenderer() { DeleteSwapChain(); }

    void SoftwareModernRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        deferredShadingPipeline_ = std::make_unique<PipelineCommon::ZeroBindPipeline>(
            SwapChain(), "assets/shaders/Core.SwModern.comp.slang.spv", GetScene());
        temporalPostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void SoftwareModernRenderer::DeleteSwapChain()
    {
        deferredShadingPipeline_.reset();
        temporalPostChain_.DeleteSwapChain();
    }

    void SoftwareModernRenderer::ReloadShaders(
        const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles)
    {
        if (deferredShadingPipeline_)
        {
            deferredShadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        temporalPostChain_.ReloadShaders(changedShaderFiles, handledShaderFiles);
    }

    void SoftwareModernRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        baseRender_.ActiveRenderView().TemporalResolve().PrepareHistoryForRead(baseRender_, commandBuffer);
        baseRender_.ImportActiveViewImagesGeneral({
            Assets::Bindless::RT_SINGLE_DIFFUSE, Assets::Bindless::RT_SINGLE_SPECULAR,
            Assets::Bindless::RT_ALBEDO, Assets::Bindless::RT_NORMAL,
            Assets::Bindless::RT_OBJEDCTID_0, Assets::Bindless::RT_OBJEDCTID_1,
            Assets::Bindless::RT_PREV_DEPTHBUFFER, Assets::Bindless::RT_MOTIONVECTOR,
            Assets::Bindless::RT_DIFFUSE_HITDIST, Assets::Bindless::RT_SPECULAR_HITDIST,
            Assets::Bindless::RT_SPECULAR_ALBEDO, Assets::Bindless::RT_ACCUMLATE_DIFFUSE,
            Assets::Bindless::RT_ACCUMLATE_SPECULAR, Assets::Bindless::RT_ACCUMLATE_ALBEDO,
            Assets::Bindless::RT_MOTIONMOMENT, Assets::Bindless::RT_DENOISED,
        }, "scene image initialization");

        const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_OBJEDCTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_DIFFUSE_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_SPECULAR_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_SPECULAR_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
        }, "software modern shading");

        {
            SCOPED_GPU_TIMER("shadingpass");
            deferredShadingPipeline_->BindPipeline(commandBuffer,
                GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }

        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        temporalPostChain_.Run(baseRender_, SwapChain(), commandBuffer, imageIndex, settings, {
            .progressiveRender = baseRender_.ActiveViewBankBase() == 0 && NextEngine::GetInstance()->IsProgressiveRendering(),
            .fastReproject = true,
            .runAtrous = true,
            .temporalFrames = baseRender_.ActiveRenderView().Schedule() != EViewSchedule::Transient
                ? uint32_t(settings.TemporalFrames) : 1u,
        });
    }
}
