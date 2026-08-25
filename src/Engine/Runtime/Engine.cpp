#include "Engine/Runtime/Engine.hpp"
#include "Engine/Rendering/BuiltinRendererProviders.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Runtime/Interface/RenderFrameConsumer.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/EngineCVars.hpp"
#include "Engine/Runtime/Subsystems/NextLocalization.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include <cstdlib>
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Editor/UiFrameDispatcher.hpp"
#include "Engine/Runtime/Interface/UiOverlay.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <initializer_list>
#include <optional>
#include <system_error>

#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/LogFile.hpp"

#define _USE_MATH_DEFINES

#include <math.h>


#define BUILDVER(X) std::string buildver(#X);
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "build.version"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Reflection/ReflectionRegistry.hpp"

// spdlog logging
#include <spdlog/stopwatch.h>


#if ANDROID
#include <spdlog/sinks/android_sink.h>
#endif

Runtime::Config::Options* GOption = nullptr;

namespace
{
    // Older Android NDK Vulkan headers do not name this newer registry value yet.
    constexpr VkDriverId kMesaKosmicKrispDriverId = static_cast<VkDriverId>(28);
    constexpr double minimizedTickIntervalSeconds = 1.0 / 60.0;

    VkDriverId GetDriverId(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);
        return driverProperties.driverID;
    }

    bool IsKosmicKrispDriver(VkPhysicalDevice physicalDevice)
    {
        return GetDriverId(physicalDevice) == kMesaKosmicKrispDriverId;
    }

    bool HasFullAmbientCubeBudget(VkPhysicalDevice physicalDevice)
    {
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
        // Estimate against the configured cascade count (Phase 2 right-sizing), matching the arena
        // layout in Scene.cpp, so devices that can hold the right-sized arena keep GI instead of
        // falling back to NoAmbient. Cubes+Voxels scale with the count; Pages is fixed; pong+scratch
        // are one cascade each.
        uint32_t cascadeCount = Assets::CUBE_CASCADE_MAX;
        if (NextEngine::GetInstance())
        {
            cascadeCount = Assets::SanitizeAmbientCubeCascadeCount(
                NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount);
        }
        float poolBrickRatio = 1.0f;
        if (NextEngine::GetInstance())
        {
            poolBrickRatio = NextEngine::GetInstance()->GetUserSettings().AmbientCubePoolBrickRatio;
        }
        const float clampedPoolBrickRatio = std::clamp(poolBrickRatio, 0.0f, 1.0f);
        const auto poolBricksPerCascade = static_cast<VkDeviceSize>(std::max(
            1.0f, std::ceil(static_cast<float>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * clampedPoolBrickRatio)));
        const VkDeviceSize poolCubesPerCascade =
            poolBricksPerCascade * static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);

        const VkDeviceSize fullAmbientCubeAllocationSize =
            static_cast<VkDeviceSize>(cascadeCount) *
                (perCascadeCount * sizeof(Assets::VoxelData) + poolCubesPerCascade * sizeof(Assets::AmbientCube)) +
            static_cast<VkDeviceSize>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT * sizeof(Assets::PageIndex) +
            poolCubesPerCascade * sizeof(Assets::AmbientCube) +
            perCascadeCount * sizeof(glm::u32vec4) +
            perCascadeCount * sizeof(glm::u32vec4);
        return largestDeviceLocalHeapSize >= fullAmbientCubeAllocationSize;
    }

    std::string ResolveScreenShotFilename(const std::string& requestedFilename, const char* defaultPrefix)
    {
        if (!requestedFilename.empty())
        {
            return requestedFilename;
        }

        const auto now = std::time(nullptr);
        const std::string filename = fmt::format("{}_{:%Y-%m-%d-%H-%M-%S}", defaultPrefix, *std::localtime(&now));
        const std::string directory = Utilities::FileHelper::GetWritableFilePath("screenshots");
        Utilities::FileHelper::EnsureDirectoryExists(directory);
        return (std::filesystem::path(directory) / filename).string();
    }
} // namespace

// RegisterEngineCVars moved to Runtime/Config/EngineCVars.*

namespace NextRenderer
{
    std::string GetBuildVersion() { return buildver; }

    Vulkan::VulkanBaseRenderer* CreateRenderer(uint32_t rendererType, Vulkan::Window* window,
                                               const VkPresentModeKHR presentMode, const bool enableValidationLayers)
    {
        Vulkan::RegisterBuiltinRendererProviders();
        std::vector<const char*> validationLayers;
        if (enableValidationLayers)
        {
            validationLayers.push_back("VK_LAYER_KHRONOS_validation");
        }

        Vulkan::Instance* instance = new Vulkan::Instance(
            *window, validationLayers, VK_API_VERSION_1_2, GOption->SyncValidation);

        const auto& physicalDevices = instance->PhysicalDevices();
        const uint32_t selectedGpuIdx = GOption->GpuIdx < physicalDevices.size() ? GOption->GpuIdx : 0;
        if (GOption->HardwareQuery && IsKosmicKrispDriver(physicalDevices[selectedGpuIdx]))
        {
            SPDLOG_WARN("KosmicKrisp detected; disabling Vulkan timestamp queries to avoid device-loss on macOS");
            GOption->HardwareQuery = false;
        }
        // Probed here, before the renderer exists: which renderers are worth registering at all is
        // a device verdict, and registering one whose resources cannot be created only defers the
        // failure to swapchain creation.
        const bool hasFullBindlessBudget =
            Vulkan::ProbeBindlessProfile(physicalDevices[selectedGpuIdx]) ==
            Assets::FBindlessProfile::Full();
        const bool hasFullAmbientCubeBudget =
            hasFullBindlessBudget && HasFullAmbientCubeBudget(physicalDevices[selectedGpuIdx]);
        const bool useRayTracingRenderer =
            hasFullAmbientCubeBudget && !GOption->ForceNoRT &&
            instance->SupportsRayQuery(physicalDevices[selectedGpuIdx]);

        std::vector<Vulkan::ERendererType> supportedTypes;
        if (!hasFullBindlessBudget)
        {
            // The only renderer that does not build the full bindless resource set.
            supportedTypes = {Vulkan::ERT_Compatibility};
        }
        else if (hasFullAmbientCubeBudget)
        {
            supportedTypes = {Vulkan::ERT_SoftwareTracing, Vulkan::ERT_SoftwareModern,
                              Vulkan::ERT_VoxelTracing, Vulkan::ERT_SoftwareModernNoAmbient};
        }
        else
        {
            supportedTypes = {Vulkan::ERT_SoftwareModernNoAmbient};
        }
        Vulkan::VulkanBaseRenderer* renderer =
            new Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers, instance);
        if (useRayTracingRenderer)
        {
            supportedTypes.emplace_back(Vulkan::ERT_PathTracing);
            supportedTypes.emplace_back(Vulkan::ERT_PathTracingLite);
        }

