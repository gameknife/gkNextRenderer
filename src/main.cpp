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
#include <fmt/format.h>
#include <iostream>

#include "TaskCoordinator.hpp"
#include "Vulkan/RayQuery/RayQueryRenderer.hpp"
#include "Vulkan/RayTrace/RayTracingRenderer.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredRenderer.hpp"
#include "Vulkan/LegacyDeferred/LegacyDeferredRenderer.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredRenderer.hpp"
#include <filesystem>

#define BUILDVER(X) std::string buildver(#X);
#include "build.version"
#include "Vulkan/Window.hpp"

namespace NextRenderer
{
    std::string GetBuildVersion()
    {
        return buildver;
    }
}

#if ANDROID
#include <imgui_impl_android.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

static void MakeExternalDirectory( android_app* app, std::string srcPath )
{
    if( std::filesystem::exists(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath) )
    {
        return;
    }
    
    std::filesystem::create_directories(std::filesystem::path(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath));
    
    AAssetDir* assetDir = AAssetManager_openDir(
                        app->activity->assetManager, srcPath.c_str());
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != NULL){
        AAsset* file = AAssetManager_open(app->activity->assetManager, (srcPath + "/" + filename).c_str(),
                                       AASSET_MODE_BUFFER);
        size_t fileLen = AAsset_getLength(file);
        std::vector<char> fileData;
        fileData.resize(fileLen);

        AAsset_read(file, static_cast<void*>(fileData.data()), fileLen);
        AAsset_close(file);

        std::string targetPath = std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath + "/" + filename;
    
        FILE* targetFile = fopen(targetPath.c_str(), "wb");
        fwrite(fileData.data(), 1, fileLen, targetFile);
        fclose(targetFile);
    }
    AAssetDir_close(assetDir);
}

class androidbuf : public std::streambuf {
public:
    enum { bufsize = 512 }; // ... or some other suitable buffer size
    androidbuf() { this->setp(buffer, buffer + bufsize - 1); }

private:
    int overflow(int c)
    {
        if (c == traits_type::eof()) {
            *this->pptr() = traits_type::to_char_type(c);
            this->sbumpc();
        }
        return this->sync()? traits_type::eof(): traits_type::not_eof(c);
    }

    int sync()
    {
        int rc = 0;
        if (this->pbase() != this->pptr()) {
            char writebuf[bufsize+1];
            memcpy(writebuf, this->pbase(), this->pptr() - this->pbase());
            writebuf[this->pptr() - this->pbase()] = '\0';

            rc = __android_log_print(ANDROID_LOG_INFO, "vkdemo", "%s", writebuf) > 0;
            this->setp(buffer, buffer + bufsize - 1);
        }
        return rc;
    }

    char buffer[bufsize];
};
#endif


namespace
{
    UserSettings CreateUserSettings(const Options& options);
    void PrintVulkanSdkInformation();
    void PrintVulkanInstanceInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void PrintVulkanLayersInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application);
    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application, bool benchmark);
    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, uint32_t gpuIdx);
}

std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication = nullptr;

