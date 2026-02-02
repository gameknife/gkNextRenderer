#pragma once

#include "DebugUtilities.hpp"
#include <vector>
#include <functional>

namespace Vulkan
{
    class Device;

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

        const class Device& device_;

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

        static void Submit(CommandPool& commandPool, const std::function<void(VkCommandBuffer)>& action)
        {
            CommandBuffers commandBuffers(commandPool, 1);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffers[0], &beginInfo);

            action(commandBuffers[0]);

            vkEndCommandBuffer(commandBuffers[0]);

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[0];

            const auto graphicsQueue = commandPool.Queue();

            vkQueueSubmit(graphicsQueue, 1, &submitInfo, nullptr);
            vkQueueWaitIdle(graphicsQueue);
        }
    };

}
