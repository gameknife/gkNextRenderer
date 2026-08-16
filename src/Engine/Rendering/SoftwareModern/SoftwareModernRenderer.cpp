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
        surfaceBuild_.CreateSwapChain(SwapChain(), GetScene());
        samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void SoftwareModernRenderer::DeleteSwapChain()
    {
        deferredShadingPipeline_.reset();
        surfaceBuild_.DeleteSwapChain();
        samplePostChain_.DeleteSwapChain();
    }

    void SoftwareModernRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
        const bool surfacePath = baseRender_.IsSurfacePathActive();
        Assets::GPUScene gpuScene =
            GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
        baseRender_.ConfigureSurfacePath(gpuScene);
        baseRender_.ConfigureCheckerboardShading(gpuScene);

        if (surfacePath)
        {
            // Resolve the visibility buffer once, at full rate. The tracing set consumes
            // RT_SPECULAR_ALBEDO, so this build writes it too.
            surfaceBuild_.Run(baseRender_, commandBuffer, gpuScene, true);
        }

        if (surfacePath)
        {
            // Core Shading consumes the surface and produces lighting. RT_ALBEDO is the exception:
            // it is written here as the compose demodulation guide for sky and emissive pixels.
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_BSDF_DATA, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute,
                 PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_DIFFUSE_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SPECULAR_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            }, "software modern surface shading");
        }
        else
        {
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
            }, "software modern shading");
        }

        {
            SCOPED_GPU_TIMER("shadingpass");
            deferredShadingPipeline_->BindPipeline(commandBuffer, gpuScene);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(
                              baseRender_.CheckerboardDispatchWidth(extent.width, gpuScene), 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
            baseRender_.ResolveCheckerboardShading(
                commandBuffer, gpuScene, PipelineCommon::ECheckerboardResolveSet::Tracing);
        }

        const auto& frameSettings = baseRender_.FrameSettings();
        samplePostChain_.Run(baseRender_, commandBuffer, imageIndex, {
            .progressiveRender = baseRender_.ActiveViewBankBase() == 0 && frameSettings.progressiveRendering,
            .progressiveSampleCount = frameSettings.progressiveAccumulatedFrames,
            .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
        });
    }
}
