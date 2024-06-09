#include "Vulkan/Enumerate.hpp"
#include "Vulkan/Strings.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Version.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/Exception.hpp"
#include "Options.hpp"
#include "Application.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

#if ANDROID
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

namespace
{
    UserSettings CreateUserSettings(const Options& options);
    void PrintVulkanSdkInformation();
    void PrintVulkanInstanceInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void PrintVulkanLayersInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application, const std::vector<uint32_t>& visible_devices);
    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, const std::vector<uint32_t>& visible_devices);
}

std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication = nullptr;

void StartApplication(uint32_t rendererType, const Vulkan::WindowConfig& windowConfig, const UserSettings& userSettings, const Options& options)
{
    switch (rendererType)
    {
    case 0:
        GApplication.reset(new NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>(
            userSettings, windowConfig, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 1:
        GApplication.reset(new NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>(
            userSettings, windowConfig, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 2:
        GApplication.reset(new NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>(
            userSettings, windowConfig, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    default:
        GApplication.reset(new NextRendererApplication<Vulkan::VulkanBaseRenderer>(
            userSettings, windowConfig, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
    }

    PrintVulkanSdkInformation();
    PrintVulkanInstanceInformation(*GApplication, options.Benchmark);
    PrintVulkanLayersInformation(*GApplication, options.Benchmark);
    PrintVulkanDevices(*GApplication, options.VisibleDevices);

    SetVulkanDevice(*GApplication, options.VisibleDevices);

    PrintVulkanSwapChainInformation(*GApplication, options.Benchmark);
}

#if ANDROID
void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        // The window is being shown, get it ready.
        {
            const char* argv[] = { "gkNextRenderer", "--benchmark", "--next-scenes" };
            const Options options(3, argv);
            GOption = &options;
            const UserSettings userSettings = CreateUserSettings(options);
            const Vulkan::WindowConfig windowConfig
            {
                "gkNextRenderer",
                options.Width,
                options.Height,
                options.Benchmark && options.Fullscreen,
                options.Fullscreen,
                !options.Fullscreen,
                options.SaveFile,
                app->window
            };
            __android_log_print(ANDROID_LOG_INFO, "vkdemo",
                            "event not handled: %d", (long long)app->window);
            StartApplication(1, windowConfig, userSettings, options);
            GApplication->Start();
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The window is being hidden or closed, clean it up.
        {
            
        }
        break;
    default:
        __android_log_print(ANDROID_LOG_INFO, "Vulkan Tutorials",
                            "event not handled: %d", cmd);
    }
}

void android_main(struct android_app* app)
{


    app->onAppCmd = handle_cmd;

    // Used to poll the events in the main loop
    int events;
    android_poll_source* source;

    // Main loop
    do {
        if (ALooper_pollAll(GApplication != nullptr ? 1 : 0, nullptr,
                            &events, (void**)&source) >= 0) {
            if (source != NULL) source->process(app, source);
                            }

        // render if vulkan is ready
        if (GApplication != nullptr) {
            GApplication->Tick();
        }
    } while (app->destroyRequested == 0);
}
#endif

int main(int argc, const char* argv[]) noexcept
{
    try
    {
        const Options options(argc, argv);
        GOption = &options;
        const UserSettings userSettings = CreateUserSettings(options);
        const Vulkan::WindowConfig windowConfig
        {
            "gkNextRenderer",
            options.Width,
            options.Height,
            options.Benchmark && options.Fullscreen,
            options.Fullscreen,
            !options.Fullscreen,
            options.SaveFile,
            nullptr
        };
        
        
        uint32_t rendererType = options.RendererType;
#if __linux__
        setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
#endif

#if __APPLE__
        setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
        setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
        setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
        setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
        setenv("MTL_CAPTURE_ENABLED", "1", 1);
        setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
        if( rendererType == 0 ) rendererType = 2;
#endif

        StartApplication(rendererType, windowConfig, userSettings, options);
        
        //applicationPtr->Run();

        {
            GApplication->Start();
            while (1)
            {
                if( GApplication->Tick() )
                {
                    break;
                }
            }
            GApplication->End();
        }

        GApplication.reset();

        return EXIT_SUCCESS;
    }

    catch (const Options::Help&)
    {
        return EXIT_SUCCESS;
    }

    catch (const std::exception& exception)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, [&exception]()
        {
            const auto stacktrace = boost::get_error_info<traced>(exception);

            std::cerr << "FATAL: " << exception.what() << std::endl;

            if (stacktrace)
            {
                std::cerr << '\n' << *stacktrace << '\n';
            }
        });
    }

    catch (...)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, []()
        {
            std::cerr << "FATAL: caught unhandled exception" << std::endl;
        });
    }

    return EXIT_FAILURE;
}

namespace
{
    UserSettings CreateUserSettings(const Options& options)
    {
        UserSettings userSettings{};

        userSettings.Benchmark = options.Benchmark;
        userSettings.BenchmarkNextScenes = options.BenchmarkNextScenes;
        userSettings.BenchmarkMaxTime = options.BenchmarkMaxTime;

        userSettings.SceneIndex = options.SceneIndex;

        userSettings.IsRayTraced = true;
        userSettings.AccumulateRays = false;
        userSettings.NumberOfSamples = options.Benchmark ? 1 : options.Samples;
        userSettings.NumberOfBounces = options.Benchmark ? 4 : options.Bounces;
        userSettings.MaxNumberOfSamples = options.MaxSamples;

        userSettings.ShowSettings = !options.Benchmark;
        userSettings.ShowOverlay = true;

        userSettings.ShowHeatmap = false;
        userSettings.HeatmapScale = 1.5f;

        userSettings.UseCheckerBoardRendering = false;
        userSettings.TemporalFrames = options.Benchmark ? 256 : options.Temporal;

        userSettings.DenoiseIteration = 0;
        userSettings.DepthPhi = 0.5f;
        userSettings.NormalPhi = 90.f;
        userSettings.ColorPhi = 5.f;

        userSettings.PaperWhiteNit = 600.f;

        return userSettings;
    }

    void PrintVulkanSdkInformation()
    {
        std::cout << "Vulkan SDK Header Version: " << VK_HEADER_VERSION << std::endl;
        std::cout << std::endl;
    }

    void PrintVulkanInstanceInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        std::cout << "Vulkan Instance Extensions: " << std::endl;

        for (const auto& extension : application.Extensions())
        {
            std::cout << "- " << extension.extensionName << " (" << Vulkan::Version(extension.specVersion) << ")" <<
                std::endl;
        }

        std::cout << std::endl;
    }

    void PrintVulkanLayersInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        std::cout << "Vulkan Instance Layers: " << std::endl;

        for (const auto& layer : application.Layers())
        {
            std::cout
                << "- " << layer.layerName
                << " (" << Vulkan::Version(layer.specVersion) << ")"
                << " : " << layer.description << std::endl;
        }

        std::cout << std::endl;
    }

    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application, const std::vector<uint32_t>& visible_devices)
    {
        std::cout << "Vulkan Devices: " << std::endl;

        for (const auto& device : application.PhysicalDevices())
        {
            VkPhysicalDeviceDriverProperties driverProp{};
            driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

            VkPhysicalDeviceProperties2 deviceProp{};
            deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProp.pNext = &driverProp;
#if !ANDROID
            vkGetPhysicalDeviceProperties2(device, &deviceProp);
#endif
            VkPhysicalDeviceFeatures features;
            vkGetPhysicalDeviceFeatures(device, &features);

            const auto& prop = deviceProp.properties;

            // Check whether device has been explicitly filtered out.
            if (!visible_devices.empty() && std::find(visible_devices.begin(), visible_devices.end(), prop.deviceID) ==
                visible_devices.end())
            {
                break;
            }

            const Vulkan::Version vulkanVersion(prop.apiVersion);
            const Vulkan::Version driverVersion(prop.driverVersion, prop.vendorID);

            std::cout << "- [" << prop.deviceID << "] ";
            std::cout << Vulkan::Strings::VendorId(prop.vendorID) << " '" << prop.deviceName;
            std::cout << "' (";
            std::cout << Vulkan::Strings::DeviceType(prop.deviceType) << ": ";
            std::cout << "vulkan " << vulkanVersion << ", ";
            std::cout << "driver " << driverProp.driverName << " " << driverProp.driverInfo << " - " << driverVersion;
            std::cout << ")" << std::endl;
        }

        std::cout << std::endl;
    }

    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        const auto& swapChain = application.SwapChain();

        std::cout << "Swap Chain: " << std::endl;
        std::cout << "- image count: " << swapChain.Images().size() << std::endl;
        std::cout << "- present mode: " << swapChain.PresentMode() << std::endl;
        std::cout << std::endl;
    }

    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, const std::vector<uint32_t>& visible_devices)
    {
#if !ANDROID
        const auto& physicalDevices = application.PhysicalDevices();
        const auto result = std::find_if(physicalDevices.begin(), physicalDevices.end(),
                                         [&](const VkPhysicalDevice& device)
                                         {
                                             VkPhysicalDeviceProperties2 prop{};
                                             prop.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                                             vkGetPhysicalDeviceProperties2(device, &prop);

                                             // Check whether device has been explicitly filtered out.
                                             if (!visible_devices.empty() && std::find(
                                                 visible_devices.begin(), visible_devices.end(),
                                                 prop.properties.deviceID) == visible_devices.end())
                                             {
                                                 return false;
                                             }

                                             // We want a device with geometry shader support.
                                             VkPhysicalDeviceFeatures deviceFeatures;
                                             vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

                                             if (!deviceFeatures.geometryShader)
                                             {
                                                 //return false;
                                             }

                                             // We want a device that supports the ray tracing extension.
                                             const auto extensions = Vulkan::GetEnumerateVector(
                                                 device, static_cast<const char*>(nullptr),
                                                 vkEnumerateDeviceExtensionProperties);
                                             const auto hasRayTracing = std::any_of(
                                                 extensions.begin(), extensions.end(),
                                                 [](const VkExtensionProperties& extension)
                                                 {
                                                     return strcmp(extension.extensionName,
                                                                   VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0;
                                                 });

                                             if (!hasRayTracing)
                                             {
                                                 //return false;
                                                 //application.SetSupportRayTracing(false);
                                             }

                                             // We want a device with a graphics queue.
                                             const auto queueFamilies = Vulkan::GetEnumerateVector(
                                                 device, vkGetPhysicalDeviceQueueFamilyProperties);
                                             const auto hasGraphicsQueue = std::any_of(
                                                 queueFamilies.begin(), queueFamilies.end(),
                                                 [](const VkQueueFamilyProperties& queueFamily)
                                                 {
                                                     return queueFamily.queueCount > 0 && queueFamily.queueFlags &
                                                         VK_QUEUE_GRAPHICS_BIT;
                                                 });

                                             return hasGraphicsQueue;
                                         });

        if (result == physicalDevices.end())
        {
            Throw(std::runtime_error("cannot find a suitable device"));
        }


        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(*result, &deviceProp);

        std::cout << "Setting Device [" << deviceProp.properties.deviceID << "]:" << std::endl;

        application.SetPhysicalDevice(*result);
#else
        const auto& physicalDevices = application.PhysicalDevices();
        application.SetPhysicalDevice(physicalDevices[0]);
#endif
        std::cout << std::endl;
    }
}
