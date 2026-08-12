#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>

namespace Vulkan {

namespace
{
    void AppendUniqueExtension(std::vector<const char*>& extensions, const char* extensionName)
    {
        if (extensionName == nullptr)
        {
            return;
        }

        const auto existing = std::find_if(extensions.begin(), extensions.end(),
            [extensionName](const char* current)
            {
                return current != nullptr && std::strcmp(current, extensionName) == 0;
            });
        if (existing == extensions.end())
        {
            extensions.push_back(extensionName);
        }
    }

}

Instance::Instance(const class Window& window, const std::vector<const char*>& validationLayers, uint32_t vulkanVersion,
                   const bool enableSynchronizationValidation) :
    window_(window),
    validationLayers_(validationLayers)
{
    // Check the minimum version.
    CheckVulkanMinimumVersion(vulkanVersion);

    // Get the list of required extensions.
    auto extensions = window.GetRequiredInstanceExtensions();
    const auto availableExtensions = GetEnumerateVector(static_cast<const char*>(nullptr), vkEnumerateInstanceExtensionProperties);

    const auto hasInstanceExtension = [&availableExtensions](const char* extensionName)
    {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
            [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
    };

    if (window.IsHeadless() && !hasInstanceExtension(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME))
    {
        Throw(std::runtime_error(
            "VK_EXT_headless_surface is unavailable from the selected Vulkan ICD. "
            "Use a Mesa driver with headless-surface support (for example Lavapipe)."));
    }

    // Check the validation layers and add them to the list of required extensions.
    CheckVulkanValidationLayerSupport(validationLayers);

    Interposer().AppendRequiredInstanceExtensions(extensions);
    
#if !ANDROID
    AppendUniqueExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    
    if (hasInstanceExtension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME))
    {
        AppendUniqueExtension(extensions, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    }
    // Required before a swapchain may be created with any colorspace other than
    // SRGB_NONLINEAR. SwapChain::ChooseSwapSurfaceFormat picks HDR10/EDR formats purely
    // from what the surface reports, so this must be enabled wherever it is available -
    // otherwise the swapchain is created with a colorspace the instance never opted into.
    if (hasInstanceExtension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
    {
        AppendUniqueExtension(extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    }

    VkInstanceCreateFlags createFlags = 0;
#if defined(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
    if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        AppendUniqueExtension(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
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
    createInfo.flags = createFlags;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

#if IOS
    if (!hasInstanceExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME))
    {
        Throw(std::runtime_error("MoltenVK requires VK_EXT_layer_settings for bindless resources"));
    }
    AppendUniqueExtension(extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Full iOS device rendering requires Metal argument buffers for the engine's bindless
    // descriptor arrays. Pass the setting while creating the statically linked MoltenVK instance.
    const VkBool32 useMetalArgumentBuffers = VK_TRUE;
    const VkLayerSettingEXT moltenVkSetting{
        "MoltenVK",
        "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS",
        VK_LAYER_SETTING_TYPE_BOOL32_EXT,
        1,
        &useMetalArgumentBuffers,
    };
    VkLayerSettingsCreateInfoEXT moltenVkSettings{};
    moltenVkSettings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    moltenVkSettings.settingCount = 1;
    moltenVkSettings.pSettings = &moltenVkSetting;
    moltenVkSettings.pNext = createInfo.pNext;
    createInfo.pNext = &moltenVkSettings;
#endif

    VkValidationFeatureEnableEXT validationFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{};
    if (enableSynchronizationValidation)
    {
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.pNext = createInfo.pNext;
        validationFeatures.enabledValidationFeatureCount = 1;
        validationFeatures.pEnabledValidationFeatures = &validationFeature;
        createInfo.pNext = &validationFeatures;
        SPDLOG_INFO("Vulkan synchronization validation enabled");
    }
    
    Check(Interposer().CreateInstance(&createInfo, nullptr, &instance_),
        "create instance");

    GetVulkanPhysicalDevices();
    GetVulkanLayers();
    GetVulkanExtensions();
}

Instance::~Instance()
{
    if (instance_ != nullptr)
    {
        Interposer().DestroyInstance(instance_, nullptr);
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
    GetEnumerateVector(instance_,
        +[](VkInstance instance, uint32_t* count, VkPhysicalDevice* devices)
        { return Interposer().EnumeratePhysicalDevices(instance, count, devices); },
        physicalDevices_);

    if (physicalDevices_.empty())
    {
        Throw(std::runtime_error("found no Vulkan physical devices"));
    }
}

bool Instance::SupportsRayQuery() const
{
    for (const auto& device : physicalDevices_)
    {
        if (SupportsRayQuery(device))
        {
            return true;
        }
    }
    return false;
}

bool Instance::SupportsRayQuery(VkPhysicalDevice physicalDevice) const
{
    const auto extensions = GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr),
                                               vkEnumerateDeviceExtensionProperties);

    const auto hasExtension = [&extensions](const char* requiredExtension)
    {
        return std::any_of(extensions.begin(), extensions.end(),
            [requiredExtension](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, requiredExtension) == 0;
            });
    };

    if (!hasExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) ||
        !hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
        !hasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME))
    {
        return false;
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceFeatures2 features = {};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &rayQueryFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    return rayQueryFeatures.rayQuery && accelerationStructureFeatures.accelerationStructure;
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
            SPDLOG_CRITICAL("Requested Vulkan validation layer '{}' is not installed; validation cannot start", layer);
            Throw(std::runtime_error("could not find the requested validation layer: '" + std::string(layer) + "'"));
        }
    }
}

}
