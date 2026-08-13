#include "Engine/Rendering/PathTracing/PathTracingLiteRenderer.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::PathTracing
{
    PathTracingLiteRenderer::~PathTracingLiteRenderer()
    {
        DeleteSwapChain();
    }

    void PathTracingLiteRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        rayTracingPipeline_ = std::make_unique<PipelineCommon::ZeroBindWithTLASPipeline>(
            SwapChain(), "assets/shaders/Core.PathTracingLite.comp.slang.spv", GetScene(),
            baseRender_.ActiveTLASHandle());
        samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void PathTracingLiteRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        samplePostChain_.DeleteSwapChain();
    }

    void PathTracingLiteRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const FFrameRenderSettings& frameSettings = baseRender_.FrameSettings();
        const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

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
            {Assets::Bindless::RT_BSDF_DATA, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_MOTIONMOMENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
        }, "path tracing lite shading");

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
        baseRender_.ConfigureCheckerboardShading(gpuScene, true);

        SCOPED_GPU_TIMER("rt lite pass");
        rayTracingPipeline_->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(
                          baseRender_.CheckerboardDispatchWidth(activeExtent.width, gpuScene), 8),
                      Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);

        baseRender_.ResolveCheckerboardShading(
            commandBuffer, gpuScene, PipelineCommon::ECheckerboardResolveSet::Tracing);

        samplePostChain_.Run(baseRender_, commandBuffer, imageIndex, {
            .progressiveRender = isPrimaryView && frameSettings.progressiveRendering,
            .progressiveSampleCount = frameSettings.progressiveAccumulatedFrames,
            .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
        });
    }
}
