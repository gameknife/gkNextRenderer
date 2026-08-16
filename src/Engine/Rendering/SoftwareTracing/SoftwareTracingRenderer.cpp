#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/RestirDI.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::SoftwareTracing {

SoftwareTracingRenderer::SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
    
}

SoftwareTracingRenderer::~SoftwareTracingRenderer()
{
    SoftwareTracingRenderer::DeleteSwapChain();
}

void SoftwareTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
    deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.SwTracing.comp.slang.spv", GetScene()));
    surfaceBuild_.CreateSwapChain(SwapChain(), GetScene());
    samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
    deferredShadingPipeline_.reset();
    restirSpatialPipeline_.reset();
    surfaceBuild_.DeleteSwapChain();
    samplePostChain_.DeleteSwapChain();
}

void SoftwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
    const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
    const auto& frameSettings = baseRender_.FrameSettings();
    const bool restirEnabled = frameSettings.userSettings.RestirEnable;
    auto& restir = baseRender_.RestirDIResources();
    if (restirEnabled && isPrimaryView)
    {
        restir.Prepare(commandBuffer, activeExtent, !frameSettings.progressiveRendering);
    }

    const bool surfacePath = baseRender_.IsSurfacePathActive();
    Assets::GPUScene gpuScene =
        GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
    baseRender_.ConfigureSurfacePath(gpuScene);
    baseRender_.ConfigureCheckerboardShading(gpuScene, !restirEnabled);
    if (restirEnabled && restir.HasResources())
    {
        gpuScene.ReservedAddress0 = restir.ResourceTableAddress();
        if (isPrimaryView)
        {
            gpuScene.CustomData1 = restir.FrameStamp();
        }
    }

    if (surfacePath)
    {
        // Resolve the visibility buffer once, at full rate, before anything shades from it.
        // The tracing set consumes RT_SPECULAR_ALBEDO, so this build writes it too.
        surfaceBuild_.Run(baseRender_, commandBuffer, gpuScene, true);
    }

    {
        SCOPED_GPU_TIMER("shadingpass");
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
            }, "software tracing surface shading");
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
                {Assets::Bindless::RT_MOTIONMOMENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            }, "software tracing shading");
        }
        // cs shading pass
        deferredShadingPipeline_->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
            Utilities::Math::GetSafeDispatchCount(
                baseRender_.CheckerboardDispatchWidth(activeExtent.width, gpuScene), 8),
            Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
    }

    if (restirEnabled && isPrimaryView && restir.HasResources())
    {
        if (!restirSpatialPipeline_)
        {
            restirSpatialPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
                SwapChain(), "assets/shaders/Core.SwRestirSpatialShade.comp.slang.spv", GetScene()));
        }

        restir.InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead |
                 PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_BSDF_DATA, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
        }, "software restir spatial shade");

        SCOPED_GPU_TIMER("software restir spatial shade");
        restirSpatialPipeline_->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(
                          baseRender_.CheckerboardDispatchWidth(activeExtent.width, gpuScene), 8),
                      Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
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