        for (auto type : supportedTypes)
        {
            renderer->RegisterLogicRenderer(type);
        }

        auto requestedType =
            Rendering::ResolveRendererChoice(static_cast<Vulkan::ERendererType>(rendererType),
                                             {useRayTracingRenderer, hasFullAmbientCubeBudget,
                                              hasFullBindlessBudget});
        if (std::find(supportedTypes.begin(), supportedTypes.end(), requestedType) == supportedTypes.end())
        {
            requestedType = *supportedTypes.begin();
        }

        renderer->SwitchLogicRenderer(requestedType);
        return renderer;
    }

} // namespace NextRenderer

namespace
{
} // namespace

Runtime::Config::UserSettings CreateUserSettings(const Runtime::Config::Options& options)
{
    Runtime::Config::UserSettings userSettings{};
    userSettings.BorderlessFullscreen = options.Fullscreen;

    return userSettings;
}

NextEngine* NextEngine::instance_ = nullptr;

NextEngine::FRuntimeServices::FRuntimeServices() = default;

NextEngine::FRuntimeServices::~FRuntimeServices() = default;

NextEngine::NextEngine(Runtime::Config::Options& options, void* userdata)
    : options_(&options)
{
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(1));

    // Idempotent: DesktopMain installs this before the engine exists so early failures
    // are captured too. This covers entry points that do not go through DesktopMain.
    Utilities::Logging::InstallFileSink();

#if defined(__APPLE__)
    // Workaround, not a policy: any vertical-blank-synchronized present mode still drops the
    // composited scene on some frames under MoltenVK, leaving the window black or flickering.
    // MoltenVK advertises only FIFO and IMMEDIATE, so MAILBOX and FIFO_RELAXED silently degrade
    // to FIFO and are unusable too - Immediate is the only mode that presents reliably.
    // Deepening the swapchain to three images fixed the present pacing (77fps median / 16.3 stdev
    // -> 111 / 4.5 on a 120Hz panel) but not the dropped frames, so the underlying defect is
    // still open. Output colorspace is unaffected: EDR stays enabled.
    options.PresentMode = static_cast<uint32_t>(VK_PRESENT_MODE_IMMEDIATE_KHR);
    SPDLOG_INFO("Apple display workaround: forcing Immediate present mode");
#endif

#if ANDROID
    std::string tag = "gknext";
    auto android_logger = spdlog::android_logger_mt("android", tag);
    android_logger->critical("Use \"adb shell logcat\" to view this message.");
    spdlog::set_default_logger(android_logger);
#endif

    GK_LOG_STAGE("---- Next Engine Initializing...");
    spdlog::stopwatch stopwatch;

    instance_ = this;
    
    // Initialize reflection system first
    Reflection::RegisterAllReflection();

    status_ = NextRenderer::EApplicationStatus::Starting;
    services_.packageFileSystem.reset(new Utilities::Package::FPackageFileSystem(Utilities::Package::EPM_OsFile));
    {
        // Paks every target may need. Game-specific ones (lego, brotato3d, ...)
        // stay with their game; these three carry assets any scene can reference,
        // so a missing mount would show up as an unexplained load failure rather
        // than as a game that was not started.
        for (const char* pak : {"assets/paks/runtime.pak",
                                "assets/paks/optional.pak",
                                "assets/paks/geo.pak"})
        {
            const std::string pakPath = Utilities::FileHelper::GetPlatformFilePath(pak);
            std::error_code ec;
            if (std::filesystem::exists(pakPath, ec))
            {
                services_.packageFileSystem->MountPak(pakPath);
            }
        }
    }

    // TUI dimensions are terminal/logical pixels. Keep the hidden render window at
    // a fixed scale so high-DPI monitors do not enlarge the terminal UI.
    const bool useSystemDpiScaling = options.SystemDpiScaling || options.Tui;
#if defined(__linux__)
    const bool hasX11Display = std::getenv("DISPLAY") != nullptr;
    const bool hasWaylandDisplay = std::getenv("WAYLAND_DISPLAY") != nullptr;
    const bool useHeadlessSurface = options.HeadlessSurface ||
        (options.AgentValidation && !options.AgentVisibleWindow && !hasX11Display && !hasWaylandDisplay);
#else
    const bool useHeadlessSurface = options.HeadlessSurface;
#endif
    if (useHeadlessSurface)
    {
        SPDLOG_INFO("No X11/Wayland display detected; agent validation will use VK_EXT_headless_surface");
        // There is no desktop to match in this path. A compact default keeps
        // unattended Lavapipe validation fast while explicit CLI dimensions
        // remain authoritative.
        if (!options.WidthSpecified)
        {
            options.Width = 480;
        }
        if (!options.HeightSpecified)
        {
            options.Height = 320;
        }
        // Lavapipe can advertise ray-query extensions, but its RT resource path is neither
        // representative of a hardware renderer nor reliable for unattended validation.
        // Use the software renderer deterministically on a display-less validation host.
        if (!options.ForceNoRT)
        {
            options.ForceNoRT = true;
            SPDLOG_INFO("Headless agent validation disables hardware ray tracing");
        }
    }
    Vulkan::Window::InitSDL(useSystemDpiScaling, options.VulkanDriver, useHeadlessSurface);
    
    Vulkan::WindowConfig windowConfig{"gkNextEngine " + NextRenderer::GetBuildVersion(),
                                      options.Width,options.Height,
                                      false, options.Fullscreen,!options.Fullscreen,
                                      options.SaveFile,userdata,options.ForceSDR};
    windowConfig.SystemDpiScaling = useSystemDpiScaling;
    windowConfig.HeadlessSurface = useHeadlessSurface;
    // Resolved before the game instance is created, not after: a game instance sizes its window in
    // its constructor, and whether anyone will see that window is part of the decision. The editor
    // sizes itself from the monitor when visible and from the requested size when hidden, which
    // only works if this is already set.
    windowConfig.HiddenWindow =
        (options.AgentValidation && !options.AgentVisibleWindow) || options.HiddenWindow || options.Tui;

    gameInstance_ = CreateGameInstance(windowConfig, options, this);
    
    // reconfigure
    windowConfig.Width = options.Width;
    windowConfig.Height = options.Height;
    
    config_.userSettings = CreateUserSettings(options);
    
    // cvars
    services_.cvarSystem = std::make_unique<NextCVar::FCVarSystem>();
    NextCVar::RegisterEngineCVars(*services_.cvarSystem, config_.userSettings, config_.showFlags, this);
    services_.cvarSystem->LoadDefaultFile("assets/configs/cvar_default.json");
    gameInstance_->ConfigureCVars(*services_.cvarSystem);
