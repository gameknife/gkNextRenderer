#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
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
        samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void SoftwareModernRenderer::DeleteSwapChain()
    {
        deferredShadingPipeline_.reset();
        samplePostChain_.DeleteSwapChain();
    }

    void SoftwareModernRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
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

        const auto& frameSettings = baseRender_.FrameSettings();
        samplePostChain_.Run(baseRender_, commandBuffer, imageIndex, {
            .progressiveRender = baseRender_.ActiveViewBankBase() == 0 && frameSettings.progressiveRendering,
            .progressiveSampleCount = frameSettings.progressiveAccumulatedFrames,
            .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
        });
    }
}
