#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace Vulkan
{
    // Interposer seam for Vulkan object creation and swapchain entry points.
    // A module (e.g. NextStreamline) can take over instance/device/swapchain
    // creation via SetInterposer; the default implementation calls the vanilla
    // Vulkan API.
    class IVulkanInterposer
    {
    public:
        virtual ~IVulkanInterposer() = default;

        // Non-null when SDL should load Vulkan through an interposer DLL.
        virtual const char* PreferredVulkanLoaderPath() { return nullptr; }
        virtual void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions) {}

        virtual VkResult CreateInstance(const VkInstanceCreateInfo* createInfo,
                                        const VkAllocationCallbacks* allocator,
                                        VkInstance* instance);
        virtual void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);
        virtual VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count,
                                                  VkPhysicalDevice* physicalDevices);
#if WIN32
        virtual VkResult CreateWin32SurfaceKHR(VkInstance instance,
                                               const VkWin32SurfaceCreateInfoKHR* createInfo,
                                               const VkAllocationCallbacks* allocator,
                                               VkSurfaceKHR* surface);
#endif
        virtual void DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                       const VkAllocationCallbacks* allocator);

        virtual VkResult CreateDevice(VkPhysicalDevice physicalDevice,
                                      const VkDeviceCreateInfo* createInfo,
                                      const VkAllocationCallbacks* allocator,
                                      VkDevice* device);
        virtual void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);
        virtual VkResult DeviceWaitIdle(VkDevice device);

        virtual VkResult CreateSwapchainKHR(VkDevice device,
                                            const VkSwapchainCreateInfoKHR* createInfo,
                                            const VkAllocationCallbacks* allocator,
                                            VkSwapchainKHR* swapchain);
        virtual void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                         const VkAllocationCallbacks* allocator);
        virtual VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                               uint32_t* count, VkImage* images);
        virtual VkResult AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                             VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex);
        virtual VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo);
    };

    // Current interposer; a process-wide default (vanilla Vulkan) when none was set.
    IVulkanInterposer& Interposer();
    // Set before instance creation; nullptr restores the default.
    void SetInterposer(IVulkanInterposer* interposer);
}
