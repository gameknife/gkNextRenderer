#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Modules/NextStreamline/StreamlineIntegration.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#if WITH_STREAMLINE && WIN32
#include <dxgi1_6.h>
#include <windows.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_helpers_vk.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#endif

namespace
{
    void AppendUnique(std::vector<const char*>& values, const char* value)
    {
        if (value == nullptr)
        {
            return;
        }

        const auto found = std::find_if(values.begin(), values.end(),
            [value](const char* current)
            {
                return current != nullptr && std::strcmp(current, value) == 0;
            });
        if (found == values.end())
        {
            values.push_back(value);
        }
    }

    bool StreamlineHasDeviceExtension(VkPhysicalDevice physicalDevice, const char* extensionName)
    {
        const auto extensions = Vulkan::GetEnumerateVector(
            physicalDevice,
            static_cast<const char*>(nullptr),
            vkEnumerateDeviceExtensionProperties);
        return std::any_of(extensions.begin(), extensions.end(),
            [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
    }

    bool HasInstanceExtension(const char* extensionName)
    {
        const auto extensions = Vulkan::GetEnumerateVector(
            static_cast<const char*>(nullptr),
            vkEnumerateInstanceExtensionProperties);
        return std::any_of(extensions.begin(), extensions.end(),
            [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
    }

#if WITH_STREAMLINE && WIN32
    bool HasNvidiaAdapter()
    {
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
    }

    std::wstring ToWidePath(const char* path)
    {
        if (path == nullptr || path[0] == '\0')
        {
            return L"sl.interposer.dll";
        }
        return std::filesystem::path(path).wstring();
    }

    sl::DLSSMode ToDLSSMode(uint32_t rawMode)
    {
        using Rendering::Upscaler::EUpscaleMode;
        switch (Rendering::Upscaler::GetUpscaleModeInfo(rawMode).mode)
        {
        case EUpscaleMode::Quality:
            return sl::DLSSMode::eMaxQuality;
        case EUpscaleMode::Balanced:
            return sl::DLSSMode::eBalanced;
        case EUpscaleMode::Performance:
            return sl::DLSSMode::eMaxPerformance;
        case EUpscaleMode::UltraPerformance:
            return sl::DLSSMode::eUltraPerformance;
        case EUpscaleMode::Native:
            return sl::DLSSMode::eDLAA;
        case EUpscaleMode::Auto:
            return sl::DLSSMode::eMaxQuality;
        }
        return sl::DLSSMode::eMaxQuality;
    }

    sl::float4x4 ToSlMatrix(const glm::mat4& matrix)
    {
        sl::float4x4 result;
        result.row[0] = sl::float4(matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]);
        result.row[1] = sl::float4(matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]);
        result.row[2] = sl::float4(matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]);
        result.row[3] = sl::float4(matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]);
        return result;
    }

    sl::Resource ToSlResource(const Rendering::Upscaler::FImageResource& image)
    {
        sl::Resource resource(
            sl::ResourceType::eTex2d,
            reinterpret_cast<void*>(image.image),
            reinterpret_cast<void*>(image.memory),
            reinterpret_cast<void*>(image.view),
            static_cast<uint32_t>(image.layout));
        resource.width = image.extent.width;
        resource.height = image.extent.height;
        resource.nativeFormat = static_cast<uint32_t>(image.format);
        resource.mipLevels = 1;
        resource.arrayLayers = 1;
        resource.usage = image.usage;
        return resource;
    }

    sl::Extent ToSlExtent(VkOffset2D offset, VkExtent2D extent)
    {
        return sl::Extent{
            static_cast<uint32_t>(std::max(offset.y, 0)),
            static_cast<uint32_t>(std::max(offset.x, 0)),
            extent.width,
            extent.height};
    }

    sl::PCLMarker ToPCLMarker(Rendering::Upscaler::EFrameMarker marker)
    {
        using Rendering::Upscaler::EFrameMarker;
        switch (marker)
        {
        case EFrameMarker::SimulationStart:
            return sl::PCLMarker::eSimulationStart;
        case EFrameMarker::SimulationEnd:
            return sl::PCLMarker::eSimulationEnd;
        case EFrameMarker::RenderSubmitStart:
            return sl::PCLMarker::eRenderSubmitStart;
        case EFrameMarker::RenderSubmitEnd:
            return sl::PCLMarker::eRenderSubmitEnd;
        case EFrameMarker::PresentStart:
            return sl::PCLMarker::ePresentStart;
        case EFrameMarker::PresentEnd:
            return sl::PCLMarker::ePresentEnd;
        }
        return sl::PCLMarker::eSimulationStart;
    }

    bool IsHDRFormatSupportedForDLSSG(VkFormat format, bool hdrOutput)
    {
        if (!hdrOutput)
        {
            return true;
        }

        return format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
               format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    }

    void OnDLSSGApiError(const sl::APIError& error)
    {
        SPDLOG_ERROR("DLSS-G Vulkan API error: {}", static_cast<int32_t>(error.vkRes));
    }

    void OnStreamlineLog(sl::LogType type, const char* message)
    {
        if (message == nullptr)
        {
            return;
        }

        const std::string_view text(message);
        const bool expectedProxyInitializationMessage =
            text.find("API hook is activated without device being created") != std::string_view::npos;
        const bool disabledCommandListTrackingMessage =
            text.find("Hook sl.common:Vulkan:CmdBindPipeline is NOT supported") != std::string_view::npos ||
            text.find("Hook sl.common:Vulkan:CmdBindDescriptorSets is NOT supported") != std::string_view::npos ||
            text.find("Hook sl.common:Vulkan:BeginCommandBuffer is NOT supported") != std::string_view::npos;
        const bool optionalRuntimeMessage =
            text.find("Ignoring plugin '") != std::string_view::npos ||
            text.find("Vulkan setAsyncFrameMarker is not implemented") != std::string_view::npos ||
            text.find("RSync will not run because it was not initialized") != std::string_view::npos ||
            text.find("Invalid no warp resource extent") != std::string_view::npos;
        const bool knownSdkShutdownMessage =
            text.find("slOnPluginShutdown") != std::string_view::npos &&
            text.find("destroyKernel(ctx.mvecKernel) failed 6") != std::string_view::npos;
        if (expectedProxyInitializationMessage ||
            disabledCommandListTrackingMessage ||
            optionalRuntimeMessage ||
            knownSdkShutdownMessage)
        {
            SPDLOG_DEBUG("[Streamline] {}", message);
            return;
        }

        switch (type)
        {
        case sl::LogType::eError:
            SPDLOG_ERROR("[Streamline] {}", message);
            break;
        case sl::LogType::eWarn:
            SPDLOG_WARN("[Streamline] {}", message);
            break;
        case sl::LogType::eInfo:
            SPDLOG_DEBUG("[Streamline] {}", message);
            break;
        default:
            break;
        }
    }

