#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

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
        if (shadingPipeline_)
        {
            shadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (gtaoPipeline_)
        {
            gtaoPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (composePipeline_)
        {
            composePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
    }

    void SoftwareModernNoAmbientRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.InitializeBarriers(commandBuffer);
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

        {
            SCOPED_GPU_TIMER("shadingpass");
            shadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);

            const auto transition = [this, commandBuffer](uint32_t bindlessId)
            {
                baseRender_.GetViewStorageImage(bindlessId)->InsertBarrier(commandBuffer,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            };
            transition(Assets::Bindless::RT_SINGLE_DIFFUSE);
            transition(Assets::Bindless::RT_OBJEDCTID_0);
            transition(Assets::Bindless::RT_PREV_DEPTHBUFFER);
            transition(Assets::Bindless::RT_MOTIONVECTOR);
            transition(Assets::Bindless::RT_NORMAL);
            transition(Assets::Bindless::RT_AMBIENT);
        }

        {
            
            const auto& settings = NextEngine::GetInstance()->GetUserSettings();
            if (settings.GTAOEnable)
            {
                SCOPED_GPU_TIMER("gtao pass");
                baseRender_.GetViewStorageImage(Assets::Bindless::RT_GTAO)->InsertBarrier(
                    commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
                gtaoPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
                const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount((extent.width + 1u) / 2u, 8),
                              Utilities::Math::GetSafeDispatchCount((extent.height + 1u) / 2u, 8), 1);

                baseRender_.GetViewStorageImage(Assets::Bindless::RT_GTAO)->InsertBarrier(
                    commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            }

            {
                SCOPED_GPU_TIMER("simplecompose pass");
                composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
            }
        }
    }
}
