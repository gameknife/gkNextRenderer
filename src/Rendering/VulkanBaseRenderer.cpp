#include "VulkanBaseRenderer.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandPool.hpp"
#include "Vulkan/CommandBuffers.hpp"
#include "Vulkan/DebugUtilsMessenger.hpp"
#include "Vulkan/DepthBuffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Fence.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/Instance.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/Semaphore.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/WindowSurface.hpp"
#include "Vulkan/Enumerate.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/Strings.hpp"
#include "Vulkan/Version.hpp"

#include "Assets/Core/Scene.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include "Assets/GPU/Texture.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"

#include "Utilities/Exception.hpp"
#include <array>
#include "Common/CoreMinimal.hpp"

#include "Options.hpp"
#include "SoftwareModern/SoftwareModernRenderer.hpp"
#include "SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "PathTracing/PathTracingRenderer.hpp"
#include "Runtime/Engine.hpp"
#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include <spdlog/spdlog.h>
#include <utility>

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
#endif

namespace StreamlineWrapper
{
    bool GStreamLineInit = false;
    bool GStreamLineEnabled = false;
    
   void LazyInit(VkDevice device, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t computeQueueIdx, uint32_t computeQueueFamily, uint32_t graphicsQueueIdx, uint32_t graphicsQueueFamily, bool& outSupportDLSS, bool& outSupportDLSSRR)
   {
#if WITH_STREAMLINE
       if (GStreamLineInit) return;
       GStreamLineInit = true;
       
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
        if(SL_FAILED(res, slInit(pref)))
        {
            SPDLOG_ERROR("Streamline slInit failed: {}", (int)res);
            return;
        }
        
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
       
       GStreamLineEnabled = true;
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
    void PrintVulkanSdkInformation()
    {
        SPDLOG_INFO("Vulkan SDK Header Version: {}", VK_HEADER_VERSION);
    }
    
    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application)
    {
        for (const auto& device : application.PhysicalDevices())
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

    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, uint32_t gpuIdx)
    {
        const auto& physicalDevices = application.PhysicalDevices();
        VkPhysicalDevice pDevice = physicalDevices[gpuIdx <= physicalDevices.size() ? gpuIdx : 0];
        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(pDevice, &deviceProp);

        SPDLOG_INFO("Setting Device [{}]", deviceProp.properties.deviceName);
        application.SetPhysicalDevice(pDevice);
    }
}

namespace Vulkan
{
    VulkanBaseRenderer::VulkanBaseRenderer(Vulkan::Window* window, const VkPresentModeKHR presentMode,
                                           const bool enableValidationLayers,
                                           Instance* instance) :
        presentMode_(presentMode)
    {
        window_ = window;
        instance_.reset(instance);
        debugUtilsMessenger_.reset(enableValidationLayers
                       ? new DebugUtilsMessenger( *instance_, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
                       : nullptr);
        surface_.reset(new Surface(*instance_));
        supportDenoiser_ = false;
        forceSDR_ = GOption->ForceSDR;

        supportRayTracing_ = !GOption->ForceNoRT && instance_->SupportsRayQuery();
    }

    VulkanBaseRenderer::~VulkanBaseRenderer()
    {
        VulkanBaseRenderer::DeleteSwapChain();
        gpuTimer_.reset();
        globalTexturePool_.reset();
        commandPool_.reset();
        commandPool2_.reset();
        device_.reset();
        surface_.reset();
        debugUtilsMessenger_.reset();
        instance_.reset();
        window_ = nullptr;
    }

    const std::vector<VkExtensionProperties>& VulkanBaseRenderer::Extensions() const
    {
        return instance_->Extensions();
    }

    const std::vector<VkLayerProperties>& VulkanBaseRenderer::Layers() const
    {
        return instance_->Layers();
    }

    const std::vector<VkPhysicalDevice>& VulkanBaseRenderer::PhysicalDevices() const
    {
        return instance_->PhysicalDevices();
    }

    void VulkanBaseRenderer::SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        if (device_)
        {
            Throw(std::logic_error("physical device has already been set"));
        }

        std::vector<const char*> requiredExtensions =
        {
            // VK_KHR_swapchain
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if __APPLE__
		    "VK_KHR_portability_subset",
#endif
        };

        VkPhysicalDeviceFeatures deviceFeatures = {};

        deviceFeatures.multiDrawIndirect = true;
        deviceFeatures.drawIndirectFirstInstance = true;

        SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nullptr);

        globalTexturePool_.reset(new Assets::GlobalTexturePool(*device_, *commandPool2_, *commandPool_));

        OnDeviceSet();
        CreateSwapChain();
        window_->Show();
    }

    void VulkanBaseRenderer::Start()
    {
        // setup vulkan
        PrintVulkanSdkInformation();
        PrintVulkanDevices(*this);
        SetVulkanDevice(*this, GOption->GpuIdx);
        PrintVulkanSwapChainInformation(*this);
        currentFrame_ = 0;

        supportDLSS_ = true;
        supportDLSSRR_ = true;
    }

    void VulkanBaseRenderer::End()
    {
        StreamlineWrapper::Shutdown();
        device_->WaitIdle();
        gpuTimer_.reset();
        globalTexturePool_.reset();
    }

    Assets::Scene& VulkanBaseRenderer::GetScene()
    {
        return *scene_.lock();
    }

    void VulkanBaseRenderer::SetScene(std::shared_ptr<Assets::Scene> scene)
    {
        scene_ = scene;
    }

    Assets::UniformBufferObject VulkanBaseRenderer::GetUniformBufferObject(
        const VkOffset2D offset, const VkExtent2D extent) const
    {
        if (DelegateGetUniformBufferObject)
        {
            return DelegateGetUniformBufferObject(offset, extent);
        }
        return {};
    }

    void VulkanBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        deviceFeatures.fillModeNonSolid = true;
        deviceFeatures.samplerAnisotropy = true;
        deviceFeatures.shaderStorageImageReadWithoutFormat = true;
        deviceFeatures.shaderStorageImageWriteWithoutFormat = true;
        deviceFeatures.shaderInt16 = true;
        deviceFeatures.shaderInt64 = true;

