#include "VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"

#include "Engine/Utilities/Exception.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "SoftwareModern/SoftwareModernRenderer.hpp"
#include "SoftwareModern/SwModernNoAmbientRenderer.hpp"
#include "SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "PathTracing/PathTracingRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include <spdlog/spdlog.h>
#include <utility>

#if WITH_STREAMLINE && WIN32
#include <dxgi1_6.h>
#endif

#if WITH_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_helpers_vk.h>
#endif

#if WITH_STREAMLINE
static sl::Resource toSlResource(const Vulkan::Image& image, VkDeviceMemory memory, VkImageView view, VkImageLayout layout)
{
    sl::Resource res(sl::ResourceType::eTex2d, (void*)image.Handle(), (void*)memory, (void*)view, (uint32_t)layout);
    res.width = image.Extent().width;
    res.height = image.Extent().height;
    res.nativeFormat = (uint32_t)image.Format();
    res.mipLevels = 1;
    res.arrayLayers = 1;
    return res;
}

static sl::float4x4 toSlMatrix(const glm::mat4& m)
{
    sl::float4x4 res;
    res.row[0] = sl::float4(m[0][0], m[1][0], m[2][0], m[3][0]);
    res.row[1] = sl::float4(m[0][1], m[1][1], m[2][1], m[3][1]);
    res.row[2] = sl::float4(m[0][2], m[1][2], m[2][2], m[3][2]);
    res.row[3] = sl::float4(m[0][3], m[1][3], m[2][3], m[3][3]);
    return res;
}

static bool HasNvidiaAdapter()
{
#if WIN32
    HMODULE dxgiModule = LoadLibraryW(L"dxgi.dll");
    if (!dxgiModule)
    {
        return false;
    }

    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory = reinterpret_cast<CreateDXGIFactory1Fn>(
        GetProcAddress(dxgiModule, "CreateDXGIFactory1"));
    if (!createFactory)
    {
        FreeLibrary(dxgiModule);
        return false;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
    {
        FreeLibrary(dxgiModule);
        return false;
    }

    bool hasNvidiaAdapter = false;
    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT result = factory->EnumAdapters1(adapterIndex, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result))
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x10DE)
        {
            hasNvidiaAdapter = true;
        }
        adapter->Release();

        if (hasNvidiaAdapter)
        {
            break;
        }
    }

    factory->Release();
    FreeLibrary(dxgiModule);
    return hasNvidiaAdapter;
#else
    return false;
#endif
}
#endif

namespace StreamlineWrapper
{
    bool GStreamLineInit = false;
    bool GStreamLineInitAttempted = false;
    bool GStreamLineEnabled = false;
    bool GStreamLineVulkanInfoSet = false;

    bool ShouldInitialize()
    {
#if WITH_STREAMLINE
        return HasNvidiaAdapter();
#else
        return false;
#endif
    }

    void Initialize()
    {
#if WITH_STREAMLINE
        if (GStreamLineInitAttempted)
        {
            return;
        }
        GStreamLineInitAttempted = true;

        sl::Preferences pref{};
        //pref.showConsole = true; // for debugging, set to false in production
        //pref.logLevel = sl::LogLevel::eVerbose;
        pref.pathsToPlugins = {}; // change this if Streamline plugins are not located next to the executable
        pref.numPathsToPlugins = 0; // change this if Streamline plugins are not located next to the executable
        pref.pathToLogsAndData = {}; // change this to enable logging to a file
        //pref.logMessageCallback = myLogMessageCallback; // highly recommended to track warning/error messages in your callback
        pref.applicationId = 12345678; // Provided by NVDA, required if using NGX components (DLSS 2/3)
        pref.engine = sl::EngineType::eCustom; // If using UE or Unity
        pref.engineVersion = "1.0.0"; // Optional version
        pref.projectId = "36cf6361-1044-4603-9ef3-066606660666"; // Optional project id (GUID format)
        pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

        sl::Feature features[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_RR };
        pref.featuresToLoad = features;
        pref.numFeaturesToLoad = sizeof(features) / sizeof(sl::Feature);
        //pref.renderAPI = sl::RenderAPI::eVulkan;

        sl::Result res;
        if (SL_FAILED(res, slInit(pref)))
        {
            SPDLOG_ERROR("Streamline slInit failed: {}", (int)res);
            return;
        }

        GStreamLineInit = true;
        GStreamLineEnabled = true;
#endif
    }

   void LazyInit(VkDevice device, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t computeQueueIdx, uint32_t computeQueueFamily, uint32_t graphicsQueueIdx, uint32_t graphicsQueueFamily, bool& outSupportDLSS, bool& outSupportDLSSRR)
   {
#if WITH_STREAMLINE
       Initialize();
       if (!GStreamLineInit)
       {
           outSupportDLSS = false;
           outSupportDLSSRR = false;
           return;
       }

       if (GStreamLineVulkanInfoSet)
       {
           return;
       }
       GStreamLineVulkanInfoSet = true;

       sl::Result res;
       sl::VulkanInfo slVulkanInfo{};
       slVulkanInfo.device = device;
       slVulkanInfo.instance = instance;
       slVulkanInfo.physicalDevice = physicalDevice;
       slVulkanInfo.computeQueueIndex = computeQueueIdx;
       slVulkanInfo.computeQueueFamily = computeQueueFamily;
       slVulkanInfo.graphicsQueueIndex = graphicsQueueIdx;
       slVulkanInfo.graphicsQueueFamily = graphicsQueueFamily;
       
       if(SL_FAILED(res, slSetVulkanInfo(slVulkanInfo)))
        {
            SPDLOG_ERROR("Streamline slSetVulkanInfo failed: {}", (int)res);
        }
       else
       {
            SPDLOG_INFO("Streamline Initialized Successfully.");
            
            sl::AdapterInfo adapterInfo{};
            adapterInfo.vkPhysicalDevice = physicalDevice;
            
            sl::Result checkRes = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo);
            outSupportDLSS = (checkRes == sl::Result::eOk);
            
            checkRes = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo);
            outSupportDLSSRR = (checkRes == sl::Result::eOk);
            
            SPDLOG_INFO("DLSS Support: {}, RR Support: {}", outSupportDLSS, outSupportDLSSRR);
       }
#else
       outSupportDLSS = false;
       outSupportDLSSRR = false;
#endif
   }


    void Shutdown()
   {
#if WITH_STREAMLINE
       if (GStreamLineEnabled)
       {
           sl::Result res;
           if(SL_FAILED(res, slShutdown()))
           {
               SPDLOG_ERROR("Streamline slShutdown failed: {}", (int)res);
           }
       }
#endif
   }
}

namespace
{
    constexpr const char* kPortabilitySubsetExtensionName = "VK_KHR_portability_subset";

    void PrintVulkanSdkInformation()
    {
        SPDLOG_INFO("Vulkan SDK Header Version: {}", VK_HEADER_VERSION);
    }
    
    void PrintVulkanDevices(const std::vector<VkPhysicalDevice>& physicalDevices)
    {
        for (const auto& device : physicalDevices)
        {
            VkPhysicalDeviceDriverProperties driverProp{};
            driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

            VkPhysicalDeviceProperties2 deviceProp{};
            deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProp.pNext = &driverProp;
            vkGetPhysicalDeviceProperties2(device, &deviceProp);
            VkPhysicalDeviceFeatures features;
            vkGetPhysicalDeviceFeatures(device, &features);

            const auto& prop = deviceProp.properties;

            const Vulkan::Version vulkanVersion(prop.apiVersion);
            const Vulkan::Version driverVersion(prop.driverVersion, prop.vendorID);

            SPDLOG_INFO("- [{}] {} '{}' ({}: vulkan {} driver {} {} - {})",
                       prop.deviceID, Vulkan::Strings::VendorId(prop.vendorID), prop.deviceName,
                       Vulkan::Strings::DeviceType(prop.deviceType),
                       to_string(vulkanVersion), driverProp.driverName, driverProp.driverInfo,
                       to_string(driverVersion));

            const auto extensions = Vulkan::GetEnumerateVector(device, static_cast<const char*>(nullptr),
                                                               vkEnumerateDeviceExtensionProperties);
            const auto hasRayTracing = std::any_of(extensions.begin(), extensions.end(),
               [](const VkExtensionProperties& extension)
               {
                   return strcmp(extension.extensionName,VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
            });
        }
    }

    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application)
    {
        const auto& swapChain = application.SwapChain();

        SPDLOG_INFO("Swap Chain: image count: {}, present mode: {}", swapChain.Images().size(),
                   static_cast<int>(swapChain.PresentMode()));
    }

    bool HasDeviceExtension(VkPhysicalDevice physicalDevice, const char* requiredExtension)
    {
        const auto extensions = Vulkan::GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr),
                                                           vkEnumerateDeviceExtensionProperties);
        return std::any_of(extensions.begin(), extensions.end(),
                           [requiredExtension](const VkExtensionProperties& extension)
                           {
                               return std::strcmp(extension.extensionName, requiredExtension) == 0;
                           });
    }

    bool AddDeviceExtensionIfAvailable(VkPhysicalDevice physicalDevice,
                                       std::vector<const char*>& requiredExtensions,
                                       const char* extensionName,
                                       const char* featureName)
    {
        if (HasDeviceExtension(physicalDevice, extensionName))
        {
            requiredExtensions.push_back(extensionName);
            return true;
        }

        SPDLOG_WARN("{} disabled because device extension {} is unavailable", featureName, extensionName);
        return false;
    }

}

namespace Vulkan
{
    FRendererRequirements GetRendererRequirements(ERendererType type)
    {
        switch (type)
        {
        case ERT_PathTracing:
            return {.requestAmbientCube = true, .requestRayTracing = true};
        case ERT_ModernDeferred:
        case ERT_LegacyDeferred:
        case ERT_VoxelTracing:
            return {.requestAmbientCube = true};
        case ERT_LegacyDeferredNoAmbient:
            return {};
        default:
            assert(false);
            return {};
        }
    }

    VulkanBaseRenderer::VulkanBaseRenderer(Vulkan::Window* window, const VkPresentModeKHR presentMode,
                                           const bool enableValidationLayers,
                                           Instance* instance) :
        presentMode_(presentMode)
    {
        ctx_.window = window;
        ctx_.instance.reset(instance);
        ctx_.debugUtilsMessenger.reset(enableValidationLayers
                       ? new DebugUtilsMessenger( *ctx_.instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
                       : nullptr);
        ctx_.surface.reset(new Surface(*ctx_.instance));
        caps_.supportDenoiser = false;
        forceSDR_ = GOption->ForceSDR;

        caps_.supportRayTracing = false;
    }

    VulkanBaseRenderer::~VulkanBaseRenderer()
    {
        VulkanBaseRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        rt_.reset();
        ctx_.gpuTimer.reset();
        ctx_.globalTexturePool.reset();
        ctx_.commandPool.reset();
        ctx_.commandPool2.reset();
        ctx_.device.reset();
        ctx_.surface.reset();
        ctx_.debugUtilsMessenger.reset();
        ctx_.instance.reset();
        ctx_.window = nullptr;
    }

    void VulkanBaseRenderer::SelectPhysicalDevice(uint32_t gpuIdx)
    {
        const auto& physicalDevices = ctx_.instance->PhysicalDevices();
        if (gpuIdx >= physicalDevices.size())
        {
            SPDLOG_WARN("Requested GPU index {} is out of range; using GPU 0", gpuIdx);
            gpuIdx = 0;
        }

        VkPhysicalDevice physicalDevice = physicalDevices[gpuIdx];
        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProp);

        SPDLOG_INFO("Setting Device [{}]", deviceProp.properties.deviceName);
        SetPhysicalDevice(physicalDevice);
    }

