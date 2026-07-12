#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <aclapi.h>
#include <dxgi1_2.h>
#endif

namespace Vulkan
{

// ============================================================================
// Core Vulkan Utilities
// ============================================================================

void Check(const VkResult result, const char* const operation)
{
    if (result != VK_SUCCESS)
    {
        Throw(std::runtime_error(fmt::format("failed to {} ({})", operation, ToString(result))));
    }
}

const char* ToString(const VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS: return "SUCCESS";
        case VK_NOT_READY: return "NOT_READY";
        case VK_TIMEOUT: return "TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_DEVICE_LOST: return "ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "ERROR_VALIDATION_FAILED_EXT";
        default:
        {
            static thread_local char buf[32];
            std::snprintf(buf, sizeof(buf), "VkResult(%d)", static_cast<int>(result));
            return buf;
        }
    }
}

// ============================================================================
// Strings
// ============================================================================

const char* Strings::DeviceType(const VkPhysicalDeviceType deviceType)
{
    switch (deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "Other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
        default: return "UnknownDeviceType";
    }
}

const char* Strings::VendorId(const uint32_t vendorId)
{
    switch (vendorId)
    {
        case 0x1002: return "AMD";
        case 0x1010: return "ImgTec";
        case 0x10DE: return "NVIDIA";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x8086: return "INTEL";
        default: return "UnknownVendor";
    }
}

// ============================================================================
// DebugUtilsMessenger (anonymous helpers)
// ============================================================================

namespace
{

const char* ObjectTypeToString(const VkObjectType objectType)
{
    switch (objectType)
    {
        case VK_OBJECT_TYPE_INSTANCE: return "Instance";
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "PhysicalDevice";
        case VK_OBJECT_TYPE_DEVICE: return "Device";
        case VK_OBJECT_TYPE_QUEUE: return "Queue";
        case VK_OBJECT_TYPE_SEMAPHORE: return "Semaphore";
        case VK_OBJECT_TYPE_COMMAND_BUFFER: return "CommandBuffer";
        case VK_OBJECT_TYPE_COMMAND_POOL: return "CommandPool";
        case VK_OBJECT_TYPE_FENCE: return "Fence";
        case VK_OBJECT_TYPE_DEVICE_MEMORY: return "DeviceMemory";
        case VK_OBJECT_TYPE_BUFFER: return "Buffer";
        case VK_OBJECT_TYPE_IMAGE: return "Image";
        case VK_OBJECT_TYPE_IMAGE_VIEW: return "ImageView";
        case VK_OBJECT_TYPE_SHADER_MODULE: return "ShaderModule";
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "PipelineLayout";
        case VK_OBJECT_TYPE_RENDER_PASS: return "RenderPass";
        case VK_OBJECT_TYPE_PIPELINE: return "Pipeline";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "DescriptorSet";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "DescriptorSetLayout";
        case VK_OBJECT_TYPE_SAMPLER: return "Sampler";
        case VK_OBJECT_TYPE_FRAMEBUFFER: return "Framebuffer";
        case VK_OBJECT_TYPE_SURFACE_KHR: return "SurfaceKHR";
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR: return "SwapchainKHR";
        case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR: return "AccelerationStructureKHR";
        case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT: return "DebugUtilsMessengerEXT";
        default: return "unknown";
    }
}

const char* SeverityTag(const VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    switch (severity)
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "INFO";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* MessageTypeTag(const VkDebugUtilsMessageTypeFlagsEXT messageType)
{
    switch (messageType)
    {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: return "GENERAL";
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: return "VALIDATION";
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: return "PERFORMANCE";
        default: return "UNKNOWN";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    const VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* const pCallbackData,
    void* const pUserData)
{
    (void)pUserData;

    // Build the full message once. Attach object list only when severity is higher than INFO.
    std::string message = fmt::format("[Vulkan][{}][{}] {}",
        SeverityTag(messageSeverity), MessageTypeTag(messageType), pCallbackData->pMessage);

    if (pCallbackData->objectCount > 0 && messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        message += fmt::format("\n\n  Objects ({}):", pCallbackData->objectCount);
        for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
        {
            const auto& object = pCallbackData->pObjects[i];
            message += fmt::format("\n  - Object: Type: {}, Handle: {:p}, Name: '{}'",
                ObjectTypeToString(object.objectType),
                reinterpret_cast<const void*>(object.objectHandle),
                object.pObjectName ? object.pObjectName : "");
        }
    }

    // Route the complete message by severity.
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

VkResult CreateDebugUtilsMessengerExt(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pCallback)
{
    const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return func != nullptr ? func(instance, pCreateInfo, pAllocator, pCallback) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerExt(VkInstance instance, VkDebugUtilsMessengerEXT callback, const VkAllocationCallbacks* pAllocator)
{
    if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")))
    {
        func(instance, callback, pAllocator);
    }
}

} // namespace

// ============================================================================
// DebugUtils
// ============================================================================

DebugUtils::DebugUtils(VkInstance instance)
    : vkSetDebugUtilsObjectNameEXT_(reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT")))
    , vkCmdBeginDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT")))
    , vkCmdEndDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT")))
{
#if !ANDROID
    if (vkSetDebugUtilsObjectNameEXT_ == nullptr || vkCmdBeginDebugUtilsLabelEXT_ == nullptr || vkCmdEndDebugUtilsLabelEXT_ == nullptr)
    {
        Throw(std::runtime_error("failed to load VK_EXT_debug_utils entry points"));
    }
#endif
}

void DebugUtils::BeginMarker(VkCommandBuffer commandBuffer, const char* name) const
{
#if !ANDROID
    VkDebugUtilsLabelEXT label
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext = nullptr,
        .pLabelName = name,
        .color = {0.0f, 0.0f, 0.0f, 0.0f}
    };
    vkCmdBeginDebugUtilsLabelEXT_(commandBuffer, &label);
#else
    (void)commandBuffer;
    (void)name;
#endif
}

void DebugUtils::EndMarker(VkCommandBuffer commandBuffer) const
{
#if !ANDROID
    vkCmdEndDebugUtilsLabelEXT_(commandBuffer);
#else
    (void)commandBuffer;
#endif
}

// ============================================================================
// DebugUtilsMessenger
// ============================================================================

DebugUtilsMessenger::DebugUtilsMessenger(const Instance& instance, VkDebugUtilsMessageSeverityFlagBitsEXT threshold)
    : instance_(instance)
    , threshold_(threshold)
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
            [[fallthrough]];
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
            [[fallthrough]];
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            [[fallthrough]];
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            break;
        default:
            Throw(std::invalid_argument("invalid threshold"));
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = severity,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = nullptr
    };

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

} // namespace Vulkan
