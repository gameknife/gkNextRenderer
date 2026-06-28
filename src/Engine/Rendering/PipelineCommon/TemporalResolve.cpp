#include "Engine/Rendering/PipelineCommon/TemporalResolve.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

namespace Vulkan::PipelineCommon
{
    void TemporalResolve::SetupHistory(
        VulkanBaseRenderer& baseRenderer,
        std::initializer_list<FTemporalHistorySpec> historySpecs)
    {
        (void)baseRenderer;
        for (const auto& spec : historySpecs)
        {
            const auto index = static_cast<size_t>(spec.channel);
            historyIds_[index] = spec.fallbackBindlessId;
        }
    }

    uint32_t TemporalResolve::History(ETemporalChannel channel) const
    {
        return historyIds_[static_cast<size_t>(channel)];
    }

    void TemporalResolve::CopyToHistory(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer,
        std::initializer_list<FTemporalCopy> copies) const
    {
        // Pure C++ image copy (no shader to read custom_data_0): resolve the accumulation/history
        // slots into the active RenderView's bank here. Base 0 == primary, unchanged.
        const uint32_t viewBase = baseRenderer.ActiveViewBankBase();
        for (const auto& copy : copies)
        {
            const auto* accumulated = baseRenderer.GetStorageImage(viewBase + copy.accumulatedBindlessId);
            const auto* history = baseRenderer.GetStorageImage(viewBase + History(copy.historyChannel));
            accumulated->InsertBarrier(
                commandBuffer,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            history->InsertBarrier(
                commandBuffer,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
        }
    }
}