#if ANDROID
    // Use the low-overhead ray-query renderer by default; an archived user setting can still override it.
    services_.cvarSystem->ExecuteCommand("r.rendererType 5");
#endif
    services_.cvarSystem->LoadUserFiles();
    
    for (const std::string& overrideCommand : options_->CVarOverrides)
    {
        const auto result = services_.cvarSystem->ExecuteCommand(overrideCommand);
        if (!result.success)
        {
            SPDLOG_WARN("Startup CVar override failed '{}': {}", overrideCommand, result.message);
        }
    }
    
    // window config tweaks
    windowConfig.Fullscreen = config_.userSettings.BorderlessFullscreen;
    
    // create windows
    window_.reset(new Vulkan::Window(windowConfig));
    SetBorderlessFullscreen(config_.userSettings.BorderlessFullscreen);

    // localization
    services_.localization = std::make_unique<NextLocalization>();
    services_.localization->LoadFromTxt(fmt::format("assets/locale/{}.txt", options_->locale), options_->locale);

    GK_LOG_STAGE("---- Next Engine Initialized in {}", stopwatch.elapsed_ms());
}

void NextEngine::TickHotReload()
{
#if GK_ENABLE_HOT_RELOAD
    if (services_.shaderHotReloader)
    {
        SCOPED_CPU_TIMER("shader hot reload");
        services_.shaderHotReloader->SetEnabled(options_->ShaderHotReload);
        services_.shaderHotReloader->SetPollInterval(options_->ShaderHotReloadInterval);
        services_.shaderHotReloader->Tick(frameState_.deltaSeconds);
    }
#endif
}

NextEngine::FHotReloadStatus NextEngine::GetHotReloadStatus() const
{
    FHotReloadStatus status{};
    status.shaderHotReloadEnabled = options_ != nullptr && options_->ShaderHotReload;
    if (options_ != nullptr)
    {
        status.shaderPollIntervalSeconds = options_->ShaderHotReloadInterval;
    }

#if GK_ENABLE_HOT_RELOAD
    if (services_.shaderHotReloader)
    {
        const auto shaderStatus = services_.shaderHotReloader->GetStatus();
        status.shaderHotReloadEnabled = shaderStatus.shaderHotReloadEnabled;
        status.shaderInitialized = shaderStatus.shaderInitialized;
        status.shaderPollIntervalSeconds = shaderStatus.shaderPollIntervalSeconds;
        status.shaderSourceRoot = shaderStatus.shaderSourceRoot;
        status.shaderOutputRoot = shaderStatus.shaderOutputRoot;
        status.shaderCompiler = shaderStatus.shaderCompiler;
    }
#endif

    return status;
}

void NextEngine::RequestShaderHotReload()
{
#if GK_ENABLE_HOT_RELOAD
    if (services_.shaderHotReloader)
    {
        services_.shaderHotReloader->RequestRebuildAll();
    }
#endif
}

NextEngine::~NextEngine()
{
    // Before anything is torn down. GetInstance() is how the script bindings and several
    // subsystems reach the engine, and leaving it pointing at freed memory turns every one of
    // them into a use-after-free the moment something runs after shutdown. Guarded because tests
    // construct and destroy engines in sequence.
    if (instance_ == this)
    {
        instance_ = nullptr;
    }

    uiOverlay_.reset();
    userInterface_.reset();
    scene_.reset();
    renderer_.reset();
    // Scene and renderer teardown may still drain or query task state. Destroying
    // the coordinator in End() made Scene::~Scene recreate it during shutdown.
    Tasks::TaskCoordinator::DestroyInstance();
    window_.reset();

    Vulkan::Window::TerminateSDL();
}

void NextEngine::Start()
{
    PERFORMANCEAPI_INSTRUMENT_FUNCTION();

    GK_LOG_STAGE("---- Next Engine Starting...");
    spdlog::stopwatch stopwatch;

    // Initialize Renderer
    const VkPresentModeKHR presentMode = options_->AgentValidation || options_->Tui ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                             : static_cast<VkPresentModeKHR>(options_->PresentMode);
    config_.userSettings.PresentMode = static_cast<uint32_t>(presentMode);
    renderer_.reset(NextRenderer::CreateRenderer(static_cast<uint32_t>(config_.userSettings.RendererType), window_.get(),
                                                 presentMode, GOption->Validation));
    
    config_.userSettings.RendererType = static_cast<int32_t>(renderer_->CurrentLogicRendererType());

    auto& rendererDelegates = renderer_->GetDelegates();
    rendererDelegates.onDeviceSet = [this]() -> void { OnRendererDeviceSet(); };
    rendererDelegates.createSwapChain = [this]() -> void { OnRendererCreateSwapChain(); };
    rendererDelegates.deleteSwapChain = [this]() -> void { OnRendererDeleteSwapChain(); };
    rendererDelegates.beforeNextTick = [this]() -> void { OnRendererBeforeNextFrame(); };
    rendererDelegates.getUniformBufferObject = [this](VkOffset2D offset, VkExtent2D extend) -> Assets::UniformBufferObject { return GetUniformBufferObject(offset, extend); };
    rendererDelegates.postRender = [this](VkCommandBuffer commandBuffer, uint32_t imageIndex) -> void { OnRendererPostRender(commandBuffer, imageIndex); };
    rendererDelegates.afterSubmit = [this]() -> void  { OnRendererAfterSubmit(); };

    renderer_->Start();
    
    for (auto it = renderFrameConsumers_.begin(); it != renderFrameConsumers_.end();)
    {
        if ((*it)->Start())
        {
            ++it;
            continue;
        }
        it = renderFrameConsumers_.erase(it);
    }

    auto resolvedRendererType = Rendering::ResolveRendererChoice(
        renderer_->CurrentLogicRendererType(), renderer_->RendererChoiceCapabilities());
    if (resolvedRendererType != renderer_->CurrentLogicRendererType())
    {
        renderer_->SwitchLogicRenderer(resolvedRendererType);
        config_.userSettings.RendererType = static_cast<int32_t>(resolvedRendererType);
    }

    // shader hot-reload
#if GK_ENABLE_HOT_RELOAD
    if (options_->ShaderHotReload && shaderHotReloaderFactory_)
    {
        services_.shaderHotReloader = shaderHotReloaderFactory_(*this);
    }
#endif
    
    // Optional physics module.
    if (physicsFactory_)
    {
        services_.physics = physicsFactory_();
        if (services_.physics)
        {
            services_.physics->Start();
        }
    }

    // Optional audio module.
    if (audioFactory_)
    {
        services_.audio = audioFactory_();
        if (services_.audio)
        {
            services_.audio->Start();
        }
    }

    // script
    if (scriptRuntimeFactory_)
    {
        scriptRuntime_ = scriptRuntimeFactory_(*this);
    }
    if (scriptRuntime_)
    {
        scriptRuntime_->Initialize();
    }

    // gameinstance init
    gameInstance_->OnInit();
    
    if (agentControl_ && agentControl_->IsRunning())
    {
        gameInstance_->RegisterAgentQueries(agentQueries_);
    }

    // SDL_INIT_GAMEPAD enumerates HID devices and costs ~190ms on Windows. Rendering and
    // keyboard/mouse input do not need it, so it runs a couple of frames after the first
    // scene is up instead of inside startup. TickGamepadInput sees no devices until then;
    // SDL reports the already-connected ones once the subsystem comes up.
    AddTickedTask(
        [this, framesUntilInit = 2](double) mutable -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running || framesUntilInit-- > 0)
            {
                return false;
            }
            if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            {
                SPDLOG_WARN("Gamepad subsystem unavailable: {}", SDL_GetError());
                return true;
            }
            SPDLOG_INFO("Gamepad subsystem ready");
            return true;
        });

    GK_LOG_STAGE("---- Next Engine Started in {}", stopwatch.elapsed_ms());
}