#if WITH_OIDN
        // Required extensions.
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
                                  });
#if WIN32 && !defined(__MINGW32__)
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#elif __linux__ || __APPLE__
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
#endif
        
#endif
        
        // Required extensions. windows only
#if WIN32
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
                                      VK_NVX_BINARY_IMPORT_EXTENSION_NAME,
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
#if WIN32
        indexingFeatures.pNext = &shaderClockFeatures;
#else
	indexingFeatures.pNext = nextDeviceFeatures;
#endif
        indexingFeatures.runtimeDescriptorArray = true;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
        indexingFeatures.descriptorBindingPartiallyBound = true;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = true;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = true;
        indexingFeatures.descriptorBindingVariableDescriptorCount = true;


        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = true;

        VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
        hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        hostQueryResetFeatures.pNext = &bufferDeviceAddressFeatures;
        hostQueryResetFeatures.hostQueryReset = true;

        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shaderFloat16Int8Features = {};
        shaderFloat16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        shaderFloat16Int8Features.pNext = &hostQueryResetFeatures;
        shaderFloat16Int8Features.shaderFloat16 = true;
        //shaderFloat16Int8Features.shaderInt8 = true;

        VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {};
        shaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        shaderDrawParametersFeatures.pNext = &shaderFloat16Int8Features;
        shaderDrawParametersFeatures.shaderDrawParameters = true;

        VkPhysicalDevice16BitStorageFeatures storage16BitFeatures = {};
        storage16BitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        storage16BitFeatures.pNext = &shaderDrawParametersFeatures;
        storage16BitFeatures.storageBuffer16BitAccess = true;

#if WITH_STREAMLINE
        VkPhysicalDeviceVulkan12Features deviceVulkan12Features = {};
        deviceVulkan12Features.timelineSemaphore = true;
        deviceVulkan12Features.pNext = &shaderDrawParametersFeatures;
        storage16BitFeatures.pNext = &deviceVulkan12Features;
        
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME,
                                      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                      VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
                                  });
#endif
        
        device_.reset(new class Device(physicalDevice, *surface_, requiredExtensions, deviceFeatures,
                                       &storage16BitFeatures));
        commandPool_.reset(new class CommandPool(*device_, device_->GraphicsFamilyIndex(), 0, true));
        commandPool2_.reset(new class CommandPool(*device_, device_->TransferFamilyIndex(), 1, true));
        gpuTimer_.reset(new VulkanGpuTimer(*device_, 200, device_->DeviceProperties()));
    }

    void VulkanBaseRenderer::OnDeviceSet()
    {
#if WITH_OIDN
        InitOIDN();
#endif

        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->OnDeviceSet();
        }

        if (DelegateOnDeviceSet)
        {
            DelegateOnDeviceSet();
        }
    }

    void VulkanBaseRenderer::CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName)
    {
        bindlessStorageImages_[bindlessIdx].reset(new RenderImage(Device(), swapChain_->RenderExtent(), format, tiling, usage, false, debugName));
        if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {
            globalTexturePool_->BindStorageTexture(bindlessIdx, bindlessStorageImages_[bindlessIdx]->GetImageView());
        }
    }

    const RenderImage* VulkanBaseRenderer::GetStorageImage(uint32_t bindlessIdx) const
    {
        assert(bindlessIdx < bindlessStorageImages_.size());
        return bindlessStorageImages_[bindlessIdx].get();
    }

    uint32_t VulkanBaseRenderer::GetTemporalStorageImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
        const char* debugName)
    {
        uint32_t targetIdx = Assets::Bindless::RT_TEMP_USAGE0 + tempStorageImageCreated_;
        auto& target = bindlessStorageImages_[targetIdx];
        assert(!target);
        CreateStorageImage(targetIdx, format, tiling, usage, debugName);
        tempStorageImageCreated_++;

        return targetIdx;
    }

