#include "Engine/Rendering/SoftwareModern/SwModernNoAmbientRenderer.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <array>

namespace Vulkan::NoAmbientDeferred
{
    Renderer::Renderer(Vulkan::VulkanBaseRenderer& baseRender) :
        LogicRendererBase(baseRender)
    {
    }

    Renderer::~Renderer()
    {
        DeleteSwapChain();
    }

    void Renderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        shadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbient.comp.slang.spv", GetScene()));
        accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
            SwapChain(), "assets/shaders/Process.ReProjectSimple.comp.slang.spv", 16));
        composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Process.ComposeSimple.comp.slang.spv", GetScene()));

        if (GOption->ReferenceMode)
        {
            prevSingleDiffuseId_ = baseRender_.GetTemporalStorageImage(
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "noAmbientPrevDiffuseTmp");
        }
        else
        {
            prevSingleDiffuseId_ = Assets::Bindless::RT_SINGLE_PREV_DIFFUSE;
        }

        historyValid_ = false;
        lastRenderedFrame_ = -1;
    }

    void Renderer::DeleteSwapChain()
    {
        shadingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipeline_.reset();
        historyValid_ = false;
        lastRenderedFrame_ = -1;
    }

    void Renderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.InitializeBarriers(commandBuffer);
        const int currentFrame = FrameCount();
        const bool canUseHistory = historyValid_ && currentFrame == lastRenderedFrame_ + 1;

        {
            SCOPED_GPU_TIMER("shadingpass");
            shadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            const auto transition = [this, commandBuffer](uint32_t bindlessId)
            {
                baseRender_.GetStorageImage(bindlessId)->InsertBarrier(commandBuffer,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            };
            transition(Assets::Bindless::RT_SINGLE_DIFFUSE);
            transition(Assets::Bindless::RT_OBJEDCTID_0);
            transition(Assets::Bindless::RT_PREV_DEPTHBUFFER);
            transition(Assets::Bindless::RT_MOTIONVECTOR);
            transition(Assets::Bindless::RT_MOTIONMOMENT);
            transition(Assets::Bindless::RT_NORMAL);
        }

        {
            SCOPED_GPU_TIMER("reproject pass");
            const std::array<uint32_t, 4> pushConst {
                uint32_t(NextEngine::GetInstance()->GetUserSettings().TemporalFrames),
                prevSingleDiffuseId_,
                canUseHistory ? 1u : 0u,
                NextEngine::GetInstance()->GetUserSettings().TAA ? 1u : 0u
            };
            accumulatePipeline_->BindPipeline(commandBuffer, pushConst.data());
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(
                commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("copy pass");
            const auto* accumulated = baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE);
            const auto* history = baseRender_.GetStorageImage(prevSingleDiffuseId_);
            accumulated->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            history->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyRegion {};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {history->GetImage().Extent().width, history->GetImage().Extent().height, 1};

            vkCmdCopyImage(commandBuffer, accumulated->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           history->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

        historyValid_ = true;
        lastRenderedFrame_ = currentFrame;
    }
}