void StartApplication(uint32_t rendererType, const Vulkan::WindowConfig& windowConfig, const UserSettings& userSettings, const Options& options)
{
    //windowConfig_.Title = windowConfig_.Title + " - " + GetRendererType();

    Vulkan::Window* window = new Vulkan::Window(windowConfig);
    
    switch (rendererType)
    {
    case 0:
        GApplication.reset(new NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 1:
        GApplication.reset(new NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 2:
        GApplication.reset(new NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 3:
        GApplication.reset(new NextRendererApplication<Vulkan::RayTracing::RayQueryRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    case 4:
        GApplication.reset(new NextRendererApplication<Vulkan::HybridDeferred::HybridDeferredRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
        break;
    default:
        GApplication.reset(new NextRendererApplication<Vulkan::VulkanBaseRenderer>(
            userSettings, window, static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode)));
    }

    fmt::print("Renderer: {}, BuildVer: {}\n", GApplication->GetRendererType(), NextRenderer::GetBuildVersion());
    
    PrintVulkanSdkInformation();
    //PrintVulkanInstanceInformation(*GApplication, options.Benchmark);
    //PrintVulkanLayersInformation(*GApplication, options.Benchmark);
    PrintVulkanDevices(*GApplication);

    SetVulkanDevice(*GApplication, options.GpuIdx);

    PrintVulkanSwapChainInformation(*GApplication, options.Benchmark);
}

#if ANDROID
void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        // The window is being shown, get it ready.
        {
            MakeExternalDirectory(app, "assets/fonts");
            MakeExternalDirectory(app, "assets/models");
            MakeExternalDirectory(app, "assets/shaders");
            MakeExternalDirectory(app, "assets/textures");
            MakeExternalDirectory(app, "assets/locale");
            
            const char* argv[] = { "gkNextRenderer", "--renderer=4", "--scene=1", "--load-scene=qx50.glb"};
            const Options options(4, argv);
            GOption = &options;
            UserSettings userSettings = CreateUserSettings(options);
            const Vulkan::WindowConfig windowConfig
            {
                "gkNextRenderer",
                options.Width,
                options.Height,
                options.Benchmark && options.Fullscreen,
                options.Fullscreen,
                !options.Fullscreen,
                options.SaveFile,
                app->window,
                options.ForceSDR
            };
            StartApplication(options.RendererType, windowConfig, userSettings, options);
            __android_log_print(ANDROID_LOG_INFO, "vkdemo",
                "start gknextrenderer: %d", options.RendererType);
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

static int32_t engine_handle_input(struct android_app* app) {
    ImGuiIO& io = ImGui::GetIO();
    //auto* engine = (struct engine*)app->userData;
    auto ib = android_app_swap_input_buffers(app);
    if (ib && ib->motionEventsCount) {
        for (int i = 0; i < ib->motionEventsCount; i++) {
            auto *event = &ib->motionEvents[i];
            int32_t ptrIdx = 0;
            switch (event->action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                // Retrieve the index for the starting and the ending of any secondary pointers
                ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                         AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_UP:
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75, GameActivityPointerAxes_getAxisValue(
            &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75);
                io.AddMouseButtonEvent(0, event->action == AMOTION_EVENT_ACTION_DOWN);

                GApplication->OnTouch(event->action == AMOTION_EVENT_ACTION_DOWN, GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75);

                break;
            case AMOTION_EVENT_ACTION_MOVE:
                // Process the move action: the new coordinates for all active touch pointers
                // are inside the event->pointers[]. Compare with our internally saved
                // coordinates to find out which pointers are actually moved. Note that there is
                // no index embedded inside event->action for AMOTION_EVENT_ACTION_MOVE (there
                // might be multiple pointers moved at the same time).
                //...
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75);

                GApplication->OnTouchMove(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75);
               break;
            }
        }
        android_app_clear_motion_events(ib);
    }

    // Process the KeyEvent in a similar way.
    //...

return 0;
}

void android_main(struct android_app* app)
{
    std::cout.rdbuf(new androidbuf);
    
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
        
        engine_handle_input(app);
        
        // render if vulkan is ready
        if (GApplication != nullptr) {
            GApplication->Tick();
        }
    } while (app->destroyRequested == 0);

    delete std::cout.rdbuf(0);
}
#endif

int main(int argc, const char* argv[]) noexcept
{
    try
    {
        const Options options(argc, argv);
        GOption = &options;
        UserSettings userSettings = CreateUserSettings(options);
        const Vulkan::WindowConfig windowConfig
        {
            "gkNextRenderer " + NextRenderer::GetBuildVersion(),
            options.Width,
            options.Height,
            options.Benchmark && options.Fullscreen,
            options.Fullscreen,
            !options.Fullscreen,
            options.SaveFile,
            nullptr,
            options.ForceSDR
        };
        
        
        uint32_t rendererType = options.RendererType;

#if __APPLE__
        setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif   
        if(options.RenderDoc)
        {
#if __linux__
            setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
#endif

#if __APPLE__
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
            setenv("MTL_CAPTURE_ENABLED", "1", 1);
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
#endif
        }

#if WIN32 && !defined(__MINGW32__)
        HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode;

        GetConsoleMode(hOutput, &dwMode);
        dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOutput, dwMode);
#endif

        StartApplication(rendererType, windowConfig, userSettings, options);

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
            fmt::print(stderr, "FATAL: caught unhandled exception\n");
        });
    }

    return EXIT_FAILURE;
}

