#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan
{
    Fence::Fence(const class Device& device, const bool signaled)
        : device_(device)
    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

        Check(vkCreateFence(device.Handle(), &fenceInfo, nullptr, &fence_), "create fence");
    }

    Fence::Fence(Fence&& other) noexcept
        : device_(other.device_), fence_(other.fence_)
    {
        other.fence_ = nullptr;
    }

    Fence::~Fence()
    {
        if (fence_ != nullptr)
        {
            vkDestroyFence(device_.Handle(), fence_, nullptr);
            fence_ = nullptr;
        }
    }

    void Fence::Reset()
    {
        Check(vkResetFences(device_.Handle(), 1, &fence_), "reset fence");
    }

    void Fence::Wait(const uint64_t timeout) const
    {
        Check(vkWaitForFences(device_.Handle(), 1, &fence_, VK_TRUE, timeout), "wait for fence");
    }

    Semaphore::Semaphore(const class Device& device)
        : device_(device)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        Check(vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore_), "create semaphores");
    }

    Semaphore::Semaphore(Semaphore&& other) noexcept
        : device_(other.device_), semaphore_(other.semaphore_)
    {
        other.semaphore_ = nullptr;
    }

    Semaphore::~Semaphore()
    {
        if (semaphore_ != nullptr)
        {
            vkDestroySemaphore(device_.Handle(), semaphore_, nullptr);
            semaphore_ = nullptr;
        }
    }

    TimelineSemaphore::TimelineSemaphore(const class Device& device, uint64_t initialValue)
        : device_(device)
    {
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = initialValue;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = &typeInfo;

        Check(vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore_), "create timeline semaphore");
    }

    TimelineSemaphore::TimelineSemaphore(TimelineSemaphore&& other) noexcept
        : device_(other.device_), semaphore_(other.semaphore_)
    {
        other.semaphore_ = nullptr;
    }

    TimelineSemaphore::~TimelineSemaphore()
    {
        if (semaphore_ != nullptr)
        {
            vkDestroySemaphore(device_.Handle(), semaphore_, nullptr);
            semaphore_ = nullptr;
        }
    }

    uint64_t TimelineSemaphore::CurrentValue() const
    {
        uint64_t value = 0;
        Check(vkGetSemaphoreCounterValue(device_.Handle(), semaphore_, &value), "get timeline semaphore counter");
        return value;
    }

    void TimelineSemaphore::Signal(uint64_t value) const
    {
        VkSemaphoreSignalInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = semaphore_;
        signalInfo.value = value;
        Check(vkSignalSemaphore(device_.Handle(), &signalInfo), "signal timeline semaphore");
    }

    void TimelineSemaphore::Wait(uint64_t value, uint64_t timeout) const
    {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &semaphore_;
        waitInfo.pValues = &value;
        Check(vkWaitSemaphores(device_.Handle(), &waitInfo, timeout), "wait timeline semaphore");
    }

    void ImageMemoryBarrier::Insert(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkImageSubresourceRange subresourceRange,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
    }

    void ImageMemoryBarrier::FullInsert(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout)
    {
        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;
        Insert(commandBuffer, image, subresourceRange, srcAccessMask, dstAccessMask, oldLayout, newLayout);
    }
}
