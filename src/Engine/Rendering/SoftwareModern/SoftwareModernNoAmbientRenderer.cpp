#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

namespace Vulkan::SoftwareModernNoAmbient
{
    SoftwareModernNoAmbientRenderer::SoftwareModernNoAmbientRenderer(Vulkan::VulkanBaseRenderer& baseRender) :
        LogicRendererBase(baseRender)
    {
    }

    SoftwareModernNoAmbientRenderer::~SoftwareModernNoAmbientRenderer()
    {
        DeleteSwapChain();
    }

    void SoftwareModernNoAmbientRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        shadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbient.comp.slang.spv", GetScene()));
        gtaoPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.GTAO.comp.slang.spv", GetScene()));
        composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Process.GTAOCompose.comp.slang.spv", GetScene()));
    }

    void SoftwareModernNoAmbientRenderer::DeleteSwapChain()
    {
        shadingPipeline_.reset();
        gtaoPipeline_.reset();
        composePipeline_.reset();
    }

    void SoftwareModernNoAmbientRenderer::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (shadingPipeline_) shadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        if (gtaoPipeline_) gtaoPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        if (composePipeline_) composePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
    }

    void SoftwareModernNoAmbientRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

        {
            SCOPED_GPU_TIMER("shadingpass");
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_AMBIENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_OBJEDCTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            }, "software modern no ambient shading");
            shadingPipeline_->BindPipeline(commandBuffer,
                GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
        }

        {
            const auto& settings = baseRender_.FrameSettings().userSettings;
            if (settings.GTAOEnable)
            {
                SCOPED_GPU_TIMER("gtao pass");
                baseRender_.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_GTAO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                }, "gtao");
                gtaoPipeline_->BindPipeline(commandBuffer,
                    GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
                const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount((extent.width + 1u) / 2u, 8),
                              Utilities::Math::GetSafeDispatchCount((extent.height + 1u) / 2u, 8), 1);
            }

            {
                SCOPED_GPU_TIMER("simplecompose pass");
                baseRender_.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_AMBIENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJEDCTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_GTAO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_DENOISED, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                }, "gtao compose");
                composePipeline_->BindPipeline(commandBuffer,
                    GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
            }
        }
    }
}
