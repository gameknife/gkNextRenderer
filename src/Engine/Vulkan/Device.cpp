#include "Device.hpp"
#include "DebugUtilities.hpp"
#include "Instance.hpp"
#include "WindowSurface.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanVideoCaps.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

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

	// for ( auto queue : queueFamilies )
	// {
	// 	std::cout << "Queue Family: " << queue.queueFlags << " count: " << queue.queueCount << std::endl;
	// }
	

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

	// Video encode queue, only when the encode extensions were requested (RemoteMode probe).
	const bool videoEncodeRequested = std::any_of(requiredExtensions.begin(), requiredExtensions.end(),
		[](const char* extension)
		{
			return std::strcmp(extension, VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME) == 0;
		});
	if (videoEncodeRequested)
	{
		videoEncodeFamilyIndex_ = Runtime::Remote::FVulkanVideoCaps::FindEncodeH264QueueFamily(physicalDevice);
		if (videoEncodeFamilyIndex_ == UINT32_MAX)
		{
			SPDLOG_WARN("Video encode extensions requested but no H.264 encode queue family was found");
		}
	}

	// Queues can be the same
	std::set<uint32_t> uniqueQueueFamilies =
	{
		graphicsFamilyIndex_,
		//computeFamilyIndex_,
		presentFamilyIndex_,
		transferFamilyIndex_
	};
	if (videoEncodeFamilyIndex_ != UINT32_MAX)
	{
		uniqueQueueFamilies.insert(videoEncodeFamilyIndex_);
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

	Check(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device_),
		"create logical device");

	debugUtils_.SetDevice(device_);

	vkGetDeviceQueue(device_, graphicsFamilyIndex_, 0, &graphicsQueue_);
	vkGetDeviceQueue(device_, computeFamilyIndex_, 0, &computeQueue_);
	vkGetDeviceQueue(device_, presentFamilyIndex_, 0, &presentQueue_);
	vkGetDeviceQueue(device_, transferFamilyIndex_, 0, &transferQueue_);
	if (videoEncodeFamilyIndex_ != UINT32_MAX)
	{
		vkGetDeviceQueue(device_, videoEncodeFamilyIndex_, 0, &videoEncodeQueue_);
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
		vkDestroyDevice(device_, nullptr);
		device_ = nullptr;
	}
}

MemoryStatsSnapshot Device::CaptureMemoryStats(bool includeDetails) const
{
	return memoryAllocator_->CaptureStats(includeDetails);
}

void Device::WaitIdle() const
{
	Check(vkDeviceWaitIdle(device_),
		"wait for device idle");
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
