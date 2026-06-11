#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#ifdef VK_USE_PLATFORM_WIN32_KHR
#	include <aclapi.h>
#	include <dxgi1_2.h>
#endif

namespace Vulkan {

// ============================================================================
// Core Vulkan Utilities
// ============================================================================

void Check(const VkResult result, const char* const operation)
{
	if (result != VK_SUCCESS)
	{
		Throw(std::runtime_error(std::string("failed to ") + operation + " (" + ToString(result) + ")"));
	}
}

const char* ToString(const VkResult result)
{
	switch (result)
	{
#define STR(r) case VK_ ##r: return #r
		STR(SUCCESS);
		STR(NOT_READY);
		STR(TIMEOUT);
		STR(EVENT_SET);
		STR(EVENT_RESET);
		STR(INCOMPLETE);
		STR(ERROR_OUT_OF_HOST_MEMORY);
		STR(ERROR_OUT_OF_DEVICE_MEMORY);
		STR(ERROR_INITIALIZATION_FAILED);
		STR(ERROR_DEVICE_LOST);
		STR(ERROR_MEMORY_MAP_FAILED);
		STR(ERROR_LAYER_NOT_PRESENT);
		STR(ERROR_EXTENSION_NOT_PRESENT);
		STR(ERROR_FEATURE_NOT_PRESENT);
		STR(ERROR_INCOMPATIBLE_DRIVER);
		STR(ERROR_TOO_MANY_OBJECTS);
		STR(ERROR_FORMAT_NOT_SUPPORTED);
		STR(ERROR_FRAGMENTED_POOL);
		STR(ERROR_UNKNOWN);
		STR(ERROR_OUT_OF_POOL_MEMORY);
		STR(ERROR_INVALID_EXTERNAL_HANDLE);
		STR(ERROR_FRAGMENTATION);
		STR(ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
		STR(ERROR_SURFACE_LOST_KHR);
		STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		STR(SUBOPTIMAL_KHR);
		STR(ERROR_OUT_OF_DATE_KHR);
		STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		STR(ERROR_VALIDATION_FAILED_EXT);
		STR(ERROR_INVALID_SHADER_NV);
		STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
		STR(ERROR_NOT_PERMITTED_EXT);
		STR(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
		STR(THREAD_IDLE_KHR);
		STR(THREAD_DONE_KHR);
		STR(OPERATION_DEFERRED_KHR);
		STR(OPERATION_NOT_DEFERRED_KHR);
		STR(PIPELINE_COMPILE_REQUIRED_EXT);
#undef STR
	default:
		return "UNKNOWN_ERROR";
	}
}

// ============================================================================
// Strings
// ============================================================================

const char* Strings::DeviceType(const VkPhysicalDeviceType deviceType)
{
	switch (deviceType)
	{
	case VK_PHYSICAL_DEVICE_TYPE_OTHER:
		return "Other";
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		return "Integrated GPU";
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		return "Discrete GPU";
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
		return "Virtual GPU";
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		return "CPU";
	default:
		return "UnknownDeviceType";
	}
}

const char* Strings::VendorId(const uint32_t vendorId)
{
	switch (vendorId)
	{
	case 0x1002:
		return "AMD";
	case 0x1010:
		return "ImgTec";
	case 0x10DE:
		return "NVIDIA";
	case 0x13B5:
		return "ARM";
	case 0x5143:
		return "Qualcomm";
	case 0x8086:
		return "INTEL";
	default:
		return "UnknownVendor";
	}
}

// ============================================================================
// DebugUtils
// ============================================================================

DebugUtils::DebugUtils(VkInstance instance)
	: vkSetDebugUtilsObjectNameEXT_(reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT")))
	, vkCmdBeginDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT")))
	, vkCmdEndDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT")))
{
#if !ANDROID
	if (vkSetDebugUtilsObjectNameEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkSetDebugUtilsObjectNameEXT'"));
	}
	if (vkCmdBeginDebugUtilsLabelEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkCmdBeginDebugUtilsLabelEXT'"));
	}
	if (vkCmdEndDebugUtilsLabelEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkCmdEndDebugUtilsLabelEXT'"));
	}
#endif
}

// ============================================================================
// DebugUtilsMessenger
// ============================================================================

namespace
{

	const char* ObjectTypeToString(const VkObjectType objectType)
	{
		switch (objectType)
		{
#define STR(e) case VK_OBJECT_TYPE_ ## e: return # e
		STR(UNKNOWN);
		STR(INSTANCE);
		STR(PHYSICAL_DEVICE);
		STR(DEVICE);
		STR(QUEUE);
		STR(SEMAPHORE);
		STR(COMMAND_BUFFER);
		STR(FENCE);
		STR(DEVICE_MEMORY);
		STR(BUFFER);
		STR(IMAGE);
		STR(EVENT);
		STR(QUERY_POOL);
		STR(BUFFER_VIEW);
		STR(IMAGE_VIEW);
		STR(SHADER_MODULE);
		STR(PIPELINE_CACHE);
		STR(PIPELINE_LAYOUT);
		STR(RENDER_PASS);
		STR(PIPELINE);
		STR(DESCRIPTOR_SET_LAYOUT);
		STR(SAMPLER);
		STR(DESCRIPTOR_POOL);
		STR(DESCRIPTOR_SET);
		STR(FRAMEBUFFER);
		STR(COMMAND_POOL);
		STR(SAMPLER_YCBCR_CONVERSION);
		STR(DESCRIPTOR_UPDATE_TEMPLATE);
		STR(SURFACE_KHR);
		STR(SWAPCHAIN_KHR);
		STR(DISPLAY_KHR);
		STR(DISPLAY_MODE_KHR);
		STR(DEBUG_REPORT_CALLBACK_EXT);
		STR(DEBUG_UTILS_MESSENGER_EXT);
		STR(ACCELERATION_STRUCTURE_KHR);
		STR(VALIDATION_CACHE_EXT);
		STR(PERFORMANCE_CONFIGURATION_INTEL);
		STR(DEFERRED_OPERATION_KHR);
		STR(INDIRECT_COMMANDS_LAYOUT_NV);
#undef STR
		default: return "unknown";
		}
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
		const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		const VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* const pCallbackData,
		void* const pUserData)
	{
		(void)pUserData;

		// Build complete message in one go
		std::string message;

		// Add severity prefix
		switch (messageSeverity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			message += "VERBOSE: ";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			message += "INFO: ";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			message += "WARNING: ";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			message += "ERROR: ";
			break;
		default:
			message += "UNKNOWN: ";
		}

		// Add message type
		switch (messageType)
		{
		case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
			message += "GENERAL: ";
			break;
		case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
			message += "VALIDATION: ";
			break;
		case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
			message += "PERFORMANCE: ";
			break;
		default:
			message += "UNKNOWN: ";
		}

		// Add main message
		message += pCallbackData->pMessage;

		// Add object information if present and severity is high enough
		if (pCallbackData->objectCount > 0 && messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		{
			message += "\n\n  Objects (";
			message += std::to_string(pCallbackData->objectCount);
			message += "):";

			for (uint32_t i = 0; i != pCallbackData->objectCount; ++i)
			{
				const auto object = pCallbackData->pObjects[i];
				message += "\n  - Object: Type: ";
				message += ObjectTypeToString(object.objectType);
				message += ", Handle: ";
				// Convert handle to hex string for better readability
				char handleStr[20];
				snprintf(handleStr, sizeof(handleStr), "%p", reinterpret_cast<void*>(object.objectHandle));
				message += handleStr;
				message += ", Name: '";
				message += (object.pObjectName ? object.pObjectName : "");
				message += "'";
			}
		}

		// Log the complete message based on severity
		switch (messageSeverity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			SPDLOG_TRACE("{}", message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			SPDLOG_INFO("{}", message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			SPDLOG_WARN("{}", message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			SPDLOG_ERROR("{}", message);
			break;
		default:
			SPDLOG_WARN("{}", message);
		}


		return VK_FALSE;
	}

	VkResult CreateDebugUtilsMessengerExt(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pCallback)
	{
		const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
		return func != nullptr
			? func(instance, pCreateInfo, pAllocator, pCallback)
			: VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	void DestroyDebugUtilsMessengerExt(VkInstance instance, VkDebugUtilsMessengerEXT callback, const VkAllocationCallbacks* pAllocator)
	{
		const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (func != nullptr) {
			func(instance, callback, pAllocator);
		}
	}
}

DebugUtilsMessenger::DebugUtilsMessenger(const Instance& instance, VkDebugUtilsMessageSeverityFlagBitsEXT threshold) :
	instance_(instance),
	threshold_(threshold)
{
	if (instance.ValidationLayers().empty())
	{
		return;
	}

	VkDebugUtilsMessageSeverityFlagsEXT severity = 0;

	switch (threshold)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		break;
	default:
		Throw(std::invalid_argument("invalid threshold"));
	}

	VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity = severity;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = VulkanDebugCallback;
	createInfo.pUserData = nullptr;

	Check(CreateDebugUtilsMessengerExt(instance_.Handle(), &createInfo, nullptr, &messenger_),
		"set up Vulkan debug callback");
}

DebugUtilsMessenger::~DebugUtilsMessenger()
{
	if (messenger_ != nullptr)
	{
		DestroyDebugUtilsMessengerExt(instance_.Handle(), messenger_, nullptr);
		messenger_ = nullptr;
	}
}

}