    class FStreamlineVulkanHooks
    {
    public:
        bool EnsureLoaded()
        {
            if (loadAttempted_)
            {
                return loaded_;
            }
            loadAttempted_ = true;

            HMODULE module = GetModuleHandleW(L"sl.interposer.dll");
            if (module == nullptr)
            {
                const std::wstring path = ToWidePath(StreamlineWrapper::PreferredVulkanLoaderPath());
                module = LoadLibraryW(path.c_str());
            }

            if (module == nullptr)
            {
                SPDLOG_WARN("Streamline Vulkan hooks disabled: failed to load sl.interposer.dll");
                return false;
            }

            module_ = module;
            getDeviceProcAddr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                GetProcAddress(module_, "vkGetDeviceProcAddr"));
            createInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
                GetProcAddress(module_, "vkCreateInstance"));
            destroyInstance_ = reinterpret_cast<PFN_vkDestroyInstance>(
                GetProcAddress(module_, "vkDestroyInstance"));
            enumeratePhysicalDevices_ = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
                GetProcAddress(module_, "vkEnumeratePhysicalDevices"));
            createWin32Surface_ = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
                GetProcAddress(module_, "vkCreateWin32SurfaceKHR"));
            destroySurface_ = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
                GetProcAddress(module_, "vkDestroySurfaceKHR"));
            createDevice_ = reinterpret_cast<PFN_vkCreateDevice>(
                GetProcAddress(module_, "vkCreateDevice"));
            destroyDevice_ = reinterpret_cast<PFN_vkDestroyDevice>(
                GetProcAddress(module_, "vkDestroyDevice"));
            if (getDeviceProcAddr_ == nullptr)
            {
                SPDLOG_WARN("Streamline Vulkan hooks disabled: sl.interposer.dll has no vkGetDeviceProcAddr");
                return false;
            }
            if (createInstance_ == nullptr ||
                destroyInstance_ == nullptr ||
                enumeratePhysicalDevices_ == nullptr ||
                createWin32Surface_ == nullptr ||
                destroySurface_ == nullptr ||
                createDevice_ == nullptr ||
                destroyDevice_ == nullptr)
            {
                SPDLOG_WARN("Streamline Vulkan hooks disabled: sl.interposer.dll exports are incomplete");
                return false;
            }

            loaded_ = true;
            return true;
        }

        bool EnsureDeviceHooks(VkDevice device)
        {
            if (device == VK_NULL_HANDLE || !EnsureLoaded())
            {
                return false;
            }

            if (hookedDevice_ == device && deviceHooksReady_)
            {
                return true;
            }

            hookedDevice_ = device;
            createSwapchain_ = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
                getDeviceProcAddr_(device, "vkCreateSwapchainKHR"));
            destroySwapchain_ = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
                getDeviceProcAddr_(device, "vkDestroySwapchainKHR"));
            getSwapchainImages_ = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
                getDeviceProcAddr_(device, "vkGetSwapchainImagesKHR"));
            acquireNextImage_ = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
                getDeviceProcAddr_(device, "vkAcquireNextImageKHR"));
            queuePresent_ = reinterpret_cast<PFN_vkQueuePresentKHR>(
                getDeviceProcAddr_(device, "vkQueuePresentKHR"));
            deviceWaitIdle_ = reinterpret_cast<PFN_vkDeviceWaitIdle>(
                getDeviceProcAddr_(device, "vkDeviceWaitIdle"));

            deviceHooksReady_ = createSwapchain_ != nullptr &&
                                destroySwapchain_ != nullptr &&
                                getSwapchainImages_ != nullptr &&
                                acquireNextImage_ != nullptr &&
                                queuePresent_ != nullptr;
            if (!deviceHooksReady_)
            {
                SPDLOG_WARN("Streamline Vulkan hooks incomplete; falling back to native Vulkan present path");
            }
            else if (!deviceHooksLogged_)
            {
                SPDLOG_INFO("Streamline Vulkan swapchain/acquire/present hooks are active");
                deviceHooksLogged_ = true;
            }
            return deviceHooksReady_;
        }

        VkResult CreateInstance(
            const VkInstanceCreateInfo* createInfo,
            const VkAllocationCallbacks* allocator,
            VkInstance* instance)
        {
            if (EnsureLoaded())
            {
                return createInstance_(createInfo, allocator, instance);
            }
            return vkCreateInstance(createInfo, allocator, instance);
        }

        void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
        {
            if (EnsureLoaded())
            {
                destroyInstance_(instance, allocator);
                return;
            }
            vkDestroyInstance(instance, allocator);
        }

        VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* physicalDevices)
        {
            if (EnsureLoaded())
            {
                return enumeratePhysicalDevices_(instance, count, physicalDevices);
            }
            return vkEnumeratePhysicalDevices(instance, count, physicalDevices);
        }

        VkResult CreateWin32Surface(
            VkInstance instance,
            const VkWin32SurfaceCreateInfoKHR* createInfo,
            const VkAllocationCallbacks* allocator,
            VkSurfaceKHR* surface)
        {
            if (EnsureLoaded())
            {
                return createWin32Surface_(instance, createInfo, allocator, surface);
            }
            return vkCreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
        }

        void DestroySurface(
            VkInstance instance,
            VkSurfaceKHR surface,
            const VkAllocationCallbacks* allocator)
        {
            if (EnsureLoaded())
            {
                destroySurface_(instance, surface, allocator);
                return;
            }
            vkDestroySurfaceKHR(instance, surface, allocator);
        }

        VkResult CreateDevice(
            VkPhysicalDevice physicalDevice,
            const VkDeviceCreateInfo* createInfo,
            const VkAllocationCallbacks* allocator,
            VkDevice* device)
        {
            if (EnsureLoaded())
            {
                return createDevice_(physicalDevice, createInfo, allocator, device);
            }
            return vkCreateDevice(physicalDevice, createInfo, allocator, device);
        }

        void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
        {
            if (EnsureLoaded())
            {
                destroyDevice_(device, allocator);
                return;
            }
            vkDestroyDevice(device, allocator);
        }

        VkResult CreateSwapchain(
            VkDevice device,
            const VkSwapchainCreateInfoKHR* createInfo,
            const VkAllocationCallbacks* allocator,
            VkSwapchainKHR* swapchain)
        {
            if (EnsureDeviceHooks(device))
            {
                return createSwapchain_(device, createInfo, allocator, swapchain);
            }
            return vkCreateSwapchainKHR(device, createInfo, allocator, swapchain);
        }

        void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
        {
            if (EnsureDeviceHooks(device))
            {
                destroySwapchain_(device, swapchain, allocator);
                return;
            }
            vkDestroySwapchainKHR(device, swapchain, allocator);
        }

        VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images)
        {
            if (EnsureDeviceHooks(device))
            {
                return getSwapchainImages_(device, swapchain, count, images);
            }
            return vkGetSwapchainImagesKHR(device, swapchain, count, images);
        }

        VkResult AcquireNextImage(
            VkDevice device,
            VkSwapchainKHR swapchain,
            uint64_t timeout,
            VkSemaphore semaphore,
            VkFence fence,
            uint32_t* imageIndex)
        {
            if (EnsureDeviceHooks(device))
            {
                return acquireNextImage_(device, swapchain, timeout, semaphore, fence, imageIndex);
            }
            return vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, imageIndex);
        }

        VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* presentInfo)
        {
            if (queuePresent_ != nullptr)
            {
                return queuePresent_(queue, presentInfo);
            }
            return vkQueuePresentKHR(queue, presentInfo);
        }

        VkResult DeviceWaitIdle(VkDevice device)
        {
            if (EnsureDeviceHooks(device) && deviceWaitIdle_ != nullptr)
            {
                return deviceWaitIdle_(device);
            }
            return vkDeviceWaitIdle(device);
        }

    private:
        bool loadAttempted_ = false;
        bool loaded_ = false;
        bool deviceHooksReady_ = false;
        bool deviceHooksLogged_ = false;
        HMODULE module_ = nullptr;
        VkDevice hookedDevice_ = VK_NULL_HANDLE;
        PFN_vkGetDeviceProcAddr getDeviceProcAddr_ = nullptr;
        PFN_vkCreateInstance createInstance_ = nullptr;
        PFN_vkDestroyInstance destroyInstance_ = nullptr;
        PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices_ = nullptr;
        PFN_vkCreateWin32SurfaceKHR createWin32Surface_ = nullptr;
        PFN_vkDestroySurfaceKHR destroySurface_ = nullptr;
        PFN_vkCreateDevice createDevice_ = nullptr;
        PFN_vkDestroyDevice destroyDevice_ = nullptr;
        PFN_vkCreateSwapchainKHR createSwapchain_ = nullptr;
        PFN_vkDestroySwapchainKHR destroySwapchain_ = nullptr;
        PFN_vkGetSwapchainImagesKHR getSwapchainImages_ = nullptr;
        PFN_vkAcquireNextImageKHR acquireNextImage_ = nullptr;
        PFN_vkQueuePresentKHR queuePresent_ = nullptr;
        PFN_vkDeviceWaitIdle deviceWaitIdle_ = nullptr;
    };

    class FStreamlineContext
    {
    public:
        bool ShouldInitialize() const
        {
            return HasNvidiaAdapter();
        }

        void Initialize()
        {
            std::lock_guard lock(mutex_);
            if (initAttempted_)
            {
                return;
            }
            initAttempted_ = true;

            if (!ShouldInitialize())
            {
                SPDLOG_INFO("Streamline disabled because no NVIDIA adapter is present");
                return;
            }

            sl::Preferences pref{};
            pref.pathsToPlugins = {};
            pref.numPathsToPlugins = 0;
            pref.pathToLogsAndData = {};
            pref.logLevel = sl::LogLevel::eDefault;
            pref.logMessageCallback = OnStreamlineLog;
            pref.applicationId = 12345678;
            pref.engine = sl::EngineType::eCustom;
            pref.engineVersion = "1.0.0";
            pref.projectId = "36cf6361-1044-4603-9ef3-066606660666";
            pref.renderAPI = sl::RenderAPI::eVulkan;
            pref.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging;

            static constexpr sl::Feature kFeatures[] = {
                sl::kFeatureDLSS,
                sl::kFeatureDLSS_RR,
                sl::kFeatureDLSS_G,
                sl::kFeatureReflex,
                sl::kFeaturePCL,
            };
            pref.featuresToLoad = kFeatures;
            pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(kFeatures));

            sl::Result result = slInit(pref);
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slInit failed: {}", static_cast<int>(result));
                return;
            }

            caps_.streamlineInitialized = true;
            initialized_ = true;
            hooks_.EnsureLoaded();
            RefreshRequirements();
        }

        void Shutdown()
        {
            std::lock_guard lock(mutex_);
            if (!initialized_)
            {
                return;
            }

            const sl::Result result = slShutdown();
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slShutdown failed: {}", static_cast<int>(result));
            }

            initialized_ = false;
            deviceReady_ = false;
            caps_ = {};
            requirementsLoaded_ = false;
            proxyInstance_ = VK_NULL_HANDLE;
            proxyDevice_ = VK_NULL_HANDLE;
            instanceExtensions_.clear();
            deviceExtensions_.clear();
        }

        bool IsInitialized() const
        {
            return initialized_;
        }

        const char* PreferredVulkanLoaderPath() const
        {
            if (!initialized_)
            {
                return nullptr;
            }

            static std::string path;
            if (path.empty())
            {
                if (std::filesystem::exists("sl.interposer.dll"))
                {
                    path = "sl.interposer.dll";
                }
#if defined(GK_STREAMLINE_INTERPOSER_DLL)
                else if (std::filesystem::exists(GK_STREAMLINE_INTERPOSER_DLL))
                {
                    path = GK_STREAMLINE_INTERPOSER_DLL;
                }
#endif
                else
                {
                    path = "sl.interposer.dll";
                }
            }
            return path.c_str();
        }

        VkResult CreateInstance(
            const VkInstanceCreateInfo* createInfo,
            const VkAllocationCallbacks* allocator,
            VkInstance* instance)
        {
            if (!initialized_)
            {
                return vkCreateInstance(createInfo, allocator, instance);
            }

            const VkResult result = hooks_.CreateInstance(createInfo, allocator, instance);
            if (result == VK_SUCCESS)
            {
                proxyInstance_ = *instance;
            }
            return result;
        }

        void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
        {
            if (initialized_ && proxyInstance_ == instance)
            {
                hooks_.DestroyInstance(instance, allocator);
                proxyInstance_ = VK_NULL_HANDLE;
                return;
            }
            vkDestroyInstance(instance, allocator);
        }

        VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* physicalDevices)
        {
            if (initialized_ && proxyInstance_ == instance)
            {
                return hooks_.EnumeratePhysicalDevices(instance, count, physicalDevices);
            }
            return vkEnumeratePhysicalDevices(instance, count, physicalDevices);
        }

        VkResult CreateWin32Surface(
            VkInstance instance,
            const VkWin32SurfaceCreateInfoKHR* createInfo,
            const VkAllocationCallbacks* allocator,
            VkSurfaceKHR* surface)
        {
            if (initialized_ && proxyInstance_ == instance)
            {
                return hooks_.CreateWin32Surface(instance, createInfo, allocator, surface);
            }
            return vkCreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
        }

        void DestroySurface(
            VkInstance instance,
            VkSurfaceKHR surface,
            const VkAllocationCallbacks* allocator)
        {
            if (initialized_ && proxyInstance_ == instance)
            {
                hooks_.DestroySurface(instance, surface, allocator);
                return;
            }
            vkDestroySurfaceKHR(instance, surface, allocator);
        }

        VkResult CreateDevice(
            VkPhysicalDevice physicalDevice,
            const VkDeviceCreateInfo* createInfo,
            const VkAllocationCallbacks* allocator,
            VkDevice* device)
        {
            std::lock_guard lock(mutex_);
            if (!initialized_ || proxyInstance_ == VK_NULL_HANDLE)
            {
                return vkCreateDevice(physicalDevice, createInfo, allocator, device);
            }

            const VkResult result = hooks_.CreateDevice(physicalDevice, createInfo, allocator, device);
            if (result == VK_SUCCESS)
            {
                proxyDevice_ = *device;
                deviceReady_ = true;
                caps_.streamlineDeviceReady = true;
            }
            return result;
        }

        void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
        {
            std::lock_guard lock(mutex_);
            if (initialized_ && proxyDevice_ == device)
            {
                hooks_.DestroyDevice(device, allocator);
                proxyDevice_ = VK_NULL_HANDLE;
                deviceReady_ = false;
                caps_.streamlineDeviceReady = false;
                return;
            }
            vkDestroyDevice(device, allocator);
        }

        void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions)
        {
            std::lock_guard lock(mutex_);
            if (!initialized_)
            {
                return;
            }

            RefreshRequirements();
            for (const char* extension : instanceExtensions_)
            {
                if (HasInstanceExtension(extension))
                {
                    AppendUnique(extensions, extension);
                }
                else
                {
                    SPDLOG_WARN("Streamline requested unavailable Vulkan instance extension {}", extension);
                }
            }
        }

        Rendering::Upscaler::FFeatureCaps AppendRequiredDeviceExtensions(
            VkPhysicalDevice physicalDevice,
            std::vector<const char*>& extensions)
        {
            std::lock_guard lock(mutex_);
            if (!initialized_)
            {
                return caps_;
            }

            RefreshRequirements();
            bool allAvailable = true;
            for (const char* extension : deviceExtensions_)
            {
                if (StreamlineHasDeviceExtension(physicalDevice, extension))
                {
                    AppendUnique(extensions, extension);
                }
                else
                {
                    allAvailable = false;
                    SPDLOG_WARN("Streamline requested unavailable Vulkan device extension {}", extension);
                }
            }

            caps_.requestedDeviceExtensionsAvailable = allAvailable;
            return caps_;
        }

        void SetVulkanInfo(const Rendering::Upscaler::FDeviceInfo& info)
        {
            std::lock_guard lock(mutex_);
            if (!initialized_)
            {
                return;
            }

            if (deviceReady_ && proxyDevice_ == info.device)
            {
                CheckFeatureSupport(info.physicalDevice);
                SPDLOG_INFO("Streamline Vulkan proxy device ready. DLSS={}, RR={}, DLSS-G={}, Reflex={}, PCL={}",
                            Rendering::Upscaler::SupportsUpscalerType(
                                caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSS),
                            Rendering::Upscaler::SupportsUpscalerType(
                                caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction),
                            Rendering::Upscaler::SupportsUpscalerType(
                                caps_.frameGenerationTypes, Rendering::Upscaler::EUpscalerType::DLSS),
                            caps_.supportReflex,
                            caps_.supportPCL);
                return;
            }

            if (deviceReady_)
            {
                return;
            }

            sl::VulkanInfo vulkanInfo{};
            vulkanInfo.device = info.device;
            vulkanInfo.instance = info.instance;
            vulkanInfo.physicalDevice = info.physicalDevice;
            vulkanInfo.computeQueueIndex = info.computeQueueIndex;
            vulkanInfo.computeQueueFamily = info.computeQueueFamily;
            vulkanInfo.graphicsQueueIndex = info.graphicsQueueIndex;
            vulkanInfo.graphicsQueueFamily = info.graphicsQueueFamily;
            vulkanInfo.opticalFlowQueueIndex = info.opticalFlowQueueIndex;
            vulkanInfo.opticalFlowQueueFamily = info.opticalFlowQueueFamily == UINT32_MAX ? 0 : info.opticalFlowQueueFamily;
            vulkanInfo.useNativeOpticalFlowMode = info.useNativeOpticalFlowMode;

            const sl::Result result = slSetVulkanInfo(vulkanInfo);
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slSetVulkanInfo failed: {}", static_cast<int>(result));
                caps_.streamlineDeviceReady = false;
                return;
            }

            deviceReady_ = true;
            caps_.streamlineDeviceReady = true;
            CheckFeatureSupport(info.physicalDevice);
            SPDLOG_INFO("Streamline device ready. DLSS={}, RR={}, DLSS-G={}, Reflex={}, PCL={}",
                        Rendering::Upscaler::SupportsUpscalerType(
                            caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSS),
                        Rendering::Upscaler::SupportsUpscalerType(
                            caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction),
                        Rendering::Upscaler::SupportsUpscalerType(
                            caps_.frameGenerationTypes, Rendering::Upscaler::EUpscalerType::DLSS),
                        caps_.supportReflex,
                        caps_.supportPCL);
        }

        Rendering::Upscaler::FFeatureCaps Caps() const
        {
            std::lock_guard lock(mutex_);
            return caps_;
        }

        Rendering::Upscaler::FFrameToken GetFrameToken(uint32_t frameIndex)
        {
            std::lock_guard lock(mutex_);
            Rendering::Upscaler::FFrameToken result{};
            result.frameIndex = frameIndex;
            if (!initialized_ || !deviceReady_)
            {
                return result;
            }

            sl::FrameToken* frameToken = nullptr;
            const sl::Result slResult = slGetNewFrameToken(frameToken, &frameIndex);
            if (slResult != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slGetNewFrameToken failed: {}", static_cast<int>(slResult));
                return result;
            }

            result.native = frameToken;
            return result;
        }

        sl::FrameToken* NativeToken(const Rendering::Upscaler::FFrameToken& token) const
        {
            return static_cast<sl::FrameToken*>(token.native);
        }

        FStreamlineVulkanHooks& Hooks()
        {
            return hooks_;
        }

    private:
        void RefreshRequirements()
        {
            if (!initialized_ || requirementsLoaded_)
            {
                return;
            }
            requirementsLoaded_ = true;

            static constexpr sl::Feature kFeatures[] = {
                sl::kFeatureDLSS,
                sl::kFeatureDLSS_RR,
                sl::kFeatureDLSS_G,
                sl::kFeatureReflex,
                sl::kFeaturePCL,
            };

            for (sl::Feature feature : kFeatures)
            {
                sl::FeatureRequirements requirements{};
                const sl::Result result = slGetFeatureRequirements(feature, requirements);
                if (result != sl::Result::eOk)
                {
                    SPDLOG_WARN("Streamline slGetFeatureRequirements({}) failed: {}",
                                feature,
                                static_cast<int>(result));
                    continue;
                }

                caps_.requiredGraphicsQueues = std::max(
                    caps_.requiredGraphicsQueues, requirements.vkNumGraphicsQueuesRequired);
                caps_.requiredComputeQueues = std::max(
                    caps_.requiredComputeQueues, requirements.vkNumComputeQueuesRequired);
                caps_.requiredOpticalFlowQueues = std::max(
                    caps_.requiredOpticalFlowQueues, requirements.vkNumOpticalFlowQueuesRequired);

                for (uint32_t i = 0; i < requirements.vkNumInstanceExtensions; ++i)
                {
                    AppendUnique(instanceExtensions_, requirements.vkInstanceExtensions[i]);
                }
                for (uint32_t i = 0; i < requirements.vkNumDeviceExtensions; ++i)
                {
                    AppendUnique(deviceExtensions_, requirements.vkDeviceExtensions[i]);
                }
            }

            if (caps_.requiredOpticalFlowQueues > 0)
            {
                SPDLOG_INFO("Streamline requested {} native optical-flow queue(s); using Streamline interop optical-flow mode",
                            caps_.requiredOpticalFlowQueues);
            }
        }

        void CheckFeatureSupport(VkPhysicalDevice physicalDevice)
        {
            sl::AdapterInfo adapterInfo{};
            adapterInfo.vkPhysicalDevice = physicalDevice;

            Rendering::Upscaler::SetUpscalerTypeSupport(
                caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSS,
                slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk);
            Rendering::Upscaler::SetUpscalerTypeSupport(
                caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction,
                slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo) == sl::Result::eOk);
            caps_.supportReflex = slIsFeatureSupported(sl::kFeatureReflex, adapterInfo) == sl::Result::eOk;
            const bool supportFrameGeneration = caps_.supportReflex &&
                slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;
            Rendering::Upscaler::SetUpscalerTypeSupport(
                caps_.frameGenerationTypes, Rendering::Upscaler::EUpscalerType::DLSS,
                supportFrameGeneration && Rendering::Upscaler::SupportsUpscalerType(
                    caps_.supportedTypes, Rendering::Upscaler::EUpscalerType::DLSS));
            Rendering::Upscaler::SetUpscalerTypeSupport(
                caps_.frameGenerationTypes,
                Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction,
                supportFrameGeneration && Rendering::Upscaler::SupportsUpscalerType(
                    caps_.supportedTypes,
                    Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction));
            caps_.supportPCL = slIsFeatureSupported(sl::kFeaturePCL, adapterInfo) == sl::Result::eOk;
        }

        mutable std::mutex mutex_;
        bool initAttempted_ = false;
        bool initialized_ = false;
        bool deviceReady_ = false;
        bool requirementsLoaded_ = false;
        VkInstance proxyInstance_ = VK_NULL_HANDLE;
        VkDevice proxyDevice_ = VK_NULL_HANDLE;
        Rendering::Upscaler::FFeatureCaps caps_{};
        std::vector<const char*> instanceExtensions_;
        std::vector<const char*> deviceExtensions_;
        FStreamlineVulkanHooks hooks_;
    };

    FStreamlineContext& StreamlineContext()
    {
        static FStreamlineContext context;
        return context;
    }

    // Set during device creation by the augmenter; masks DLSS caps when the
    // required device extensions could not be enabled.
    bool GStreamlineDeviceExtsEnabled = false;

    class FStreamlineUpscaler final : public Rendering::Upscaler::IUpscaler
    {
    public:
        void OnDeviceCreated(
            const Rendering::Upscaler::FDeviceInfo& deviceInfo,
            Rendering::Upscaler::FFeatureCaps& caps) override
        {
            StreamlineContext().SetVulkanInfo(deviceInfo);
            caps = StreamlineContext().Caps();
            if (!GStreamlineDeviceExtsEnabled)
            {
                caps.supportedTypes = 0;
                caps.frameGenerationTypes = 0;
            }

            if (caps.supportPCL)
            {
                sl::PCLOptions pclOptions{};
                const sl::Result result = slPCLSetOptions(pclOptions);
                if (result != sl::Result::eOk)
                {
                    SPDLOG_WARN("Streamline slPCLSetOptions failed: {}", static_cast<int>(result));
                }
            }
        }

        void OnSwapChainDestroyed() override
        {
            sl::ViewportHandle viewport(0);
            if (lastDLSSGEnabled_)
            {
                sl::DLSSGOptions options{};
                options.mode = sl::DLSSGMode::eOff;
                const sl::Result optionsResult = slDLSSGSetOptions(viewport, options);
                if (optionsResult != sl::Result::eOk)
                {
                    SPDLOG_WARN("Streamline slDLSSGSetOptions(off) failed before swapchain destruction: {}",
                                static_cast<int>(optionsResult));
                }
                const sl::Result freeResult = slFreeResources(sl::kFeatureDLSS_G, viewport);
                if (freeResult != sl::Result::eOk)
                {
                    SPDLOG_WARN("Streamline slFreeResources(DLSS-G) failed before swapchain destruction: {}",
                                static_cast<int>(freeResult));
                }
                lastDLSSGEnabled_ = false;
            }

            ReleaseSuperResolutionResources(
                Rendering::Upscaler::EUpscalerType::None);

            sl::ResourceTag nullDepth(nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent);
            sl::ResourceTag nullMvec(nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent);
            sl::ResourceTag nullHudless(nullptr, sl::kBufferTypeHUDLessColor,
                                        sl::ResourceLifecycle::eValidUntilPresent);
            sl::ResourceTag nullUi(nullptr, sl::kBufferTypeUIColorAndAlpha,
                                   sl::ResourceLifecycle::eValidUntilPresent);
            const sl::ResourceTag tags[] = {nullDepth, nullMvec, nullHudless, nullUi};
            if (lastFrameToken_)
            {
                slSetTagForFrame(*StreamlineContext().NativeToken(lastFrameToken_), viewport, tags,
                                 static_cast<uint32_t>(std::size(tags)), nullptr);
            }
        }

        void SetActiveType(Rendering::Upscaler::EUpscalerType type) override
        {
            ReleaseSuperResolutionResources(type);
        }

        void Shutdown() override
        {
            OnSwapChainDestroyed();

            // Process-wide SL teardown; the engine no longer knows about Streamline.
            StreamlineContext().Shutdown();
        }

        Rendering::Upscaler::FOptimalRenderSettings GetOptimalRenderSettings(
            uint32_t superResolutionMode,
            VkExtent2D outputExtent,
            bool upscalerEnabled,
            bool hdrOutput,
            Rendering::Upscaler::EUpscalerType type) override
        {
            Rendering::Upscaler::FOptimalRenderSettings result{};
            const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(superResolutionMode);
            result.renderExtent = Rendering::Upscaler::ScaleExtent(outputExtent, modeInfo.fallbackScale);
            result.minRenderExtent = result.renderExtent;
            result.maxRenderExtent = outputExtent;

            const auto caps = StreamlineContext().Caps();
            if (!upscalerEnabled ||
                !Rendering::Upscaler::SupportsUpscalerType(caps.supportedTypes, type) ||
                outputExtent.width == 0 || outputExtent.height == 0)
            {
                return result;
            }

            sl::DLSSOptions options{};
            options.mode = ToDLSSMode(superResolutionMode);
            options.outputWidth = outputExtent.width;
            options.outputHeight = outputExtent.height;
            options.colorBuffersHDR = hdrOutput ? sl::Boolean::eTrue : sl::Boolean::eFalse;

            sl::DLSSOptimalSettings settings{};
            const sl::Result slResult = slDLSSGetOptimalSettings(options, settings);
            if (slResult != sl::Result::eOk ||
                settings.optimalRenderWidth == 0 ||
                settings.optimalRenderHeight == 0)
            {
                SPDLOG_WARN("Streamline slDLSSGetOptimalSettings failed: {}; using fallback scale {}",
                            static_cast<int>(slResult),
                            modeInfo.fallbackScale);
                return result;
            }

            result.renderExtent = {settings.optimalRenderWidth, settings.optimalRenderHeight};
            result.minRenderExtent = {settings.renderWidthMin, settings.renderHeightMin};
            result.maxRenderExtent = {settings.renderWidthMax, settings.renderHeightMax};
            result.fromStreamline = true;
            return result;
        }

        Rendering::Upscaler::FFrameToken BeginFrame(
            uint32_t frameIndex,
            bool frameGenerationEnabled,
            uint32_t frameLimitFps) override
        {
            const auto token = StreamlineContext().GetFrameToken(frameIndex);
            if (token)
            {
                lastFrameToken_ = token;
            }
            SetReflexOptions(frameGenerationEnabled, frameGenerationEnabled ? frameLimitFps : 0);
            return token;
        }

        void MarkFrame(Rendering::Upscaler::EFrameMarker marker, const Rendering::Upscaler::FFrameToken& frameToken) override
        {
            const auto caps = StreamlineContext().Caps();
            if (!caps.supportPCL || !frameToken)
            {
                return;
            }

            const sl::Result result = slPCLSetMarker(
                ToPCLMarker(marker),
                *StreamlineContext().NativeToken(frameToken));
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slPCLSetMarker failed: {}", static_cast<int>(result));
            }
        }

        void SetReflexOptions(bool lowLatencyEnabled, uint32_t frameLimitFps) override
        {
            const auto caps = StreamlineContext().Caps();
            if (!caps.supportReflex)
            {
                return;
            }

            const sl::ReflexMode mode = lowLatencyEnabled ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
            const uint32_t frameLimitUs = frameLimitFps > 0
                ? std::max(1u, 1000000u / frameLimitFps)
                : 0u;
            if (reflexOptionsSet_ && lastReflexMode_ == mode && lastFrameLimitUs_ == frameLimitUs)
            {
                return;
            }

            sl::ReflexOptions options{};
            options.mode = mode;
            options.frameLimitUs = frameLimitUs;
            const sl::Result result = slReflexSetOptions(options);
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slReflexSetOptions failed: {}", static_cast<int>(result));
                return;
            }

            reflexOptionsSet_ = true;
            lastReflexMode_ = mode;
            lastFrameLimitUs_ = frameLimitUs;
        }

        void ReflexSleep(const Rendering::Upscaler::FFrameToken& frameToken) override
        {
            const auto caps = StreamlineContext().Caps();
            if (!caps.supportReflex || !frameToken)
            {
                return;
            }

            const sl::Result result = slReflexSleep(*StreamlineContext().NativeToken(frameToken));
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slReflexSleep failed: {}", static_cast<int>(result));
            }
        }

        bool Evaluate(const Rendering::Upscaler::FFrameInputs& inputs) override
        {
            const auto caps = StreamlineContext().Caps();
            if (!Rendering::Upscaler::SupportsUpscalerType(caps.supportedTypes, inputs.upscalerType) ||
                !inputs.frameToken || inputs.ubo == nullptr)
            {
                return false;
            }

            const bool useRR = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(inputs.upscalerType)).requiresRayReconstruction;
            if (!SetSuperResolutionOptions(inputs, useRR))
            {
                return false;
            }

            SetCommonConstants(inputs);
            TagSuperResolutionResources(inputs, useRR);

            sl::ViewportHandle viewport(0);
            const sl::BaseStructure* evaluateInputs[] = {&viewport};
            const sl::Feature feature = useRR ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS;
            const sl::Result result = slEvaluateFeature(
                feature,
                *StreamlineContext().NativeToken(inputs.frameToken),
                evaluateInputs,
                static_cast<uint32_t>(std::size(evaluateInputs)),
                inputs.commandBuffer);
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slEvaluateFeature({}) failed: {}", feature, static_cast<int>(result));
                return false;
            }
            lastDLSSRRActive_ = useRR;
            lastDLSSActive_ = !useRR;

            return true;
        }

        void TagFrameGeneration(const Rendering::Upscaler::FFrameInputs& inputs) override
        {
            const auto caps = StreamlineContext().Caps();
            if (!inputs.frameToken ||
                !Rendering::Upscaler::SupportsUpscalerType(
                    caps.frameGenerationTypes, inputs.upscalerType))
            {
                return;
            }

            const bool hdrFormatSupported =
                IsHDRFormatSupportedForDLSSG(inputs.swapchainFormat, inputs.hdrOutput);
            if (inputs.enableFrameGeneration && inputs.hdrOutput && !hdrFormatSupported &&
                lastUnsupportedDLSSGHDRFormat_ != inputs.swapchainFormat)
            {
                SPDLOG_WARN(
                    "DLSS-G disabled: HDR swapchain format {} is unsupported; use an HDR10 10-bit packed format",
                    static_cast<uint32_t>(inputs.swapchainFormat));
                lastUnsupportedDLSSGHDRFormat_ = inputs.swapchainFormat;
            }
            else if (hdrFormatSupported)
            {
                lastUnsupportedDLSSGHDRFormat_ = VK_FORMAT_UNDEFINED;
            }

            const bool canEnable =
                inputs.enableFrameGeneration &&
                caps.supportReflex &&
                caps.supportPCL &&
                inputs.depth.IsValid() &&
                inputs.motionVectors.IsValid() &&
                inputs.hudlessColor.IsValid() &&
                hdrFormatSupported;

            SetFrameGenerationOptions(inputs, canEnable);
            if (!canEnable)
            {
                return;
            }

            SetCommonConstants(inputs);

            sl::ViewportHandle viewport(0);
            const sl::Extent renderExtent = ToSlExtent({0, 0}, inputs.renderExtent);
            const sl::Extent outputExtent = ToSlExtent(inputs.outputOffset, inputs.outputExtent);

            TagResource(inputs, inputs.depth, sl::kBufferTypeDepth,
                        sl::ResourceLifecycle::eValidUntilPresent, renderExtent);
            TagResource(inputs, inputs.motionVectors, sl::kBufferTypeMotionVectors,
                        sl::ResourceLifecycle::eValidUntilPresent, renderExtent);
            TagResource(inputs, inputs.hudlessColor, sl::kBufferTypeHUDLessColor,
                        sl::ResourceLifecycle::eValidUntilPresent, outputExtent);

            sl::ResourceTag backbufferTag(nullptr, sl::kBufferTypeBackbuffer,
                                          sl::ResourceLifecycle::eValidUntilPresent, &outputExtent);
            const sl::Result backbufferResult = slSetTagForFrame(
                *StreamlineContext().NativeToken(inputs.frameToken),
                viewport,
                &backbufferTag,
                1,
                nullptr);
            if (backbufferResult != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline backbuffer tag failed: {}", static_cast<int>(backbufferResult));
            }

            if (inputs.uiColorAndAlpha.IsValid())
            {
                TagResource(inputs, inputs.uiColorAndAlpha, sl::kBufferTypeUIColorAndAlpha,
                            sl::ResourceLifecycle::eValidUntilPresent, outputExtent);
            }
            else
            {
                sl::ResourceTag noUiTag(nullptr, sl::kBufferTypeUIColorAndAlpha,
                                        sl::ResourceLifecycle::eValidUntilPresent, &outputExtent);
                const sl::Result result = slSetTagForFrame(
                    *StreamlineContext().NativeToken(inputs.frameToken),
                    viewport,
                    &noUiTag,
                    1,
                    nullptr);
                if (result != sl::Result::eOk)
                {
                    SPDLOG_WARN("Streamline no-UI tag failed: {}", static_cast<int>(result));
                }
            }

        }

        void UpdateFrameGenerationState() override
        {
            const auto caps = StreamlineContext().Caps();
            if (caps.frameGenerationTypes == 0)
            {
                frameGenerationState_ = {};
                return;
            }

            sl::ViewportHandle viewport(0);
            sl::DLSSGState state{};
            const sl::Result result = slDLSSGGetState(viewport, state, nullptr);
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slDLSSGGetState failed: {}", static_cast<int>(result));
                frameGenerationState_.valid = false;
                return;
            }

            frameGenerationState_.valid = true;
            frameGenerationState_.numFramesActuallyPresented = state.numFramesActuallyPresented;
            frameGenerationState_.numFramesToGenerateMax = state.numFramesToGenerateMax;
            frameGenerationState_.minWidthOrHeight = state.minWidthOrHeight;
            frameGenerationState_.estimatedVRAMUsageInBytes = state.estimatedVRAMUsageInBytes;
            frameGenerationState_.statusMask = static_cast<uint32_t>(state.status);
            if (state.status != sl::DLSSGStatus::eOk && lastStatusMask_ != frameGenerationState_.statusMask)
            {
                SPDLOG_WARN("DLSS-G runtime status is not OK: mask={}", frameGenerationState_.statusMask);
                lastStatusMask_ = frameGenerationState_.statusMask;
            }
            else if (state.status == sl::DLSSGStatus::eOk &&
                     (!frameGenerationStateLogged_ ||
                      lastNumFramesActuallyPresented_ != frameGenerationState_.numFramesActuallyPresented))
            {
                SPDLOG_INFO("DLSS-G state OK: presented={}, maxGenerate={}, vram={} MB",
                            frameGenerationState_.numFramesActuallyPresented,
                            frameGenerationState_.numFramesToGenerateMax,
                            frameGenerationState_.estimatedVRAMUsageInBytes / (1024ull * 1024ull));
                frameGenerationStateLogged_ = true;
                lastNumFramesActuallyPresented_ = frameGenerationState_.numFramesActuallyPresented;
                lastStatusMask_ = frameGenerationState_.statusMask;
            }
        }

        Rendering::Upscaler::FFrameGenerationState FrameGenerationState() const override
        {
            return frameGenerationState_;
        }

    private:
        void ReleaseSuperResolutionResources(Rendering::Upscaler::EUpscalerType activeType)
        {
            sl::ViewportHandle viewport(0);
            if (lastDLSSRRActive_ &&
                activeType != Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction)
            {
                sl::DLSSDOptions options{};
                options.mode = sl::DLSSMode::eOff;
                slDLSSDSetOptions(viewport, options);
                slFreeResources(sl::kFeatureDLSS_RR, viewport);
                lastDLSSRRActive_ = false;
                SPDLOG_INFO("Streamline DLSS Ray Reconstruction GPU resources released");
            }
            if (lastDLSSActive_ &&
                activeType != Rendering::Upscaler::EUpscalerType::DLSS)
            {
                sl::DLSSOptions options{};
                options.mode = sl::DLSSMode::eOff;
                slDLSSSetOptions(viewport, options);
                slFreeResources(sl::kFeatureDLSS, viewport);
                lastDLSSActive_ = false;
                SPDLOG_INFO("Streamline DLSS GPU resources released");
            }
        }

        bool SetSuperResolutionOptions(const Rendering::Upscaler::FFrameInputs& inputs, bool useRR)
        {
            sl::ViewportHandle viewport(0);
            if (useRR)
            {
                sl::DLSSDOptions options{};
                options.mode = ToDLSSMode(inputs.superResolutionMode);
                options.dlaaPreset = sl::DLSSDPreset::ePresetE;
                options.qualityPreset = sl::DLSSDPreset::ePresetE;
                options.balancedPreset = sl::DLSSDPreset::ePresetE;
                options.performancePreset = sl::DLSSDPreset::ePresetE;
                options.ultraPerformancePreset = sl::DLSSDPreset::ePresetE;
                options.outputWidth = inputs.outputExtent.width;
                options.outputHeight = inputs.outputExtent.height;
                options.colorBuffersHDR = (inputs.inputColorIsLinear || inputs.hdrOutput)
                    ? sl::Boolean::eTrue : sl::Boolean::eFalse;
                options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;

                const sl::Result result = slDLSSDSetOptions(viewport, options);
                if (result != sl::Result::eOk)
                {
                    SPDLOG_ERROR("Streamline slDLSSDSetOptions failed: {}", static_cast<int>(result));
                    return false;
                }
                return true;
            }

            sl::DLSSOptions options{};
            options.mode = ToDLSSMode(inputs.superResolutionMode);
            options.outputWidth = inputs.outputExtent.width;
            options.outputHeight = inputs.outputExtent.height;
            options.colorBuffersHDR = (inputs.inputColorIsLinear || inputs.hdrOutput)
                ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            options.dlaaPreset = sl::DLSSPreset::ePresetJ;
            options.qualityPreset = sl::DLSSPreset::ePresetJ;
            options.balancedPreset = sl::DLSSPreset::ePresetJ;
            options.performancePreset = sl::DLSSPreset::ePresetJ;
            options.ultraPerformancePreset = sl::DLSSPreset::ePresetF;

            const sl::Result result = slDLSSSetOptions(viewport, options);
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slDLSSSetOptions failed: {}", static_cast<int>(result));
                return false;
            }
            return true;
        }

        void SetFrameGenerationOptions(const Rendering::Upscaler::FFrameInputs& inputs, bool enable)
        {
            sl::ViewportHandle viewport(0);
            sl::DLSSGOptions options{};
            options.mode = enable ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
            options.numFramesToGenerate = std::max(1u, inputs.frameGenerationMultiplier) - 1u;
            if (frameGenerationState_.valid && frameGenerationState_.numFramesToGenerateMax > 0)
            {
                options.numFramesToGenerate = std::clamp(
                    options.numFramesToGenerate,
                    1u,
                    frameGenerationState_.numFramesToGenerateMax);
            }
            options.numBackBuffers = inputs.backBufferCount;
            options.mvecDepthWidth = inputs.renderExtent.width;
            options.mvecDepthHeight = inputs.renderExtent.height;
            options.colorWidth = inputs.outputExtent.width;
            options.colorHeight = inputs.outputExtent.height;
            options.colorBufferFormat = static_cast<uint32_t>(inputs.swapchainFormat);
            options.mvecBufferFormat = static_cast<uint32_t>(inputs.motionVectors.format);
            options.depthBufferFormat = static_cast<uint32_t>(inputs.depth.format);
            options.hudLessBufferFormat = static_cast<uint32_t>(inputs.hudlessColor.format);
            options.uiBufferFormat = static_cast<uint32_t>(inputs.uiColorAndAlpha.format);
            options.onErrorCallback = OnDLSSGApiError;
            options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;

            const sl::Result result = slDLSSGSetOptions(viewport, options);
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slDLSSGSetOptions failed: {}", static_cast<int>(result));
                return;
            }
            if (lastDLSSGEnabled_ != enable)
            {
                SPDLOG_INFO("DLSS-G {}", enable ? "enabled" : "disabled");
                if (!enable && lastDLSSGEnabled_)
                {
                    slFreeResources(sl::kFeatureDLSS_G, viewport);
                }
                lastDLSSGEnabled_ = enable;
            }
        }

        void SetCommonConstants(const Rendering::Upscaler::FFrameInputs& inputs)
        {
            if (inputs.ubo == nullptr || !inputs.frameToken)
            {
                return;
            }
            if (hasConstantsFrameIndex_ && lastConstantsFrameIndex_ == inputs.frameIndex)
            {
                return;
            }

            const Assets::UniformBufferObject& ubo = *inputs.ubo;
            const auto reprojection = Rendering::Upscaler::CalculateReprojectionTransforms(
                ubo.ProjectionUnJit,
                ubo.ModelView,
                ubo.PrevViewProjectionUnJit);
            const glm::vec2 motionVectorScale =
                Rendering::Upscaler::CalculateMotionVectorScale(inputs.renderExtent);

            sl::Constants constants{};
            constants.cameraViewToClip = ToSlMatrix(ubo.ProjectionUnJit);
            constants.clipToCameraView = ToSlMatrix(ubo.ProjectionInverseUnJit);
            constants.clipToPrevClip = ToSlMatrix(reprojection.clipToPrevClip);
            constants.prevClipToClip = ToSlMatrix(reprojection.prevClipToClip);
            constants.jitterOffset = sl::float2(ubo.Jitter.x, ubo.Jitter.y);
            constants.mvecScale = {motionVectorScale.x, motionVectorScale.y};
            constants.cameraPos = sl::float3(
                ubo.ModelViewInverse[3][0],
                ubo.ModelViewInverse[3][1],
                ubo.ModelViewInverse[3][2]);
            constants.cameraFwd = sl::float3(
                -ubo.ModelViewInverse[2][0],
                -ubo.ModelViewInverse[2][1],
                -ubo.ModelViewInverse[2][2]);
            constants.cameraUp = sl::float3(
                ubo.ModelViewInverse[1][0],
                ubo.ModelViewInverse[1][1],
                ubo.ModelViewInverse[1][2]);
            constants.cameraRight = sl::float3(
                ubo.ModelViewInverse[0][0],
                ubo.ModelViewInverse[0][1],
                ubo.ModelViewInverse[0][2]);
            constants.cameraNear = inputs.camera.nearPlane;
            constants.cameraFar = inputs.camera.farPlane;
            constants.cameraFOV = inputs.camera.verticalFovRadians;
            constants.cameraAspectRatio = inputs.camera.aspectRatio;
            constants.depthInverted = sl::Boolean::eFalse;
            constants.cameraMotionIncluded = sl::Boolean::eTrue;
            constants.motionVectors3D = sl::Boolean::eFalse;
            constants.reset = inputs.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
            
            sl::ViewportHandle viewport(0);
            const sl::Result result = slSetConstants(
                constants,
                *StreamlineContext().NativeToken(inputs.frameToken),
                viewport);
            if (result != sl::Result::eOk)
            {
                SPDLOG_ERROR("Streamline slSetConstants failed: {}", static_cast<int>(result));
                return;
            }
            lastConstantsFrameIndex_ = inputs.frameIndex;
            hasConstantsFrameIndex_ = true;
        }

        void TagResource(
            const Rendering::Upscaler::FFrameInputs& inputs,
            const Rendering::Upscaler::FImageResource& image,
            sl::BufferType type,
            sl::ResourceLifecycle lifecycle,
            const sl::Extent& extent)
        {
            if (!image.IsValid())
            {
                return;
            }

            sl::ViewportHandle viewport(0);
            sl::Resource resource = ToSlResource(image);
            sl::ResourceTag tag(&resource, type, lifecycle, &extent);
            const sl::Result result = slSetTagForFrame(
                *StreamlineContext().NativeToken(inputs.frameToken),
                viewport,
                &tag,
                1,
                inputs.commandBuffer);
            if (result != sl::Result::eOk)
            {
                SPDLOG_WARN("Streamline slSetTagForFrame({}) failed: {}", type, static_cast<int>(result));
            }
        }

        void TagSuperResolutionResources(const Rendering::Upscaler::FFrameInputs& inputs, bool useRR)
        {
            const sl::Extent renderExtent = ToSlExtent({0, 0}, inputs.renderExtent);
            const sl::Extent outputExtent = ToSlExtent(inputs.outputOffset, inputs.outputExtent);

            TagResource(inputs, inputs.depth, sl::kBufferTypeDepth,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.motionVectors, sl::kBufferTypeMotionVectors,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.scalingInputColor, sl::kBufferTypeScalingInputColor,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.scalingOutputColor, sl::kBufferTypeScalingOutputColor,
                        sl::ResourceLifecycle::eOnlyValidNow, outputExtent);

            if (!useRR)
            {
                return;
            }

            TagResource(inputs, inputs.albedo, sl::kBufferTypeAlbedo,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.specularAlbedo, sl::kBufferTypeSpecularAlbedo,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.normalRoughness, sl::kBufferTypeNormalRoughness,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.motionVectors, sl::kBufferTypeSpecularMotionVectors,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.diffuseNoisy, sl::kBufferTypeDiffuseHitNoisy,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.specularNoisy, sl::kBufferTypeSpecularHitNoisy,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.diffuseHitDistance, sl::kBufferTypeDiffuseHitDistance,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
            TagResource(inputs, inputs.specularHitDistance, sl::kBufferTypeSpecularHitDistance,
                        sl::ResourceLifecycle::eOnlyValidNow, renderExtent);
        }

        Rendering::Upscaler::FFrameToken lastFrameToken_{};
        uint32_t lastConstantsFrameIndex_ = 0;
        bool hasConstantsFrameIndex_ = false;
        Rendering::Upscaler::FFrameGenerationState frameGenerationState_{};
        uint32_t lastStatusMask_ = 0;
        uint32_t lastNumFramesActuallyPresented_ = 0;
        bool frameGenerationStateLogged_ = false;
        VkFormat lastUnsupportedDLSSGHDRFormat_ = VK_FORMAT_UNDEFINED;
        bool lastDLSSGEnabled_ = false;
        bool lastDLSSActive_ = false;
        bool lastDLSSRRActive_ = false;
        bool reflexOptionsSet_ = false;
        sl::ReflexMode lastReflexMode_ = sl::ReflexMode::eOff;
        uint32_t lastFrameLimitUs_ = 0;
    };

    bool AddStreamlineDeviceExtensionIfAvailable(VkPhysicalDevice physicalDevice,
                                                 std::vector<const char*>& requiredExtensions,
                                                 const char* extensionName,
                                                 const char* featureName)
    {
        if (StreamlineHasDeviceExtension(physicalDevice, extensionName))
        {
            AppendUnique(requiredExtensions, extensionName);
            return true;
        }
        SPDLOG_WARN("{} disabled because device extension {} is unavailable", featureName, extensionName);
        return false;
    }

    bool ChainContainsSType(const void* featureChain, const VkStructureType sType)
    {
        for (const auto* node = static_cast<const VkBaseInStructure*>(featureChain); node != nullptr;
             node = node->pNext)
        {
            if (node->sType == sType)
            {
                return true;
            }
        }
        return false;
    }

    // Requests SL device extensions/features during logical device creation.
    class FStreamlineDeviceAugmenter final : public Vulkan::IDeviceCreationAugmenter
    {
    public:
        void* OnPhysicalDeviceSelected(VkInstance /*instance*/,
                                       VkPhysicalDevice physicalDevice,
                                       std::vector<const char*>& requiredExtensions,
                                       void* featureChain) override
        {
            const auto streamlineCaps =
                StreamlineWrapper::AppendRequiredDeviceExtensions(physicalDevice, requiredExtensions);
            const bool hasLegacyStreamlineExtensions =
                AddStreamlineDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                                        VK_NVX_BINARY_IMPORT_EXTENSION_NAME,
                                                        "Streamline binary import") &&
                AddStreamlineDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                                        VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME,
                                                        "Streamline image view handles");
            AddStreamlineDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                                    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                                    "Streamline buffer device address");
            if (StreamlineHasDeviceExtension(physicalDevice, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            {
                AppendUnique(requiredExtensions, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            }

            GStreamlineDeviceExtsEnabled = streamlineCaps.streamlineInitialized &&
                                           streamlineCaps.requestedDeviceExtensionsAvailable &&
                                           hasLegacyStreamlineExtensions;
            if (GStreamlineDeviceExtsEnabled &&
                !ChainContainsSType(featureChain, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES))
            {
                timelineSemaphoreFeatures_ = {};
                timelineSemaphoreFeatures_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
                timelineSemaphoreFeatures_.timelineSemaphore = true;
                timelineSemaphoreFeatures_.pNext = featureChain;
                return &timelineSemaphoreFeatures_;
            }
            return featureChain;
        }

    private:
        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures_{};
    };

    // Routes the engine's Vulkan interposer seam to the SL proxies.
    class FStreamlineInterposer final : public Vulkan::IVulkanInterposer
    {
    public:
        const char* PreferredVulkanLoaderPath() override
        {
            return StreamlineWrapper::PreferredVulkanLoaderPath();
        }

        void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions) override
        {
            AppendUnique(extensions, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
            AppendUnique(extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            StreamlineWrapper::AppendRequiredInstanceExtensions(extensions);
        }

        VkResult CreateInstance(const VkInstanceCreateInfo* createInfo, const VkAllocationCallbacks* allocator,
                                VkInstance* instance) override
        {
            return StreamlineWrapper::CreateInstance(createInfo, allocator, instance);
        }

        void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) override
        {
            StreamlineWrapper::DestroyInstance(instance, allocator);
        }

        VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count,
                                          VkPhysicalDevice* physicalDevices) override
        {
            return StreamlineWrapper::EnumeratePhysicalDevices(instance, count, physicalDevices);
        }

#if WIN32
        VkResult CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* createInfo,
                                       const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) override
        {
            return StreamlineWrapper::CreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
        }
