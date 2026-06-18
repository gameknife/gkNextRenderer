#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"

#include <vulkan/vulkan.h>

// NVIDIA Streamline process-wide integration. SL SDK symbols stay in the cpp so
// non-Windows platforms can include this header without the SDK.
namespace StreamlineWrapper
{
    bool ShouldInitialize();
    void Initialize();
    void Shutdown();
    bool IsInitialized();
    const char* PreferredVulkanLoaderPath();

    void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions);
    Rendering::Upscaler::FFeatureCaps AppendRequiredDeviceExtensions(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& extensions);

    Rendering::Upscaler::FFeatureCaps CachedCaps();

    VkResult CreateInstance(
        const VkInstanceCreateInfo* createInfo,
        const VkAllocationCallbacks* allocator,
        VkInstance* instance);
    void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);
    VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* physicalDevices);
#if WIN32
    VkResult CreateWin32SurfaceKHR(
        VkInstance instance,
        const VkWin32SurfaceCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator,
        VkSurfaceKHR* surface);
#endif
    void DestroySurfaceKHR(
        VkInstance instance,
        VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator);
    VkResult CreateDevice(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* createInfo,
        const VkAllocationCallbacks* allocator,
        VkDevice* device);
    void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);

    VkResult CreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator,
        VkSwapchainKHR* swapchain);
    void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator);
    VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images);
    VkResult AcquireNextImageKHR(
        VkDevice device,
        VkSwapchainKHR swapchain,
        uint64_t timeout,
        VkSemaphore semaphore,
        VkFence fence,
        uint32_t* imageIndex);
    VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo);
    VkResult DeviceWaitIdle(VkDevice device);
}
