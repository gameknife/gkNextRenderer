#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vulkan/vulkan.h>

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
        const char* referenceDebugName;
    };

    struct FTemporalCopy
    {
        uint32_t accumulatedBindlessId;
        ETemporalChannel historyChannel;
    };

    class TemporalResolve final
    {
    public:
        void SetupHistory(
            VulkanBaseRenderer& baseRenderer,
            std::initializer_list<FTemporalHistorySpec> historySpecs);

        uint32_t History(ETemporalChannel channel) const;

        void CopyToHistory(
            VulkanBaseRenderer& baseRenderer,
            VkCommandBuffer commandBuffer,
            std::initializer_list<FTemporalCopy> copies) const;

    private:
        std::array<uint32_t, static_cast<size_t>(ETemporalChannel::Count)> historyIds_{};
    };
}