#endif

        void DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                               const VkAllocationCallbacks* allocator) override
        {
            StreamlineWrapper::DestroySurfaceKHR(instance, surface, allocator);
        }

        VkResult CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* createInfo,
                              const VkAllocationCallbacks* allocator, VkDevice* device) override
        {
            return StreamlineWrapper::CreateDevice(physicalDevice, createInfo, allocator, device);
        }

        void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) override
        {
            StreamlineWrapper::DestroyDevice(device, allocator);
        }

        VkResult DeviceWaitIdle(VkDevice device) override
        {
            return StreamlineWrapper::DeviceWaitIdle(device);
        }

        VkResult CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* createInfo,
                                    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) override
        {
            return StreamlineWrapper::CreateSwapchainKHR(device, createInfo, allocator, swapchain);
        }

        void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                 const VkAllocationCallbacks* allocator) override
        {
            StreamlineWrapper::DestroySwapchainKHR(device, swapchain, allocator);
        }

        VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* count,
                                       VkImage* images) override
        {
            return StreamlineWrapper::GetSwapchainImagesKHR(device, swapchain, count, images);
        }

        VkResult AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                     VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) override
        {
            return StreamlineWrapper::AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence,
                                                          imageIndex);
        }

        VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) override
        {
            return StreamlineWrapper::QueuePresentKHR(queue, presentInfo);
        }
    };