namespace
{
    UserSettings CreateUserSettings(const Options& options)
    {
        SceneList::ScanScenes();
        
        UserSettings userSettings{};

        userSettings.Benchmark = options.Benchmark;
        userSettings.BenchmarkNextScenes = options.BenchmarkNextScenes;
        userSettings.BenchmarkMaxTime = options.BenchmarkMaxTime;
        userSettings.BenchmarkMaxFrame = options.BenchmarkMaxFrame;
        userSettings.SceneIndex = options.SceneIndex;

        if(options.SceneName != "")
        {
            std::string mappedSceneName = "";
            bool foundInAssets = false;

            //if found options.SceneName in key of Assets::sceneNames - set mappedSceneName to compare and find scene
            Assets::uo_string_string_t::const_iterator got = Assets::sceneNames.find(options.SceneName);
            if (got != Assets::sceneNames.end()) mappedSceneName = got->second;

            for( uint32_t i = 0; i < SceneList::AllScenes.size(); i++ )
            {
                if( SceneList::AllScenes[i].first == options.SceneName || SceneList::AllScenes[i].first == mappedSceneName )
                {
                    userSettings.SceneIndex = i;
                    foundInAssets = true;
                    break;
                }
            }

            if(!foundInAssets)
            {
                userSettings.SceneIndex = SceneList::AddExternalScene(options.SceneName);
            }
        }
        
        userSettings.IsRayTraced = true;
        userSettings.AccumulateRays = false;
        userSettings.NumberOfSamples = options.Benchmark ? 1 : options.Samples;
        userSettings.NumberOfBounces = options.Benchmark ? 4 : options.Bounces;
        userSettings.MaxNumberOfBounces = options.MaxBounces;
        userSettings.RR_MIN_DEPTH = options.RR_MIN_DEPTH;
        userSettings.AdaptiveSample = options.AdaptiveSample;
        userSettings.AdaptiveVariance = 6.0f;
        userSettings.AdaptiveSteps = 8;
        userSettings.TAA = true;

        userSettings.ShowSettings = !options.Benchmark;
        userSettings.ShowOverlay = true;

        userSettings.ShowVisualDebug = false;
        userSettings.HeatmapScale = 1.5f;

        userSettings.UseCheckerBoardRendering = false;
        userSettings.TemporalFrames = options.Benchmark ? 256 : options.Temporal;

        userSettings.Denoiser = options.Denoiser;

        userSettings.PaperWhiteNit = 600.f;

        userSettings.SunRotation = 0.5f;
        userSettings.SunLuminance = 500.f;
        userSettings.SkyIntensity = 50.f;
        
        userSettings.AutoFocus = false;

        return userSettings;
    }

    void PrintVulkanSdkInformation()
    {
        fmt::print("Vulkan SDK Header Version: {}\n\n", VK_HEADER_VERSION);
    }

    void PrintVulkanInstanceInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        puts("Vulkan Instance Extensions:");

        for (const auto& extension : application.Extensions())
        {
            fmt::print("- {} ({})\n", extension.extensionName, to_string(Vulkan::Version(extension.specVersion)));
        }

        puts("");
    }

    void PrintVulkanLayersInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        puts("Vulkan Instance Layers:");

        for (const auto& layer : application.Layers())
        {
            fmt::print("- {} ({}) : {}\n", layer.layerName, to_string(Vulkan::Version(layer.specVersion)), layer.description);
        }

        puts("");
    }

    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application)
    {
        puts("Vulkan Devices:");

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

            fmt::print("- [{}] {} '{}' ({}: vulkan {} driver {} {} - {})\n",
                        prop.deviceID, Vulkan::Strings::VendorId(prop.vendorID), prop.deviceName, Vulkan::Strings::DeviceType(prop.deviceType),
                        to_string(vulkanVersion), driverProp.driverName, driverProp.driverInfo, to_string(driverVersion));
        }

        puts("");
    }

    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        const auto& swapChain = application.SwapChain();

        fmt::print("Swap Chain:\n- image count: {}\n- present mode: {}\n\n", swapChain.Images().size(), static_cast<int>(swapChain.PresentMode()));
    }

    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, uint32_t gpuIdx)
    {
        const auto& physicalDevices = application.PhysicalDevices();
        VkPhysicalDevice pDevice = physicalDevices[gpuIdx <= physicalDevices.size() ? gpuIdx : 0 ];
        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(pDevice, &deviceProp);

        fmt::print("Setting Device [{}]\n", deviceProp.properties.deviceName);
        application.SetPhysicalDevice(pDevice);

        puts("");
    }
}
