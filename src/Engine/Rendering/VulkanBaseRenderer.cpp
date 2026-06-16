#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Rendering/VoxelTracing/VoxelTracingRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Rendering/Upscaler/StreamlineIntegration.hpp"
#include <utility>

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
    namespace
    {
        using RendererFactory = std::unique_ptr<LogicRendererBase> (*)(VulkanBaseRenderer&);

        struct RendererDescriptor
        {
            ERendererType type;
            const char* name;
            FRendererRequirements requirements;
            uint32_t referenceColumn;
            uint32_t referenceRow;
            RendererFactory factory;
        };

        template <typename T>
        std::unique_ptr<LogicRendererBase> CreateLogicRenderer(VulkanBaseRenderer& baseRenderer)
        {
            return std::make_unique<T>(baseRenderer);
        }

        const std::array RendererDescriptors{
            RendererDescriptor{ERT_PathTracing, "PathTracing", {true, true}, 1, 1,
                               &CreateLogicRenderer<PathTracing::PathTracingRenderer>},
            RendererDescriptor{ERT_SoftwareTracing, "SoftwareTracing", {true, false}, 1, 0,
                               &CreateLogicRenderer<SoftwareTracing::SoftwareTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModern, "SoftwareModern", {true, false}, 0, 0,
                               &CreateLogicRenderer<SoftwareModern::SoftwareModernRenderer>},
            RendererDescriptor{ERT_VoxelTracing, "VoxelTracing", {true, false}, 0, 1,
                               &CreateLogicRenderer<VoxelTracing::VoxelTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModernNoAmbient, "SoftwareModernNoAmbient", {}, 0, 1,
                               &CreateLogicRenderer<SoftwareModernNoAmbient::SoftwareModernNoAmbientRenderer>},
        };

        const RendererDescriptor& GetRendererDescriptor(ERendererType type)
        {
            const auto descriptor = std::find_if(
                RendererDescriptors.begin(), RendererDescriptors.end(),
                [type](const RendererDescriptor& candidate) { return candidate.type == type; });
            assert(descriptor != RendererDescriptors.end());
            return descriptor != RendererDescriptors.end() ? *descriptor : RendererDescriptors.front();
        }
    }

    FRendererRequirements GetRendererRequirements(ERendererType type)
    {
        return GetRendererDescriptor(type).requirements;
    }

    const char* GetRendererName(ERendererType type)
    {
        return GetRendererDescriptor(type).name;
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

        // Vulkan Video H.264 encode (remote play hardware path). Probe before device creation;
        // the feature struct must outlive ctx_.device.reset().
        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features = {};
        if (GOption->RemoteMode && GOption->RemoteEncoder != "openh264")
        {
            videoCaps_ = FVulkanVideoCaps::Probe(ctx_.instance->Handle(), physicalDevice);
            videoCaps_.LogSummary();
            if (videoCaps_.Usable())
            {
                requiredExtensions.insert(requiredExtensions.end(),
                    {
                        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
                    });
                if (videoCaps_.maintenance1Present)
                {
                    requiredExtensions.push_back(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
                }
                synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
                synchronization2Features.pNext = nextDeviceFeatures;
                synchronization2Features.synchronization2 = true;
                nextDeviceFeatures = &synchronization2Features;
            }
            else if (GOption->RemoteEncoder == "vulkan")
            {
                SPDLOG_WARN("RemotePlay: --remote-encoder vulkan requested but Vulkan Video H.264 encode is not "
                            "usable on this device; falling back to openh264");
            }
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
        CREATE_STORAGE_IMAGE(RT_ATROUS_PING, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_PONG, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_OUT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_SPEC_OUT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);

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


    void VulkanBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        logicRenderers_.renderers[type] = GetRendererDescriptor(type).factory(*this);
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

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (GOption->ReferenceMode)
        {
            // 后面渲染器会很多，这里只渲染加入reference的，并从rtDenoised Resolve到FrameBuffer
            // 然后就跳过后面的resolve流程了
            for (auto& logicRenderer : logicRenderers_.renderers)
            {
                const auto& rendererDescriptor = GetRendererDescriptor(logicRenderer.first);

                {
                    SCOPED_GPU_TIMER(rendererDescriptor.name);
                    logicRenderer.second->Render(commandBuffer, imageIndex);
                }

                {
                    SCOPED_GPU_TIMER("resolve pass");
                    SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
                
                    GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                              VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
                
                    const std::array<uint32_t, 5> pushConst{
                        imageIndex,
                        rendererDescriptor.referenceColumn * SwapChain().Extent().width / 2,
                        rendererDescriptor.referenceRow * SwapChain().Extent().height / 2,
                        SwapChain().RenderExtent().width,
                        SwapChain().RenderExtent().height};
                
                    overlay_.simpleComposePipeline->BindPipeline(commandBuffer, pushConst.data());
                  
                    vkCmdDispatch(
                        commandBuffer,
                        Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                        Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
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

                vkCmdDispatch(
                    commandBuffer,
                    Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, 8),
                    Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, 8), 1);
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

    void VulkanBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();

        if (CurrentRendererRequirements().requestAmbientCube && !ShouldSkipAmbientCubeUpdates())
        {
            if (settings.UseGpuAmbientCubeSdf &&
                GetScene().ConsumeGpuDistanceFieldRebuild())
            {
                RebuildDistanceFieldCascades(commandBuffer, imageIndex);
            }

            if (settings.BakeSpeedLevel != 2)
            {
                const bool useHardware = caps_.supportRayTracing && !GOption->ForceSoftGen;
                BakeAmbientCubeCascade(commandBuffer, imageIndex, useHardware);
            }
        }

        DispatchVisualDebugger(commandBuffer, imageIndex);
        CopyObjectIdHistory(commandBuffer);
    }

    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        frame_.lastUBO = GetUniformBufferObject(frame_.swapChain->RenderOffset(), frame_.swapChain->OutputExtent());
        frame_.uniformBuffers[imageIndex].SetValue(frame_.lastUBO);
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

}