#define CREATE_STORAGE_IMAGE(idx, fmt, tiling, usage) CreateStorageImage(Assets::Bindless::idx, fmt, tiling, usage, #idx)
    
    void VulkanBaseRenderer::CreateRenderImages()
    {
        screenShotImage_.reset(new Image(*device_, swapChain_->Extent(), 1, swapChain_->Format(),
                                         VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        screenShotImageMemory_.reset(new DeviceMemory(
            screenShotImage_->
            AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));

        bindlessStorageImages_.resize(Assets::Bindless::RT_COUNT);
        
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

        for (uint32_t i = 0; i != swapChain_->Images().size(); i++)
        {
            globalTexturePool_->BindStorageTexture( Assets::Bindless::RT_SWAPCHAIN0 + i, *swapChain_->ImageViews()[i] );
        }

#if WITH_OIDN
        SetupOIDN(swapChain_->RenderExtent());
#endif
    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        // 窗口等待
        while (window_->IsMinimized())
        {
            window_->WaitForEvents();
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
        
        swapChain_.reset(new class SwapChain(*device_, presentMode_, forceSDR_));
        swapChain_->UpdateRenderViewport(0, 0, (uint32_t)(swapChain_->Extent().width / scale), (uint32_t)(swapChain_->Extent().height / scale));
        swapChain_->UpdateOutputViewport( 0, 0, swapChain_->Extent().width, swapChain_->Extent().height);

        // depthBuffer
        depthBuffer_.reset(new class DepthBuffer(*commandPool_, swapChain_->Extent()));

        // 同步对象
        for (size_t i = 0; i != swapChain_->ImageViews().size(); ++i)
        {
            imageAvailableSemaphores_.emplace_back(*device_);
            renderFinishedSemaphores_.emplace_back(*device_);
            inFlightFences_.emplace_back(*device_, true);
            uniformBuffers_.emplace_back(*device_);
        }

        // commandbuffer
        commandBuffers_.reset(new CommandBuffers(*commandPool_, static_cast<uint32_t>(swapChain_->ImageViews().size())));

        currentFence = nullptr;

        // 公用RenderImages
        CreateRenderImages();

        // 最简单的fallback pipeline, 也用作 wireframe pipeline
        wireframePipeline_.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true));
        wireframeFramebuffer_.reset(new FrameBuffer(swapChain_->RenderExtent(), GetStorageImage(Assets::Bindless::RT_DENOISED)->GetImageView(), wireframePipeline_->RenderPass()));

        // 公用Pipeline
        simpleComposePipeline_.reset( new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.UpScaleFSR.comp.slang.spv", 20));
        bufferClearPipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*swapChain_, "assets/shaders/Util.BufferClear.comp.slang.spv", 4));
        softAmbientCubeGenPipeline_.reset( new PipelineCommon::ZeroBindPipeline(*swapChain_, "assets/shaders/Bake.SwAmbientCube.comp.slang.spv"));
        gpuCullPipeline_.reset(new PipelineCommon::ZeroBindPipeline(*swapChain_, "assets/shaders/Task.GpuCull.comp.slang.spv"));
        skinningPipeline_.reset(new PipelineCommon::ZeroBindPipeline(*swapChain_, "assets/shaders/Task.Skinning.comp.slang.spv"));
        visualDebuggerPipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*swapChain_, "assets/shaders/Util.VisualDebugger.comp.slang.spv", 20));

        visibilityPipeline_.reset(new PipelineCommon::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        visibilityFrameBuffer_.reset(new FrameBuffer(swapChain_->RenderExtent(), GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(), visibilityPipeline_->RenderPass()));

        // 逻辑Renderer
        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->CreateSwapChain(swapChain_->RenderExtent());
        }

        // Delegate
        if (DelegateCreateSwapChain)
        {
            DelegateCreateSwapChain();
        }
    }

    void VulkanBaseRenderer::DeleteSwapChain()
    {
        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->DeleteSwapChain();
        }

#if WITH_OIDN
        rtDenoise0_.reset();
        rtDenoise1_.reset();
        rtAlbedo_.reset();
        rtNormal_.reset();
