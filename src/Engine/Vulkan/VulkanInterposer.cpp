#include "Engine/Vulkan/VulkanInterposer.hpp"

namespace Vulkan
{
    VkResult IVulkanInterposer::CreateInstance(const VkInstanceCreateInfo* createInfo,
                                               const VkAllocationCallbacks* allocator,
                                               VkInstance* instance)
    {
        return vkCreateInstance(createInfo, allocator, instance);
    }

    void IVulkanInterposer::DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
    {
        vkDestroyInstance(instance, allocator);
    }

    VkResult IVulkanInterposer::EnumeratePhysicalDevices(VkInstance instance, uint32_t* count,
                                                         VkPhysicalDevice* physicalDevices)
    {
        return vkEnumeratePhysicalDevices(instance, count, physicalDevices);
    }

#if WIN32
    VkResult IVulkanInterposer::CreateWin32SurfaceKHR(VkInstance instance,
                                                      const VkWin32SurfaceCreateInfoKHR* createInfo,
                                                      const VkAllocationCallbacks* allocator,
                                                      VkSurfaceKHR* surface)
    {
        return vkCreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
    }
#endif

    void IVulkanInterposer::DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                              const VkAllocationCallbacks* allocator)
    {
        vkDestroySurfaceKHR(instance, surface, allocator);
    }

    VkResult IVulkanInterposer::CreateDevice(VkPhysicalDevice physicalDevice,
                                             const VkDeviceCreateInfo* createInfo,
                                             const VkAllocationCallbacks* allocator,
                                             VkDevice* device)
    {
        return vkCreateDevice(physicalDevice, createInfo, allocator, device);
    }

    void IVulkanInterposer::DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
    {
        vkDestroyDevice(device, allocator);
    }

    VkResult IVulkanInterposer::DeviceWaitIdle(VkDevice device)
    {
        return vkDeviceWaitIdle(device);
    }

    VkResult IVulkanInterposer::CreateSwapchainKHR(VkDevice device,
                                                   const VkSwapchainCreateInfoKHR* createInfo,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkSwapchainKHR* swapchain)
    {
        return vkCreateSwapchainKHR(device, createInfo, allocator, swapchain);
    }

    void IVulkanInterposer::DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                const VkAllocationCallbacks* allocator)
    {
        vkDestroySwapchainKHR(device, swapchain, allocator);
    }

    VkResult IVulkanInterposer::GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                      uint32_t* count, VkImage* images)
    {
        return vkGetSwapchainImagesKHR(device, swapchain, count, images);
    }

    VkResult IVulkanInterposer::AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                    uint64_t timeout, VkSemaphore semaphore, VkFence fence,
                                                    uint32_t* imageIndex)
    {
        return vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, imageIndex);
    }

    VkResult IVulkanInterposer::QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo)
    {
        return vkQueuePresentKHR(queue, presentInfo);
    }

    namespace
    {
        IVulkanInterposer& DefaultInterposer()
        {
            static IVulkanInterposer defaultInterposer;
            return defaultInterposer;
        }

        IVulkanInterposer* GInterposer = nullptr;
    }

    IVulkanInterposer& Interposer()
    {
        return GInterposer != nullptr ? *GInterposer : DefaultInterposer();
    }

    void SetInterposer(IVulkanInterposer* interposer)
    {
        GInterposer = interposer;
    }
}