#endif
}

namespace StreamlineWrapper
{
#if WITH_STREAMLINE && WIN32
    Vulkan::IVulkanInterposer& InterposerInstance()
    {
        static FStreamlineInterposer interposer;
        return interposer;
    }

    Vulkan::IDeviceCreationAugmenter& DeviceAugmenterInstance()
    {
        static FStreamlineDeviceAugmenter augmenter;
        return augmenter;
    }
#endif

    bool ShouldInitialize()
    {
#if WITH_STREAMLINE && WIN32
        return StreamlineContext().ShouldInitialize();
#else
        return false;
#endif
    }

    void Initialize()
    {
#if WITH_STREAMLINE && WIN32
        StreamlineContext().Initialize();
#endif
    }

    void Shutdown()
    {
#if WITH_STREAMLINE && WIN32
        StreamlineContext().Shutdown();
#endif
    }

    bool IsInitialized()
    {
#if WITH_STREAMLINE && WIN32
        return StreamlineContext().IsInitialized();
#else
        return false;
#endif
    }

    const char* PreferredVulkanLoaderPath()
    {
#if WITH_STREAMLINE && WIN32
        return StreamlineContext().PreferredVulkanLoaderPath();
#else
        return nullptr;
#endif
    }

    void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions)
    {
#if WITH_STREAMLINE && WIN32
        StreamlineContext().AppendRequiredInstanceExtensions(extensions);
#else
        (void)extensions;
#endif
    }

    Rendering::Upscaler::FFeatureCaps AppendRequiredDeviceExtensions(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& extensions)
    {
#if WITH_STREAMLINE && WIN32
        return StreamlineContext().AppendRequiredDeviceExtensions(physicalDevice, extensions);
#else
        (void)physicalDevice;
        (void)extensions;
        return {};
#endif
    }

    Rendering::Upscaler::FFeatureCaps CachedCaps()
    {
#if WITH_STREAMLINE && WIN32
        return StreamlineContext().Caps();
#else
        return {};
#endif
    }

    VkResult CreateInstance(
        const VkInstanceCreateInfo* createInfo,
        const VkAllocationCallbacks* allocator,
        VkInstance* instance)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().CreateInstance(createInfo, allocator, instance);
        }
