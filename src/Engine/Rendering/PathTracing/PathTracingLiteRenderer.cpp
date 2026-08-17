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
        surfaceBuild_.CreateSwapChain(SwapChain(), GetScene());
        scheduler_.CreateSwapChain(SwapChain(), GetScene());
        standardBucketPipeline_ = std::make_unique<PipelineCommon::ZeroBindWithTLASPipeline>(
            SwapChain(), "assets/shaders/Core.PathTracingLiteStandard.comp.slang.spv", GetScene(),
            baseRender_.ActiveTLASHandle());
        backgroundBucketPipeline_ = std::make_unique<PipelineCommon::ZeroBindPipeline>(
            SwapChain(), "assets/shaders/Core.TracingBackground.comp.slang.spv", GetScene());
        emissiveBucketPipeline_ = std::make_unique<PipelineCommon::ZeroBindPipeline>(
            SwapChain(), "assets/shaders/Core.TracingEmissive.comp.slang.spv", GetScene());
        samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void PathTracingLiteRenderer::DeleteSwapChain()
    {
        surfaceBuild_.DeleteSwapChain();
        scheduler_.DeleteSwapChain();
        standardBucketPipeline_.reset();
        backgroundBucketPipeline_.reset();
        emissiveBucketPipeline_.reset();
        samplePostChain_.DeleteSwapChain();
    }

    void PathTracingLiteRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const FFrameRenderSettings& frameSettings = baseRender_.FrameSettings();
        const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
        baseRender_.ConfigureCheckerboardShading(gpuScene);

        // Resolve the visibility buffer once, at full rate. Lite consumes RT_SPECULAR_ALBEDO, so
        // this build writes it too.
        surfaceBuild_.Run(baseRender_, commandBuffer, gpuScene, true);
        scheduler_.Classify(baseRender_, commandBuffer, gpuScene);

        {
            SCOPED_GPU_TIMER("rt lite pass");
            // Core Shading consumes the surface and produces lighting. RT_ALBEDO is the exception:
            // it is written here as the compose demodulation guide for sky and emissive pixels.
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_MOTIONMOMENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_BSDF_DATA, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute,
                 PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_DIFFUSE_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SPECULAR_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            }, "path tracing lite surface shading");

            using EBucket = PipelineCommon::ShadingSchedulerPass::EBucket;
            scheduler_.DispatchBucket(commandBuffer, *standardBucketPipeline_, gpuScene, EBucket::Standard);
            scheduler_.DispatchBucket(commandBuffer, *backgroundBucketPipeline_, gpuScene, EBucket::Background);
            scheduler_.DispatchBucket(commandBuffer, *emissiveBucketPipeline_, gpuScene, EBucket::Emissive);
        }

        baseRender_.ResolveCheckerboardShading(
            commandBuffer, gpuScene, PipelineCommon::ECheckerboardResolveSet::Tracing);

        samplePostChain_.Run(baseRender_, commandBuffer, imageIndex, {
            .progressiveRender = isPrimaryView && frameSettings.progressiveRendering,
            .progressiveSampleCount = frameSettings.progressiveAccumulatedFrames,
            .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
        });
    }
}
