#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <array>

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
        gtaoComposePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Process.GTAOCompose.comp.slang.spv", GetScene()));
        accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
            SwapChain(), "assets/shaders/Process.ReProjectSimple.comp.slang.spv", 16));
        composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Process.ComposeSimple.comp.slang.spv", GetScene()));

        temporalResolve_.SetupHistory(baseRender_, {
            {PipelineCommon::ETemporalChannel::Diffuse, Assets::Bindless::RT_SINGLE_PREV_DIFFUSE},
        });

        historyValid_ = false;
        lastRenderedFrame_ = -1;
    }

    void SoftwareModernNoAmbientRenderer::DeleteSwapChain()
    {
        shadingPipeline_.reset();
        gtaoPipeline_.reset();
        gtaoComposePipeline_.reset();
        accumulatePipeline_.reset();
        composePipeline_.reset();
        historyValid_ = false;
        lastRenderedFrame_ = -1;
    }

    void SoftwareModernNoAmbientRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.InitializeBarriers(commandBuffer);
        const int currentFrame = FrameCount();
        const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
        const bool canUseHistory = isPrimaryView && historyValid_ && currentFrame == lastRenderedFrame_ + 1;

        {
            SCOPED_GPU_TIMER("shadingpass");
            const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
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
            transition(Assets::Bindless::RT_MOTIONMOMENT);
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
                SCOPED_GPU_TIMER("gtao compose pass");
                const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
                gtaoComposePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
                baseRender_.GetViewStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE)->InsertBarrier(
                    commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            }
        }

        {
            SCOPED_GPU_TIMER("reproject pass");
            const auto& settings = NextEngine::GetInstance()->GetUserSettings();
            const bool dlssSuperResolutionActive = settings.DLSS && baseRender_.SupportDLSS();
            const uint32_t taaEnabled = isPrimaryView && settings.TAA && !dlssSuperResolutionActive ? 1u : 0u;
            const std::array<uint32_t, 4> pushConst {
                isPrimaryView ? uint32_t(settings.TemporalFrames) : 1u,
                temporalResolve_.History(PipelineCommon::ETemporalChannel::Diffuse),
                canUseHistory ? 1u : 0u,
                taaEnabled
            };
            const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
            accumulatePipeline_->BindPipeline(commandBuffer, pushConst.data());
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);

            baseRender_.GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(
                commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
            composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("copy pass");
            temporalResolve_.CopyToHistory(baseRender_, commandBuffer, {
                {Assets::Bindless::RT_ACCUMLATE_DIFFUSE, PipelineCommon::ETemporalChannel::Diffuse},
            });
        }

        if (isPrimaryView)
        {
            historyValid_ = true;
            lastRenderedFrame_ = currentFrame;
        }
    }
}
