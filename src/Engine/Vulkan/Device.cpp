#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <fmt/format.h>

namespace Vulkan {

namespace
{
    std::vector<VkQueueFamilyProperties>::const_iterator FindQueue(
        const std::vector<VkQueueFamilyProperties>& queueFamilies,
        const std::string& name,
        const VkQueueFlags requiredBits,
        const VkQueueFlags excludedBits,
        uint32_t minCount)
    {
        const auto family = std::find_if(queueFamilies.begin(), queueFamilies.end(), [requiredBits, excludedBits, minCount](const VkQueueFamilyProperties& queueFamily)
        {
            return 
                queueFamily.queueCount >= minCount && 
                queueFamily.queueFlags & requiredBits &&
                !(queueFamily.queueFlags & excludedBits);
        });

        if (family == queueFamilies.end())
        {
            Throw(std::runtime_error(fmt::format("found no matching {} queue", name)));
        }

        return family;
    }
}

Device::Device(
    VkPhysicalDevice physicalDevice, 
    const class Surface& surface, 
    const std::vector<const char*>& requiredExtensions,
    const VkPhysicalDeviceFeatures& deviceFeatures,
    const void* nextDeviceFeatures) :
    physicalDevice_(physicalDevice),
    surface_(surface),
    debugUtils_(surface.Instance().Handle())
{
    CheckRequiredExtensions(physicalDevice, requiredExtensions);

    const auto queueFamilies = GetEnumerateVector(physicalDevice, vkGetPhysicalDeviceQueueFamilyProperties);


    // Find the graphics queue.
    const auto graphicsFamily = FindQueue(queueFamilies, "graphics", VK_QUEUE_GRAPHICS_BIT, 0, 1);

    // USE SPARSE BINDING AS THREAD LOAD QUEUE
    // On MoltenVK, the total queue count is 1, cannot create more than 1 queue.
#if __APPLE__
    const auto transferFamily = graphicsFamily;
#else
#if ANDROID
    //const auto transferFamily = graphicsFamily;
    auto transferFamily = std::find_if(queueFamilies.begin(), queueFamilies.end(), [](const VkQueueFamilyProperties& queueFamily)
    {
        return queueFamily.queueCount >= 1 &&
            (queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT);
    });
#else
    auto transferFamily = std::find_if(queueFamilies.begin(), queueFamilies.end(), [](const VkQueueFamilyProperties& queueFamily)
    {
        return queueFamily.queueCount >= 1 &&
            (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT);
    });
#endif
    if (transferFamily == queueFamilies.end())
    {
        SPDLOG_INFO("No dedicated transfer queue found; using graphics queue for transfers");
        transferFamily = graphicsFamily;
    }
#endif
    
    //Commented out for Macos compatibility, and this queue is not in use actually
    //const auto computeFamily = FindQueue(queueFamilies, "compute", VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
    
    // Find the presentation queue (usually the same as graphics queue).
    const auto presentFamily = std::find_if(queueFamilies.begin(), queueFamilies.end(), [&](const VkQueueFamilyProperties& queueFamily)
    {
        VkBool32 presentSupport = false;
        const uint32_t i = static_cast<uint32_t>(&queueFamily - queueFamilies.data());
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface.Handle(), &presentSupport);
        return queueFamily.queueCount > 0 && presentSupport;
    });

    if (presentFamily == queueFamilies.end())
    {
        Throw(std::runtime_error("found no presentation queue"));
    }

    graphicsFamilyIndex_ = static_cast<uint32_t>(graphicsFamily - queueFamilies.begin());
    computeFamilyIndex_ = graphicsFamilyIndex_;
    presentFamilyIndex_ = static_cast<uint32_t>(presentFamily - queueFamilies.begin());
    transferFamilyIndex_ = static_cast<uint32_t>(transferFamily - queueFamilies.begin());

    // Queues can be the same
    std::set<uint32_t> uniqueQueueFamilies =
    {
        graphicsFamilyIndex_,
        //computeFamilyIndex_,
        presentFamilyIndex_,
        transferFamilyIndex_
    };

    // Extra queue families requested by modules (e.g. NextRemote video encode).
    for (IDeviceCreationAugmenter* augmenter : DeviceCreationAugmenters())
    {
        const uint32_t extraFamily = augmenter->AdditionalQueueFamily(physicalDevice);
        if (extraFamily != UINT32_MAX)
        {
            uniqueQueueFamilies.insert(extraFamily);
            extraQueues_[extraFamily] = VK_NULL_HANDLE;
        }
    }

    // Create queues
    std::vector<float> queuePriority = {1.0f};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    for (uint32_t queueFamilyIndex : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = queuePriority.data();

        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Create device
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = nextDeviceFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledLayerCount = static_cast<uint32_t>(surface_.Instance().ValidationLayers().size());
    createInfo.ppEnabledLayerNames = surface_.Instance().ValidationLayers().data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    Check(Interposer().CreateDevice(physicalDevice, &createInfo, nullptr, &device_),
        "create logical device");

    debugUtils_.SetDevice(device_);

    vkGetDeviceQueue(device_, graphicsFamilyIndex_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, computeFamilyIndex_, 0, &computeQueue_);
    vkGetDeviceQueue(device_, presentFamilyIndex_, 0, &presentQueue_);
    vkGetDeviceQueue(device_, transferFamilyIndex_, 0, &transferQueue_);
    for (auto& [familyIndex, queue] : extraQueues_)
    {
        vkGetDeviceQueue(device_, familyIndex, 0, &queue);
    }

    vkGetPhysicalDeviceProperties(PhysicalDevice(), &deviceProp_);
    
    deviceProcedures_.reset(new DeviceProcedures(*this, true, true));
    memoryAllocator_.reset(new MemoryAllocator(*this));
}

Device::~Device()
{
    if (device_ != nullptr)
    {
        memoryAllocator_.reset();
        deviceProcedures_.reset();
        Interposer().DestroyDevice(device_, nullptr);
        device_ = nullptr;
    }
}

MemoryStatsSnapshot Device::CaptureMemoryStats(bool includeDetails) const
{
    return memoryAllocator_->CaptureStats(includeDetails);
}

void Device::WaitIdle() const
{
    Check(Interposer().DeviceWaitIdle(device_),
        "wait for device idle");
}

VkQueue Device::QueueForFamilyIndex(uint32_t queueFamilyIndex) const
{
    if (queueFamilyIndex == graphicsFamilyIndex_)
    {
        return graphicsQueue_;
    }
    if (queueFamilyIndex == computeFamilyIndex_)
    {
        return computeQueue_;
    }
    if (queueFamilyIndex == presentFamilyIndex_)
    {
        return presentQueue_;
    }
    if (queueFamilyIndex == static_cast<uint32_t>(transferFamilyIndex_))
    {
        return transferQueue_;
    }
    if (const auto extraQueue = extraQueues_.find(queueFamilyIndex); extraQueue != extraQueues_.end())
    {
        return extraQueue->second;
    }
    Throw(std::runtime_error(fmt::format("queue family {} is not available on this device", queueFamilyIndex)));
}

void Device::CheckRequiredExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const
{
    const auto availableExtensions = GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr), vkEnumerateDeviceExtensionProperties);
    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());

    for (const auto& extension : availableExtensions) 
    {
        required.erase(extension.extensionName);
    }

    if (!required.empty())
    {
        bool first = true;
        std::string extensions;

        for (const auto& extension : required)
        {
            if (!first)
            {
                extensions += ", ";
            }

            extensions += extension;
            first = false;
        }

        Throw(std::runtime_error("missing required extensions: " + extensions));
    }
}

}