    void VulkanBaseRenderer::SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        if (ctx_.device)
        {
            Throw(std::logic_error("physical device has already been set"));
        }

        std::vector<const char*> requiredExtensions =
        {
            // VK_KHR_swapchain
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkPhysicalDeviceMemoryProperties memoryProperties = {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        VkDeviceSize largestDeviceLocalHeapSize = 0;
        for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
        {
            if ((memoryProperties.memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                largestDeviceLocalHeapSize =
                    std::max(largestDeviceLocalHeapSize, memoryProperties.memoryHeaps[heapIndex].size);
            }
        }

        const VkDeviceSize perCascadeCount =
            static_cast<VkDeviceSize>(Assets::CUBE_SIZE_XY) * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const VkDeviceSize fullAmbientCubeAllocationSize =
            static_cast<VkDeviceSize>(Assets::CUBE_CASCADE_MAX) * perCascadeCount *
                (sizeof(Assets::VoxelData) + sizeof(Assets::AmbientCube)) +
            static_cast<VkDeviceSize>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT * sizeof(Assets::PageIndex) +
            perCascadeCount * (sizeof(Assets::AmbientCube) + sizeof(glm::u32vec4));
        caps_.fullAmbientCubeBudget = largestDeviceLocalHeapSize >= fullAmbientCubeAllocationSize;
        if (!caps_.fullAmbientCubeBudget)
        {
            SPDLOG_WARN("Largest Vulkan device-local memory heap is {} MB, smaller than full ambient-cube allocation {} MB; ambient-cube renderers will use the no-ambient fallback",
                        static_cast<uint64_t>(largestDeviceLocalHeapSize / (1024 * 1024)),
                        static_cast<uint64_t>(fullAmbientCubeAllocationSize / (1024 * 1024)));
        }

        caps_.supportRayTracing = !GOption->ForceNoRT && ctx_.instance->SupportsRayQuery(physicalDevice);
        if (caps_.supportRayTracing)
        {
            rt_ = std::make_unique<RayTracingResources>();
        }

        SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nullptr);

        ctx_.globalTexturePool.reset(new Assets::GlobalTexturePool(*ctx_.device, *ctx_.commandPool2, *ctx_.commandPool));

        OnDeviceSet();
        CreateSwapChain();
        // Keep hidden windows hidden (agent validation captures, unit-test engine fixture):
        // showing here would override SDL_WINDOW_HIDDEN and pop a window that steals focus.
        if (!ctx_.window->Config().HiddenWindow)
        {
            ctx_.window->Show();
        }
    }

    void VulkanBaseRenderer::Start()
    {
        // setup vulkan
        PrintVulkanSdkInformation();
        PrintVulkanDevices(ctx_.instance->PhysicalDevices());
        SelectPhysicalDevice(GOption->GpuIdx);
        PrintVulkanSwapChainInformation(*this);
        frame_.currentFrame = 0;

        caps_.supportDLSS = caps_.streamlineExtsEnabled;
        caps_.supportDLSSRR = caps_.streamlineExtsEnabled;
    }

    void VulkanBaseRenderer::End()
    {
        StreamlineWrapper::Shutdown();
        ctx_.device->WaitIdle();
        ctx_.gpuTimer.reset();
        ctx_.globalTexturePool.reset();
    }

    Assets::Scene& VulkanBaseRenderer::GetScene()
    {
        return *scene_.lock();
    }

    void VulkanBaseRenderer::SetScene(std::shared_ptr<Assets::Scene> scene)
    {
        scene_ = scene;
        RequestClearAmbientCubeCache();
    }

    Assets::UniformBufferObject VulkanBaseRenderer::GetUniformBufferObject(
        const VkOffset2D offset, const VkExtent2D extent) const
    {
        if (delegates_.getUniformBufferObject)
        {
            return delegates_.getUniformBufferObject(offset, extent);
        }
        return {};
    }

    void VulkanBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        // Ray query / acceleration-structure extensions (conditional on caps_.supportRayTracing).
        // The feature structs must outlive ctx_.device.reset(), so declare them in the outer scope.
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
        if (caps_.supportRayTracing)
        {
            requiredExtensions.insert(requiredExtensions.end(),
                {
                    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                    VK_KHR_RAY_QUERY_EXTENSION_NAME,
                });
            accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelerationStructureFeatures.pNext = nextDeviceFeatures;
            accelerationStructureFeatures.accelerationStructure = true;
            rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQueryFeatures.pNext = &accelerationStructureFeatures;
            rayQueryFeatures.rayQuery = true;
            nextDeviceFeatures = &rayQueryFeatures;
        }

        VkPhysicalDeviceFeatures supportedFeatures = {};
        vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

        VkPhysicalDeviceDescriptorIndexingFeatures supportedIndexingFeatures = {};
        supportedIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures = {};
        supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        supportedBufferDeviceAddressFeatures.pNext = &supportedIndexingFeatures;

        VkPhysicalDeviceHostQueryResetFeaturesEXT supportedHostQueryResetFeatures = {};
        supportedHostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        supportedHostQueryResetFeatures.pNext = &supportedBufferDeviceAddressFeatures;

        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR supportedShaderFloat16Int8Features = {};
        supportedShaderFloat16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        supportedShaderFloat16Int8Features.pNext = &supportedHostQueryResetFeatures;

        VkPhysicalDeviceShaderDrawParametersFeatures supportedShaderDrawParametersFeatures = {};
        supportedShaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        supportedShaderDrawParametersFeatures.pNext = &supportedShaderFloat16Int8Features;

        VkPhysicalDevice16BitStorageFeatures supportedStorage16BitFeatures = {};
        supportedStorage16BitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        supportedStorage16BitFeatures.pNext = &supportedShaderDrawParametersFeatures;

        VkPhysicalDeviceFeatures2 supportedFeatures2 = {};
        supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures2.pNext = &supportedStorage16BitFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures2);

        VkPhysicalDeviceProperties deviceProperties = {};
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        auto enableDeviceExtensionIfAvailable = [&](const char* extensionName)
        {
            if (HasDeviceExtension(physicalDevice, extensionName) &&
                std::find(requiredExtensions.begin(), requiredExtensions.end(), extensionName) == requiredExtensions.end())
            {
                requiredExtensions.push_back(extensionName);
            }
        };

        enableDeviceExtensionIfAvailable(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(kPortabilitySubsetExtensionName);
        if (deviceProperties.apiVersion < VK_API_VERSION_1_2 &&
            !HasDeviceExtension(physicalDevice, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        {
            Throw(std::runtime_error("VK_KHR_buffer_device_address is required"));
        }

        deviceFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
        deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
        deviceFeatures.shaderStorageImageReadWithoutFormat = supportedFeatures.shaderStorageImageReadWithoutFormat;
        deviceFeatures.shaderStorageImageWriteWithoutFormat = supportedFeatures.shaderStorageImageWriteWithoutFormat;
        deviceFeatures.shaderInt16 = supportedFeatures.shaderInt16;
        deviceFeatures.shaderInt64 = supportedFeatures.shaderInt64;

        // Optional heatmap instrumentation.
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
                                      VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
                                  });

        // Opt-in into mandatory device features.
        VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
        shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
        shaderClockFeatures.pNext = nextDeviceFeatures;
        shaderClockFeatures.shaderSubgroupClock = true;
#endif
        
        // support bindless material
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        indexingFeatures.pNext = &shaderClockFeatures;
#else
	indexingFeatures.pNext = nextDeviceFeatures;
#endif
        indexingFeatures.runtimeDescriptorArray = supportedIndexingFeatures.runtimeDescriptorArray;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = supportedIndexingFeatures.shaderSampledImageArrayNonUniformIndexing;
        indexingFeatures.descriptorBindingPartiallyBound = supportedIndexingFeatures.descriptorBindingPartiallyBound;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind;
        indexingFeatures.descriptorBindingVariableDescriptorCount = supportedIndexingFeatures.descriptorBindingVariableDescriptorCount;


        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = supportedBufferDeviceAddressFeatures.bufferDeviceAddress;

        VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
        hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        hostQueryResetFeatures.pNext = &bufferDeviceAddressFeatures;
        hostQueryResetFeatures.hostQueryReset = supportedHostQueryResetFeatures.hostQueryReset;

        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shaderFloat16Int8Features = {};
        shaderFloat16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        shaderFloat16Int8Features.pNext = &hostQueryResetFeatures;
        shaderFloat16Int8Features.shaderFloat16 = supportedShaderFloat16Int8Features.shaderFloat16;
        //shaderFloat16Int8Features.shaderInt8 = true;

        VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {};
        shaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        shaderDrawParametersFeatures.pNext = &shaderFloat16Int8Features;
        shaderDrawParametersFeatures.shaderDrawParameters = supportedShaderDrawParametersFeatures.shaderDrawParameters;

        VkPhysicalDevice16BitStorageFeatures storage16BitFeatures = {};
        storage16BitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        storage16BitFeatures.pNext = &shaderDrawParametersFeatures;
        storage16BitFeatures.storageBuffer16BitAccess = supportedStorage16BitFeatures.storageBuffer16BitAccess;
        storage16BitFeatures.storagePushConstant16 = supportedStorage16BitFeatures.storagePushConstant16;

#if WITH_STREAMLINE
        VkPhysicalDeviceVulkan12Features deviceVulkan12Features = {};
        deviceVulkan12Features.timelineSemaphore = true;
        deviceVulkan12Features.pNext = &shaderDrawParametersFeatures;
        const bool hasStreamlineExtensions =
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_NVX_BINARY_IMPORT_EXTENSION_NAME, "Streamline binary import") &&
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME, "Streamline image view handles") &&
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, "Streamline buffer device address") &&
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, "Streamline EXT buffer device address");
        if (hasStreamlineExtensions)
        {
            storage16BitFeatures.pNext = &deviceVulkan12Features;
            caps_.streamlineExtsEnabled = true;
        }
        else
        {
            caps_.streamlineExtsEnabled = false;
        }
#endif

        ctx_.device.reset(new class Device(physicalDevice, *ctx_.surface, requiredExtensions, deviceFeatures,
                                       &storage16BitFeatures));
        ctx_.commandPool.reset(new class CommandPool(*ctx_.device, ctx_.device->GraphicsFamilyIndex(), 0, true));
        ctx_.commandPool2.reset(new class CommandPool(*ctx_.device, ctx_.device->TransferFamilyIndex(), 1, true));
        ctx_.gpuTimer.reset(new VulkanGpuTimer(*ctx_.device, 200, ctx_.device->DeviceProperties()));
    }

    void VulkanBaseRenderer::OnDeviceSet()
    {
        if (caps_.supportRayTracing)
        {
            rt_->properties.reset(new RayTracing::RayTracingProperties(Device()));
        }

        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->OnDeviceSet();
        }

        if (delegates_.onDeviceSet)
        {
            delegates_.onDeviceSet();
        }
    }

    void VulkanBaseRenderer::CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName)
    {
        bindless_.images[bindlessIdx].reset(new RenderImage(Device(), frame_.swapChain->RenderExtent(), format, tiling, usage, false, debugName));
        if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {
            ctx_.globalTexturePool->BindStorageTexture(bindlessIdx, bindless_.images[bindlessIdx]->GetImageView());
        }
    }

    const RenderImage* VulkanBaseRenderer::GetStorageImage(uint32_t bindlessIdx) const
    {
        assert(bindlessIdx < bindless_.images.size());
        return bindless_.images[bindlessIdx].get();
    }

    uint32_t VulkanBaseRenderer::GetTemporalStorageImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
        const char* debugName)
    {
        uint32_t targetIdx = Assets::Bindless::RT_TEMP_USAGE0 + bindless_.tempCreated;
        auto& target = bindless_.images[targetIdx];
        assert(!target);
        CreateStorageImage(targetIdx, format, tiling, usage, debugName);
        bindless_.tempCreated++;

        return targetIdx;
    }