#endif
        return vkCreateInstance(createInfo, allocator, instance);
    }

    void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            StreamlineContext().DestroyInstance(instance, allocator);
            return;
        }
#endif
        vkDestroyInstance(instance, allocator);
    }

    VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* physicalDevices)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().EnumeratePhysicalDevices(instance, count, physicalDevices);
        }
#endif
        return vkEnumeratePhysicalDevices(instance, count, physicalDevices);
    }

#if WIN32
    VkResult CreateWin32SurfaceKHR(
        VkInstance instance,
        const VkWin32SurfaceCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator,
        VkSurfaceKHR* surface)
    {
#if WITH_STREAMLINE
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().CreateWin32Surface(instance, createInfo, allocator, surface);
        }
#endif
        return vkCreateWin32SurfaceKHR(instance, createInfo, allocator, surface);
    }
#endif

    void DestroySurfaceKHR(
        VkInstance instance,
        VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            StreamlineContext().DestroySurface(instance, surface, allocator);
            return;
        }
#endif
        vkDestroySurfaceKHR(instance, surface, allocator);
    }

    VkResult CreateDevice(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* createInfo,
        const VkAllocationCallbacks* allocator,
        VkDevice* device)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().CreateDevice(physicalDevice, createInfo, allocator, device);
        }
