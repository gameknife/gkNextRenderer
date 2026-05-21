#pragma once

#include "DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <vector>
#include <functional>
#include <mutex>

namespace Vulkan
{
    // ============================================================================
    // CommandPool
    // ============================================================================

    class CommandPool final
    {
    public:

        VULKAN_NON_COPIABLE(CommandPool)

        CommandPool(const Device& device, uint32_t queueFamilyIndex, uint32_t queue, bool allowReset);
        ~CommandPool();

        const class Device& Device() const { return device_; }
        VkQueue Queue() const { return queue_; }

    private:
        friend class SingleTimeCommands;

        const class Device& device_;
        mutable std::mutex mutex_;

        VULKAN_HANDLE(VkCommandPool, commandPool_)

        VkQueue queue_;
    };

    // ============================================================================
    // CommandBuffers
    // ============================================================================

    class CommandBuffers final
    {
    public:

        VULKAN_NON_COPIABLE(CommandBuffers)

        CommandBuffers(CommandPool& commandPool, uint32_t size);
        ~CommandBuffers();

        uint32_t Size() const { return static_cast<uint32_t>(commandBuffers_.size()); }
        VkCommandBuffer& operator [] (const size_t i) { return commandBuffers_[i]; }

        VkCommandBuffer Begin(size_t i);
        void End(size_t);

    private:

        const CommandPool& commandPool_;

        std::vector<VkCommandBuffer> commandBuffers_;
    };

    // ============================================================================
    // SingleTimeCommands
    // ============================================================================

    class SingleTimeCommands final
    {
    public:

        static void Submit(CommandPool& commandPool, const std::function<void(VkCommandBuffer)>& action);
    };

}
