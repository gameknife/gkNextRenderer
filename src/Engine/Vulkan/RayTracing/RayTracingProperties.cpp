#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"

// Core hardware RT implementation owned by gkNextEngine.
#include "Engine/Vulkan/Device.hpp"
#include <algorithm>
#include <cstring>

namespace Vulkan::RayTracing {

RayTracingProperties::RayTracingProperties(const class Device& device) :
    device_(device)
{
    accelProps_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    const auto extensions = GetEnumerateVector(device.PhysicalDevice(), static_cast<const char*>(nullptr),
                                               vkEnumerateDeviceExtensionProperties);
    const bool hasRayTracingPipeline = std::any_of(extensions.begin(), extensions.end(),
        [](const VkExtensionProperties& extension)
        {
            return std::strcmp(extension.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0;
        });

    if (hasRayTracingPipeline)
    {
        pipelineProps_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        accelProps_.pNext = &pipelineProps_;
    }

    props.pNext = &accelProps_;
    vkGetPhysicalDeviceProperties2(device.PhysicalDevice(), &props);
}

}
