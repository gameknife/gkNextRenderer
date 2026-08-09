#include "Engine/Vulkan/VulkanInterposer.hpp"

#include <algorithm>

namespace Vulkan
{
    bool IVulkanSwapchainInterposer::OwnsPresent(const VkPresentInfoKHR* presentInfo) const
    {
        if (presentInfo == nullptr || presentInfo->pSwapchains == nullptr)
        {
            return false;
        }
        for (uint32_t i = 0; i < presentInfo->swapchainCount; ++i)
        {
            if (OwnsSwapchain(presentInfo->pSwapchains[i]))
            {
                return true;
            }
        }
        return false;
    }

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

        std::vector<IVulkanSwapchainInterposer*>& SwapchainInterposers()
        {
            static std::vector<IVulkanSwapchainInterposer*> interposers;
            return interposers;
        }

        IVulkanInterposer& PrimaryInterposer()
        {
            return GInterposer != nullptr ? *GInterposer : DefaultInterposer();
        }

        class FCompositeInterposer final : public IVulkanInterposer
        {
        public:
            const char* PreferredVulkanLoaderPath() override
            {
                return PrimaryInterposer().PreferredVulkanLoaderPath();
            }

            void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions) override
            {
                PrimaryInterposer().AppendRequiredInstanceExtensions(extensions);
            }

            VkResult CreateInstance(const VkInstanceCreateInfo* createInfo,
                                    const VkAllocationCallbacks* allocator,
                                    VkInstance* instance) override
            {
                return PrimaryInterposer().CreateInstance(createInfo, allocator, instance);
            }

            void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) override
            {
                PrimaryInterposer().DestroyInstance(instance, allocator);
            }

            VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count,
                                              VkPhysicalDevice* physicalDevices) override
            {
                return PrimaryInterposer().EnumeratePhysicalDevices(instance, count, physicalDevices);
            }

#if WIN32
            VkResult CreateWin32SurfaceKHR(VkInstance instance,
                                           const VkWin32SurfaceCreateInfoKHR* createInfo,
                                           const VkAllocationCallbacks* allocator,
                                           VkSurfaceKHR* surface) override
            {
                return PrimaryInterposer().CreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
            }
#endif

            void DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                   const VkAllocationCallbacks* allocator) override
            {
                PrimaryInterposer().DestroySurfaceKHR(instance, surface, allocator);
            }

            VkResult CreateDevice(VkPhysicalDevice physicalDevice,
                                  const VkDeviceCreateInfo* createInfo,
                                  const VkAllocationCallbacks* allocator,
                                  VkDevice* device) override
            {
                return PrimaryInterposer().CreateDevice(physicalDevice, createInfo, allocator, device);
            }

            void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) override
            {
                PrimaryInterposer().DestroyDevice(device, allocator);
            }

            VkResult DeviceWaitIdle(VkDevice device) override
            {
                for (IVulkanSwapchainInterposer* interposer : SwapchainInterposers())
                {
                    interposer->BeforeDeviceWaitIdle(device);
                }
                return PrimaryInterposer().DeviceWaitIdle(device);
            }

            VkResult CreateSwapchainKHR(VkDevice device,
                                        const VkSwapchainCreateInfoKHR* createInfo,
                                        const VkAllocationCallbacks* allocator,
                                        VkSwapchainKHR* swapchain) override
            {
                for (IVulkanSwapchainInterposer* interposer : SwapchainInterposers())
                {
                    VkResult result = VK_SUCCESS;
                    if (interposer->TryCreateSwapchainKHR(
                            device, createInfo, allocator, swapchain, result))
                    {
                        return result;
                    }
                }
                return PrimaryInterposer().CreateSwapchainKHR(device, createInfo, allocator, swapchain);
            }

            void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                     const VkAllocationCallbacks* allocator) override
            {
                if (IVulkanSwapchainInterposer* owner = FindOwner(swapchain))
                {
                    owner->DestroySwapchainKHR(device, swapchain, allocator);
                    return;
                }
                PrimaryInterposer().DestroySwapchainKHR(device, swapchain, allocator);
            }

            VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                           uint32_t* count, VkImage* images) override
            {
                if (IVulkanSwapchainInterposer* owner = FindOwner(swapchain))
                {
                    return owner->GetSwapchainImagesKHR(device, swapchain, count, images);
                }
                return PrimaryInterposer().GetSwapchainImagesKHR(device, swapchain, count, images);
            }

            VkResult AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                         VkSemaphore semaphore, VkFence fence,
                                         uint32_t* imageIndex) override
            {
                if (IVulkanSwapchainInterposer* owner = FindOwner(swapchain))
                {
                    return owner->AcquireNextImageKHR(
                        device, swapchain, timeout, semaphore, fence, imageIndex);
                }
                return PrimaryInterposer().AcquireNextImageKHR(
                    device, swapchain, timeout, semaphore, fence, imageIndex);
            }

            VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) override
            {
                for (IVulkanSwapchainInterposer* interposer : SwapchainInterposers())
                {
                    if (interposer->OwnsPresent(presentInfo))
                    {
                        return interposer->QueuePresentKHR(queue, presentInfo);
                    }
                }
                return PrimaryInterposer().QueuePresentKHR(queue, presentInfo);
            }

        private:
            static IVulkanSwapchainInterposer* FindOwner(VkSwapchainKHR swapchain)
            {
                for (IVulkanSwapchainInterposer* interposer : SwapchainInterposers())
                {
                    if (interposer->OwnsSwapchain(swapchain))
                    {
                        return interposer;
                    }
                }
                return nullptr;
            }
        };
    }

    IVulkanInterposer& Interposer()
    {
        static FCompositeInterposer compositeInterposer;
        return compositeInterposer;
    }

    void SetInterposer(IVulkanInterposer* interposer)
    {
        GInterposer = interposer;
    }

    void RegisterSwapchainInterposer(IVulkanSwapchainInterposer* interposer)
    {
        if (interposer != nullptr &&
            std::find(SwapchainInterposers().begin(), SwapchainInterposers().end(), interposer) ==
                SwapchainInterposers().end())
        {
            SwapchainInterposers().push_back(interposer);
        }
    }
}
