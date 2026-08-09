#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <map>
#include <vector>

namespace Vulkan
{
    // Registered by modules before logical device creation to request extra device
    // extensions / feature-chain entries and dedicated queue families (e.g. the
    // NextRemote video encode queue) without the engine core knowing about them.
    class IDeviceCreationAugmenter
    {
    public:
        virtual ~IDeviceCreationAugmenter() = default;

        // Append device extensions and optionally extend the feature chain.
        // Returns the new chain head (or featureChain unchanged). Any feature
        // struct linked into the chain must outlive device creation.
        virtual void* OnPhysicalDeviceSelected(VkInstance instance,
                                               VkPhysicalDevice physicalDevice,
                                               std::vector<const char*>& requiredExtensions,
                                               void* featureChain) = 0;

        // Additional queue family to create a queue for, or UINT32_MAX.
        virtual uint32_t AdditionalQueueFamily(VkPhysicalDevice physicalDevice) { return UINT32_MAX; }

        // Request minimum queue counts per family for features that require
        // multiple distinct VkQueue handles.
        virtual void AugmentQueueRequests(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          std::map<uint32_t, uint32_t>& queueCounts) {}
    };

    void RegisterDeviceCreationAugmenter(IDeviceCreationAugmenter* augmenter);
    const std::vector<IDeviceCreationAugmenter*>& DeviceCreationAugmenters();
}
