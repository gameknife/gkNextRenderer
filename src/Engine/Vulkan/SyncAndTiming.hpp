#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include <array>
#include <cstddef>
#include <initializer_list>

namespace Vulkan
{
    class Fence final
    {
    public:
        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;
        Fence& operator=(Fence&&) = delete;

        explicit Fence(const class Device& device, bool signaled);
        Fence(Fence&& other) noexcept;
        ~Fence();

        const class Device& Device() const { return device_; }
        const VkFence& Handle() const { return fence_; }

        void Reset();
        void Wait(uint64_t timeout) const;

    private:
        const class Device& device_;
        VkFence fence_{};
    };

    class Semaphore final
    {
    public:
        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;
        Semaphore& operator=(Semaphore&&) = delete;

        explicit Semaphore(const class Device& device);
        Semaphore(Semaphore&& other) noexcept;
        ~Semaphore();

        const class Device& Device() const { return device_; }

    private:
        const class Device& device_;
        VULKAN_HANDLE(VkSemaphore, semaphore_)
    };

    class TimelineSemaphore final
    {
    public:
        TimelineSemaphore(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(TimelineSemaphore&&) = delete;

        explicit TimelineSemaphore(const class Device& device, uint64_t initialValue = 0);
        TimelineSemaphore(TimelineSemaphore&& other) noexcept;
        ~TimelineSemaphore();

        const class Device& Device() const { return device_; }

        uint64_t CurrentValue() const;
        void Signal(uint64_t value) const;
        void Wait(uint64_t value, uint64_t timeout = UINT64_MAX) const;

    private:
        const class Device& device_;
        VULKAN_HANDLE(VkSemaphore, semaphore_)
    };

    // Buffer memory barrier helpers for command stream synchronization.
    class BufferMemoryBarrier final
    {
    public:
        static VkBufferMemoryBarrier Make(VkBuffer buffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, const VkBufferMemoryBarrier& barrier);
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkBuffer buffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t barrierCount, const VkBufferMemoryBarrier* barriers);
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, std::initializer_list<VkBufferMemoryBarrier> barriers)
        {
            Insert(commandBuffer, srcStageMask, dstStageMask, static_cast<uint32_t>(barriers.size()), barriers.begin());
        }

        template <std::size_t N>
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, const std::array<VkBufferMemoryBarrier, N>& barriers)
        {
            Insert(commandBuffer, srcStageMask, dstStageMask, static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        template <std::size_t N>
        static void Insert(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, const VkBufferMemoryBarrier (&barriers)[N])
        {
            Insert(commandBuffer, srcStageMask, dstStageMask, static_cast<uint32_t>(N), barriers);
        }
    };

    // Image layout/memory barrier helpers for command stream synchronization.
    class ImageMemoryBarrier final
    {
    public:
        static void Insert(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkImageSubresourceRange subresourceRange,
            VkAccessFlags srcAccessMask,
            VkAccessFlags dstAccessMask,
            VkImageLayout oldLayout,
            VkImageLayout newLayout);

        // Full-image color barrier (single mip / single layer)
        static void FullInsert(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkAccessFlags srcAccessMask,
            VkAccessFlags dstAccessMask,
            VkImageLayout oldLayout,
            VkImageLayout newLayout);
    };
}