#endif

        if (DelegateDeleteSwapChain)
        {
            DelegateDeleteSwapChain();
        }

        for ( auto& storageImage : bindlessStorageImages_ )
        {
            storageImage.reset();
        }
        tempStorageImageCreated_ = 0;

        visibilityPipeline_.reset();
        visibilityFrameBuffer_.reset();
        
        screenShotImageMemory_.reset();
        screenShotImage_.reset();
        commandBuffers_.reset();
        wireframePipeline_.reset();
        wireframeFramebuffer_.reset();
        bufferClearPipeline_.reset();
        softAmbientCubeGenPipeline_.reset();
        gpuCullPipeline_.reset();
        skinningPipeline_.reset();

        skinnedVertexBuffer_.reset();
        skinnedVertexBufferMemory_.reset();
        skinnedSimpleVertexBuffer_.reset();
        skinnedSimpleVertexBufferMemory_.reset();
        jointMatricesBuffer_.reset();
        jointMatricesBufferMemory_.reset();

        simpleComposePipeline_.reset();
        visualDebuggerPipeline_.reset();
        uniformBuffers_.clear();
        inFlightFences_.clear();
        renderFinishedSemaphores_.clear();
        imageAvailableSemaphores_.clear();
        depthBuffer_.reset();
        swapChain_.reset();

        currentFence = nullptr;
    }

    void VulkanBaseRenderer::RecreateSwapChain()
    {
        device_->WaitIdle();
        DeleteSwapChain();
        CreateSwapChain();
    }

    void VulkanBaseRenderer::CaptureScreenShot()
    {
        SingleTimeCommands::Submit(CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            SCOPED_GPU_TIMER("screenshot");
            const auto& image = swapChain_->Images()[currentImageIndex_];

            ImageMemoryBarrier::FullInsert(commandBuffer, image, 0, VK_ACCESS_TRANSFER_READ_BIT,
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            ImageMemoryBarrier::FullInsert(commandBuffer, screenShotImage_->Handle(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
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
                           screenShotImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[currentImageIndex_],
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
        int rtxFlags = supportRayTracing_ ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        size_t requiredVertexSize = vertCount * sizeof(Assets::GPUVertex);
        if (!skinnedVertexBuffer_ || currentSkinnedVertexBufferSize_ < requiredVertexSize)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(*commandPool_, "SkinnedVertices", flags | rtxFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requiredVertexSize, skinnedVertexBuffer_, skinnedVertexBufferMemory_);
            currentSkinnedVertexBufferSize_ = (uint32_t)requiredVertexSize;
        }

        size_t requiredSimpleVertexSize = vertCount * sizeof(short) * 4;
        if (!skinnedSimpleVertexBuffer_ || currentSkinnedSimpleVertexBufferSize_ < requiredSimpleVertexSize)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(*commandPool_, "SkinnedVerticesSimple", flags | rtxFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requiredSimpleVertexSize, skinnedSimpleVertexBuffer_, skinnedSimpleVertexBufferMemory_);
            currentSkinnedSimpleVertexBufferSize_ = (uint32_t)requiredSimpleVertexSize;
        }

        uint32_t totalJoints = 0;
        for (auto& node : scene.Nodes())
        {
            if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
            {
                totalJoints += (uint32_t)skinnedMesh->GetJointMatrices().size();
            }
        }

        if (totalJoints > 0)
        {
            size_t requiredJointSize = totalJoints * sizeof(glm::mat4);
            if (!jointMatricesBuffer_ || currentJointMatrixBufferSize_ < requiredJointSize)
            {
                Vulkan::BufferUtil::CreateDeviceBufferLocal(*commandPool_, "JointMatrices", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requiredJointSize, jointMatricesBuffer_, jointMatricesBufferMemory_);
                currentJointMatrixBufferSize_ = (uint32_t)requiredJointSize;
            }

            // Map and upload
            glm::mat4* data = (glm::mat4*)jointMatricesBufferMemory_->Map(0, requiredJointSize);
            uint32_t offset = 0;
            for (auto& node : scene.Nodes())
            {
                if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
                {
                    const auto& matrices = skinnedMesh->GetJointMatrices();
                    std::memcpy(data + offset, matrices.data(), matrices.size() * sizeof(glm::mat4));
                    offset += (uint32_t)matrices.size();
                }
            }
            jointMatricesBufferMemory_->Unmap();
        }
    }

    void VulkanBaseRenderer::PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        UpdateSkinningBuffers();
        InitializeBarriers(commandBuffer);

        if (true)
        {
            SCOPED_GPU_TIMER("skinning pass");
            auto& scene = GetScene();

            if (skinnedVertexBuffer_) {
                scene.SetSkinningBuffers(skinnedVertexBuffer_->GetDeviceAddress(), skinnedSimpleVertexBuffer_->GetDeviceAddress(), jointMatricesBuffer_ ? jointMatricesBuffer_->GetDeviceAddress() : 0);
            } else {
                scene.SetSkinningBuffers(0, 0, 0);
            }

            skinningPipeline_->BindPipeline(commandBuffer, scene, imageIndex);

            Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex);
            if (skinnedVertexBuffer_)
            {
                uint32_t proxyIdx = 0;
                for ( size_t i = 0; i < skinModelUpdateRequests_.size(); i++ )
                {
                    uint32_t modelId = skinModelUpdateRequests_[i];
                    if (modelId != -1)
                    {
                       auto model = scene.GetModel(modelId);
                        uint32_t vertexOffset = scene.Offsets()[modelId * 10].vertexOffset;
                        uint32_t vertexCount = model->NumberOfVertices();

                        gpuScene.custom_data_0 = proxyIdx;
                        gpuScene.custom_data_1 = vertexOffset;
                        gpuScene.custom_data_2 = vertexCount;

                        VkPipelineLayout layout = skinningPipeline_->PipelineLayout().Handle();
                        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, sizeof(Assets::GPUScene), &gpuScene);

                        uint32_t groupCount = (vertexCount + 63) / 64;
                        vkCmdDispatch(commandBuffer, groupCount, 1, 1);
                    }
                }

                // Memory Barrier for SkinnedVertices (Compute Write -> Shader/RT Read)
                VkBufferMemoryBarrier skinnedBufferBarrier = {};
                skinnedBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                skinnedBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                skinnedBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                skinnedBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                skinnedBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                skinnedBufferBarrier.buffer = skinnedVertexBuffer_->Handle();
                skinnedBufferBarrier.offset = 0;
                skinnedBufferBarrier.size = VK_WHOLE_SIZE;

                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1, &skinnedBufferBarrier, 0, nullptr);
            }
        }

        {
            SCOPED_GPU_TIMER("gpu cull");

            VkBufferMemoryBarrier nodeMatrixBarrier = {};
            nodeMatrixBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            nodeMatrixBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; // 假设由CPU更新
            nodeMatrixBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // 计算着色器将读取
            nodeMatrixBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            nodeMatrixBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            nodeMatrixBarrier.buffer = GetScene().NodeMatrixBuffer().Handle();
            nodeMatrixBarrier.offset = 0;
            nodeMatrixBarrier.size = VK_WHOLE_SIZE;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT, // 源阶段：CPU写入
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // 目标阶段：计算着色器
                0,
                0, nullptr,
                1, &nodeMatrixBarrier,
                0, nullptr
            );

            gpuCullPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);

            uint32_t groupCount = GetScene().GetIndirectDrawBatchCount() / 64 + 1;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            VkBufferMemoryBarrier bufferBarrier = {};
            bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.buffer = GetScene().IndirectDrawBuffer().Handle();
            bufferBarrier.offset = 0;
            bufferBarrier.size = VK_WHOLE_SIZE;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0,
                0, nullptr,
                1, &bufferBarrier,
                0, nullptr
            );
        }

        {
            SCOPED_GPU_TIMER("clear pass");
            
            bufferClearPipeline_->BindPipeline(commandBuffer, &imageIndex);
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
                
            vkCmdClearColorImage(commandBuffer, SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &imageRange);
                
            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex],
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("visibility pass");

            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = visibilityPipeline_->RenderPass().Handle();
            renderPassInfo.framebuffer = visibilityFrameBuffer_->Handle();
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();


            // make it to generate gbuffer
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                const auto& scene = GetScene();
                const VkBuffer indexBuffer = scene.IndexBuffer().Handle();

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline_->Handle());
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(commandBuffer, visibilityPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(Assets::GPUScene), &(scene.FetchGPUScene(imageIndex)));
                
                vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0,
                                         scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
            }
            vkCmdEndRenderPass(commandBuffer);

            // copy draw to storage buffer
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().width, GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().height, 1};

            GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            
            vkCmdCopyImage(commandBuffer, GetStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
            
            GetStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
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
            PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::DrawFrame", PERFORMANCEAPI_MAKE_COLOR(200, 255, 200));
            SCOPED_CPU_TIMER("draw-frame");
            const auto noTimeout = std::numeric_limits<uint64_t>::max();

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Prepare", PERFORMANCEAPI_MAKE_COLOR(255, 255, 200));
                BeforeNextFrame();
            }

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::QueryWait", PERFORMANCEAPI_MAKE_COLOR(255, 255, 200));
                SCOPED_CPU_TIMER("hwquery");
                gpuTimer_->FrameEnd((*commandBuffers_)[currentImageIndex_]);
            }

            // next frame synchronization objects
            const auto imageAvailableSemaphore = imageAvailableSemaphores_[currentFrame_].Handle();
            const auto renderFinishedSemaphore = renderFinishedSemaphores_[currentFrame_].Handle();

            auto result = vkAcquireNextImageKHR(device_->Handle(), swapChain_->Handle(), noTimeout,
                                                imageAvailableSemaphore, nullptr, &currentImageIndex_);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                RecreateSwapChain();
                return;
            }

            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
            }

            const auto commandBuffer = commandBuffers_->Begin(currentFrame_);
            gpuTimer_->Reset(commandBuffer);

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Render", PERFORMANCEAPI_MAKE_COLOR(200, 200, 255));
                SCOPED_GPU_TIMER("[gpu time]");

                {
                    SCOPED_GPU_TIMER("[pre-render]");
                    PreRender(commandBuffer, currentImageIndex_);
                    skinModelUpdateRequests_.clear();
                }

                {
                    SCOPED_GPU_TIMER("[render]");
                    Render(commandBuffer, currentImageIndex_);
                }

                {
                    SCOPED_GPU_TIMER("[post-render]");
                    PostRender(commandBuffer, currentImageIndex_);
                }

                if (DelegatePostRender)
                {
                    SCOPED_GPU_TIMER("imgui");
                    DelegatePostRender(commandBuffer, currentImageIndex_);
                }
            }
            commandBuffers_->End(currentFrame_);

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::UpdateNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                UpdateUniformBuffer(currentImageIndex_);
            }

            // wait the last frame command buffer to complete
            if (currentFence)
            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Fence", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                SCOPED_CPU_TIMER("fence");
                currentFence->Wait(noTimeout);
            }

            if (GetScene().UpdateNodes())
            {
                AfterUpdateScene();
            }

            AfterRenderCmd();
            currentFence = &(inFlightFences_[currentFrame_]);

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkCommandBuffer commandBuffers[]{commandBuffer};
            VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
            {
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = waitSemaphores;
                submitInfo.pWaitDstStageMask = waitStages;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = commandBuffers;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = signalSemaphores;

                currentFence->Reset();

                Check(vkQueueSubmit(device_->GraphicsQueue(), 1, &submitInfo, currentFence->Handle()),
                      "submit draw command buffer");
            }

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Present", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                SCOPED_CPU_TIMER("present");
                VkSwapchainKHR swapChains[] = {swapChain_->Handle()};
                VkPresentInfoKHR presentInfo = {};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = signalSemaphores;
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &currentImageIndex_;
                presentInfo.pResults = nullptr; // Optional

                result = vkQueuePresentKHR(device_->PresentQueue(), &presentInfo);

                AfterPresent();

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

            currentFrame_ = (currentFrame_ + 1) % inFlightFences_.size();
            frameCount_++;
        }
        gpuTimer_->CpuFrameEnd();
    }

    void VulkanBaseRenderer::BeforeNextFrame()
    {
#if WITH_OIDN
        ExecuteOIDN();
#endif

        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->BeforeNextFrame();
        }

        if (DelegateBeforeNextTick)
        {
            DelegateBeforeNextTick();
        }
    }

    void VulkanBaseRenderer::InitializeBarriers(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("barriers");
        for ( auto& storageImage : bindlessStorageImages_ )
        {
            if ( storageImage ) storageImage->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    void VulkanBaseRenderer::UpdateStreamline(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
#if WITH_STREAMLINE
        if (!supportDLSS_) return;
        
        StreamlineWrapper::LazyInit(device_->Handle(), instance_->Handle(), device_->PhysicalDevice(), 0, device_->ComputeFamilyIndex(), 0, device_->GraphicsFamilyIndex(), supportDLSS_, supportDLSSRR_);

        auto& settings = NextEngine::GetInstance()->GetUserSettings();
        
        sl::ViewportHandle viewport(0);
        sl::FrameToken* frameToken;
        uint32_t uintFrameCount = (uint32_t)frameCount_;
        if (SL_FAILED(res0, slGetNewFrameToken(frameToken, &uintFrameCount)))
        {
            SPDLOG_ERROR("slGetNewFrameToken failed: {}", (int)res0);
            return;
        }

        bool useDLSSRR = SupportDLSSRR() && settings.DLSSRR;
        
        // 1. DLSS Options
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
        constants.cameraViewToClip = toSlMatrix(lastUBO.ProjectionUnJit);
        constants.clipToCameraView = toSlMatrix(lastUBO.ProjectionInverseUnJit);
        constants.clipToPrevClip = toSlMatrix(lastUBO.PrevViewProjectionUnJit * lastUBO.ModelViewInverse * lastUBO.ProjectionInverseUnJit); 
        constants.prevClipToClip = toSlMatrix(lastUBO.ProjectionUnJit * lastUBO.ModelView * lastUBO.PrevViewProjectionUnJit);
        
        constants.jitterOffset = sl::float2(lastUBO.Jitter.x, lastUBO.Jitter.y);
        constants.mvecScale = {1.0f / (float)SwapChain().RenderExtent().width,1.0f / (float)SwapChain().RenderExtent().height}; 
        
        constants.cameraPos = sl::float3(lastUBO.ModelViewInverse[3][0], lastUBO.ModelViewInverse[3][1], lastUBO.ModelViewInverse[3][2]);
        constants.cameraFwd = sl::float3(-lastUBO.ModelViewInverse[2][0], -lastUBO.ModelViewInverse[2][1], -lastUBO.ModelViewInverse[2][2]);
        constants.cameraUp = sl::float3(lastUBO.ModelViewInverse[1][0], lastUBO.ModelViewInverse[1][1], lastUBO.ModelViewInverse[1][2]);
        constants.cameraRight = sl::float3(lastUBO.ModelViewInverse[0][0], lastUBO.ModelViewInverse[0][1], lastUBO.ModelViewInverse[0][2]);
        
        auto& camera = GetScene().GetRenderCamera();
        constants.cameraNear = camera.NearPlane;
        constants.cameraFar = camera.FarPlane;
        constants.cameraFOV = glm::radians(camera.FieldOfView); 
        constants.cameraAspectRatio = (float)SwapChain().Extent().width / (float)SwapChain().Extent().height;
        
        constants.depthInverted = sl::Boolean::eFalse;
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.reset = frameCount_ < 2 ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        
        if (SL_FAILED(res2, slSetConstants(constants, *frameToken, viewport)))
        {
            SPDLOG_ERROR("slSetConstants failed: {}", (int)res2);
        }

        // 3. Tags
        // Depth
        auto slDepth = toSlResource(depthBuffer_->GetImage(), depthBuffer_->GetImageMemory().Handle(), depthBuffer_->ImageView().Handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        slDepth.width = SwapChain().RenderExtent().width;
        slDepth.height = SwapChain().RenderExtent().height;
        sl::ResourceTag tagDepth(&slDepth, sl::kBufferTypeDepth, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagDepth, 1, commandBuffer);

        // Motion Vectors
        auto& resMV = bindlessStorageImages_[Assets::Bindless::RT_MOTIONVECTOR];
        auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagMV(&slMV, sl::kBufferTypeMotionVectors, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

        // Scaling Input (Color)
        auto& resInput = bindlessStorageImages_[Assets::Bindless::RT_DENOISED];
        auto slInput = toSlResource(resInput->GetImage(), resInput->GetImageMemory().Handle(), resInput->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagInput(&slInput, sl::kBufferTypeScalingInputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagInput, 1, commandBuffer);

        // Scaling Output
        sl::Resource slOutput(sl::ResourceType::eTex2d, (void*)swapChain_->Images()[imageIndex], nullptr, (void*)swapChain_->ImageViews()[imageIndex]->Handle(), (uint32_t)VK_IMAGE_LAYOUT_GENERAL);
        slOutput.width = SwapChain().Extent().width;
        slOutput.height = SwapChain().Extent().height;
        slOutput.nativeFormat = (uint32_t)swapChain_->Format();
        sl::ResourceTag tagOutput(&slOutput, sl::kBufferTypeScalingOutputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagOutput, 1, commandBuffer);
        
        if (useDLSSRR)
        {
            // Albedo
            auto& resAlbedo = bindlessStorageImages_[Assets::Bindless::RT_ALBEDO];
            auto slAlbedo = toSlResource(resAlbedo->GetImage(), resAlbedo->GetImageMemory().Handle(), resAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagAlbedo(&slAlbedo, sl::kBufferTypeAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagAlbedo, 1, commandBuffer);

            // Specular Albedo
            auto& resSpecAlbedo = bindlessStorageImages_[Assets::Bindless::RT_SPECULAR_ALBEDO];
            auto slSpecAlbedo = toSlResource(resSpecAlbedo->GetImage(), resSpecAlbedo->GetImageMemory().Handle(), resSpecAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagSpecAlbedo(&slSpecAlbedo, sl::kBufferTypeSpecularAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagSpecAlbedo, 1, commandBuffer);

            // Normals
            auto& resNormal = bindlessStorageImages_[Assets::Bindless::RT_NORMAL];
            auto slNormal = toSlResource(resNormal->GetImage(), resNormal->GetImageMemory().Handle(), resNormal->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagNormal(&slNormal, sl::kBufferTypeNormalRoughness, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagNormal, 1, commandBuffer);
            
            // auto& resMV = bindlessStorageImages_[Assets::Bindless::RT_MOTIONVECTOR];
            // auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagMV(&slMV, sl::kBufferTypeSpecularMotionVectors, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

            // Diffuse Noisy
            // auto& resDiffNoisy = bindlessStorageImages_[Assets::Bindless::RT_ACCUMLATE_DIFFUSE];
            // auto slDiffNoisy = toSlResource(resDiffNoisy->GetImage(), resDiffNoisy->GetImageMemory().Handle(), resDiffNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagDiffNoisy(&slDiffNoisy, sl::kBufferTypeDiffuseHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagDiffNoisy, 1, commandBuffer);
            //
            // // Specular Noisy
            // auto& resSpecNoisy = bindlessStorageImages_[Assets::Bindless::RT_ACCUMLATE_SPECULAR];
            // auto slSpecNoisy = toSlResource(resSpecNoisy->GetImage(), resSpecNoisy->GetImageMemory().Handle(), resSpecNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagSpecNoisy(&slSpecNoisy, sl::kBufferTypeSpecularHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagSpecNoisy, 1, commandBuffer);

            // Diffuse Hit Dist
            auto& resDiffHitDist = bindlessStorageImages_[Assets::Bindless::RT_DIFFUSE_HITDIST];
            auto slDiffHitDist = toSlResource(resDiffHitDist->GetImage(), resDiffHitDist->GetImageMemory().Handle(), resDiffHitDist->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagDiffHitDist(&slDiffHitDist, sl::kBufferTypeDiffuseHitDistance, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagDiffHitDist, 1, commandBuffer);
            //
            // // Specular Hit Dist
            // auto& resSpecHitDist = bindlessStorageImages_[Assets::Bindless::RT_SPECULAR_HITDIST];
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
            logicRenderers_[type] = std::make_unique<RayTracing::PathTracingRenderer>(*this);
            break;
        case ERendererType::ERT_ModernDeferred:
            logicRenderers_[type] = std::make_unique<ModernDeferred::SoftwareTracingRenderer>(*this);
            break;
        case ERendererType::ERT_LegacyDeferred:
            logicRenderers_[type] = std::make_unique<LegacyDeferred::SoftwareModernRenderer>(*this);
            break;
        case ERendererType::ERT_VoxelTracing:
            logicRenderers_[type] = std::make_unique<VoxelTracing::VoxelTracingRenderer>(*this);
            break;
        default:
            assert(false);
        }
        currentLogicRenderer_ = type;
    }

    void VulkanBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        currentLogicRenderer_ = type;
    }

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (GOption->ReferenceMode)
        {
            // 后面渲染器会很多，这里只渲染加入reference的，并从rtDenoised Resolve到FrameBuffer
            // 然后就跳过后面的resolve流程了
            for (auto& logicRenderer : logicRenderers_)
            {
                std::string rendererName = "";
                std::string folderName = "";
                switch (logicRenderer.first)
                {
                case ERendererType::ERT_PathTracing:
                    rendererName = "PathTracing";
                    folderName = "PT-";
                    break;
                case ERendererType::ERT_ModernDeferred:
                    rendererName = "SoftTracing";
                    folderName = "ST-";
                    break;
                case ERendererType::ERT_LegacyDeferred:
                    rendererName = "SoftModern";
                    folderName = "SM-";
                    break;
                case ERendererType::ERT_VoxelTracing:
                    rendererName = "VoxelTracing";
                    folderName = "VT-";
                    break;
                default:
                    rendererName = "UnknownRenderer";
                    folderName = "UK-";
                    break;
                }

                {
                    SCOPED_GPU_TIMER_FOLDER(rendererName.c_str(), folderName.c_str());
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
                
                    simpleComposePipeline_->BindPipeline(commandBuffer, pushConst.data());
                  
                    vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8,
                                  SwapChain().RenderExtent().height / 8, 1);
                    SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
                }
            }
        }
        else
        {
            if (logicRenderers_.find(currentLogicRenderer_) != logicRenderers_.end())
            {
                SCOPED_GPU_TIMER("logic renderer");
                logicRenderers_[currentLogicRenderer_]->Render(commandBuffer, imageIndex);
            }

            	if (NextEngine::GetInstance()->GetShowFlags().ShowWireframe)            {
                SCOPED_GPU_TIMER("wireframe");
                
                VkRenderPassBeginInfo renderPassInfo = {};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = wireframePipeline_->RenderPass().Handle();
                renderPassInfo.framebuffer = wireframeFramebuffer_->Handle();
                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = swapChain_->RenderExtent();
                
                vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                {
                    auto& scene = GetScene();
                
                    VkDescriptorSet descriptorSets[] = {wireframePipeline_->DescriptorSet(imageIndex)};
                    VkBuffer vertexBuffers[] = {scene.SimpleVertexBuffer().Handle()};
                    const VkBuffer indexBuffer = scene.PrimAddressBuffer().Handle();
                    VkDeviceSize offsets[] = {0};
                
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframePipeline_->Handle());
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            wireframePipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    
                    // drawcall one by one, old school pipeline only
                    for (const auto& node : scene.GetNodeProxys())
                    {
                        auto& offset = scene.Offsets()[node.modelId];
                        const auto indexCount = static_cast<uint32_t>(offset.indexCount);
                        if (indexCount == 0) continue;
                
                        glm::mat4 worldMatrix = node.worldTS;
                        vkCmdPushConstants(commandBuffer, wireframePipeline_->PipelineLayout().Handle(),
                                           VK_SHADER_STAGE_VERTEX_BIT,0, sizeof(glm::mat4), &worldMatrix);
                        vkCmdDrawIndexed(commandBuffer, indexCount, 1, offset.indexOffset, static_cast<int>(offset.vertexOffset), 0);
                    }
                }
                vkCmdEndRenderPass(commandBuffer);
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
                simpleComposePipeline_->BindPipeline(commandBuffer, pushConst.data());

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
                SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
            }
        }
    }

    void VulkanBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        //if (NextEngine::GetInstance()->IsProgressiveRendering())  return;
        // soft ambient cube generation
        if (!supportRayTracing_ || GOption->ForceSoftGen)
        {
            const int cubesPerGroup = 64;
            const int count = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
            const int group = count / cubesPerGroup;

            int temporalFrames = 120;
            switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
            {
            case 0:
                temporalFrames = 30;
                break;
            case 1:
                temporalFrames = 120;
                break;
            case 2:
                temporalFrames = 300;
                break;
            default:
                temporalFrames = 120;
                break;
            }

            if (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel != 2)
            {
                SCOPED_GPU_TIMER("sw-lightbake");
                
                int frame = (int)(frameCount_ % temporalFrames);
                int groupPerFrame = group / temporalFrames;
                int offset = frame * groupPerFrame;
                int offsetInCubes = offset * cubesPerGroup;

                
                softAmbientCubeGenPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);

                Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
                gpuScene.custom_data_0 = offsetInCubes;
                    
                VkPipelineLayout layout = softAmbientCubeGenPipeline_->PipelineLayout().Handle();
                vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(Assets::GPUScene), &gpuScene);
                
                vkCmdDispatch(commandBuffer, groupPerFrame, 1, 1);
            }
        }

        if (VisualDebug())
        {
            SCOPED_GPU_TIMER("visual debugger");
            SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);

            std::array<uint32_t, 5> pushConst = { imageIndex, uint32_t(SwapChain().OutputOffset().x), uint32_t(SwapChain().OutputOffset().y), uint32_t(SwapChain().OutputExtent().width), uint32_t(SwapChain().OutputExtent().height) };
            visualDebuggerPipeline_->BindPipeline(commandBuffer, pushConst.data());
            
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);

            SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
        }

        {
            SCOPED_GPU_TIMER("objectid copy");
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                     VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            GetStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                     VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().width, GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().height, 1};

            vkCmdCopyImage(commandBuffer, GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           GetStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }
    }

    void VulkanBaseRenderer::CaptureOIDN(VkCommandBuffer commandBuffer)
    {
#if WITH_OIDN
        if (supportDenoiser_)
        {
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Extent().width, GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Extent().height, 1};

            rtAlbedo_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            vkCmdCopyImage(commandBuffer, GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtAlbedo_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
            
            rtNormal_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            GetStorageImage(Assets::Bindless::RT_NORMAL)->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            vkCmdCopyImage(commandBuffer, GetStorageImage(Assets::Bindless::RT_NORMAL)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtNormal_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
            
            
            rtDenoise0_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            vkCmdCopyImage(commandBuffer, GetStorageImage(Assets::Bindless::RT_DENOISED)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtDenoise0_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
            
            rtDenoise1_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdCopyImage(commandBuffer, rtDenoise1_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, GetStorageImage(Assets::Bindless::RT_DENOISED)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }
#endif
    }

    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        lastUBO = GetUniformBufferObject(swapChain_->RenderOffset(), swapChain_->OutputExtent());
        uniformBuffers_[imageIndex].SetValue(lastUBO);
    }

#if WITH_OIDN
    void VulkanBaseRenderer::InitOIDN()
    {
        // Query the UUID of the Vulkan physical device
        VkPhysicalDeviceIDProperties id_properties{};
        id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &id_properties;
        vkGetPhysicalDeviceProperties2(Device().PhysicalDevice(), &properties);

        oidn::UUID uuid;
        std::memcpy(uuid.bytes, id_properties.deviceUUID, sizeof(uuid.bytes));

        oidnDevice = oidn::newDevice(uuid); // CPU or GPU if available
        oidnDevice.commit();
    }

    void VulkanBaseRenderer::SetupOIDN(const VkExtent2D& extent)
    {
        const auto format = SwapChain().Format();
        const auto tiling = VK_IMAGE_TILING_OPTIMAL;
 
        rtDenoise0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, "denoise0"));
        rtDenoise1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, true, "denoise1"));
        rtAlbedo_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, "albedocopy"));
        rtNormal_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, "normalcopy"));
        
        size_t SrcImageSize = extent.width * extent.height * 4 * 2;
        size_t SrcImageW8 = 4 * 2 * extent.width;
        size_t SrcImage8 = 4 * 2;

