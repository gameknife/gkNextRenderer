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
    deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
        SwapChain(), "assets/shaders/Core.SwTracing.comp.slang.spv", GetScene()));
    samplePostChain_.CreateSwapChain(SwapChain(), GetScene());
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
    deferredShadingPipeline_.reset();
    restirSpatialPipeline_.reset();
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

    Assets::GPUScene gpuScene =
        GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
    if (restirEnabled && restir.HasResources())
    {
        gpuScene.ReservedAddress0 = restir.ResourceTableAddress();
        if (isPrimaryView)
        {
            gpuScene.CustomData1 = restir.FrameStamp();
        }
    }

    {
        SCOPED_GPU_TIMER("shadingpass");
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
        // cs shading pass
        deferredShadingPipeline_->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
            Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
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
                      Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                      Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
    }

    samplePostChain_.Run(baseRender_, commandBuffer, imageIndex, {
        .progressiveRender = isPrimaryView && frameSettings.progressiveRendering,
        .progressiveSampleCount = frameSettings.progressiveAccumulatedFrames,
        .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
    });
}
}