#endif
        return vkCreateDevice(physicalDevice, createInfo, allocator, device);
    }

    void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            StreamlineContext().DestroyDevice(device, allocator);
            return;
        }
#endif
        vkDestroyDevice(device, allocator);
    }

    VkResult CreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator,
        VkSwapchainKHR* swapchain)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().Hooks().CreateSwapchain(device, createInfo, allocator, swapchain);
        }
#endif
        return vkCreateSwapchainKHR(device, createInfo, allocator, swapchain);
    }

    void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            StreamlineContext().Hooks().DestroySwapchain(device, swapchain, allocator);
            return;
        }
#endif
        vkDestroySwapchainKHR(device, swapchain, allocator);
    }

    VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().Hooks().GetSwapchainImages(device, swapchain, count, images);
        }
#endif
        return vkGetSwapchainImagesKHR(device, swapchain, count, images);
    }

    VkResult AcquireNextImageKHR(
        VkDevice device,
        VkSwapchainKHR swapchain,
        uint64_t timeout,
        VkSemaphore semaphore,
        VkFence fence,
        uint32_t* imageIndex)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().Hooks().AcquireNextImage(
                device,
                swapchain,
                timeout,
                semaphore,
                fence,
                imageIndex);
        }
#endif
        return vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, imageIndex);
    }

    VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().Hooks().QueuePresent(queue, presentInfo);
        }
#endif
        return vkQueuePresentKHR(queue, presentInfo);
    }

    VkResult DeviceWaitIdle(VkDevice device)
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineContext().IsInitialized())
        {
            return StreamlineContext().Hooks().DeviceWaitIdle(device);
        }
#endif
        return vkDeviceWaitIdle(device);
    }
}

namespace Rendering::Upscaler
{
    std::unique_ptr<IUpscaler> CreateStreamlineUpscaler()
    {
#if WITH_STREAMLINE && WIN32
        if (StreamlineWrapper::IsInitialized())
        {
            return std::make_unique<FStreamlineUpscaler>();
        }
#endif
        return nullptr;
    }
}
