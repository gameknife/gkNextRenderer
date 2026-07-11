#include "Engine/Rendering/PipelineCommon/TemporalResolve.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan::PipelineCommon
{
    void TemporalResolve::SetupDefaultHistory()
    {
        historyStateTracker_.Reset();
        historyIds_[static_cast<size_t>(ETemporalChannel::Diffuse)] = Assets::Bindless::RT_SINGLE_PREV_DIFFUSE;
        historyIds_[static_cast<size_t>(ETemporalChannel::Specular)] = Assets::Bindless::RT_SINGLE_PREV_SPECULAR;
        historyIds_[static_cast<size_t>(ETemporalChannel::Albedo)] = Assets::Bindless::RT_SINGLE_PREV_ALBEDO;
    }

    uint32_t TemporalResolve::History(ETemporalChannel channel) const
    {
        return historyIds_[static_cast<size_t>(channel)];
    }

    bool TemporalResolve::IsHistoryValidForFrame(const int currentFrame) const
    {
        return historyValid_ && currentFrame == lastRenderedFrame_ + 1;
    }

    void TemporalResolve::MarkHistoryValid(const int currentFrame)
    {
        historyValid_ = true;
        lastRenderedFrame_ = currentFrame;
    }

    void TemporalResolve::InvalidateHistory()
    {
        historyValid_ = false;
        lastRenderedFrame_ = -1;
    }

    namespace
    {
        FImageHandle HistoryHandle(uint32_t absoluteBindlessId)
        {
            return {static_cast<uint64_t>(absoluteBindlessId) + 1u};
        }

        void EmitBarrier(VkCommandBuffer commandBuffer, VkImage image, const FImageBarrier& barrier)
        {
            ImageMemoryBarrier::Insert(commandBuffer, barrier.srcStages, barrier.dstStages,
                                       image, barrier.range, barrier.srcAccess, barrier.dstAccess,
                                       barrier.oldLayout, barrier.newLayout);
        }
    }

    void TemporalResolve::PrepareHistoryForRead(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer)
    {
        const uint32_t viewBase = baseRenderer.ActiveViewBankBase();
        for (const uint32_t historyId : historyIds_)
        {
            const uint32_t absoluteId = viewBase + historyId;
            const auto* history = baseRenderer.GetStorageImage(absoluteId);
            if (const auto barrier = historyStateTracker_.Use(
                    {.image = HistoryHandle(absoluteId),
                     .stages = ERenderStage::Compute,
                     .access = EResourceAccess::ShaderRead,
                     .layout = VK_IMAGE_LAYOUT_GENERAL},
                    "temporal history read"))
            {
                EmitBarrier(commandBuffer, history->GetImage().Handle(), *barrier);
            }
        }
    }

    void TemporalResolve::CopyToHistory(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer,
        std::initializer_list<FTemporalCopy> copies)
    {
        // Pure C++ image copy (no shader to read custom_data_0): resolve the accumulation/history
        // slots into the active RenderView's bank here. Base 0 == primary, unchanged.
        const uint32_t viewBase = baseRenderer.ActiveViewBankBase();
        for (const auto& copy : copies)
        {
            const auto* accumulated = baseRenderer.GetStorageImage(viewBase + copy.accumulatedBindlessId);
            const auto* history = baseRenderer.GetStorageImage(viewBase + History(copy.historyChannel));
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ImageMemoryBarrier::Insert(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                accumulated->GetImage().Handle(), range,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            const uint32_t absoluteHistoryId = viewBase + History(copy.historyChannel);
            if (const auto barrier = historyStateTracker_.Use(
                    {.image = HistoryHandle(absoluteHistoryId),
                     .stages = ERenderStage::Transfer,
                     .access = EResourceAccess::TransferWrite,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                    "copy temporal history"))
            {
                EmitBarrier(commandBuffer, history->GetImage().Handle(), *barrier);
            }

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {
                history->GetImage().Extent().width,
                history->GetImage().Extent().height,
                1};

            vkCmdCopyImage(
                commandBuffer,
                accumulated->GetImage().Handle(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                history->GetImage().Handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copyRegion);

            ImageMemoryBarrier::Insert(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                accumulated->GetImage().Handle(), range,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            if (const auto barrier = historyStateTracker_.Use(
                    {.image = HistoryHandle(absoluteHistoryId),
                     .stages = ERenderStage::Compute,
                     .access = EResourceAccess::ShaderRead,
                     .layout = VK_IMAGE_LAYOUT_GENERAL},
                    "temporal history steady state"))
            {
                EmitBarrier(commandBuffer, history->GetImage().Handle(), *barrier);
            }
        }
        MarkHistoryValid(baseRenderer.FrameCount());
    }
}