#define CREATE_STORAGE_IMAGE(idx, fmt, tiling, usage) CreateStorageImage(Assets::Bindless::idx, fmt, tiling, usage, #idx)
    
    void VulkanBaseRenderer::CreateRenderImages()
    {
        screenshot_.image.reset(new Image(*ctx_.device, frame_.swapChain->Extent(), 1, frame_.swapChain->Format(),
                                         VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        screenshot_.imageMemory.reset(new DeviceMemory(
            screenshot_.image->
            AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        ctx_.device->DebugUtils().SetObjectName(screenshot_.image->Handle(), "Screenshot Image");
        screenshot_.imageMemory->SetName("Screenshot Memory");

        bindless_.images.resize(Assets::Bindless::RT_COUNT);
        
        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER_DRAW, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJEDCTID_0, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJEDCTID_1, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MOTIONVECTOR, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SHADER_TIMER, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_DENOISED, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_PREV_DEPTHBUFFER, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_SPECULAR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_SPECULAR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_SPECULAR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MOTIONMOMENT, VK_FORMAT_R16_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_DIFFUSE_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);

        for (uint32_t i = 0; i != frame_.swapChain->Images().size(); i++)
        {
            ctx_.globalTexturePool->BindStorageTexture( Assets::Bindless::RT_SWAPCHAIN0 + i, *frame_.swapChain->ImageViews()[i] );
        }

    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        // 窗口等待
        while (ctx_.window->IsMinimized())
        {
            ctx_.window->WaitForEvents();
        }

        // SwapChaine
        float scale = 1.0f;
        auto& settings = NextEngine::GetInstance()->GetUserSettings();
        if (!GOption->ReferenceMode)
        {
            switch(settings.SuperResolution)
            {
                case 0: scale = 1.5f; break; // Quality
                case 1: scale = 1.7f; break; // Balanced
                case 2: scale = 2.0f; break; // Performance
                case 3: scale = 3.0f; break; // UltraPerformance
                case 4: scale = 1.0f; break; // Native
                default: scale = 1.5f; break;
            }
        }
        
        frame_.swapChain.reset(new class SwapChain(*ctx_.device, presentMode_, forceSDR_));
        frame_.swapChain->UpdateRenderViewport(0, 0, (uint32_t)(frame_.swapChain->Extent().width / scale), (uint32_t)(frame_.swapChain->Extent().height / scale));
        frame_.swapChain->UpdateOutputViewport( 0, 0, frame_.swapChain->Extent().width, frame_.swapChain->Extent().height);

        // depthBuffer
        frame_.depthBuffer.reset(new class DepthBuffer(*ctx_.commandPool, frame_.swapChain->Extent()));

        // 同步对象
        for (size_t i = 0; i != frame_.swapChain->ImageViews().size(); ++i)
        {
            frame_.imageAvailableSemaphores.emplace_back(*ctx_.device);
            frame_.renderFinishedSemaphores.emplace_back(*ctx_.device);
            frame_.inFlightFences.emplace_back(*ctx_.device, true);
            frame_.uniformBuffers.emplace_back(*ctx_.device);
        }

        // commandbuffer
        frame_.commandBuffers.reset(new CommandBuffers(*ctx_.commandPool, static_cast<uint32_t>(frame_.swapChain->ImageViews().size())));

        frame_.currentFence = nullptr;

        // 公用RenderImages
        CreateRenderImages();

        overlay_.wireframePipeline.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true));
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframeFrameBuffers.reserve(frame_.swapChain->ImageViews().size());
        for (const auto& imageView : frame_.swapChain->ImageViews())
        {
            overlay_.wireframeFrameBuffers.emplace_back(frame_.swapChain->Extent(), *imageView, overlay_.wireframePipeline->RenderPass());
        }

        // 公用Pipeline
        overlay_.simpleComposePipeline.reset( new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.UpScaleFSR.comp.slang.spv", 20));
        overlay_.bufferClearPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.BufferClear.comp.slang.spv", 4));
        // Shared swap-chain resources must cover every registered renderer because
        // switching logic renderers does not recreate the swap chain.
        if (RegisteredRendererRequirements().requestAmbientCube)
        {
            ambient_.softBake.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.SwAmbientCube.comp.slang.spv", GetScene()));
            ambient_.clearCache.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.ClearAmbientCubeCache.comp.slang.spv", GetScene()));
            ambient_.propagation.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.PropagationAmbientCube.comp.slang.spv", GetScene()));
            ambient_.inject.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.InjectAmbientCube.comp.slang.spv", GetScene()));
            ambient_.distanceFieldInit.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.DistanceFieldInit.comp.slang.spv", GetScene()));
            ambient_.distanceFieldJump.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.DistanceFieldJump.comp.slang.spv", GetScene()));
            ambient_.distanceFieldResolve.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.DistanceFieldResolve.comp.slang.spv", GetScene()));

            if (caps_.supportRayTracing)
            {
                rt_->directLightGenPipeline.reset(new PipelineCommon::ZeroBindWithTLASPipeline(SwapChain(), "assets/shaders/Bake.HwAmbientCube.comp.slang.spv", GetScene()));
            }
        }
        overlay_.gpuCullCompactPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang.spv", GetScene()));
        overlay_.softMeshShaderFinalizePipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderFinalize.comp.slang.spv", GetScene()));
        overlay_.softMeshShaderExpandPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderExpand.comp.slang.spv", GetScene()));
        overlay_.shadowGpuCullCompactPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact.comp.slang.spv", GetScene()));
        skin_.pipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.Skinning.comp.slang.spv", GetScene()));
        overlay_.visualDebuggerPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.VisualDebugger.comp.slang.spv", 20));

        overlay_.visibilityPipeline.reset(new PipelineCommon::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        overlay_.visibilityFrameBuffer.reset(new FrameBuffer(frame_.swapChain->RenderExtent(), GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(), overlay_.visibilityPipeline->RenderPass()));

        // 太阳方向光 CSM 阴影 pass + 注册 4 个 cascade 到 Bindless
        {
            overlay_.sunShadowPass.reset(new Shadow::ShadowMapPass(*ctx_.device));
            overlay_.sunShadowPass->CreateResources(GetScene());

            auto* texPool = Assets::GlobalTexturePool::GetInstance();
            for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
            {
                texPool->BindShadowMap(i, GetScene().SunShadowImageView(i), GetScene().SunShadowSampler());
            }
        }

        // 逻辑Renderer
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->CreateSwapChain(frame_.swapChain->RenderExtent());
        }

        // Delegate
        if (delegates_.createSwapChain)
        {
            delegates_.createSwapChain();
        }
    }

    void VulkanBaseRenderer::DeleteSwapChain()
    {
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->DeleteSwapChain();
        }

        if (delegates_.deleteSwapChain)
        {
            delegates_.deleteSwapChain();
        }

        for ( auto& storageImage : bindless_.images )
        {
            storageImage.reset();
        }
        bindless_.tempCreated = 0;

        overlay_.visibilityPipeline.reset();
        overlay_.visibilityFrameBuffer.reset();
        overlay_.sunShadowPass.reset();
        
        screenshot_.image.reset();
        screenshot_.imageMemory.reset();
        frame_.commandBuffers.reset();
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        ambient_.softBake.reset();
        ambient_.clearCache.reset();
        ambient_.propagation.reset();
        ambient_.inject.reset();
        ambient_.distanceFieldInit.reset();
        ambient_.distanceFieldJump.reset();
        ambient_.distanceFieldResolve.reset();
        if (rt_)
        {
            rt_->directLightGenPipeline.reset();
        }
        overlay_.gpuCullCompactPipeline.reset();
        overlay_.softMeshShaderFinalizePipeline.reset();
        overlay_.softMeshShaderExpandPipeline.reset();
        overlay_.shadowGpuCullCompactPipeline.reset();
        skin_.pipeline.reset();

        skin_.vertexBuffer.reset();
        skin_.vertexMemory.reset();
        skin_.jointBuffer.reset();
        skin_.jointMemory.reset();

        overlay_.simpleComposePipeline.reset();
        overlay_.visualDebuggerPipeline.reset();
        frame_.uniformBuffers.clear();
        frame_.inFlightFences.clear();
        frame_.renderFinishedSemaphores.clear();
        frame_.imageAvailableSemaphores.clear();
        frame_.depthBuffer.reset();
        frame_.swapChain.reset();

        frame_.currentFence = nullptr;
    }

    void VulkanBaseRenderer::RecreateSwapChain()
    {
        ctx_.device->WaitIdle();
        DeleteSwapChain();
        CreateSwapChain();
    }

    void VulkanBaseRenderer::ReloadShaders()
    {
        RecreateSwapChain();
    }

    void VulkanBaseRenderer::CaptureScreenShot()
    {
        SingleTimeCommands::Submit(CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            SCOPED_GPU_TIMER("screenshot");
            const auto& image = frame_.swapChain->Images()[frame_.currentImageIndex];

            ImageMemoryBarrier::FullInsert(commandBuffer, image, 0, VK_ACCESS_TRANSFER_READ_BIT,
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            ImageMemoryBarrier::FullInsert(commandBuffer, screenshot_.image->Handle(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

            vkCmdCopyImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshot_.image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[frame_.currentImageIndex],
                                           VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        });
    }

    void VulkanBaseRenderer::UpdateSkinningBuffers()
    {
        auto& scene = GetScene();
        uint32_t vertCount = scene.GetVerticeCount();
        if (vertCount == 0) return;

        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        int rtxFlags = caps_.supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        size_t requiredVertexSize = vertCount * sizeof(Assets::GPUVertex);
        if (!skin_.vertexBuffer || skin_.vertexBufferSize < requiredVertexSize)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(*ctx_.commandPool, "SkinnedVertices", flags | rtxFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requiredVertexSize, skin_.vertexBuffer, skin_.vertexMemory);
            skin_.vertexBufferSize = (uint32_t)requiredVertexSize;
        }

        uint32_t totalJoints = 0;
        for (auto& node : scene.Nodes())
        {
            if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
            {
                totalJoints += (uint32_t)skinnedMesh->GetJointMatrices().size();
            }
        }

        if (totalJoints > 0)
        {
            size_t requiredJointSize = totalJoints * sizeof(glm::mat4);
            if (!skin_.jointBuffer || skin_.jointBufferSize < requiredJointSize)
            {
                Vulkan::BufferUtil::CreateDeviceBufferLocal(*ctx_.commandPool, "JointMatrices", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requiredJointSize, skin_.jointBuffer, skin_.jointMemory);
                skin_.jointBufferSize = (uint32_t)requiredJointSize;
            }

            // Map and upload
            glm::mat4* data = (glm::mat4*)skin_.jointMemory->Map(0, requiredJointSize);
            uint32_t offset = 0;
            for (auto& node : scene.Nodes())
            {
                if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
                {
                    const auto& matrices = skinnedMesh->GetJointMatrices();
                    std::memcpy(data + offset, matrices.data(), matrices.size() * sizeof(glm::mat4));
                    offset += (uint32_t)matrices.size();
                }
            }
            skin_.jointMemory->Unmap();
        }
    }

    void VulkanBaseRenderer::PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        UpdateAccelerationStructuresTop(commandBuffer);
        UpdateSkinningBuffers();
        InitializeBarriers(commandBuffer);
        HandleAmbientCubeCacheInvalidation(commandBuffer, imageIndex);
        DispatchSkinning(commandBuffer, imageIndex);
        DispatchGpuCulling(commandBuffer, imageIndex);
        DispatchClearPass(commandBuffer, imageIndex);
        DispatchVisibilityPass(commandBuffer, imageIndex);
        DispatchSunShadow(commandBuffer, imageIndex);
        UpdateAccelerationStructuresBottom(commandBuffer);
    }

    void VulkanBaseRenderer::UpdateAccelerationStructuresTop(VkCommandBuffer commandBuffer)
    {
        if (!caps_.supportRayTracing)
        {
            return;
        }
        if (!(GOption->ReferenceMode || CurrentRendererRequirements().requestRayTracing ||
              (CurrentRendererRequirements().requestAmbientCube && !GOption->ForceSoftGen)))
        {
            return;
        }
        SCOPED_GPU_TIMER("TLAS Update");
        if (rt_->tlasUpdateRequest > 0)
        {
            rt_->tlas[0].Update(commandBuffer, rt_->tlasUpdateRequest);
            rt_->tlasUpdateRequest = 0;
        }
    }

    void VulkanBaseRenderer::UpdateAccelerationStructuresBottom(VkCommandBuffer commandBuffer)
    {
        if (!caps_.supportRayTracing || !rt_->blasScratch)
        {
            return;
        }
        SCOPED_GPU_TIMER("BLAS Update");
        VkDeviceSize scratchOffset = 0;
        for (size_t i = 0; i < skin_.updateRequests.size(); i++)
        {
            int32_t modelId = skin_.updateRequests[i];
            if (modelId != -1)
            {
                rt_->blas[modelId].Update(commandBuffer, *rt_->blasScratch, scratchOffset);
                scratchOffset += rt_->blas[modelId].BuildSizes().buildScratchSize;
            }
        }
        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);
    }

    void VulkanBaseRenderer::HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!CurrentRendererRequirements().requestAmbientCube)
        {
            return;
        }

        const bool useAmbientCubePropagation = NextEngine::GetInstance()->GetUserSettings().UseAmbientCubePropagation;
        if (!ambient_.propagationStateInitialized)
        {
            ambient_.lastPropagation = useAmbientCubePropagation;
            ambient_.propagationStateInitialized = true;
        }
        else if (ambient_.lastPropagation != useAmbientCubePropagation)
        {
            ambient_.lastPropagation = useAmbientCubePropagation;
            RequestClearAmbientCubeCache();
        }

        if (ambient_.requestClearCache)
        {
            ClearAmbientCubeCache(commandBuffer, imageIndex);
            ambient_.requestClearCache = false;
        }
    }

    void VulkanBaseRenderer::DispatchSkinning(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("skinning pass");
        auto& scene = GetScene();

        if (skin_.vertexBuffer)
        {
            scene.SetSkinningBuffers(skin_.vertexBuffer->GetDeviceAddress(),
                                     skin_.jointBuffer ? skin_.jointBuffer->GetDeviceAddress() : 0);
        }
        else
        {
            scene.SetSkinningBuffers(0, 0);
        }

        skin_.pipeline->BindPipeline(commandBuffer, scene, imageIndex);

        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex);
        if (!skin_.vertexBuffer)
        {
            return;
        }

        for (size_t i = 0; i < skin_.updateRequests.size(); i++)
        {
            uint32_t modelId = skin_.updateRequests[i];
            if (modelId == static_cast<uint32_t>(-1))
            {
                continue;
            }
            const auto* model = scene.GetModel(modelId);
            if (!model)
            {
                continue;
            }

            uint32_t proxyIdx = std::numeric_limits<uint32_t>::max();
            const uint32_t skinnedProxyModelId = modelId * 10;
            const auto& nodeProxys = scene.GetNodeProxys();
            for (uint32_t nodeIdx = 0; nodeIdx < nodeProxys.size(); ++nodeIdx)
            {
                if (nodeProxys[nodeIdx].modelId == skinnedProxyModelId)
                {
                    proxyIdx = nodeIdx;
                    break;
                }
            }
            if (proxyIdx == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }

            uint32_t vertexOffset = scene.Offsets()[modelId * 10].vertexOffset;
            uint32_t vertexCount = model->NumberOfVertices();

            gpuScene.custom_data_0 = proxyIdx;
            gpuScene.custom_data_1 = vertexOffset;
            gpuScene.custom_data_2 = vertexCount;

            vkCmdPushConstants(commandBuffer, skin_.pipeline->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            uint32_t groupCount = (vertexCount + 63) / 64;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }

        // Skinned vertex buffer barrier: compute write -> shader/RT read.
        VkBufferMemoryBarrier skinnedBufferBarrier = {};
        skinnedBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        skinnedBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        skinnedBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkPipelineStageFlags skinnedDstStages =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (caps_.supportRayTracing)
        {
            skinnedBufferBarrier.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            skinnedDstStages |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }
        skinnedBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        skinnedBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        skinnedBufferBarrier.buffer = skin_.vertexBuffer->Handle();
        skinnedBufferBarrier.offset = 0;
        skinnedBufferBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             skinnedDstStages, 0, 0, nullptr, 1, &skinnedBufferBarrier, 0, nullptr);
    }

    void VulkanBaseRenderer::DispatchGpuCulling(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("gpu cull");

        const uint32_t indirectDrawBatchCount = GetScene().GetIndirectDrawBatchCount();
        if (indirectDrawBatchCount == 0)
        {
            return;
        }

        VkBufferMemoryBarrier nodeMatrixBarrier = {};
        nodeMatrixBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        nodeMatrixBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        nodeMatrixBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        nodeMatrixBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        nodeMatrixBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        nodeMatrixBarrier.buffer = GetScene().NodeMatrixBuffer().Handle();
        nodeMatrixBarrier.offset = 0;
        nodeMatrixBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &nodeMatrixBarrier, 0, nullptr);

        auto& scene = GetScene();
        vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);

        VkBufferMemoryBarrier counterClearBarrier = {};
        counterClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        counterClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterClearBarrier.buffer = scene.SoftMeshShaderCounterBuffer().Handle();
        counterClearBarrier.offset = 0;
        counterClearBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             1, &counterClearBarrier, 0, nullptr);

        auto bindCompute = [&](const PipelineCommon::ZeroBindPipeline& pipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle());
            pipeline.PipelineLayout().BindDescriptorSets(commandBuffer, 0);
        };

        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = 0;
        gpuScene.custom_data_1 = indirectDrawBatchCount;
        gpuScene.custom_data_2 = scene.GetMaxSceneTriangles();

        bindCompute(*overlay_.gpuCullCompactPipeline);
        vkCmdPushConstants(commandBuffer, overlay_.gpuCullCompactPipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        uint32_t groupCount = (indirectDrawBatchCount + 63) / 64;
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        std::array<VkBufferMemoryBarrier, 2> compactBarriers{};
        compactBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        compactBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        compactBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        compactBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        compactBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        compactBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
        compactBarriers[0].offset = 0;
        compactBarriers[0].size = VK_WHOLE_SIZE;
        compactBarriers[1] = compactBarriers[0];
        compactBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             static_cast<uint32_t>(compactBarriers.size()), compactBarriers.data(),
                             0, nullptr);

        bindCompute(*overlay_.softMeshShaderFinalizePipeline);
        gpuScene.custom_data_0 = 0;
        vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderFinalizePipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        std::array<VkBufferMemoryBarrier, 2> expandInputBarriers{};
        expandInputBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expandInputBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expandInputBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        expandInputBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[0].buffer = scene.SoftMeshShaderDispatchArgBuffer().Handle();
        expandInputBarriers[0].offset = 0;
        expandInputBarriers[0].size = sizeof(VkDispatchIndirectCommand);
        expandInputBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expandInputBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expandInputBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        expandInputBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
        expandInputBarriers[1].offset = 0;
        expandInputBarriers[1].size = sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(expandInputBarriers.size()),
                             expandInputBarriers.data(), 0, nullptr);

        bindCompute(*overlay_.softMeshShaderExpandPipeline);
        vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderExpandPipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(), 0);

        std::array<VkBufferMemoryBarrier, 2> drawBarriers{};
        drawBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        drawBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].buffer = scene.SoftMeshShaderPrimBuffer().Handle();
        drawBarriers[0].offset = 0;
        drawBarriers[0].size = VK_WHOLE_SIZE;
        drawBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        drawBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
        drawBarriers[1].offset = 0;
        drawBarriers[1].size = sizeof(VkDrawIndirectCommand);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(drawBarriers.size()), drawBarriers.data(),
                             0, nullptr);
    }

    void VulkanBaseRenderer::DispatchClearPass(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("clear pass");

        overlay_.bufferClearPipeline->BindPipeline(commandBuffer, &imageIndex);
        vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange imageRange = {};
        imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageRange.baseMipLevel = 0;
        imageRange.levelCount = 1;
        imageRange.baseArrayLayer = 0;
        imageRange.layerCount = 1;

        ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex],
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(commandBuffer, SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor, 1, &imageRange);
        ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void VulkanBaseRenderer::DispatchVisibilityPass(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("visibility pass");

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeVisibilityPipeline = *overlay_.visibilityPipeline;
        renderPassInfo.renderPass = activeVisibilityPipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = overlay_.visibilityFrameBuffer->Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& scene = GetScene();

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activeVisibilityPipeline.Handle());
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex);
            activeVisibilityPipeline.PipelineLayout().BindDescriptorSets(
                commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdPushConstants(commandBuffer, activeVisibilityPipeline.PipelineLayout().Handle(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(Assets::GPUScene), &gpuScene);
            if (scene.GetIndirectDrawBatchCount() > 0)
            {
                vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), 0,
                                  1, sizeof(VkDrawIndirectCommand));
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        // Copy the depth/id render target into the storage-image variant for later passes.
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().width,
                             GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().height, 1};

        GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->InsertBarrier(commandBuffer,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyImage(commandBuffer,
            GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void VulkanBaseRenderer::DispatchSunShadow(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!overlay_.sunShadowPass || !GetScene().GetEnvSettings().HasSun)
        {
            return;
        }

        SCOPED_GPU_TIMER("shadow pass");
        auto& scene = GetScene();
        const uint32_t indirectDrawBatchCount = scene.GetIndirectDrawBatchCount();
        const uint32_t groupCount = (indirectDrawBatchCount + 63) / 64;
        const uint32_t activeCascadeMask = NextEngine::GetInstance()->GetSunShadowCascadeUpdateMask();
        Assets::GPUScene shadowGpuScene = scene.FetchGPUScene(imageIndex);

        if (activeCascadeMask != 0)
        {
            vkCmdFillBuffer(commandBuffer, scene.NodeMatrixBuffer().Handle(),
                            Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET,
                            sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount, 0);

            VkBufferMemoryBarrier statsClearBarrier = {};
            statsClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            statsClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            statsClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            statsClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statsClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statsClearBarrier.buffer = scene.NodeMatrixBuffer().Handle();
            statsClearBarrier.offset = Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET;
            statsClearBarrier.size = sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 1, &statsClearBarrier, 0, nullptr);
        }

        if (activeCascadeMask != 0)
        {
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), sizeof(VkDrawIndirectCommand),
                            sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount, 0);

            std::array<VkBufferMemoryBarrier, 2> clearBarriers{};
            clearBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            clearBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            clearBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
            clearBarriers[0].offset = 0;
            clearBarriers[0].size = VK_WHOLE_SIZE;
            clearBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            clearBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            clearBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
            clearBarriers[1].offset = sizeof(VkDrawIndirectCommand);
            clearBarriers[1].size = sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 0, nullptr, static_cast<uint32_t>(clearBarriers.size()), clearBarriers.data(), 0, nullptr);
        }

        if (groupCount > 0 && activeCascadeMask != 0)
        {
            auto bindCompute = [&](const PipelineCommon::ZeroBindPipeline& pipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle());
                pipeline.PipelineLayout().BindDescriptorSets(commandBuffer, 0);
            };

            shadowGpuScene.custom_data_0 = activeCascadeMask;
            shadowGpuScene.custom_data_1 = indirectDrawBatchCount;
            shadowGpuScene.custom_data_2 = scene.GetMaxSceneTriangles();

            bindCompute(*overlay_.shadowGpuCullCompactPipeline);
            vkCmdPushConstants(commandBuffer, overlay_.shadowGpuCullCompactPipeline->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            std::array<VkBufferMemoryBarrier, 2> compactBarriers{};
            compactBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            compactBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            compactBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            compactBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            compactBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            compactBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
            compactBarriers[0].offset = 0;
            compactBarriers[0].size = VK_WHOLE_SIZE;
            compactBarriers[1] = compactBarriers[0];
            compactBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                                 static_cast<uint32_t>(compactBarriers.size()), compactBarriers.data(),
                                 0, nullptr);

            bindCompute(*overlay_.softMeshShaderFinalizePipeline);
            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                shadowGpuScene.custom_data_0 = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderFinalizePipeline->PipelineLayout().Handle(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
                vkCmdDispatch(commandBuffer, 1, 1, 1);
            }

            std::array<VkBufferMemoryBarrier, 2> expandInputBarriers{};
            expandInputBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            expandInputBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            expandInputBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            expandInputBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[0].buffer = scene.SoftMeshShaderDispatchArgBuffer().Handle();
            expandInputBarriers[0].offset = sizeof(VkDispatchIndirectCommand);
            expandInputBarriers[0].size = sizeof(VkDispatchIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            expandInputBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            expandInputBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            expandInputBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            expandInputBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
            expandInputBarriers[1].offset = sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount;
            expandInputBarriers[1].size =
                sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount *
                Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, static_cast<uint32_t>(expandInputBarriers.size()),
                                 expandInputBarriers.data(), 0, nullptr);

            bindCompute(*overlay_.softMeshShaderExpandPipeline);
            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                shadowGpuScene.custom_data_0 = slot;
                vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderExpandPipeline->PipelineLayout().Handle(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
                vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(),
                                      sizeof(VkDispatchIndirectCommand) * slot);
            }

            std::array<VkBufferMemoryBarrier, 2> drawBarriers{};
            drawBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            drawBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            drawBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            drawBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[0].buffer = scene.SoftMeshShaderShadowPrimBuffer().Handle();
            drawBarriers[0].offset = 0;
            drawBarriers[0].size = VK_WHOLE_SIZE;
            drawBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            drawBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            drawBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            drawBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
            drawBarriers[1].offset = sizeof(VkDrawIndirectCommand);
            drawBarriers[1].size = sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                 0, 0, nullptr, static_cast<uint32_t>(drawBarriers.size()), drawBarriers.data(),
                                 0, nullptr);
        }

        for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
        {
            if ((activeCascadeMask & (1u << cascade)) == 0u)
            {
                continue;
            }

            overlay_.sunShadowPass->DrawCascade(
                commandBuffer, scene, shadowGpuScene, cascade);
        }
    }

    void VulkanBaseRenderer::DrawFrame()
    {
        if (requestRecreateSwapChain_)
        {
            RecreateSwapChain();
            requestRecreateSwapChain_ = false;
            return;
        }

        {
            SCOPED_CPU_TIMER("draw-frame");
            const auto noTimeout = std::numeric_limits<uint64_t>::max();

            {
                SCOPED_CPU_TIMER("prepare");
                BeforeNextFrame();
            }

            {
                SCOPED_CPU_TIMER("hwquery");
                ctx_.gpuTimer->FrameEnd((*frame_.commandBuffers)[frame_.currentImageIndex]);
            }

            // next frame synchronization objects
            const auto imageAvailableSemaphore = frame_.imageAvailableSemaphores[frame_.currentFrame].Handle();
            const auto renderFinishedSemaphore = frame_.renderFinishedSemaphores[frame_.currentFrame].Handle();

            auto result = VkResult(VK_SUCCESS);
            {
                SCOPED_CPU_TIMER("acquire-frame");
                result = vkAcquireNextImageKHR(ctx_.device->Handle(), frame_.swapChain->Handle(), noTimeout,
                                                imageAvailableSemaphore, nullptr, &frame_.currentImageIndex);
            }
            
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                RecreateSwapChain();
                return;
            }

            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
            }

            // Wait before CPU-side uniform/stat writes and command-buffer reuse.
            auto* const previousSubmitFence = frame_.currentFence;
            auto* const frameSlotFence = &frame_.inFlightFences[frame_.currentFrame];
            if (previousSubmitFence)
            {
                SCOPED_CPU_TIMER("fence");
                previousSubmitFence->Wait(noTimeout);
            }
            if (frameSlotFence != previousSubmitFence)
            {
                SCOPED_CPU_TIMER("frame-slot-fence");
                frameSlotFence->Wait(noTimeout);
            }
            frame_.currentFence = frameSlotFence;

            {
                SCOPED_CPU_TIMER("update uniform");
                UpdateUniformBuffer(frame_.currentImageIndex);
            }

            const auto commandBuffer = frame_.commandBuffers->Begin(frame_.currentFrame);
            ctx_.gpuTimer->Reset(commandBuffer);

            {
                SCOPED_GPU_TIMER("[gpu]");

                {
                    SCOPED_GPU_TIMER("[pre-render]");
                    PreRender(commandBuffer, frame_.currentImageIndex);
                    skin_.updateRequests.clear();
                }

                {
                    SCOPED_GPU_TIMER("[render]");
                    Render(commandBuffer, frame_.currentImageIndex);
                }

                {
                    SCOPED_GPU_TIMER("[post-render]");
                    PostRender(commandBuffer, frame_.currentImageIndex);
                }

                if (delegates_.postRender)
                {
                    SCOPED_GPU_TIMER("imgui");
                    delegates_.postRender(commandBuffer, frame_.currentImageIndex);
                }
            }
            frame_.commandBuffers->End(frame_.currentFrame);

            {
                SCOPED_CPU_TIMER("update nodes");
                if (GetScene().UpdateNodes())
                {
                    AfterUpdateScene();
                }
            }

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkCommandBuffer commandBuffers[]{commandBuffer};
            VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
            {
                SCOPED_CPU_TIMER("submit");
                frame_.currentFence = &(frame_.inFlightFences[frame_.currentFrame]);
                {
                    submitInfo.waitSemaphoreCount = 1;
                    submitInfo.pWaitSemaphores = waitSemaphores;
                    submitInfo.pWaitDstStageMask = waitStages;
                    submitInfo.commandBufferCount = 1;
                    submitInfo.pCommandBuffers = commandBuffers;
                    submitInfo.signalSemaphoreCount = 1;
                    submitInfo.pSignalSemaphores = signalSemaphores;

                    frame_.currentFence->Reset();

                    Check(vkQueueSubmit(ctx_.device->GraphicsQueue(), 1, &submitInfo, frame_.currentFence->Handle()),
                          "submit draw command buffer");
                }
            }

            {
                SCOPED_CPU_TIMER("present");
                VkSwapchainKHR swapChains[] = {frame_.swapChain->Handle()};
                VkPresentInfoKHR presentInfo = {};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = signalSemaphores;
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &frame_.currentImageIndex;
                presentInfo.pResults = nullptr; // Optional

                result = vkQueuePresentKHR(ctx_.device->PresentQueue(), &presentInfo);

                if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
                {
                    RecreateSwapChain();
                    return;
                }

                if (result != VK_SUCCESS)
                {
                    Throw(std::runtime_error(std::string("failed to present next image (") + ToString(result) + ")"));
                }
            }

            frame_.currentFrame = (frame_.currentFrame + 1) % frame_.inFlightFences.size();
            frame_.frameCount++;
        }
    }

    void VulkanBaseRenderer::BeforeNextFrame()
    {
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->BeforeNextFrame();
        }

        if (delegates_.beforeNextTick)
        {
            delegates_.beforeNextTick();
        }
    }

    void VulkanBaseRenderer::InitializeBarriers(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("barriers");
        for ( auto& storageImage : bindless_.images )
        {
            if ( storageImage ) storageImage->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    void VulkanBaseRenderer::UpdateStreamline(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
#if WITH_STREAMLINE
        if (!caps_.supportDLSS) return;
        
        StreamlineWrapper::LazyInit(ctx_.device->Handle(), ctx_.instance->Handle(), ctx_.device->PhysicalDevice(), 0, ctx_.device->ComputeFamilyIndex(), 0, ctx_.device->GraphicsFamilyIndex(), caps_.supportDLSS, caps_.supportDLSSRR);

        auto& settings = NextEngine::GetInstance()->GetUserSettings();
        
        sl::ViewportHandle viewport(0);
        sl::FrameToken* frameToken;
        uint32_t uintFrameCount = (uint32_t)frame_.frameCount;
        if (SL_FAILED(res0, slGetNewFrameToken(frameToken, &uintFrameCount)))
        {
            SPDLOG_ERROR("slGetNewFrameToken failed: {}", (int)res0);
            return;
        }

        bool useDLSSRR = SupportDLSSRR() && settings.DLSSRR;
        
        // 1. DLSS Runtime::Config::Options
        if (useDLSSRR)
        {
            sl::DLSSDOptions dlssOptions;
            switch (settings.SuperResolution)
            {
                case 0: dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
                case 1: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
                case 2: dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
                case 3: dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
                case 4: dlssOptions.mode = sl::DLSSMode::eDLAA; break;
                default: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            }
            dlssOptions.dlaaPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.qualityPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.balancedPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.performancePreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.ultraPerformancePreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.outputWidth = SwapChain().Extent().width;
            dlssOptions.outputHeight = SwapChain().Extent().height;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
            dlssOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
        
            if (SL_FAILED(res1, slDLSSDSetOptions(viewport, dlssOptions)))
            {
                SPDLOG_ERROR("slDLSSDSetOptions failed: {}", (int)res1);
            }
        }
        else
        {
            sl::DLSSOptions dlssOptions;
            switch (settings.SuperResolution)
            {
                case 0: dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
                case 1: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
                case 2: dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
                case 3: dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
                case 4: dlssOptions.mode = sl::DLSSMode::eDLAA; break;
                default: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            }
            dlssOptions.outputWidth = SwapChain().Extent().width;
            dlssOptions.outputHeight = SwapChain().Extent().height;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
            
            if (SL_FAILED(res1, slDLSSSetOptions(viewport, dlssOptions)))
            {
                SPDLOG_ERROR("slDLSSSetOptions failed: {}", (int)res1);
            }
        }

        // 2. Constants
        sl::Constants constants{};
        constants.cameraViewToClip = toSlMatrix(frame_.lastUBO.ProjectionUnJit);
        constants.clipToCameraView = toSlMatrix(frame_.lastUBO.ProjectionInverseUnJit);
        constants.clipToPrevClip = toSlMatrix(frame_.lastUBO.PrevViewProjectionUnJit * frame_.lastUBO.ModelViewInverse * frame_.lastUBO.ProjectionInverseUnJit);
        constants.prevClipToClip = toSlMatrix(frame_.lastUBO.ProjectionUnJit * frame_.lastUBO.ModelView * frame_.lastUBO.PrevViewProjectionUnJit);
        
        constants.jitterOffset = sl::float2(frame_.lastUBO.Jitter.x, frame_.lastUBO.Jitter.y);
        constants.mvecScale = {1.0f / (float)SwapChain().RenderExtent().width,1.0f / (float)SwapChain().RenderExtent().height}; 
        
        constants.cameraPos = sl::float3(frame_.lastUBO.ModelViewInverse[3][0], frame_.lastUBO.ModelViewInverse[3][1], frame_.lastUBO.ModelViewInverse[3][2]);
        constants.cameraFwd = sl::float3(-frame_.lastUBO.ModelViewInverse[2][0], -frame_.lastUBO.ModelViewInverse[2][1], -frame_.lastUBO.ModelViewInverse[2][2]);
        constants.cameraUp = sl::float3(frame_.lastUBO.ModelViewInverse[1][0], frame_.lastUBO.ModelViewInverse[1][1], frame_.lastUBO.ModelViewInverse[1][2]);
        constants.cameraRight = sl::float3(frame_.lastUBO.ModelViewInverse[0][0], frame_.lastUBO.ModelViewInverse[0][1], frame_.lastUBO.ModelViewInverse[0][2]);
        
        auto& camera = GetScene().GetRenderCamera();
        constants.cameraNear = camera.NearPlane;
        constants.cameraFar = camera.FarPlane;
        constants.cameraFOV = glm::radians(camera.FieldOfView); 
        constants.cameraAspectRatio = (float)SwapChain().Extent().width / (float)SwapChain().Extent().height;
        
        constants.depthInverted = sl::Boolean::eFalse;
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.reset = frame_.frameCount < 2 ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        
        if (SL_FAILED(res2, slSetConstants(constants, *frameToken, viewport)))
        {
            SPDLOG_ERROR("slSetConstants failed: {}", (int)res2);
        }

        // 3. Tags
        // Depth
        auto slDepth = toSlResource(frame_.depthBuffer->GetImage(), frame_.depthBuffer->GetImageMemory().Handle(), frame_.depthBuffer->ImageView().Handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        slDepth.width = SwapChain().RenderExtent().width;
        slDepth.height = SwapChain().RenderExtent().height;
        sl::ResourceTag tagDepth(&slDepth, sl::kBufferTypeDepth, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagDepth, 1, commandBuffer);

        // Motion Vectors
        auto& resMV = bindless_.images[Assets::Bindless::RT_MOTIONVECTOR];
        auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagMV(&slMV, sl::kBufferTypeMotionVectors, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

        // Scaling Input (Color)
        auto& resInput = bindless_.images[Assets::Bindless::RT_DENOISED];
        auto slInput = toSlResource(resInput->GetImage(), resInput->GetImageMemory().Handle(), resInput->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagInput(&slInput, sl::kBufferTypeScalingInputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagInput, 1, commandBuffer);

        // Scaling Output
        sl::Resource slOutput(sl::ResourceType::eTex2d, (void*)frame_.swapChain->Images()[imageIndex], nullptr, (void*)frame_.swapChain->ImageViews()[imageIndex]->Handle(), (uint32_t)VK_IMAGE_LAYOUT_GENERAL);
        slOutput.width = SwapChain().Extent().width;
        slOutput.height = SwapChain().Extent().height;
        slOutput.nativeFormat = (uint32_t)frame_.swapChain->Format();
        sl::ResourceTag tagOutput(&slOutput, sl::kBufferTypeScalingOutputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagOutput, 1, commandBuffer);
        
        if (useDLSSRR)
        {
            // Albedo
            auto& resAlbedo = bindless_.images[Assets::Bindless::RT_ALBEDO];
            auto slAlbedo = toSlResource(resAlbedo->GetImage(), resAlbedo->GetImageMemory().Handle(), resAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagAlbedo(&slAlbedo, sl::kBufferTypeAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagAlbedo, 1, commandBuffer);

            // Specular Albedo
            auto& resSpecAlbedo = bindless_.images[Assets::Bindless::RT_SPECULAR_ALBEDO];
            auto slSpecAlbedo = toSlResource(resSpecAlbedo->GetImage(), resSpecAlbedo->GetImageMemory().Handle(), resSpecAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagSpecAlbedo(&slSpecAlbedo, sl::kBufferTypeSpecularAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagSpecAlbedo, 1, commandBuffer);

            // Normals
            auto& resNormal = bindless_.images[Assets::Bindless::RT_NORMAL];
            auto slNormal = toSlResource(resNormal->GetImage(), resNormal->GetImageMemory().Handle(), resNormal->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagNormal(&slNormal, sl::kBufferTypeNormalRoughness, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagNormal, 1, commandBuffer);
            
            // auto& resMV = bindless_.images[Assets::Bindless::RT_MOTIONVECTOR];
            // auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagMV(&slMV, sl::kBufferTypeSpecularMotionVectors, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

            // Diffuse Noisy
            // auto& resDiffNoisy = bindless_.images[Assets::Bindless::RT_ACCUMLATE_DIFFUSE];
            // auto slDiffNoisy = toSlResource(resDiffNoisy->GetImage(), resDiffNoisy->GetImageMemory().Handle(), resDiffNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagDiffNoisy(&slDiffNoisy, sl::kBufferTypeDiffuseHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagDiffNoisy, 1, commandBuffer);
            //
            // // Specular Noisy
            // auto& resSpecNoisy = bindless_.images[Assets::Bindless::RT_ACCUMLATE_SPECULAR];
            // auto slSpecNoisy = toSlResource(resSpecNoisy->GetImage(), resSpecNoisy->GetImageMemory().Handle(), resSpecNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagSpecNoisy(&slSpecNoisy, sl::kBufferTypeSpecularHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagSpecNoisy, 1, commandBuffer);

            // Diffuse Hit Dist
            auto& resDiffHitDist = bindless_.images[Assets::Bindless::RT_DIFFUSE_HITDIST];
            auto slDiffHitDist = toSlResource(resDiffHitDist->GetImage(), resDiffHitDist->GetImageMemory().Handle(), resDiffHitDist->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagDiffHitDist(&slDiffHitDist, sl::kBufferTypeDiffuseHitDistance, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagDiffHitDist, 1, commandBuffer);
            //
            // // Specular Hit Dist
            // auto& resSpecHitDist = bindless_.images[Assets::Bindless::RT_SPECULAR_HITDIST];
            // auto slSpecHitDist = toSlResource(resSpecHitDist->GetImage(), resSpecHitDist->GetImageMemory().Handle(), resSpecHitDist->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagSpecHitDist(&slSpecHitDist, sl::kBufferTypeSpecularHitDistance, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagSpecHitDist, 1, commandBuffer);
        }

        // 4. Evaluate
        const sl::BaseStructure* inputs[] = { &viewport };
        if (SL_FAILED(res3, slEvaluateFeature(useDLSSRR ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS, *frameToken, inputs, 1, commandBuffer)))
        {
            SPDLOG_ERROR("slEvaluateFeature DLSS failed: {}", (int)res3);
        }
#endif
    }

    void VulkanBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        switch (type)
        {
        case ERendererType::ERT_PathTracing:
            logicRenderers_.renderers[type] = std::make_unique<RayTracing::PathTracingRenderer>(*this);
            break;
        case ERendererType::ERT_ModernDeferred:
            logicRenderers_.renderers[type] = std::make_unique<ModernDeferred::SoftwareTracingRenderer>(*this);
            break;
        case ERendererType::ERT_LegacyDeferred:
            logicRenderers_.renderers[type] = std::make_unique<LegacyDeferred::SoftwareModernRenderer>(*this);
            break;
        case ERendererType::ERT_LegacyDeferredNoAmbient:
            logicRenderers_.renderers[type] = std::make_unique<NoAmbientDeferred::Renderer>(*this);
            break;
        case ERendererType::ERT_VoxelTracing:
            logicRenderers_.renderers[type] = std::make_unique<VoxelTracing::VoxelTracingRenderer>(*this);
            break;
        default:
            assert(false);
        }
        logicRenderers_.current = type;
    }

    FRendererRequirements VulkanBaseRenderer::CurrentRendererRequirements() const
    {
        const auto renderer = logicRenderers_.renderers.find(logicRenderers_.current);
        if (renderer != logicRenderers_.renderers.end())
        {
            return renderer->second->Requirements();
        }

        return GetRendererRequirements(logicRenderers_.current);
    }

    FRendererRequirements VulkanBaseRenderer::RegisteredRendererRequirements() const
    {
        FRendererRequirements requirements;
        for (const auto& logicRenderer : logicRenderers_.renderers)
        {
            requirements.Merge(logicRenderer.second->Requirements());
        }
        return requirements;
    }

    void VulkanBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        logicRenderers_.current = type;
    }

    void VulkanBaseRenderer::DrawWireframeOverlay(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!overlay_.wireframePipeline || imageIndex >= overlay_.wireframeFrameBuffers.size())
        {
            SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
            return;
        }

        SCOPED_GPU_TIMER("wireframe");

        ImageMemoryBarrier::FullInsert(
            commandBuffer, SwapChain().Images()[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeWireframePipeline = *overlay_.wireframePipeline;
        renderPassInfo.renderPass = activeWireframePipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = overlay_.wireframeFrameBuffers[imageIndex].Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = SwapChain().Extent();
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& scene = GetScene();
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activeWireframePipeline.Handle());
            activeWireframePipeline.PipelineLayout().BindDescriptorSets(
                commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdPushConstants(commandBuffer, activeWireframePipeline.PipelineLayout().Handle(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            if (scene.GetIndirectDrawBatchCount() > 0)
            {
                vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), 0,
                                  1, sizeof(VkDrawIndirectCommand));
            }
        }
        vkCmdEndRenderPass(commandBuffer);
    }

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (GOption->ReferenceMode)
        {
            // 后面渲染器会很多，这里只渲染加入reference的，并从rtDenoised Resolve到FrameBuffer
            // 然后就跳过后面的resolve流程了
            for (auto& logicRenderer : logicRenderers_.renderers)
            {
                const char* rendererName = "";
                switch (logicRenderer.first)
                {
                case ERendererType::ERT_PathTracing:
                    rendererName = "PathTracing";
                    break;
                case ERendererType::ERT_ModernDeferred:
                    rendererName = "SoftTracing";
                    break;
                case ERendererType::ERT_LegacyDeferred:
                    rendererName = "SoftModern";
                    break;
                case ERendererType::ERT_LegacyDeferredNoAmbient:
                    rendererName = "SoftModernNoAmbient";
                    break;
                case ERendererType::ERT_VoxelTracing:
                    rendererName = "VoxelTracing";
                    break;
                default:
                    rendererName = "UnknownRenderer";
                    break;
                }

                {
                    SCOPED_GPU_TIMER(rendererName);
                    logicRenderer.second->Render(commandBuffer, imageIndex);
                }

                {
                    SCOPED_GPU_TIMER("resolve pass");
                    SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
                
                    GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                              VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
                
                    std::array<uint32_t, 5> pushConst;
                    
                    switch (logicRenderer.first)
                    {
                    case ERendererType::ERT_PathTracing:
                        pushConst = { imageIndex,SwapChain().Extent().width / 2, SwapChain().Extent().height / 2, SwapChain().RenderExtent().width, SwapChain().RenderExtent().height};
                        break;
                    case ERendererType::ERT_ModernDeferred:
                        pushConst = { imageIndex,SwapChain().Extent().width / 2, 0, SwapChain().RenderExtent().width, SwapChain().RenderExtent().height};
                        break;
                    case ERendererType::ERT_LegacyDeferred:
                        pushConst = { imageIndex,0, 0, SwapChain().RenderExtent().width, SwapChain().RenderExtent().height};
                        break;
                    default:
                        pushConst = { imageIndex,0,SwapChain().Extent().height / 2, SwapChain().RenderExtent().width, SwapChain().RenderExtent().height};
                        break;
                    }
                
                    overlay_.simpleComposePipeline->BindPipeline(commandBuffer, pushConst.data());
                  
                    vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8,
                                  SwapChain().RenderExtent().height / 8, 1);
                    SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
                }
            }
        }
        else
        {
            if (logicRenderers_.renderers.find(logicRenderers_.current) != logicRenderers_.renderers.end())
            {
                SCOPED_GPU_TIMER("logic renderer");
                logicRenderers_.renderers[logicRenderers_.current]->Render(commandBuffer, imageIndex);
            }

            {
                SCOPED_GPU_TIMER("resolve pass");

                //SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
                GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

#if WITH_STREAMLINE
                if (SupportDLSS() && NextEngine::GetInstance()->GetUserSettings().DLSS)
                {
                    UpdateStreamline(commandBuffer, imageIndex);
                }
                else
#endif
                {
#if false
                std::array<uint32_t, 5> pushConst = { imageIndex, uint32_t(SwapChain().OutputOffset().x), uint32_t(SwapChain().OutputOffset().y), uint32_t(SwapChain().OutputExtent().width), uint32_t(SwapChain().OutputExtent().height) };
                overlay_.simpleComposePipeline->BindPipeline(commandBuffer, pushConst.data());

                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);
#else
                VkImageBlit blitRegion = {};
                blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blitRegion.srcOffsets[0] = {0, 0, 0};
                blitRegion.srcOffsets[1] = {static_cast<int32_t>(SwapChain().RenderExtent().width), static_cast<int32_t>(SwapChain().RenderExtent().height), 1};
                blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blitRegion.dstOffsets[0] = {static_cast<int32_t>(SwapChain().OutputOffset().x), static_cast<int32_t>(SwapChain().OutputOffset().y), 0};
                blitRegion.dstOffsets[1] = {static_cast<int32_t>(SwapChain().OutputOffset().x + SwapChain().OutputExtent().width),
                                           static_cast<int32_t>(SwapChain().OutputOffset().y + SwapChain().OutputExtent().height), 1};

                vkCmdBlitImage(commandBuffer,
                               GetStorageImage(Assets::Bindless::RT_DENOISED)->GetImage().Handle(), VK_IMAGE_LAYOUT_GENERAL,
                               SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_GENERAL,
                               1, &blitRegion,
                               VK_FILTER_LINEAR);
#endif
                }

                if (NextEngine::GetInstance()->GetShowFlags().ShowWireframe)
                {
                    DrawWireframeOverlay(commandBuffer, imageIndex);
                }
                else
                {
                    SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
                }
            }
        }
    }

    void VulkanBaseRenderer::ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("clear-ambient-cube-cache");

        constexpr uint32_t cubesPerGroup = 64;
        constexpr uint32_t perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        constexpr uint32_t totalCubeCount = Assets::CUBE_CASCADE_MAX * perCascadeCount;
        const uint32_t groupCount = (totalCubeCount + cubesPerGroup - 1) / cubesPerGroup;

        ambient_.clearCache->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = totalCubeCount;
        gpuScene.custom_data_1 = 0;
        gpuScene.custom_data_2 = 0;

        vkCmdPushConstants(commandBuffer, ambient_.clearCache->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        VkBufferMemoryBarrier barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = GetScene().AmbientCubeBuffer().Handle();
        barriers[0].offset = GetScene().AmbientCubesByteOffset();
        barriers[0].size = static_cast<VkDeviceSize>(totalCubeCount) * sizeof(Assets::AmbientCube);

        barriers[1] = barriers[0];
        barriers[1].buffer = GetScene().FarAmbientCubeBuffer().Handle();
        barriers[1].offset = GetScene().AmbientVoxelsByteOffset();
        barriers[1].size = static_cast<VkDeviceSize>(totalCubeCount) * sizeof(Assets::VoxelData);

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 2, barriers, 0, nullptr);
    }

    void VulkanBaseRenderer::BakeAmbientCubeCascade(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool useHardware)
    {
        Vulkan::PipelineBase* pipeline = useHardware ? static_cast<Vulkan::PipelineBase*>(rt_->directLightGenPipeline.get())
                                                     : static_cast<Vulkan::PipelineBase*>(ambient_.softBake.get());
        if (!pipeline)
        {
            return;
        }

        const int cubesPerGroup = 64;
        const int perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const int group = perCascadeCount / cubesPerGroup;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());

        int temporalFrames = 120;
        switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
        {
        case 0: temporalFrames = 30; break;
        case 1: temporalFrames = 120; break;
        case 2: temporalFrames = 300; break;
        default: temporalFrames = 120; break;
        }

        SCOPED_GPU_TIMER(useHardware ? "hw-lightbake" : "sw-lightbake");

        const uint32_t safeCascadeCount = std::max(1u, cascadeCount);
        const int frame = static_cast<int>((frame_.frameCount / safeCascadeCount) % temporalFrames);
        const int groupPerFrame = std::max(1, (group + temporalFrames - 1) / temporalFrames);
        const int offset = frame * groupPerFrame;
        if (offset >= group)
        {
            return;
        }

        const int dispatchGroupCount = std::min(groupPerFrame, group - offset);
        const int offsetInCubes = offset * cubesPerGroup;
        const uint32_t cascadeIndex = static_cast<uint32_t>(frame_.frameCount % safeCascadeCount);
        const uint32_t cascadeBaseOffset = cascadeIndex * static_cast<uint32_t>(perCascadeCount);
        VkBuffer cubeBuffer = GetScene().AmbientCubeBuffer().Handle();
        VkBuffer pongBuffer = GetScene().AmbientCubePongBuffer().Handle();
        const VkDeviceSize cascadeByteOffset =
            GetScene().AmbientCubesByteOffset() + static_cast<VkDeviceSize>(cascadeBaseOffset) * sizeof(Assets::AmbientCube);
        const VkDeviceSize pongByteOffset = GetScene().AmbientCubesPongByteOffset();
        const VkDeviceSize cascadeByteSize = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(Assets::AmbientCube);

        // ping (cube) -> pong copy with surrounding barriers
        VkBufferMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        preCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.buffer = cubeBuffer;
        preCopyBarrier.offset = cascadeByteOffset;
        preCopyBarrier.size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &preCopyBarrier, 0, nullptr);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = cascadeByteOffset;
        copyRegion.dstOffset = pongByteOffset;
        copyRegion.size = cascadeByteSize;
        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

        VkBufferMemoryBarrier postCopyBarriers[2]{};
        postCopyBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postCopyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].buffer = pongBuffer;
        postCopyBarriers[0].offset = pongByteOffset;
        postCopyBarriers[0].size = cascadeByteSize;
        postCopyBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        postCopyBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].buffer = cubeBuffer;
        postCopyBarriers[1].offset = cascadeByteOffset;
        postCopyBarriers[1].size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 2, postCopyBarriers, 0, nullptr);

        // Dispatch the chosen bake pipeline. Both share the same GPUScene push constant layout.
        if (useHardware)
        {
            rt_->directLightGenPipeline->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }
        else
        {
            ambient_.softBake->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = cascadeBaseOffset + offsetInCubes;
        gpuScene.custom_data_1 = cascadeIndex;
        gpuScene.custom_data_2 = NextEngine::GetInstance()->GetUserSettings().UseAmbientCubePropagation ? 1u : 0u;

        vkCmdPushConstants(commandBuffer, pipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);
    }

    void VulkanBaseRenderer::BakeAmbientCubePropagation(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("propagation-lightbake");

        const int cubesPerGroup = 64;
        const int perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const int group = perCascadeCount / cubesPerGroup;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t safeCascadeCount = std::max(1u, cascadeCount);
        const uint32_t cascadeIndex = static_cast<uint32_t>(frame_.frameCount % safeCascadeCount);
        const uint32_t cascadeBaseOffset = cascadeIndex * static_cast<uint32_t>(perCascadeCount);

        VkBuffer cubeBuffer = GetScene().AmbientCubeBuffer().Handle();
        VkBuffer pongBuffer = GetScene().AmbientCubePongBuffer().Handle();
        const VkDeviceSize cascadeByteOffset =
            GetScene().AmbientCubesByteOffset() + static_cast<VkDeviceSize>(cascadeBaseOffset) * sizeof(Assets::AmbientCube);
        const VkDeviceSize pongByteOffset = GetScene().AmbientCubesPongByteOffset();
        const VkDeviceSize cascadeByteSize = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(Assets::AmbientCube);

        VkBufferMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        preCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.buffer = cubeBuffer;
        preCopyBarrier.offset = cascadeByteOffset;
        preCopyBarrier.size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &preCopyBarrier, 0, nullptr);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = cascadeByteOffset;
        copyRegion.dstOffset = pongByteOffset;
        copyRegion.size = cascadeByteSize;
        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

        VkBufferMemoryBarrier postCopyBarriers[2]{};
        postCopyBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postCopyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].buffer = pongBuffer;
        postCopyBarriers[0].offset = pongByteOffset;
        postCopyBarriers[0].size = cascadeByteSize;
        postCopyBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        postCopyBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].buffer = cubeBuffer;
        postCopyBarriers[1].offset = cascadeByteOffset;
        postCopyBarriers[1].size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 2, postCopyBarriers, 0, nullptr);

        ambient_.propagation->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = cascadeBaseOffset;
        gpuScene.custom_data_1 = cascadeIndex;
        gpuScene.custom_data_2 = 0;

        vkCmdPushConstants(commandBuffer, ambient_.propagation->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, group, 1, 1);

        VkBufferMemoryBarrier propagationToInjectionBarrier{};
        propagationToInjectionBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        propagationToInjectionBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        propagationToInjectionBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        propagationToInjectionBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        propagationToInjectionBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        propagationToInjectionBarrier.buffer = cubeBuffer;
        propagationToInjectionBarrier.offset = cascadeByteOffset;
        propagationToInjectionBarrier.size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &propagationToInjectionBarrier, 0, nullptr);

        ambient_.inject->BindPipeline(commandBuffer, GetScene(), imageIndex);

        vkCmdPushConstants(commandBuffer, ambient_.inject->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, group, 1, 1);

        VkBufferMemoryBarrier postInjectionBarrier{};
        postInjectionBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postInjectionBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        postInjectionBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        postInjectionBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postInjectionBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postInjectionBarrier.buffer = cubeBuffer;
        postInjectionBarrier.offset = cascadeByteOffset;
        postInjectionBarrier.size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &postInjectionBarrier, 0, nullptr);
    }

    void VulkanBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (CurrentRendererRequirements().requestAmbientCube)
        {
            if (NextEngine::GetInstance()->GetUserSettings().UseGpuAmbientCubeSdf &&
                GetScene().ConsumeGpuDistanceFieldRebuild())
            {
                RebuildDistanceFieldCascades(commandBuffer, imageIndex);
            }

            if (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel != 2)
            {
                const bool useHardware = caps_.supportRayTracing && !GOption->ForceSoftGen;
                BakeAmbientCubeCascade(commandBuffer, imageIndex, useHardware);

                if (NextEngine::GetInstance()->GetUserSettings().UseAmbientCubePropagation)
                {
                    BakeAmbientCubePropagation(commandBuffer, imageIndex);
                }
            }
        }

        DispatchVisualDebugger(commandBuffer, imageIndex);
        CopyObjectIdHistory(commandBuffer);
    }

    void VulkanBaseRenderer::RebuildDistanceFieldCascades(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("gpu-distance-field");

        const int cubesPerGroup = 64;
        const int perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const int group = perCascadeCount / cubesPerGroup;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());

        VkBuffer voxelBuffer = GetScene().FarAmbientCubeBuffer().Handle();
        VkBuffer seedBufferA = GetScene().AmbientCubePongBuffer().Handle();
        VkBuffer seedBufferB = GetScene().AmbientCubeSdfScratchBuffer().Handle();
        const VkDeviceSize cascadeByteSize = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(Assets::VoxelData);
        const VkDeviceSize seedByteSize = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(glm::u32vec4);
        const VkDeviceSize seedAByteOffset = GetScene().AmbientCubesPongByteOffset();
        const VkDeviceSize seedBByteOffset = GetScene().AmbientSdfScratchByteOffset();

        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            const uint32_t cascadeBaseOffset = cascadeIndex * static_cast<uint32_t>(perCascadeCount);
            const VkDeviceSize cascadeByteOffset =
                GetScene().AmbientVoxelsByteOffset() + static_cast<VkDeviceSize>(cascadeBaseOffset) * sizeof(Assets::VoxelData);

            VkBufferMemoryBarrier preSdfBarrier{};
            preSdfBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            preSdfBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            preSdfBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            preSdfBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            preSdfBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            preSdfBarrier.buffer = voxelBuffer;
            preSdfBarrier.offset = cascadeByteOffset;
            preSdfBarrier.size = cascadeByteSize;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 1, &preSdfBarrier, 0, nullptr);

            Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
            gpuScene.custom_data_0 = cascadeBaseOffset;
            gpuScene.custom_data_1 = cascadeIndex;
            gpuScene.custom_data_2 = 0;

            ambient_.distanceFieldInit->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdPushConstants(commandBuffer, ambient_.distanceFieldInit->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            vkCmdDispatch(commandBuffer, group, 1, 1);

            VkBufferMemoryBarrier initBarrier{};
            initBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            initBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.buffer = seedBufferA;
            initBarrier.offset = seedAByteOffset;
            initBarrier.size = seedByteSize;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 1, &initBarrier, 0, nullptr);

            uint32_t passParity = 0;
            for (uint32_t step = 128; step >= 1; step >>= 1, ++passParity)
            {
                ambient_.distanceFieldJump->BindPipeline(commandBuffer, GetScene(), imageIndex);

                gpuScene.custom_data_1 = passParity;
                gpuScene.custom_data_2 = step;
                vkCmdPushConstants(commandBuffer, ambient_.distanceFieldJump->PipelineLayout().Handle(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
                vkCmdDispatch(commandBuffer, group, 1, 1);

                VkBufferMemoryBarrier jumpBarriers[2]{};
                jumpBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                jumpBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                jumpBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                jumpBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                jumpBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                jumpBarriers[0].buffer = seedBufferA;
                jumpBarriers[0].offset = seedAByteOffset;
                jumpBarriers[0].size = seedByteSize;
                jumpBarriers[1] = jumpBarriers[0];
                jumpBarriers[1].buffer = seedBufferB;
                jumpBarriers[1].offset = seedBByteOffset;
                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 2, jumpBarriers, 0, nullptr);

                if (step == 1) break;
            }

            ambient_.distanceFieldResolve->BindPipeline(commandBuffer, GetScene(), imageIndex);
            gpuScene.custom_data_1 = passParity - 1;
            gpuScene.custom_data_2 = 0;
            vkCmdPushConstants(commandBuffer, ambient_.distanceFieldResolve->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            vkCmdDispatch(commandBuffer, group, 1, 1);

            VkBufferMemoryBarrier postResolveBarrier{};
            postResolveBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            postResolveBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            postResolveBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            postResolveBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            postResolveBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            postResolveBarrier.buffer = voxelBuffer;
            postResolveBarrier.offset = cascadeByteOffset;
            postResolveBarrier.size = cascadeByteSize;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 1, &postResolveBarrier, 0, nullptr);
        }
    }

    void VulkanBaseRenderer::DispatchVisualDebugger(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!VisualDebug())
        {
            return;
        }

        SCOPED_GPU_TIMER("visual debugger");
        SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);

        std::array<uint32_t, 5> pushConst = {
            imageIndex,
            uint32_t(SwapChain().OutputOffset().x), uint32_t(SwapChain().OutputOffset().y),
            uint32_t(SwapChain().OutputExtent().width), uint32_t(SwapChain().OutputExtent().height)
        };
        overlay_.visualDebuggerPipeline->BindPipeline(commandBuffer, pushConst.data());
        vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);

        SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::CopyObjectIdHistory(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("objectid copy");
        GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->InsertBarrier(commandBuffer,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        GetStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->InsertBarrier(commandBuffer,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().width,
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().height, 1};

        vkCmdCopyImage(commandBuffer,
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);
    }
    
    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        frame_.lastUBO = GetUniformBufferObject(frame_.swapChain->RenderOffset(), frame_.swapChain->OutputExtent());
        frame_.uniformBuffers[imageIndex].SetValue(frame_.lastUBO);
    }

    std::vector<RayTracing::TopLevelAccelerationStructure>& VulkanBaseRenderer::TLAS()
    {
        static std::vector<RayTracing::TopLevelAccelerationStructure> empty;
        return rt_ ? rt_->tlas : empty;
    }

    void VulkanBaseRenderer::OnPreLoadScene()
    {
        if (caps_.supportRayTracing)
        {
            DeleteAccelerationStructures();
        }
    }

    void VulkanBaseRenderer::OnPostLoadScene()
    {
        skin_.updateRequests.clear();
        if (caps_.supportRayTracing)
        {
            CreateAccelerationStructures();
        }
    }

    void VulkanBaseRenderer::AfterUpdateScene()
    {
        if (!caps_.supportRayTracing)
        {
            return;
        }

        auto& scene = GetScene();

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        auto& nodeTrans = scene.GetNodeProxys();
        if (nodeTrans.empty() || rt_->blas.empty())
        {
            rt_->tlasUpdateRequest = 0;
            return;
        }

        instances.reserve(nodeTrans.size());
        for (size_t i = 0; i < nodeTrans.size(); i++)
        {
            auto& node = nodeTrans[i];
            const size_t blasIndex = node.modelId / 10;
            if (blasIndex >= rt_->blas.size())
            {
                SPDLOG_WARN("Skipping TLAS instance with stale model index {} (BLAS count {})", blasIndex,
                            rt_->blas.size());
                continue;
            }
            instances.push_back(RayTracing::TopLevelAccelerationStructure::CreateInstance(
                rt_->blas[blasIndex], glm::transpose(node.worldTS), node.instanceId, node.visible && !node.nort));
        }

        int instanceCount = static_cast<int>(instances.size());
        if (instanceCount > 0)
        {
            auto* data = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(
                rt_->instancesMemory->Map(0, instances.size() * sizeof(VkAccelerationStructureInstanceKHR)));
            std::memcpy(data, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
            rt_->instancesMemory->Unmap();
        }

        rt_->tlasUpdateRequest = instanceCount;
    }

    namespace
    {
        template <class TAccelerationStructure>
        VkAccelerationStructureBuildSizesInfoKHR GetTotalRequirements(
            const std::vector<TAccelerationStructure>& accelerationStructures)
        {
            VkAccelerationStructureBuildSizesInfoKHR total{};
            for (const auto& accelerationStructure : accelerationStructures)
            {
                total.accelerationStructureSize += accelerationStructure.BuildSizes().accelerationStructureSize;
                total.buildScratchSize += accelerationStructure.BuildSizes().buildScratchSize;
                total.updateScratchSize += accelerationStructure.BuildSizes().updateScratchSize;
            }
            return total;
        }
    }

    void VulkanBaseRenderer::CreateAccelerationStructures()
    {
        const auto timer = std::chrono::high_resolution_clock::now();

        SingleTimeCommands::Submit(CommandPool(), [this](VkCommandBuffer commandBuffer)
        {
            CreateBottomLevelStructures(commandBuffer);
            CreateTopLevelStructures(commandBuffer);
        });

        const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();
        SPDLOG_INFO("- built acceleration structures in {:.2f}ms", elapsed * 1000.f);
    }

    void VulkanBaseRenderer::DeleteAccelerationStructures()
    {
        if (rt_)
        {
            rt_->tlas.clear();
            rt_->instancesBuffer.reset();
            rt_->instancesMemory.reset();
            rt_->tlasScratch.reset();
            rt_->tlasScratchMemory.reset();
            rt_->tlasBuffer.reset();
            rt_->tlasMemory.reset();

            rt_->blas.clear();
            rt_->blasScratch.reset();
            rt_->blasScratchMemory.reset();
            rt_->blasBuffer.reset();
            rt_->blasMemory.reset();
        }
    }

    void VulkanBaseRenderer::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();

        UpdateSkinningBuffers();

        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t aabbOffset = 0;

        if (scene.Models().empty())
        {
            RayTracing::BottomLevelGeometry geometries;
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);
        }

        for (size_t modelIdx = 0; modelIdx < scene.Models().size(); ++modelIdx)
        {
            auto& model = scene.Models()[modelIdx];
            bool hasSkin = false;
            for (const auto& node : scene.Nodes())
            {
                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (render && render->GetModelId() == modelIdx && render->GetSkinIndex() != -1)
                {
                    hasSkin = true;
                    break;
                }
            }

            const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
            const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
            RayTracing::BottomLevelGeometry geometries;

            VkDeviceAddress vertexAddr = 0;
            if (hasSkin && skin_.vertexBuffer)
            {
                vertexAddr = skin_.vertexBuffer->GetDeviceAddress();
            }

            geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true, vertexAddr);
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);

            vertexOffset += vertexCount * sizeof(Assets::GPUVertex);
            indexOffset += indexCount * sizeof(uint32_t);
            aabbOffset += sizeof(VkAabbPositionsKHR);
        }

        const auto total = GetTotalRequirements(rt_->blas);

        rt_->blasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->blasMemory.reset(new DeviceMemory(
            rt_->blasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));
        rt_->blasScratch.reset(new Buffer(Device(), total.buildScratchSize,
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->blasScratchMemory.reset(new DeviceMemory(
            rt_->blasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));

        debugUtils.SetObjectName(rt_->blasBuffer->Handle(), "BLAS Buffer");
        rt_->blasMemory->SetName("BLAS Memory");
        debugUtils.SetObjectName(rt_->blasScratch->Handle(), "BLAS Scratch Buffer");
        rt_->blasScratchMemory->SetName("BLAS Scratch Memory");

        VkDeviceSize resultOffset = 0;
        VkDeviceSize scratchOffset = 0;

        for (size_t i = 0; i != rt_->blas.size(); ++i)
        {
            rt_->blas[i].Generate(commandBuffer, *rt_->blasScratch, scratchOffset, *rt_->blasBuffer, resultOffset);
            resultOffset += rt_->blas[i].BuildSizes().accelerationStructureSize;
            scratchOffset += rt_->blas[i].BuildSizes().buildScratchSize;
            debugUtils.SetObjectName(rt_->blas[i].Handle(), ("BLAS #" + std::to_string(i)).c_str());
        }
    }

    void VulkanBaseRenderer::CreateTopLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& debugUtils = Device().DebugUtils();
        const uint32_t kMaxInstanceCount = 65535;

        rt_->instancesBuffer.reset(new Buffer(Device(), kMaxInstanceCount * sizeof(VkAccelerationStructureInstanceKHR),
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->instancesMemory.reset(new DeviceMemory(
            rt_->instancesBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);

        rt_->tlas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties,
                            rt_->instancesBuffer->GetDeviceAddress(), kMaxInstanceCount);

        const auto total = GetTotalRequirements(rt_->tlas);

        rt_->tlasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->tlasMemory.reset(new DeviceMemory(
            rt_->tlasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        rt_->tlasScratch.reset(new Buffer(Device(), total.buildScratchSize,
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->tlasScratchMemory.reset(new DeviceMemory(
            rt_->tlasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        debugUtils.SetObjectName(rt_->tlasBuffer->Handle(), "TLAS Buffer");
        rt_->tlasMemory->SetName("TLAS Memory");
        debugUtils.SetObjectName(rt_->tlasScratch->Handle(), "TLAS Scratch Buffer");
        rt_->tlasScratchMemory->SetName("TLAS Scratch Memory");
        debugUtils.SetObjectName(rt_->instancesBuffer->Handle(), "TLAS Instances Buffer");
        rt_->instancesMemory->SetName("TLAS Instances Memory");

        rt_->tlas[0].Generate(commandBuffer, *rt_->tlasScratch, 0, *rt_->tlasBuffer, 0);
        debugUtils.SetObjectName(rt_->tlas[0].Handle(), "TLAS");
    }

}
