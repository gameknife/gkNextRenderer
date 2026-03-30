#include "Instance.hpp"
#include "DebugUtilities.hpp"
#include "DebugUtilities.hpp"
#include "WindowSurface.hpp"
#include "Utilities/Exception.hpp"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>

namespace Vulkan {

Instance::Instance(const class Window& window, const std::vector<const char*>& validationLayers, uint32_t vulkanVersion) :
	window_(window),
	validationLayers_(validationLayers)
{
	// Check the minimum version.
	CheckVulkanMinimumVersion(vulkanVersion);

	// Get the list of required extensions.
	auto extensions = window.GetRequiredInstanceExtensions();

	// Check the validation layers and add them to the list of required extensions.
	CheckVulkanValidationLayerSupport(validationLayers);

#if WITH_STREAMLINE
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif
    
#if !ANDROID
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
	
	extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#if WIN32
	extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
#endif	
    
#if __APPLE__
	extensions.push_back("VK_EXT_swapchain_colorspace");
#endif

	// Create the Vulkan instance.
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "gkNextRenderer";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = vulkanVersion;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	createInfo.ppEnabledLayerNames = validationLayers.data();
    
	Check(vkCreateInstance(&createInfo, nullptr, &instance_),
		"create instance");

	GetVulkanPhysicalDevices();
	GetVulkanLayers();
	GetVulkanExtensions();
}

Instance::~Instance()
{
	if (instance_ != nullptr)
	{
		vkDestroyInstance(instance_, nullptr);
		instance_ = nullptr;
	}
}

void Instance::GetVulkanExtensions()
{
	GetEnumerateVector(static_cast<const char*>(nullptr), vkEnumerateInstanceExtensionProperties, extensions_);
}

void Instance::GetVulkanLayers()
{
	GetEnumerateVector(vkEnumerateInstanceLayerProperties, layers_);
}

void Instance::GetVulkanPhysicalDevices()
{
	GetEnumerateVector(instance_, vkEnumeratePhysicalDevices, physicalDevices_);

	if (physicalDevices_.empty())
	{
		Throw(std::runtime_error("found no Vulkan physical devices"));
	}
}

bool Instance::SupportsRayQuery() const
{
	for (const auto& device : physicalDevices_)
	{
		const auto extensions = GetEnumerateVector(device, static_cast<const char*>(nullptr),
		                                           vkEnumerateDeviceExtensionProperties);
		const auto hasRayQuery = std::any_of(extensions.begin(), extensions.end(),
			[](const VkExtensionProperties& extension)
			{
				return std::strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
			});
		if (hasRayQuery)
		{
			return true;
		}
	}
	return false;
}

void Instance::CheckVulkanMinimumVersion(const uint32_t minVersion)
{
	#if !ANDROID
	uint32_t version;
	Check(vkEnumerateInstanceVersion(&version),
		"query instance version");

	if (minVersion > version)
	{
		std::string out = fmt::format("minimum required version not found (required {}, found {})", to_string(Version(minVersion)), to_string(Version(version)));

		Throw(std::runtime_error(out));
	}
	#endif
}

void Instance::CheckVulkanValidationLayerSupport(const std::vector<const char*>& validationLayers)
{
	const auto availableLayers = GetEnumerateVector(vkEnumerateInstanceLayerProperties);

	for (const char* layer : validationLayers)
	{
		auto result = std::find_if(availableLayers.begin(), availableLayers.end(), [layer](const VkLayerProperties& layerProperties)
		{
			return strcmp(layer, layerProperties.layerName) == 0;
		});

		if (result == availableLayers.end())
		{
			Throw(std::runtime_error("could not find the requested validation layer: '" + std::string(layer) + "'"));
		}
	}
}

}