#if __linux__ || __APPLE__
        oidn::BufferRef colorBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueFD, rtDenoise0_->GetExternalHandle(), SrcImageSize);
        oidn::BufferRef outBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueFD,rtDenoise1_->GetExternalHandle(), SrcImageSize);
        oidn::BufferRef albedoBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueFD, rtAlbedo_->GetExternalHandle(), SrcImageSize);
        oidn::BufferRef normalBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueFD, rtNormal_->GetExternalHandle(), SrcImageSize);
#else
        oidn::BufferRef colorBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtDenoise0_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef outBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtDenoise1_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef albedoBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtAlbedo_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef normalBuf = oidnDevice.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtNormal_->GetExternalHandle(), nullptr, SrcImageSize);
#endif
        
        oidnFilter = oidnDevice.newFilter("RT"); // generic ray tracing filter
        oidnFilter.setImage("color", colorBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // beauty
        oidnFilter.setImage("albedo", albedoBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // aux
        oidnFilter.setImage("normal", normalBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // aux
        oidnFilter.setImage("output", outBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // denoised beauty
        oidnFilter.set("hdr", true); // beauty image is HDR
        oidnFilter.set("quality", oidn::Quality::Balanced);
        oidnFilter.set("cleanAux", true);
        oidnFilter.commit();
    }

    void VulkanBaseRenderer::ExecuteOIDN()
    {
        SCOPED_CPU_TIMER("OIDN");
        if (supportDenoiser_)
        {
            oidnFilter.executeAsync();
            oidnDevice.sync();
        }
    }
#endif
}