#include "SyncAndTiming.hpp"
#include "Device.hpp"

namespace Vulkan
{

// ============================================================================
// Fence Implementation
// ============================================================================

Fence::Fence(const class Device& device, const bool signaled) :
    device_(device)
{
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    Check(vkCreateFence(device.Handle(), &fenceInfo, nullptr, &fence_),
        "create fence");
}

Fence::Fence(Fence&& other) noexcept :
    device_(other.device_),
    fence_(other.fence_)
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
    Check(vkResetFences(device_.Handle(), 1, &fence_),
        "reset fence");
}

void Fence::Wait(const uint64_t timeout) const
{
    Check(vkWaitForFences(device_.Handle(), 1, &fence_, VK_TRUE, timeout),
        "wait for fence");
}

// ============================================================================
// Semaphore Implementation
// ============================================================================

Semaphore::Semaphore(const class Device& device) :
    device_(device)
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    Check(vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore_),
        "create semaphores");
}

Semaphore::Semaphore(Semaphore&& other) noexcept :
    device_(other.device_),
    semaphore_(other.semaphore_)
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

}
