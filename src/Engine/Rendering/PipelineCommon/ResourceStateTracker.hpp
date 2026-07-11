#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan::PipelineCommon
{
    enum class ERenderStage : uint32_t
    {
        None = 0,
        Transfer = 1u << 0u,
        Compute = 1u << 1u,
        ColorAttachment = 1u << 2u,
        DepthStencil = 1u << 3u,
        Present = 1u << 4u,
    };

    enum class EResourceAccess : uint32_t
    {
        None = 0,
        TransferRead = 1u << 0u,
        TransferWrite = 1u << 1u,
        ShaderRead = 1u << 2u,
        ShaderWrite = 1u << 3u,
        ColorRead = 1u << 4u,
        ColorWrite = 1u << 5u,
        DepthRead = 1u << 6u,
        DepthWrite = 1u << 7u,
    };

    constexpr ERenderStage operator|(ERenderStage lhs, ERenderStage rhs)
    {
        return static_cast<ERenderStage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    constexpr EResourceAccess operator|(EResourceAccess lhs, EResourceAccess rhs)
    {
        return static_cast<EResourceAccess>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    struct FImageHandle
    {
        uint64_t value = 0;
        auto operator<=>(const FImageHandle&) const = default;
    };

    struct FImageState
    {
        bool initialized = false;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        ERenderStage stages = ERenderStage::None;
        EResourceAccess access = EResourceAccess::None;
        uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
        std::string lastPass;
    };

    struct FImageUse
    {
        FImageHandle image;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ERenderStage stages = ERenderStage::None;
        EResourceAccess access = EResourceAccess::None;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool discardPreviousContents = false;
    };

    struct FImageBarrier
    {
        FImageHandle image;
        VkImageSubresourceRange range{};
        VkPipelineStageFlags srcStages = 0;
        VkPipelineStageFlags dstStages = 0;
        VkAccessFlags srcAccess = 0;
        VkAccessFlags dstAccess = 0;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    class FResourceStateTracker final
    {
    public:
        std::optional<FImageBarrier> Use(const FImageUse& use, std::string_view passName)
        {
            if (use.image.value == 0 || use.layout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                throw std::invalid_argument("tracked image use requires a handle and defined target layout");
            }

            FImageState& previous = states_[use.image.value];
            const bool firstUse = !previous.initialized;
            const bool discard = use.discardPreviousContents;
            const bool needsBarrier = firstUse || discard || previous.layout != use.layout ||
                                      IsWrite(previous.access) || IsWrite(use.access);

            std::optional<FImageBarrier> barrier;
            if (needsBarrier)
            {
                barrier = FImageBarrier{
                    .image = use.image,
                    .range = use.range,
                    .srcStages = firstUse || discard ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : ToVkStages(previous.stages),
                    .dstStages = ToVkStages(use.stages),
                    .srcAccess = firstUse || discard ? 0u : ToVkAccess(previous.access),
                    .dstAccess = ToVkAccess(use.access),
                    .oldLayout = firstUse || discard ? VK_IMAGE_LAYOUT_UNDEFINED : previous.layout,
                    .newLayout = use.layout,
                };
            }

            previous = {
                .initialized = true,
                .layout = use.layout,
                .stages = use.stages,
                .access = use.access,
                .queueFamily = previous.queueFamily,
                .lastPass = std::string(passName),
            };
            return barrier;
        }

        const FImageState* Find(FImageHandle image) const
        {
            const auto found = states_.find(image.value);
            return found == states_.end() ? nullptr : &found->second;
        }

        void Reset() { states_.clear(); }

        static VkPipelineStageFlags ToVkStages(ERenderStage stages)
        {
            const uint32_t value = static_cast<uint32_t>(stages);
            VkPipelineStageFlags result = 0;
            if (value & static_cast<uint32_t>(ERenderStage::Transfer)) result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (value & static_cast<uint32_t>(ERenderStage::Compute)) result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            if (value & static_cast<uint32_t>(ERenderStage::ColorAttachment)) result |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (value & static_cast<uint32_t>(ERenderStage::DepthStencil)) result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            if (value & static_cast<uint32_t>(ERenderStage::Present)) result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            return result != 0 ? result : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }

        static VkAccessFlags ToVkAccess(EResourceAccess access)
        {
            const uint32_t value = static_cast<uint32_t>(access);
            VkAccessFlags result = 0;
            if (value & static_cast<uint32_t>(EResourceAccess::TransferRead)) result |= VK_ACCESS_TRANSFER_READ_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::TransferWrite)) result |= VK_ACCESS_TRANSFER_WRITE_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::ShaderRead)) result |= VK_ACCESS_SHADER_READ_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::ShaderWrite)) result |= VK_ACCESS_SHADER_WRITE_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::ColorRead)) result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::ColorWrite)) result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::DepthRead)) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            if (value & static_cast<uint32_t>(EResourceAccess::DepthWrite)) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            return result;
        }

    private:
        static bool IsWrite(EResourceAccess access)
        {
            constexpr uint32_t writeMask =
                static_cast<uint32_t>(EResourceAccess::TransferWrite) |
                static_cast<uint32_t>(EResourceAccess::ShaderWrite) |
                static_cast<uint32_t>(EResourceAccess::ColorWrite) |
                static_cast<uint32_t>(EResourceAccess::DepthWrite);
            return (static_cast<uint32_t>(access) & writeMask) != 0;
        }

        std::unordered_map<uint64_t, FImageState> states_;
    };
}
