#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vulkan/vulkan.h>
#include "Engine/Rendering/PipelineCommon/ResourceStateTracker.hpp"

namespace Vulkan
{
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    enum class ETemporalChannel : uint32_t
    {
        Diffuse,
        Specular,
        Albedo,
        Count,
    };

    struct FTemporalHistorySpec
    {
        ETemporalChannel channel;
        uint32_t fallbackBindlessId;
    };

    struct FTemporalCopy
    {
        uint32_t accumulatedBindlessId;
        ETemporalChannel historyChannel;
    };

    class TemporalResolve final
    {
    public:
        void SetupDefaultHistory();

        uint32_t History(ETemporalChannel channel) const;
        bool IsHistoryValidForFrame(int currentFrame) const;
        void MarkHistoryValid(int currentFrame);
        void InvalidateHistory();
        void PrepareHistoryForRead(VulkanBaseRenderer& baseRenderer, VkCommandBuffer commandBuffer);

        void CopyToHistory(
            VulkanBaseRenderer& baseRenderer,
            VkCommandBuffer commandBuffer,
            std::initializer_list<FTemporalCopy> copies);

    private:
        std::array<uint32_t, static_cast<size_t>(ETemporalChannel::Count)> historyIds_{};
        int lastRenderedFrame_ = -1;
        bool historyValid_ = false;
        FResourceStateTracker historyStateTracker_;
    };
}
