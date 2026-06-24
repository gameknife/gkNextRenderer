#include "Engine/Rendering/PipelineCommon/TemporalResolve.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

#include "Engine/Options.hpp"

namespace Vulkan::PipelineCommon
{
    void TemporalResolve::SetupHistory(
        VulkanBaseRenderer& baseRenderer,
        std::initializer_list<FTemporalHistorySpec> historySpecs)
    {
        for (const auto& spec : historySpecs)
        {
            const auto index = static_cast<size_t>(spec.channel);
            const VkFormat historyFormat = GOption->HighPrecisionProgressiveHistory
                ? VK_FORMAT_R32G32B32A32_SFLOAT
                : VK_FORMAT_R16G16B16A16_SFLOAT;
            historyIds_[index] = GOption->ReferenceMode
                ? baseRenderer.GetTemporalStorageImage(
                    historyFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    spec.referenceDebugName)
                : spec.fallbackBindlessId;
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
        for (const auto& copy : copies)
        {
            const auto* accumulated = baseRenderer.GetStorageImage(copy.accumulatedBindlessId);
            const auto* history = baseRenderer.GetStorageImage(History(copy.historyChannel));
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