bool NextEngine::HandleEvent(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        SDL_SetModState(static_cast<SDL_Keymod>(event.key.mod));
    }

    const bool globalCaptureShortcut = HandleGlobalCaptureShortcut(event);

    if (userInterface_)
    {
        userInterface_->HandleEvent(&event);
    }
    const bool rmlUiConsumed = uiOverlay_ && uiOverlay_->HandleEvent(event);

#if IOS || ANDROID
    // SDL also synthesizes mouse events for touches. Mobile camera gestures
    // consume the finger events directly, so don't deliver the synthetic
    // mouse stream a second time to gameplay/editor input.
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.which == SDL_TOUCH_MOUSEID)
    {
        return false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID)
    {
        return false;
    }
#endif

    if (scriptRuntime_)
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (!rmlUiConsumed)
            {
                scriptRuntime_->HandleEvent(event);
            }
            break;
        default:
            break;
        }
    }

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            return true;
        }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (renderer_)
        {
            const bool hasSwapChain = renderer_->HasSwapChain();
            const VkExtent2D framebufferSize = window_ ? window_->FramebufferSize() : VkExtent2D{0, 0};
            const VkExtent2D swapChainExtent = hasSwapChain ? renderer_->SwapChain().Extent() : VkExtent2D{0, 0};

            if (!hasSwapChain || framebufferSize.width != swapChainExtent.width ||
                framebufferSize.height != swapChainExtent.height)
            {
                renderer_->RequestRecreateSwapChain();
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (!rmlUiConsumed && !globalCaptureShortcut)
        {
            OnKey(event);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        inputState_.mousePos = glm::dvec2(event.button.x, event.button.y);
        if (event.button.button != 0)
        {
            const uint32_t mask = SDL_BUTTON_MASK(event.button.button);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                inputState_.mouseButtons |= mask;
            }
            else
            {
                inputState_.mouseButtons &= ~mask;
            }
        }
        if (!rmlUiConsumed)
        {
            OnMouseButton(event);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (rmlUiConsumed)
        {
            break;
        }
        if (event.motion.which == Runtime::Remote::remoteMouseId)
        {
            inputState_.mousePos += glm::dvec2(event.motion.xrel, event.motion.yrel);
            // Remote relative input arrives as deltas from the browser's
            // pointer-lock path, but most game/editor controllers interpret
            // OnCursorPosition() as an absolute cursor stream and compute their
            // own delta internally. Feed the accumulated absolute position here.
            OnCursorPosition(inputState_.mousePos.x, inputState_.mousePos.y);
            break;
        }
        if (window_ && SDL_GetWindowRelativeMouseMode(window_->Handle()))
        {
            inputState_.mousePos += glm::dvec2(event.motion.xrel, event.motion.yrel);
            OnCursorPosition(event.motion.xrel, event.motion.yrel);
        }
        else
        {
            inputState_.mousePos = glm::dvec2(event.motion.x, event.motion.y);
            OnCursorPosition(event.motion.x, event.motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!rmlUiConsumed)
        {
            OnScroll(event.wheel.x, event.wheel.y);
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (!rmlUiConsumed || event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
        {
            OnTouch(event);
        }
        break;
    case SDL_EVENT_DROP_FILE:
        OnDropFile(event.drop.data);
        break;
    default:
        break;
    }
    return false;
}

bool NextEngine::Tick(bool forcingDelta)
{
    PERFORMANCEAPI_INSTRUMENT_FUNCTION();

    {
        SCOPED_CPU_TIMER("engine");

        taskQueues_.ticked.insert(
            taskQueues_.ticked.end(),
            std::make_move_iterator(taskQueues_.pendingTicked.begin()),
            std::make_move_iterator(taskQueues_.pendingTicked.end()));
        taskQueues_.pendingTicked.clear();

        // make sure the output is flushed
        std::cout << std::flush;

        // Hot change renderer
        {
            auto requestedRendererType =
                Rendering::ResolveRendererChoice(static_cast<Vulkan::ERendererType>(config_.userSettings.RendererType),
                                                 renderer_->RendererChoiceCapabilities());
            if (requestedRendererType != static_cast<Vulkan::ERendererType>(config_.userSettings.RendererType))
            {
                config_.userSettings.RendererType = static_cast<int32_t>(requestedRendererType);
            }
            if (renderer_->CurrentLogicRendererType() != requestedRendererType)
            {
                renderer_->SwitchLogicRenderer(requestedRendererType);
                GkProfiling::Message(fmt::format("Renderer switched to {}",
                                                 Vulkan::GetRendererName(requestedRendererType)));
            }
        }

        // delta time calc
        {
            const auto prevTime = frameState_.time;
            double currentTime = GetWindow().GetTime();
            double minimumTickInterval = 0.0;
            if (window_ && window_->IsMinimized())
            {
                minimumTickInterval = minimizedTickIntervalSeconds;
            }
            if (!forcingDelta && minimumTickInterval > 0.0)
            {
                const double elapsed = currentTime - prevTime;
                if (elapsed < minimumTickInterval)
                {
                    const double remainingSeconds = minimumTickInterval - std::max(0.0, elapsed);
                    const auto delayMilliseconds = static_cast<Uint32>(std::max(
                        1.0, std::ceil(remainingSeconds * 1000.0)));
                    SDL_Delay(delayMilliseconds);
                    currentTime = GetWindow().GetTime();
                }
            }
            frameState_.time = currentTime;
            frameState_.deltaSeconds = frameState_.time - prevTime;
            if (forcingDelta)
                frameState_.deltaSeconds = 1.0 / 30.0;
            float invDelta = static_cast<float>(frameState_.deltaSeconds) / 60.0f;
            frameState_.smoothedDeltaSeconds =
                glm::mix(frameState_.smoothedDeltaSeconds, frameState_.deltaSeconds, invDelta * 100.0f);
        }

        TickHotReload();
        
        
        
        // 这里是一帧tick的开始，这里开始scene nodes可能会被操纵，在这里要把所有nodes的线程更新完成并准备好提交GPU
        if (scene_) scene_->EndUpdateNodes();

        if (services_.physics)
        {
            SCOPED_CPU_TIMER("physics collect");
            if (forcingDelta)
            {
                services_.physics->CompleteTick();
                if (services_.physics->TryCompleteTick() && scene_) scene_->SyncPhysics();
            }
            else if (services_.physics->TryCompleteTick() && scene_)
            {
                scene_->SyncPhysics();
            }
        }

        // Scene Update
        if (scene_)
        {
            SCOPED_CPU_TIMER("scene tick");
            scene_->Tick(static_cast<float>(frameState_.deltaSeconds));
        }

        if (scriptRuntime_)
        {
            SCOPED_CPU_TIMER("script runtime");
            scriptRuntime_->Tick(frameState_.deltaSeconds);
        }

        // tick
        if (status_ == NextRenderer::EApplicationStatus::Running)
        {
            SCOPED_CPU_TIMER("game tick");
            gameInstance_->OnTick(frameState_.deltaSeconds);
        }

        {
            SCOPED_CPU_TIMER("ticked tasks");
            for (auto it = taskQueues_.ticked.begin(); it != taskQueues_.ticked.end();)
            {
                if ((*it)(frameState_.deltaSeconds))
                {
                    it = taskQueues_.ticked.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        {
            SCOPED_CPU_TIMER("delayed tasks");
            for (auto it = taskQueues_.delayed.begin(); it != taskQueues_.delayed.end();)
            {
                if (frameState_.time > it->triggerTime)
                {
                    // update the next trigger time
                    it->triggerTime = frameState_.time + it->loopTime;

                    // execute
                    if (it->task())
                    {
                        it = taskQueues_.delayed.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                else
                {
                    ++it;
                }
            }
        }

        if (config_.userSettings.TickPhysics && services_.physics)
        {
            SCOPED_CPU_TIMER("physics kick");
            if (services_.physics->TryCompleteTick() && scene_) scene_->SyncPhysics();
            services_.physics->KickTick(frameState_.deltaSeconds);
        }

        // 到这里，这一帧的nodes操作已经结束，这里可以发起Scene Nodes的多线程更新
        if (scene_) scene_->StartUpdateNodes();
        
        {
            SCOPED_CPU_TIMER("draw frame");
            if (ShouldCaptureScreenShotThisFrame())
            {
                renderer_->RequestScreenShotCapture();
            }
            renderer_->DrawFrame();
        }
        frameState_.totalFrames = renderer_->FrameCount();
        if (options_->ExitAfterFrames > 0 && frameState_.totalFrames >= options_->ExitAfterFrames)
        {
            RequestExit(0);
        }

        if (progressiveRender_.enabled)
        {
            progressiveRender_.accumulatedFrames =
                std::min(progressiveRender_.accumulatedFrames + 1, FProgressiveRenderState::TargetFrames);
        }

        AdvanceScreenShotCapture();

        // sample gamepad stats
        {
            TickGamepadInput();
        }

        if (agentControl_)
        {
            agentControl_->Pump();
        }

        if (!renderFrameConsumers_.empty())
        {
            for (const auto& consumer : renderFrameConsumers_)
            {
                consumer->Tick();
            }
        }
    }

    GkProfiling::FrameMark();
    return closeRequested_;
}

void NextEngine::End()
{
    // Persist user data while SDL and the process-wide application state are still
    // alive. Fast exit may either skip destructors (quick_exit) or run this engine's
    // destructor during static teardown (exit), where writable-path statics and
    // options may already have been destroyed.
    if (services_.cvarSystem)
    {
        services_.cvarSystem->SaveUserFile("assets/configs/cvar_user.json");
    }

    if (services_.localization)
    {
        services_.localization->SaveToTxt(fmt::format("assets/locale/{}.txt", options_->locale));
    }

    if (agentControl_) { agentControl_->Stop(); agentControl_.reset(); }

    if (services_.physics)
    {
        services_.physics->CompleteTick();
    }
    
    //if (!GOption->FastExit)
    {
        Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();
        Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
    }

    if (services_.audio)
    {
        services_.audio->Stop();
    }

    if (services_.physics)
    {
        services_.physics->Stop();
    }
    if (gameInstance_)
    {
        gameInstance_->OnDestroy();
    }
    if (renderer_)
    {
        renderer_->End();
    }
    userInterface_.reset();

    if (services_.localization)
    {
        services_.localization->SaveToTxt(fmt::format("assets/locale/{}.txt", options_->locale));
    }

    renderFrameConsumers_.clear();
}

void NextEngine::AddRenderFrameConsumer(std::unique_ptr<Runtime::IRenderFrameConsumer> consumer)
{
    if (consumer)
    {
        renderFrameConsumers_.push_back(std::move(consumer));
    }
}

void NextEngine::AddTimerTask(double delay, DelayedTask task)
{
    taskQueues_.delayed.push_back({frameState_.time + delay, delay, task});
}

glm::dvec2 NextEngine::GetMousePos()
{
    return inputState_.mousePos;
}

void NextEngine::RequestClose()
{
    closeRequested_ = true;
    if (window_)
    {
        window_->Close();
    }
}

void NextEngine::RequestExit(int exitCode)
{
    requestedExitCode_ = exitCode;
    RequestClose();
}

void NextEngine::RequestMinimize() { window_->Minimize(); }

bool NextEngine::IsBorderlessFullscreen() const
{
    if (!window_)
    {
        return config_.userSettings.BorderlessFullscreen;
    }

    return window_->IsBorderlessFullscreen();
}

bool NextEngine::SetBorderlessFullscreen(bool enable)
{
    config_.userSettings.BorderlessFullscreen = enable;
    if (!window_)
    {
        return true;
    }

    return window_->SetBorderlessFullscreen(enable);
}

bool NextEngine::ToggleBorderlessFullscreen()
{
    if (!window_)
    {
        config_.userSettings.BorderlessFullscreen = !config_.userSettings.BorderlessFullscreen;
        return true;
    }

    const bool success = window_->ToggleBorderlessFullscreen();
    if (success)
    {
        config_.userSettings.BorderlessFullscreen = window_->IsBorderlessFullscreen();
    }
    return success;
}

void NextEngine::ConfigureCustomTitleBarDrag(bool enabled, float titleBarHeight, float leftReservedWidth,
                                             float rightReservedWidth)
{
    if (!window_)
    {
        return;
    }

    float coordinateScale = 1.0f;
#if WIN32
    coordinateScale = std::max(1.0f, window_->ContentScale());
#endif
    const int titleBarHeightInt = std::max(0, static_cast<int>(std::lround(titleBarHeight * coordinateScale)));
    const int leftReservedWidthInt =
        std::max(0, static_cast<int>(std::lround(leftReservedWidth * coordinateScale)));
    const int rightReservedWidthInt =
        std::max(0, static_cast<int>(std::lround(rightReservedWidth * coordinateScale)));
    window_->ConfigureCustomTitleBarDrag(enabled, titleBarHeightInt, leftReservedWidthInt, rightReservedWidthInt);
}

bool NextEngine::IsMaximized() { return window_->IsMaximumed(); }

void NextEngine::ToggleMaximize()
{
    if (window_->IsMaximumed())
    {
        window_->Restore();
    }
    else
    {
        window_->Maximum();
    }
}

void NextEngine::RequestScreenShot(FScreenShotSpec spec)
{
    ++screenShot_.queuedRequests;
    AddTickedTask([this, spec = std::move(spec)](double) mutable {
        if (screenShot_.hasPending ||
            (!spec.allowOverlappingExports &&
             (screenShot_.exportPending || screenShot_.asyncExportsInFlight > 0)))
        {
            return false;
        }

        --screenShot_.queuedRequests;
        if (spec.accumulateFrames > 0)
        {
            screenShot_.previousProgressiveEnabled = progressiveRender_.enabled;
            screenShot_.captureFramesRemaining = spec.accumulateFrames;
            spec.filename = ResolveScreenShotFilename(spec.filename, "hq_screenshot");

            progressiveRender_.enabled = true;
            progressiveRender_.accumulatedFrames = 0;
            spdlog::info("High quality capture started: accumulating {} frames...",
                         spec.accumulateFrames);
        }
        else
        {
            spec.filename = ResolveScreenShotFilename(spec.filename, "screenshot");
            screenShot_.captureFramesRemaining = 0;
        }
        screenShot_.pending = std::move(spec);
        screenShot_.captureSubmitted = false;
        screenShot_.captureSubmitSerial = 0;
        screenShot_.hasPending = true;
        return true;
    });
}

bool NextEngine::ShouldCaptureScreenShotThisFrame() const
{
    return screenShot_.hasPending && !screenShot_.captureSubmitted &&
        screenShot_.captureFramesRemaining <= 1;
}

void NextEngine::AdvanceScreenShotCapture()
{
    if (!screenShot_.hasPending)
    {
        return;
    }

    if (screenShot_.captureFramesRemaining > 1)
    {
        --screenShot_.captureFramesRemaining;
        return;
    }

    screenShot_.captureFramesRemaining = 0;
    if (!screenShot_.captureSubmitted)
    {
        if (renderer_->IsScreenShotCaptureReady())
        {
            screenShot_.captureSubmitted = true;
            screenShot_.captureSubmitSerial = renderer_->ScreenShotCaptureSubmitSerial();
        }
        return;
    }

    if (renderer_->CompletedSubmitSerial() < screenShot_.captureSubmitSerial)
    {
        return;
    }

    screenShot_.exportPending = true;
    ++screenShot_.asyncExportsInFlight;
    const bool exitAfterCapture = screenShot_.pending.exitAfterCapture;
    SaveScreenShot(screenShot_.pending);
    if (screenShot_.pending.accumulateFrames > 0)
    {
        spdlog::info("High quality capture queued: {} ({} frames accumulated)",
                     screenShot_.pending.filename, screenShot_.pending.accumulateFrames);

        progressiveRender_.enabled = screenShot_.previousProgressiveEnabled;
    }
    screenShot_.hasPending = false;
    screenShot_.pending = {};
    if (exitAfterCapture)
    {
        SPDLOG_INFO("Screenshot capture completed; requesting agent exit");
        RequestExit(0);
    }
}

void NextEngine::SaveScreenShot(const FScreenShotSpec& spec)
{
    const bool allowOverlappingExports = spec.allowOverlappingExports;
    if (!screenShotService_)
    {
        SPDLOG_ERROR("Screenshot export requested without the NextCapture module");
        screenShot_.exportPending = false;
        if (screenShot_.asyncExportsInFlight > 0)
        {
            --screenShot_.asyncExportsInFlight;
        }
        if (spec.exitAfterCapture)
        {
            RequestExit(1);
        }
        return;
    }
    screenShotService_->SaveSwapChainToFile(
        renderer_.get(), spec.filename, spec.x, spec.y, spec.width, spec.height,
        spec.fileFormat,
        spec.sync,
        [this, allowOverlappingExports]()
        {
            if (!allowOverlappingExports)
            {
                screenShot_.exportPending = false;
            }
            if (screenShot_.asyncExportsInFlight > 0)
            {
                --screenShot_.asyncExportsInFlight;
            }
        },
        [this, allowOverlappingExports]()
        {
            if (allowOverlappingExports)
            {
                screenShot_.exportPending = false;
            }
        });
}

glm::ivec2 NextEngine::GetMonitorSize() const
{
    glm::ivec2 size{1920, 1080};

    SDL_Rect rect;
    SDL_DisplayID id = SDL_GetPrimaryDisplay();
    if (!SDL_GetDisplayUsableBounds(id, &rect))
    {
        SDL_GetDisplayBounds(id, &rect);
    }
    size.x = rect.w;
    size.y = rect.h;

    return size;
}

void NextEngine::RayCast(glm::vec3 rayOrigin, glm::vec3 rayDir,
                            std::function<bool(Assets::RayCastResult rayResult)> callback)
{
    // CPU Raycast in scene
    Assets::RayCastResult result = scene_->GetCPUAccelerationStructure().RayCastInCPU(rayOrigin, rayDir);
    callback(result);
}

void NextEngine::SetProgressiveRendering(bool enable)
{
    if (progressiveRender_.enabled == enable)
    {
        return;
    }

    progressiveRender_.enabled = enable;
    progressiveRender_.accumulatedFrames = 0;
}

void NextEngine::ResetProgressiveRenderingAccumulation()
{
    progressiveRender_.accumulatedFrames = 0;
}

bool NextEngine::SetReferenceMode(const bool enabled)
{
    if (options_ == nullptr || options_->ReferenceMode == enabled)
    {
        return false;
    }

    options_->ReferenceMode = enabled;
    ApplyReferenceModeFromOptions();
    return true;
}

void NextEngine::ApplyReferenceModeFromOptions()
{
    if (renderer_ == nullptr)
    {
        return;
    }

    renderer_->RenderViews().InvalidateAllTemporalHistory(Vulkan::EHistoryInvalidationReason::RendererChanged);
    renderer_->RequestRecreateSwapChain();
    ResetProgressiveRenderingAccumulation();
    GkProfiling::Message(fmt::format("Reference comparison {}", options_->ReferenceMode ? "enabled" : "disabled"));
}

bool NextEngine::RequestRendererType(const Vulkan::ERendererType type)
{
    if (!renderer_)
    {
        return false;
    }

    const Vulkan::ERendererType resolved =
        Rendering::ResolveRendererChoice(type, renderer_->RendererChoiceCapabilities());
    const bool changed = config_.userSettings.RendererType != static_cast<int32_t>(resolved) ||
        renderer_->CurrentLogicRendererType() != resolved;
    config_.userSettings.RendererType = static_cast<int32_t>(resolved);
    if (renderer_->CurrentLogicRendererType() != resolved)
    {
        renderer_->SwitchLogicRenderer(resolved);
    }
    return changed;
}

bool NextEngine::SetUpscalerConfiguration(Rendering::Upscaler::EUpscalerType type, uint32_t mode)
{
    Runtime::Config::UserSettings& settings = config_.userSettings;
    const bool changed = settings.UpscalerType != static_cast<int32_t>(type) || settings.SuperResolution != mode;
    if (!changed)
    {
        return false;
    }

    settings.UpscalerType = static_cast<int32_t>(type);
    settings.SuperResolution = mode;
    ApplyUpscalerConfigurationFromSettings();
    return true;
}

bool NextEngine::ApplyUpscalerConfigurationFromSettings()
{
    if (!renderer_)
    {
        return false;
    }

    Runtime::Config::UserSettings& settings = config_.userSettings;
    auto type = static_cast<Rendering::Upscaler::EUpscalerType>(settings.UpscalerType);
    uint32_t mode = settings.SuperResolution;
    if (type >= Rendering::Upscaler::EUpscalerType::Count ||
        (type != Rendering::Upscaler::EUpscalerType::None && !renderer_->SupportsUpscaler(type)))
    {
        type = renderer_->SupportsUpscaler(Rendering::Upscaler::EUpscalerType::NativeTAAU)
            ? Rendering::Upscaler::EUpscalerType::NativeTAAU
            : Rendering::Upscaler::EUpscalerType::None;
    }
    if (mode > static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Auto))
    {
        mode = static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Quality);
    }
    settings.UpscalerType = static_cast<int32_t>(type);
    settings.SuperResolution = mode;
    if (!renderer_->SupportsFrameGeneration(type))
    {
        settings.FrameGeneration = false;
    }
    renderer_->RequestRecreateSwapChain();
    return true;
}

VkDeviceAddress NextEngine::TryGetGPUAccelerationStructureAddress() const
{
    if (renderer_ && renderer_->SupportsRayTracing() && !renderer_->TLAS().empty())
    {
        return renderer_->TLAS()[0].GetDeviceAddress();
    }
    return -1;
}

VkAccelerationStructureKHR NextEngine::TryGetGPUAccelerationStructureHandle() const
{
    if (renderer_ && renderer_->SupportsRayTracing() && !renderer_->TLAS().empty())
    {
        return renderer_->TLAS()[0].Handle();
    }
    return nullptr;
}

void NextEngine::OnRendererDeviceSet()
{
    // Configure the texture policy before the initial HDR textures are queued. The
    // GlobalTexturePool is created immediately before this callback, so HDR loads
    // can start at their lowest mip instead of being demoted one per tick later.
    if (auto* texturePool = Assets::GlobalTexturePool::GetInstance())
    {
        texturePool->SetHdrStreamingPolicy([this]() { return config_.userSettings.StreamHDRTextures; });
        texturePool->SetHdrShUpdatedCallback([this]()
        {
            if (scene_)
            {
                scene_->UpdateHDRSH();
            }
            if (renderer_)
            {
                renderer_->OnHdrShUpdated();
            }
        });
    }

    // Environment maps exist to be sampled; a profile that binds no scene textures would upload
    // eleven of them for nothing. The scene itself is still built and committed below either way,
    // so nothing downstream has to special-case a missing renderer scene.
    if (renderer_->BindlessProfile().bindsSceneTextures)
    {
        // global textures
        // texture id 0: dynamic hdri sky
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/river_road_2.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/canary_wharf_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/kloppenheim_01_puresky_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/kloppenheim_07_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/std_env.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/rainforest_trail_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/studio_small_03_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/studio_small_09_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/sunset_fairway_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/umhlanga_sunrise_1k.hdr");
        Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/shanghai_bund_1k.hdr");
    }

    // Single path on purpose: the renderer must always own a live scene, or every consumer that
    // reaches GetScene() needs its own guard. Scene sizes its own ambient arena from the registered
    // renderers' requirements, so a compatibility device gets the right-sized allocation for free.
    scene_.reset(new Assets::Scene(renderer_->CommandPool(), renderer_->SupportsRayTracing()));
    CommitSceneToRenderer({.rebuildMeshBuffer = false,
                           .resetFrameCounter = false,
                           .refreshSwapChainResources = false});

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextEngine::OnRendererCreateSwapChain()
{
    if (window_ && window_->IsHeadless() && !options_->AgentValidation)
    {
        SPDLOG_INFO("Headless surface disables the SDL/ImGui UI backend outside agent validation");
        return;
    }
    if (window_ && window_->IsHeadless())
    {
        SPDLOG_INFO("Headless agent validation enables ImGui rendering without an SDL platform backend");
    }
    if (userInterface_.get() == nullptr)
    {
        // ImGui platform viewports create additional SDL windows and Vulkan surfaces.
        // A VK_EXT_headless_surface has neither, so retain the main-swapchain UI
        // but do not initialize an application's multi-viewport backend.
        auto multiViewportBackend = window_->IsHeadless()
            ? std::unique_ptr<NextUI::IMultiViewportBackend>{}
            : gameInstance_->CreateMultiViewportBackend();
        if (userInterfaceFactory_)
        {
            userInterface_ = userInterfaceFactory_(
                *this,
                [this]() { gameInstance_->OnPreConfigUI(); },
                [this]() { gameInstance_->OnInitUI(); },
                std::move(multiViewportBackend));
        }
    }
    if (!userInterface_)
    {
        return;
    }
    if (uiOverlay_.get() == nullptr && uiOverlayFactory_)
    {
        uiOverlay_ = uiOverlayFactory_(*this);
    }
    userInterface_->OnCreateSurface(renderer_->SwapChain(), renderer_->DepthBuffer());
}

void NextEngine::OnRendererDeleteSwapChain()
{
    if (screenShot_.hasPending)
    {
        // Swapchain recreation invalidates the capture image and its submit
        // serial. Keep the request alive and retry it on the new swapchain.
        screenShot_.captureSubmitted = false;
        screenShot_.captureSubmitSerial = 0;
    }
    if (userInterface_.get() != nullptr)
    {
        userInterface_->OnDestroySurface();
    }
    for (const auto& consumer : renderFrameConsumers_)
    {
        consumer->OnRendererDeleteSwapChain();
    }
}

void NextEngine::OnRendererPostLoadScene()
{
    for (const auto& consumer : renderFrameConsumers_)
    {
        consumer->OnRendererPostLoadScene();
    }
}

void NextEngine::OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    SCOPED_CPU_TIMER("post render");
    const bool suppressAllUi = ShouldCaptureScreenShotThisFrame() && !screenShot_.pending.includeUi &&
        (screenShot_.pending.forceUiHidden ||
         !gameInstance_ || !gameInstance_->ShouldRenderUiDuringScreenshot());

    if (userInterface_)
    {
        SCOPED_CPU_TIMER("imgui record");
        userInterface_->RenderPreparedDrawData(commandBuffer, renderer_->SwapChain(), imageIndex, suppressAllUi);
    }

    if (!renderFrameConsumers_.empty())
    {
        SCOPED_CPU_TIMER("frame consumers");
        for (const auto& consumer : renderFrameConsumers_)
        {
            consumer->RecordFrame(commandBuffer, imageIndex, *renderer_);
        }
    }
}

void NextEngine::OnRendererAfterSubmit()
{
    if (!userInterface_)
    {
        return;
    }

    SCOPED_CPU_TIMER("ui prepare");
    static double lastTimestamp = 0.0;
    double now = GetWindow().GetTime();

    // Record delta time between calls to Render.
    if (frameState_.totalFrames % 30 == 0)
    {
        const auto timeDelta = now - frameState_.lastFrameTime;
        frameState_.lastFrameTime = now;
        frameState_.frameRate = static_cast<float>(30 / timeDelta);
    }

    // Render the UI
    NextUI::Statistics stats = {};

    stats.FrameTime = static_cast<float>((now - lastTimestamp) * 1000.0);
    lastTimestamp = now;

    stats.Stats["gpu"] = renderer_->Device().DeviceProperties().deviceName;

    stats.FramebufferSize = GetWindow().FramebufferSize();
    stats.RenderSize = renderer_->SwapChain().RenderExtent();
    stats.FrameRate = frameState_.frameRate;
    stats.RenderTime = GetTime();

    stats.TotalFrames = frameState_.totalFrames;
    stats.InstanceCount = static_cast<uint32_t>(scene_->GetNodeProxies().size());
    stats.NodeCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    const auto& gpuDrivenStat = scene_->GetGpuDrivenStat();
    const uint32_t visibleTriangleCount = gpuDrivenStat.TriangleCount > gpuDrivenStat.CulledTriangleCount
        ? gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount
        : 0u;
    const auto memoryStats = renderer_->Device().CaptureMemoryStats();
    GkProfiling::Plot("FPS", static_cast<int64_t>(std::lround(frameState_.frameRate)));
    GkProfiling::Plot("Draw calls", static_cast<int64_t>(gpuDrivenStat.VisibleCount));
    GkProfiling::Plot("Triangles", static_cast<int64_t>(visibleTriangleCount));
    GkProfiling::Plot("VRAM used", static_cast<int64_t>(memoryStats.deviceLocalUsageBytes));

    // Renderer::visualDebug_ = config_.userSettings.ShowVisualDebug;
    {
        SCOPED_CPU_TIMER("pre render");
        userInterface_->PreRender();
        if (uiOverlay_)
        {
            uiOverlay_->BeginFrame();
        }
    }
    const bool suppressAllUi = ShouldCaptureScreenShotThisFrame() && !screenShot_.pending.includeUi &&
        (screenShot_.pending.forceUiHidden ||
         !gameInstance_ || !gameInstance_->ShouldRenderUiDuringScreenshot());
    NextUI::FUiFrameResult uiResult{NextUI::EUiDeveloperLayer::None};
    NextGameInstanceBase::FGameUiFrameContext uiContext;
    uiContext.surfaceKind = NextGameInstanceBase::FGameUiFrameContext::ESurfaceKind::MainWindow;
    uiContext.framebufferExtent = renderer_->SwapChain().OutputExtent();
    uiContext.viewCamera = &scene_->GetRenderCamera();
    uiContext.allowWindowCommands = true;
    uiContext.policy.allowApplicationUi = !suppressAllUi;
    uiContext.policy.allowedDeveloperLayers = suppressAllUi
        ? NextUI::EUiDeveloperLayer::None
        : NextUI::EUiDeveloperLayer::All;
    if (uiContext.policy.allowApplicationUi)
    {
        SCOPED_CPU_TIMER("game ui");
        uiResult = gameInstance_->RenderUiFrame(uiContext);
    }
    if (uiOverlay_ && !suppressAllUi)
    {
        SCOPED_CPU_TIMER("overlay render");
        uiOverlay_->RenderFrame();
    }
    {
        if (config_.showFlags.DebugPhysicsOverlay)
        {
            SCOPED_CPU_TIMER("physics debug ui");
            Assets::Camera debugCamera = scene_->GetRenderCamera();
            gameInstance_->OverrideRenderCamera(debugCamera);
            if (debugUiProvider_)
            {
                debugUiProvider_->DrawPhysicsOverlay(*scene_, debugCamera);
            }
            gameInstance_->DrawAdditionalPhysicsDebugOverlay(debugCamera);
        }
        if (debugUiProvider_ && config_.showFlags.DebugCVarPanel)
        {
            SCOPED_CPU_TIMER("cvar editor ui");
            debugUiProvider_->DrawCVarEditor(*this, config_.showFlags.DebugCVarPanel);
        }
        if (debugUiProvider_ && config_.showFlags.DebugProfileOverlay)
        {
            SCOPED_CPU_TIMER("profile debug ui");
            debugUiProvider_->DrawProfileOverlay(*this, stats,
                                                 gameInstance_->GetGraphicsDebugPanelTopOffset());
        }
    }
    const NextUI::EUiDeveloperLayer developerLayers =
        uiResult.requestedDeveloperLayers & uiContext.policy.allowedDeveloperLayers;
    if (developerLayers != NextUI::EUiDeveloperLayer::None)
    {
        SCOPED_CPU_TIMER("overlay ui");
        NextUI::FUiFrameDispatcher::DrawDeveloperLayers(
            *this, stats, developerLayers, config_.showFlags.DebugProfileOverlay);
    }
    if (debugUiProvider_)
    {
        SCOPED_CPU_TIMER("graphics debug ui");
        debugUiProvider_->DrawGraphicsPanel(*this, config_.showFlags.DebugGraphicsPanel,
                                            gameInstance_->GetGraphicsDebugPanelTopOffset());
    }
    {
        SCOPED_CPU_TIMER("imgui prepare draw data");
        userInterface_->PrepareDrawData();
    }
}

void NextEngine::OnRendererBeforeNextFrame()
{
    SCOPED_CPU_TIMER("task coordinator");
    Tasks::TaskCoordinator::GetInstance()->Tick();
}
