#include "Engine.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/EngineCVars.hpp"
#include "Engine/Runtime/Subsystems/QuickJSEngine.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"
#include "Engine/Runtime/Subsystems/NextLocalization.h"
#include "Engine/Runtime/Subsystems/VoiceInputService.hpp"
#include "Engine/Runtime/Command/DeleteNodesCommand.hpp"
#include "Engine/Runtime/Command/DuplicateNodesCommand.hpp"
#include "Engine/Runtime/ScreenShot.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Editor/ConsoleLogBuffer.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Utilities/GraphicsDebugPanel.hpp"
#include "Engine/Runtime/Utilities/PhysicsDebugOverlay.hpp"
#include "Engine/Runtime/Utilities/ProfileDebugOverlay.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Vulkan/ShaderHotReloader.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Localization.hpp"

#define _USE_MATH_DEFINES
#include <math.h>

#include <entt/meta/factory.hpp>

#define BUILDVER(X) std::string buildver(#X);
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Runtime/Platform/PlatformCommon.h"
#include "build.version"

#include "Engine/Common/CoreMinimal.hpp"
#include "Reflection/ReflectionRegistry.h"

// spdlog logging
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#if ANDROID
#include <spdlog/sinks/android_sink.h>
#endif

Runtime::Config::Options* GOption = nullptr;

void NextEngine::RegisterReflection()
{
    using namespace entt::literals;

    entt::meta_factory<NextEngine>()
        .type("NextEngine"_hs)
        .func<&NextEngine::GetTotalFrames>("GetTotalFrames")
        .func<&NextEngine::RegisterJSCallback>("RegisterJSCallback");
}

namespace
{
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
        return GetDriverId(physicalDevice) == VK_DRIVER_ID_MESA_KOSMICKRISP;
    }

    Vulkan::ERendererType ResolveRendererType(
        Vulkan::ERendererType requestedType,
        bool supportsRayTracing,
        bool hasFullAmbientCubeBudget)
    {
        if (!supportsRayTracing && Vulkan::GetRendererRequirements(requestedType).requestRayTracing)
        {
            requestedType = Vulkan::ERT_ModernDeferred;
        }
        if (!hasFullAmbientCubeBudget && Vulkan::GetRendererRequirements(requestedType).requestAmbientCube)
        {
            return Vulkan::ERT_LegacyDeferredNoAmbient;
        }
        return requestedType;
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
        const VkDeviceSize fullAmbientCubeAllocationSize =
            static_cast<VkDeviceSize>(cascadeCount) * perCascadeCount *
                (sizeof(Assets::VoxelData) + sizeof(Assets::AmbientCube)) +
            static_cast<VkDeviceSize>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT * sizeof(Assets::PageIndex) +
            perCascadeCount * (sizeof(Assets::AmbientCube) + sizeof(glm::u32vec4));
        return largestDeviceLocalHeapSize >= fullAmbientCubeAllocationSize;
    }

    std::string ResolveScreenShotFilename(const std::string& requestedFilename, const char* defaultPrefix)
    {
        if (!requestedFilename.empty())
        {
            return requestedFilename;
        }

        const auto now = std::time(nullptr);
        return fmt::format("{}_{:%Y-%m-%d-%H-%M-%S}", defaultPrefix, *std::localtime(&now));
    }
} // namespace

// RegisterEngineCVars moved to Runtime/Config/EngineCVars.*

namespace NextRenderer
{
    std::string GetBuildVersion() { return buildver; }

    Vulkan::VulkanBaseRenderer* CreateRenderer(uint32_t rendererType, Vulkan::Window* window,
                                               const VkPresentModeKHR presentMode, const bool enableValidationLayers)
    {
        std::vector<const char*> validationLayers;
        if (enableValidationLayers)
        {
            validationLayers.push_back("VK_LAYER_KHRONOS_validation");
        }
#if WITH_STREAMLINE
        if (StreamlineWrapper::ShouldInitialize())
        {
            StreamlineWrapper::Initialize();
        }
        else
        {
            SPDLOG_INFO("Streamline DLSS plugins disabled because no NVIDIA adapter is present");
        }
#endif
        Vulkan::Instance* instance = new Vulkan::Instance(*window, validationLayers, VK_API_VERSION_1_2);

        const auto& physicalDevices = instance->PhysicalDevices();
        const uint32_t selectedGpuIdx = GOption->GpuIdx < physicalDevices.size() ? GOption->GpuIdx : 0;
        if (GOption->HardwareQuery && IsKosmicKrispDriver(physicalDevices[selectedGpuIdx]))
        {
            SPDLOG_WARN("KosmicKrisp detected; disabling Vulkan timestamp queries to avoid device-loss on macOS");
            GOption->HardwareQuery = false;
        }
        const bool hasFullAmbientCubeBudget = HasFullAmbientCubeBudget(physicalDevices[selectedGpuIdx]);
        const bool useRayTracingRenderer =
            hasFullAmbientCubeBudget && !GOption->ForceNoRT &&
            instance->SupportsRayQuery(physicalDevices[selectedGpuIdx]);

        std::vector<Vulkan::ERendererType> supportedTypes;
        if (hasFullAmbientCubeBudget)
        {
            supportedTypes = {Vulkan::ERT_ModernDeferred, Vulkan::ERT_LegacyDeferred,
                              Vulkan::ERT_VoxelTracing, Vulkan::ERT_LegacyDeferredNoAmbient};
        }
        else
        {
            supportedTypes = {Vulkan::ERT_LegacyDeferredNoAmbient};
        }
        Vulkan::VulkanBaseRenderer* renderer =
            new Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers, instance);
        if (useRayTracingRenderer)
        {
            supportedTypes.emplace_back(Vulkan::ERT_PathTracing);
        }

        for (auto type : supportedTypes)
        {
            renderer->RegisterLogicRenderer(type);
        }

        auto requestedType =
            ResolveRendererType(static_cast<Vulkan::ERendererType>(rendererType), useRayTracingRenderer,
                                hasFullAmbientCubeBudget);
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
    struct SceneTaskContext
    {
        bool success;
        float elapsed;
        std::array<char, 256> outputInfo;
    };
} // namespace

Runtime::Config::UserSettings CreateUserSettings(const Runtime::Config::Options& options)
{
    (void)options;
    Runtime::Scene::SceneList::ScanScenes();

    Runtime::Config::UserSettings userSettings{};

    userSettings.RendererType = 0;
    userSettings.SceneIndex = 0;
    userSettings.CameraIdx = 0;

    userSettings.NumberOfSamples = 8;
    userSettings.NumberOfBounces = 5;
    userSettings.MaxNumberOfBounces = 10;

    userSettings.AdaptiveSample = false;
    userSettings.AdaptiveVariance = 6.0f;
    userSettings.AdaptiveSteps = 4;

    userSettings.TAA = true;

    userSettings.ShowSettings = true;
    userSettings.ShowOverlay = true;
    userSettings.BorderlessFullscreen = options.Fullscreen;

    userSettings.HeatmapScale = 1.0f;

    userSettings.UseCheckerBoardRendering = false;
    userSettings.TemporalFrames = 16;

    userSettings.Denoiser = false;
    userSettings.DenoiseSigma = 0.5f;
    userSettings.DenoiseSigmaLum = 10.0f;
    userSettings.DenoiseSigmaNormal = 0.1f;
    userSettings.DenoiseSize = 5;

    userSettings.PaperWhiteNit = 600.f;
    
    userSettings.SuperResolution = 0;
    userSettings.DLSS = false;
    userSettings.DLSSRR = false;

    userSettings.BakeSpeedLevel = 1;

    userSettings.TickPhysics = true;
    userSettings.TickAnimation = true;
    userSettings.SceneEpsilonScale = 1.0f;
    userSettings.AmbientCubeUnit = Assets::CUBE_UNIT;
    userSettings.AmbientCubeOffsetX = 0.0f;
    userSettings.AmbientCubeOffsetY = 0.0f;
    userSettings.AmbientCubeOffsetZ = 0.0f;
    userSettings.AmbientCubeCascadeCount = 3;
    userSettings.AmbientCubeCascadeRatio = 2.0f;

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

#if ANDROID
    std::string tag = "gknext";
    auto android_logger = spdlog::android_logger_mt("android", tag);
    android_logger->critical("Use \"adb shell logcat\" to view this message.");
    spdlog::set_default_logger(android_logger);
#endif
    Runtime::Editor::AttachConsoleLogSinkToDefaultLogger();

    SPDLOG_INFO("---- Next Engine Initializing...");
    spdlog::stopwatch stopwatch;

    instance_ = this;
    
    // Initialize reflection system first
    Reflection::RegisterAllReflection();

    status_ = NextRenderer::EApplicationStatus::Starting;

    agentValidation_.active = options.AgentValidation;
    agentValidation_.waitFrames = options.AgentValidationFrames;
    agentValidation_.outputPath = options.AgentValidationOutput;

    services_.packageFileSystem.reset(new Utilities::Package::FPackageFileSystem(Utilities::Package::EPM_OsFile));

    // Optional pak: assets moved out of the repo to reduce its size. Mounted automatically when present
    // so LoadFile can fall back to it for files missing on disk (see FileHelper::LoadFile).
    {
        const std::string optionalPakPath = Utilities::FileHelper::GetPlatformFilePath("assets/paks/optional.pak");
        std::error_code ec;
        if (std::filesystem::exists(optionalPakPath, ec))
        {
            services_.packageFileSystem->MountPak(optionalPakPath);
        }
    }

    services_.aiService = std::make_unique<NextAI::FAIService>();

    Vulkan::Window::InitGLFW();
    // Create Window
    Vulkan::WindowConfig windowConfig{"gkNextRenderer " + NextRenderer::GetBuildVersion(),
                                      options.Width,
                                      options.Height,
                                      options.Fullscreen,
                                      options.Fullscreen,
                                      !options.Fullscreen,
                                      options.SaveFile,
                                      userdata,
                                      options.ForceSDR};
    gameInstance_ = CreateGameInstance(windowConfig, options, this);
    config_.userSettings = CreateUserSettings(options);
    services_.cvarSystem = std::make_unique<NextCVar::FCVarSystem>();
    NextCVar::RegisterEngineCVars(*services_.cvarSystem, config_.userSettings, config_.showFlags, this);
    services_.cvarSystem->LoadDefaultFile("assets/configs/cvar_default.json");
    gameInstance_->ApplyDefaultCVars(*services_.cvarSystem);
    services_.cvarSystem->LoadUserFile("assets/configs/cvar_user.json");
    windowConfig.Fullscreen = config_.userSettings.BorderlessFullscreen;
    // Hide the window for agent validation captures and for any caller that asked for it (e.g. the
    // unit-test engine fixture). The capture+auto-exit state machine stays gated on AgentValidation.
    windowConfig.HiddenWindow = options.AgentValidation || options.HiddenWindow;
    window_.reset(new Vulkan::Window(windowConfig));
    SetBorderlessFullscreen(config_.userSettings.BorderlessFullscreen);
    services_.quickJSEngine = std::make_unique<QuickJSEngine>();

    services_.localization = std::make_unique<NextLocalization>();
    services_.localization->LoadFromTxt(fmt::format("assets/locale/{}.txt", options_->locale), options_->locale);

    SPDLOG_INFO("---- Next Engine Initialized in {}", stopwatch.elapsed_ms());
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
        status.shaderHotReloadEnabled = shaderStatus.enabled;
        status.shaderInitialized = shaderStatus.initialized;
        status.shaderPollIntervalSeconds = shaderStatus.pollIntervalSeconds;
        status.shaderSourceRoot = shaderStatus.sourceRoot;
        status.shaderOutputRoot = shaderStatus.outputRoot;
        status.shaderCompiler = shaderStatus.slangExecutable;
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
    if (services_.cvarSystem)
    {
        services_.cvarSystem->SaveUserFile("assets/configs/cvar_user.json");
    }

    if (services_.localization)
    {
        services_.localization->SaveToTxt(fmt::format("assets/locale/{}.txt", options_->locale));
    }

    scene_.reset();
    renderer_.reset();
    window_.reset();

    Vulkan::Window::TerminateGLFW();
}

void NextEngine::Start()
{
    PERFORMANCEAPI_INSTRUMENT_FUNCTION();

    SPDLOG_INFO("---- Next Engine Starting...");
    spdlog::stopwatch stopwatch;

    // Initialize Renderer
    bool shouldEnableValidation = GOption->Validation;
    
    // Agent validation renders as fast as the GPU allows (uncapped) so the fixed frame budget is
    // reached in a fraction of the wall-clock time a vsync-locked present mode would take.
    const VkPresentModeKHR presentMode = options_->AgentValidation
                                             ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                             : static_cast<VkPresentModeKHR>(options_->PresentMode);
    renderer_.reset(NextRenderer::CreateRenderer(static_cast<uint32_t>(config_.userSettings.RendererType), window_.get(),
                                                 presentMode,
                                                 shouldEnableValidation));
    config_.userSettings.RendererType = static_cast<int32_t>(renderer_->CurrentLogicRendererType());

    auto& rendererDelegates = renderer_->GetDelegates();
    rendererDelegates.onDeviceSet = [this]() -> void { OnRendererDeviceSet(); };
    rendererDelegates.createSwapChain = [this]() -> void { OnRendererCreateSwapChain(); };
    rendererDelegates.deleteSwapChain = [this]() -> void { OnRendererDeleteSwapChain(); };
    rendererDelegates.beforeNextTick = [this]() -> void { OnRendererBeforeNextFrame(); };
    rendererDelegates.getUniformBufferObject = [this](VkOffset2D offset,
                                                      VkExtent2D extend) -> Assets::UniformBufferObject
    { return GetUniformBufferObject(offset, extend); };
    rendererDelegates.postRender = [this](VkCommandBuffer commandBuffer, uint32_t imageIndex) -> void
    { OnRendererPostRender(commandBuffer, imageIndex); };

    renderer_->Start();
    auto resolvedRendererType = ResolveRendererType(
        renderer_->CurrentLogicRendererType(), renderer_->SupportsRayTracing(), renderer_->HasFullAmbientCubeBudget());
    if (resolvedRendererType != renderer_->CurrentLogicRendererType())
    {
        renderer_->SwitchLogicRenderer(resolvedRendererType);
        config_.userSettings.RendererType = static_cast<int32_t>(resolvedRendererType);
    }

#if GK_ENABLE_HOT_RELOAD
    if (options_->ShaderHotReload)
    {
        services_.shaderHotReloader = std::make_unique<Vulkan::ShaderHotReloader>();
        services_.shaderHotReloader->Initialize(*renderer_);
    }
#endif

    services_.physics.reset(new NextPhysics());
    services_.physics->Start();

    services_.audio = std::make_unique<NextAudio>();
    services_.audio->Start();

    services_.voiceInputService = std::make_unique<NextAI::VoiceInputService>();
    NextAI::FVoiceInputConfig voiceConfig;
    if (services_.aiService)
    {
        services_.aiService->TryGetVoiceInputConfig(voiceConfig);
    }
    services_.voiceInputService->Initialize(voiceConfig);

    if (services_.quickJSEngine)
    {
        services_.quickJSEngine->Initialize();
    }

    gameInstance_->OnInit();

    SPDLOG_INFO("---- Next Engine Started in {}", stopwatch.elapsed_ms());
}

bool NextEngine::HandleEvent(SDL_Event& event)
{
    userInterface_->HandleEvent(&event);

    if (services_.quickJSEngine)
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            services_.quickJSEngine->HandleInputEvent(event);
            break;
        default:
            break;
        }
    }

    switch (event.type)
    {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            return true;
        }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (renderer_)
        {
            renderer_->RequestRecreateSwapChain();
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        OnKey(event);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        OnMouseButton(event);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (window_ && SDL_GetWindowRelativeMouseMode(window_->Handle()))
        {
            OnCursorPosition(event.motion.xrel, event.motion.yrel);
        }
        else
        {
            OnCursorPosition(event.motion.x, event.motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        OnScroll(event.wheel.x, event.wheel.y);
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

    if (GpuTimer())
    {
        GpuTimer()->CpuFrameBegin();
    }

    {
        SCOPED_CPU_TIMER("engine");

        // make sure the output is flushed
        std::cout << std::flush;

        // Hot change renderer
        {
            SCOPED_CPU_TIMER("renderer switch");
            auto requestedRendererType =
                ResolveRendererType(static_cast<Vulkan::ERendererType>(config_.userSettings.RendererType),
                                    renderer_->SupportsRayTracing(), renderer_->HasFullAmbientCubeBudget());
            if (requestedRendererType != static_cast<Vulkan::ERendererType>(config_.userSettings.RendererType))
            {
                config_.userSettings.RendererType = static_cast<int32_t>(requestedRendererType);
            }

            if (renderer_->CurrentLogicRendererType() != requestedRendererType)
            {
                renderer_->SwitchLogicRenderer(requestedRendererType);
            }
        }

        // delta time calc
        {
            SCOPED_CPU_TIMER("delta");
            const auto prevTime = frameState_.time;
            frameState_.time = GetWindow().GetTime();
            frameState_.deltaSeconds = frameState_.time - prevTime;
            if (forcingDelta)
                frameState_.deltaSeconds = 1.0 / 30.0;
            float invDelta = static_cast<float>(frameState_.deltaSeconds) / 60.0f;
            frameState_.smoothedDeltaSeconds =
                glm::mix(frameState_.smoothedDeltaSeconds, frameState_.deltaSeconds, invDelta * 100.0f);
        }

        TickHotReload();

        // Scene Update
        if (scene_)
        {
            SCOPED_CPU_TIMER("scene tick");
            scene_->Tick(static_cast<float>(frameState_.deltaSeconds));
        }

#if WITH_PHYSIC
        if (config_.userSettings.TickPhysics && services_.physics)
        {
            SCOPED_CPU_TIMER("physics");
            services_.physics->Tick(frameState_.deltaSeconds);
        }
#endif

        if (services_.quickJSEngine)
        {
            SCOPED_CPU_TIMER("quickjs");
            services_.quickJSEngine->Tick(frameState_.deltaSeconds);
        }

        // tick
        if (status_ == NextRenderer::EApplicationStatus::Running)
        {
            SCOPED_CPU_TIMER("game tick");
            gameInstance_->OnTick(frameState_.deltaSeconds);
        }

        {
            SCOPED_CPU_TIMER("ticked tasks");

            // Remove completed ticked tasks.
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

            // Run due delayed tasks and remove completed ones.
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

        {
            PERFORMANCEAPI_INSTRUMENT_COLOR("Engine::TickRenderer", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
            renderer_->DrawFrame();
        }
        frameState_.totalFrames = renderer_->FrameCount();

        if (screenShot_.hasPending)
        {
            SCOPED_CPU_TIMER("screenshot");
            renderer_->Device().WaitIdle();
            Runtime::ScreenShot::SaveSwapChainToFile(renderer_.get(),
                                           screenShot_.pending.filename,
                                           screenShot_.pending.x,
                                           screenShot_.pending.y,
                                           screenShot_.pending.width,
                                           screenShot_.pending.height);
            screenShot_.hasPending = false;
            screenShot_.pending = {};
        }

        if (progressiveRender_.warmupFramesRemaining > 0)
        {
            progressiveRender_.warmupFramesRemaining--;
            if (progressiveRender_.warmupFramesRemaining == 0)
            {
                progressiveRender_.enabled = true;
            }
        }

        // High quality capture: count down accumulated frames after DrawFrame
        if (screenShot_.captureFramesRemaining > 0)
        {
            SCOPED_CPU_TIMER("hq capture");
            screenShot_.captureFramesRemaining--;
            if (screenShot_.captureFramesRemaining == 0)
            {
                renderer_->Device().WaitIdle();
                Runtime::ScreenShot::SaveSwapChainToFile(renderer_.get(),
                                               screenShot_.captureSpec.filename,
                                               screenShot_.captureSpec.x,
                                               screenShot_.captureSpec.y,
                                               screenShot_.captureSpec.width,
                                               screenShot_.captureSpec.height);
                spdlog::info("High quality capture saved: {} ({} frames accumulated)",
                             screenShot_.captureSpec.filename, screenShot_.captureTotalFrames);

                progressiveRender_.enabled = screenShot_.previousProgressiveEnabled;
                progressiveRender_.warmupFramesRemaining = screenShot_.previousProgressiveWarmupFrames;
                screenShot_.captureSpec = {};
            }
        }

        // sample gamepad stats
        {
            SCOPED_CPU_TIMER("gamepad");
            TickGamepadInput();
        }

        if (agentValidation_.active)
        {
            TickAgentValidation();
        }
    }

    if (GpuTimer())
    {
        GpuTimer()->CpuFrameEnd();
    }
    return false;
}

void NextEngine::End()
{
    if (!GOption->FastExit)
    {
        Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();
        Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();
        Tasks::TaskCoordinator::DestroyInstance();
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
}

void NextEngine::RegisterJSCallback(std::function<void(double)> callback)
{
    if (services_.quickJSEngine)
    {
        services_.quickJSEngine->RegisterTickCallback(std::move(callback));
    }
}

void NextEngine::AddTimerTask(double delay, DelayedTask task)
{
    taskQueues_.delayed.push_back({frameState_.time + delay, delay, task});
}

glm::dvec2 NextEngine::GetMousePos()
{
    float fx{}, fy{};
    SDL_GetMouseState(&fx, &fy);
    return glm::dvec2(fx, fy);
}

void NextEngine::RequestClose()
{
    if (window_)
    {
        window_->Close();
    }
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

    const int titleBarHeightInt = std::max(0, static_cast<int>(titleBarHeight));
    const int leftReservedWidthInt = std::max(0, static_cast<int>(leftReservedWidth));
    const int rightReservedWidthInt = std::max(0, static_cast<int>(rightReservedWidth));
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
    if (spec.accumulateFrames > 0)
    {
        if (screenShot_.captureFramesRemaining > 0)
        {
            spdlog::warn("High quality capture already in progress, ignoring request");
            return;
        }

        screenShot_.previousProgressiveEnabled = progressiveRender_.enabled;
        screenShot_.previousProgressiveWarmupFrames = progressiveRender_.warmupFramesRemaining;
        screenShot_.captureTotalFrames = spec.accumulateFrames;
        screenShot_.captureFramesRemaining = spec.accumulateFrames;
        screenShot_.captureSpec = std::move(spec);
        screenShot_.captureSpec.filename =
            ResolveScreenShotFilename(screenShot_.captureSpec.filename, "hq_screenshot");

        progressiveRender_.enabled = true;
        progressiveRender_.warmupFramesRemaining = 0;
        spdlog::info("High quality capture started: accumulating {} frames...",
                     screenShot_.captureTotalFrames);
        return;
    }

    spec.filename = ResolveScreenShotFilename(spec.filename, "screenshot");
    if (spec.sync)
    {
        renderer_->Device().WaitIdle();
        Runtime::ScreenShot::SaveSwapChainToFile(renderer_.get(), spec.filename, spec.x, spec.y, spec.width, spec.height);
        return;
    }

    screenShot_.pending = std::move(spec);
    screenShot_.hasPending = true;
}

// 生成一个随机抖动偏移
glm::vec2 GenerateJitter(float screenWidth, float screenHeight)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-0.5f, 0.5f);

    float jitterX = static_cast<float>(dis(gen)) / screenWidth;
    float jitterY = static_cast<float>(dis(gen)) / screenHeight;

    return glm::vec2(jitterX, jitterY);
}

// 创建抖动矩阵
glm::mat4 CreateJitterMatrix(float jitterX, float jitterY)
{
    glm::mat4 jitterMatrix = glm::mat4(1.0f);
    jitterMatrix[3][0] = jitterX;
    jitterMatrix[3][1] = jitterY;
    return jitterMatrix;
}

// 调制投影矩阵
glm::mat4 RandomJitterProjectionMatrix(const glm::mat4& projectionMatrix, float screenWidth, float screenHeight)
{
    glm::vec2 jitter = GenerateJitter(screenWidth, screenHeight);
    glm::mat4 jitterMatrix = CreateJitterMatrix(jitter.x, jitter.y);
    return jitterMatrix * projectionMatrix;
}

// 生成Halton序列的单一维度
float HaltonSequence(int index, int base)
{
    float f = 1.0f;
    float result = 0.0f;
    while (index > 0)
    {
        f = f / base;
        result = result + f * (index % base);
        index = index / base;
    }
    return result;
}

// 生成2D Halton序列
std::vector<glm::vec2> GenerateHaltonSequence(int count)
{
    std::vector<glm::vec2> sequence;
    for (int i = 0; i < count; ++i)
    {
        float x = HaltonSequence(i + 1, 2); // 基数2
        float y = HaltonSequence(i + 1, 3); // 基数3
        sequence.push_back(glm::vec2(x, y));
    }
    return sequence;
}

glm::mat4 HaltonJitterProjectionMatrix(const glm::mat4& projectionMatrix, float screenWidth, float screenHeight)
{
    glm::vec2 jitter = GenerateJitter(screenWidth, screenHeight);
    glm::mat4 jitterMatrix = CreateJitterMatrix(jitter.x, jitter.y);
    return jitterMatrix * projectionMatrix;
}

glm::ivec2 NextEngine::GetMonitorSize() const
{
    glm::ivec2 size{1920, 1080};

    SDL_Rect rect;
    SDL_DisplayID id = SDL_GetPrimaryDisplay();
    SDL_GetDisplayBounds(id, &rect);
    size.x = rect.w;
    size.y = rect.h;

    return size;
}

void NextEngine::RayCastGPU(glm::vec3 rayOrigin, glm::vec3 rayDir,
                            std::function<bool(Assets::RayCastResult rayResult)> callback)
{
    // CPU Raycast in scene
    Assets::RayCastResult result = scene_->GetCPUAccelerationStructure().RayCastInCPU(rayOrigin, rayDir);
    callback(result);
}

void NextEngine::SetProgressiveRendering(bool enable, bool directly)
{
    if (directly)
    {
        progressiveRender_.enabled = enable;
        return;
    }

    if (enable)
    {
        if (progressiveRender_.warmupFramesRemaining == 0)
        {
            progressiveRender_.warmupFramesRemaining = config_.userSettings.TemporalFrames * 2;
        }
    }
    else
    {
        progressiveRender_.warmupFramesRemaining = 0;
        progressiveRender_.enabled = false;
    }
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

Assets::UniformBufferObject NextEngine::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent)
{
    Assets::UniformBufferObject ubo = {};

    // a copy, simple struct
    Assets::Camera renderCam = scene_->GetRenderCamera();
    gameInstance_->OverrideRenderCamera(renderCam);
    ubo.ModelView = renderCam.ModelView;

    scene_->OverrideModelView(ubo.ModelView);
    ubo.Projection =
        glm::perspective(glm::radians(renderCam.FieldOfView), extent.width / static_cast<float>(extent.height),
                         renderCam.NearPlane, renderCam.FarPlane);

    ubo.FastGather = config_.userSettings.FastGather;
    ubo.SelectedId = scene_->GetSelectedId();
    ubo.SuperResolution = GOption->ReferenceMode ? 2 : config_.userSettings.SuperResolution;
    ubo.Projection[1][1] *= -1;

    glm::mat4x4 projectionUnJit = ubo.Projection;
    // handle android vulkan pre rotation
#if ANDROID
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);

    ubo.Projection = glm::perspective(glm::radians(renderCam.FieldOfView),
                                      extent.height / static_cast<float>(extent.width), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
    ubo.Projection = pre_rotate_mat * ubo.Projection;

    projectionUnJit = ubo.Projection;
#endif

    if (config_.userSettings.TAA || config_.userSettings.DLSS)
    {
        std::vector<glm::vec2> haltonSeq = GenerateHaltonSequence(config_.userSettings.TemporalFrames);
        glm::vec2 jitter =
            haltonSeq[frameState_.totalFrames % config_.userSettings.TemporalFrames] - glm::vec2(0.5f, 0.5f);

        ubo.Projection[2][0] = jitter.x / static_cast<float>(extent.width) * 2.0f;
        ubo.Projection[2][1] = jitter.y / static_cast<float>(extent.height) * 2.0f;

        ubo.Jitter = glm::vec4(jitter.x, jitter.y, 0, 0);
    }
    else
    {
        ubo.Jitter = glm::vec4(0, 0, 0, 0);
    }

    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    ubo.ViewProjectionUnJit = projectionUnJit * ubo.ModelView;
    ubo.ProjectionUnJit = projectionUnJit;
    ubo.ProjectionInverseUnJit = glm::inverse(projectionUnJit);

    ubo.PrevViewProjection = renderState_.previousUniformBuffer.TotalFrames != 0
                                  ? renderState_.previousUniformBuffer.ViewProjection
                                  : ubo.ViewProjection;
    ubo.PrevViewProjectionUnJit = renderState_.previousUniformBuffer.TotalFrames != 0
                                      ? renderState_.previousUniformBuffer.ViewProjectionUnJit
                                      : ubo.ViewProjectionUnJit;

    ubo.ViewportRect =
        glm::vec4(renderer_->SwapChain().RenderOffset().x, renderer_->SwapChain().RenderOffset().y,
                  renderer_->SwapChain().RenderExtent().width, renderer_->SwapChain().RenderExtent().height);

    const glm::vec4 sunDirection = glm::vec4(scene_->GetEnvSettings().SunDirection(), 0.0f);
    const bool hasSun = scene_->GetEnvSettings().HasSun && scene_->GetEnvSettings().SunIntensity > 0;
    {
        const auto cascades = scene_->GetEnvSettings().ComputeSunCascades(
            ubo.ViewProjectionUnJit, renderCam.NearPlane, renderCam.FarPlane, 400.f);
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(renderer_->FrameCount(), 0));
        const bool forceRefresh = !renderState_.cachedSunCascadesValid ||
                                  (bool)renderState_.previousUniformBuffer.HasSun != hasSun ||
                                  renderState_.previousUniformBuffer.SunDirection != sunDirection;

        if (!hasSun)
        {
            renderState_.cachedSunCascadesValid = false;
            renderState_.sunShadowCascadeUpdateMask = 0u;
            renderState_.sunShadowInitializedMask = 0u;
            renderState_.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
        }
        else
        {
            if (!renderState_.cachedSunCascadesValid)
            {
                // 未初始化 cascade 对应的贴图已经被清成 depth=1，先给 UBO 一个有效矩阵。
                renderState_.cachedSunCascades = cascades;
            }
            if (forceRefresh)
            {
                renderState_.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
            }

            const uint32_t priorityCascadeMask =
                renderState_.sunShadowDirtyMask |
                (Assets::Scene::kSunShadowCascadeMask & ~renderState_.sunShadowInitializedMask);
            const uint32_t activeCascadeMask =
                Assets::Scene::BuildSunShadowCascadeUpdateMask(frameIndex, priorityCascadeMask);

            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) != 0u)
                {
                    renderState_.cachedSunCascades.viewProjection[cascade] = cascades.viewProjection[cascade];
                    renderState_.cachedSunCascades.splits[cascade] = cascades.splits[cascade];
                }
            }

            renderState_.sunShadowCascadeUpdateMask = activeCascadeMask;
            renderState_.sunShadowInitializedMask |= activeCascadeMask;
            renderState_.sunShadowDirtyMask &= ~activeCascadeMask;
            renderState_.cachedSunCascadesValid = true;
        }

        for (int i = 0; i < 4; ++i)
        {
            ubo.SunCascadeViewProjection[i] = renderState_.cachedSunCascades.viewProjection[i];
        }
        ubo.CascadeSplits = renderState_.cachedSunCascades.splits;
    }

    ubo.SelectedId = scene_->GetSelectedId();

    // Camera Stuff
    ubo.Aperture = renderCam.Aperture;
    ubo.FocusDistance = renderCam.FocalDistance;

    // SceneStuff
    ubo.SkyRotation = scene_->GetEnvSettings().SkyRotation;
    ubo.MaxNumberOfBounces = config_.userSettings.MaxNumberOfBounces;
    ubo.TotalFrames = frameState_.totalFrames;
    ubo.NumberOfSamples = config_.userSettings.NumberOfSamples;
    ubo.NumberOfBounces = config_.userSettings.NumberOfBounces;
    ubo.AdaptiveSample = config_.userSettings.AdaptiveSample;
    ubo.AdaptiveVariance = config_.userSettings.AdaptiveVariance;
    ubo.AdaptiveSteps = config_.userSettings.AdaptiveSteps;
    ubo.TAA = config_.userSettings.TAA;
    ubo.RandomSeed = rand();
    ubo.SunDirection = sunDirection;
    ubo.SunColor = glm::vec4(1, 1, 1, 0) * scene_->GetEnvSettings().SunIntensity;
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * scene_->GetEnvSettings().SkyIntensity;
    ubo.HasSky = scene_->GetEnvSettings().HasSky;
    ubo.HasSun = hasSun;

    if (ubo.HasSun != renderState_.previousUniformBuffer.HasSun ||
        ubo.SunDirection != renderState_.previousUniformBuffer.SunDirection)
    {
        scene_->MarkEnvDirty();
    }

    ubo.ShowHeatmap = config_.showFlags.ShowVisualDebug;
    ubo.HeatmapScale = config_.userSettings.HeatmapScale;
    ubo.DebugDraw_Lighting = config_.showFlags.DebugDraw_Lighting;
    ubo.DebugDraw_ShadowCascadeCoverage = config_.showFlags.DebugDraw_ShadowCascadeCoverage;
    ubo.UseCheckerBoard = config_.userSettings.UseCheckerBoardRendering;
    ubo.TemporalFrames = progressiveRender_.enabled ? 256 : config_.userSettings.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();

    ubo.PaperWhiteNit = config_.userSettings.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    ubo.BFSigma = config_.userSettings.DenoiseSigma;
    ubo.BFSigmaLum = config_.userSettings.DenoiseSigmaLum;
    ubo.BFSigmaNormal = config_.userSettings.DenoiseSigmaNormal;

    ubo.BFSize = config_.userSettings.Denoiser ? config_.userSettings.DenoiseSize : 0;

    ubo.ShowEdge = config_.showFlags.ShowEdge;
    ubo.ProgressiveRender = progressiveRender_.enabled;
    ubo.SceneEpsilonScale = config_.userSettings.SceneEpsilonScale;
    const float ambientCubeUnit = Assets::SanitizeAmbientCubeUnit(config_.userSettings.AmbientCubeUnit);
    const glm::vec3 ambientCubeOffsetBias =
        glm::vec3(config_.userSettings.AmbientCubeOffsetX, config_.userSettings.AmbientCubeOffsetY,
                  config_.userSettings.AmbientCubeOffsetZ);
    uint32_t ambientCubeCascadeCount =
        Assets::SanitizeAmbientCubeCascadeCount(config_.userSettings.AmbientCubeCascadeCount);
    if (scene_)
    {
        // Never advertise more cascades than the arena was sized for (Phase 2 right-sizing).
        ambientCubeCascadeCount = std::min(ambientCubeCascadeCount, scene_->AmbientCubeCascadeCapacity());
    }
    const float ambientCubeCascadeRatio =
        Assets::SanitizeAmbientCubeCascadeRatio(config_.userSettings.AmbientCubeCascadeRatio);
    ubo.AmbientCubeUnit = ambientCubeUnit;
    ubo.AmbientCubeOffset = Assets::CalculateAmbientCubeOffset(ambientCubeUnit, ambientCubeOffsetBias);
    ubo.AmbientCubeCascadeParams = glm::vec4(float(ambientCubeCascadeCount), ambientCubeCascadeRatio, 0.0f, 0.0f);

    // Other Setup
    renderer_->SetDenoiserEnabled(config_.userSettings.Denoiser);
    renderer_->SetVisualDebugEnabled(config_.showFlags.ShowVisualDebug);
    // UBO Backup, for motion vector calc
    renderState_.previousUniformBuffer = ubo;

    return ubo;
}

void NextEngine::OnRendererDeviceSet()
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

    // texture id 11 - 99: system texture
    // Assets::GlobalTexturePool::LoadTexture("assets/textures/white.png", true);


    // fill to 100, id > 100, general textures

    // if(GOption->HDRIfile != "") Assets::GlobalTexturePool::UpdateHDRTexture(0, GOption->HDRIfile.c_str(),
    // Vulkan::SamplerConfig());

    scene_.reset(new Assets::Scene(renderer_->CommandPool(), renderer_->SupportsRayTracing()));
    renderer_->SetScene(scene_);
    renderer_->OnPostLoadScene();

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextEngine::OnRendererCreateSwapChain()
{
    if (userInterface_.get() == nullptr)
    {
        userInterface_.reset(new NextUI::UserInterface(
            this, renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer(), config_.userSettings,
            [this]() -> void { gameInstance_->OnPreConfigUI(); }, [this]() -> void { gameInstance_->OnInitUI(); }));
    }
    userInterface_->OnCreateSurface(renderer_->SwapChain(), renderer_->DepthBuffer());
}

void NextEngine::OnRendererDeleteSwapChain()
{
    if (userInterface_.get() != nullptr)
    {
        userInterface_->OnDestroySurface();
    }
}

void NextEngine::OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    SCOPED_CPU_TIMER("ui");
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
    stats.InstanceCount = static_cast<uint32_t>(scene_->GetNodeProxys().size());
    stats.NodeCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    // Renderer::visualDebug_ = config_.userSettings.ShowVisualDebug;
    {
        SCOPED_CPU_TIMER("pre render");
        userInterface_->PreRender();
    }
    bool uiHandled = false;
    {
        SCOPED_CPU_TIMER("game ui");
        uiHandled = gameInstance_->OnRenderUI();
    }
    const bool suppressAllUi = screenShot_.hasPending;
    if (!suppressAllUi)
    {
        if (config_.showFlags.DebugPhysicsOverlay)
        {
            SCOPED_CPU_TIMER("physics debug ui");
            Assets::Camera debugCamera = scene_->GetRenderCamera();
            gameInstance_->OverrideRenderCamera(debugCamera);
            Runtime::DrawPhysicsDebugOverlay(*scene_, debugCamera);
            gameInstance_->DrawAdditionalPhysicsDebugOverlay(debugCamera);
        }
        {
            SCOPED_CPU_TIMER("graphics debug ui");
            Runtime::GraphicsDebugPanel::DrawPanel(*this, config_.showFlags.DebugGraphicsPanel,
                                                   gameInstance_->GetGraphicsDebugPanelTopOffset());
        }
        if (config_.showFlags.DebugProfileOverlay)
        {
            SCOPED_CPU_TIMER("profile debug ui");
            Runtime::DrawProfileDebugOverlay(*this, stats, renderer_->GpuTimer(),
                                             gameInstance_->GetGraphicsDebugPanelTopOffset());
        }
    }
    if (!uiHandled && !suppressAllUi)
    {
        SCOPED_CPU_TIMER("overlay ui");
        userInterface_->Render(stats, renderer_->GpuTimer(), scene_.get(), config_.showFlags.DebugProfileOverlay);
    }
    {
        SCOPED_CPU_TIMER("imgui submit");
        userInterface_->PostRender(commandBuffer, renderer_->SwapChain(), imageIndex, suppressAllUi);
    }
}

void NextEngine::OnKey(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
#if __APPLE__
        const bool hasCommand = (modifiers & SDL_KMOD_GUI) != 0;
        if (hasCommand && event.key.key == SDLK_Q)
        {
            RequestClose();
            return;
        }
#endif

        const bool altPressed = (modifiers & SDL_KMOD_ALT) != 0;
        const bool isAltEnter =
            altPressed && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER);
        const bool isF11 = event.key.key == SDLK_F11;

        if (isAltEnter || isF11)
        {
            if (services_.cvarSystem)
            {
                auto result = services_.cvarSystem->ExecuteCommand("cvar.toggle sys.fullscreen");
                if (!result.success)
                {
                    ToggleBorderlessFullscreen();
                }
            }
            else
            {
                ToggleBorderlessFullscreen();
            }
            return;
        }
    }

    if (userInterface_->WantsToCaptureKeyboard())
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (Runtime::GraphicsDebugPanel::TryHandleRendererShortcut(event.key.key, true,
                                                                   config_.showFlags.DebugGraphicsPanel, *this))
        {
            return;
        }

        if (HandleDebugShortcut(event.key.key))
        {
            return;
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
        const bool hasCtrlOrCmd = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (hasCtrlOrCmd)
        {
            const bool hasShift = (modifiers & SDL_KMOD_SHIFT) != 0;
            std::vector<uint32_t> selectedIds;
            const auto& currentSelection = GetScene().GetSelectedIds();
            selectedIds.reserve(currentSelection.size() + 1);
            for (uint32_t id : currentSelection)
            {
                selectedIds.push_back(id);
            }
            if (selectedIds.empty())
            {
                const uint32_t selectedId = GetScene().GetSelectedId();
                if (selectedId != static_cast<uint32_t>(-1))
                {
                    selectedIds.push_back(selectedId);
                }
            }

            if (event.key.key == SDLK_Z)
            {
                if (hasShift)
                {
                    if (commandHistory_.Redo())
                    {
                        return;
                    }
                }
                else
                {
                    if (commandHistory_.Undo())
                    {
                        return;
                    }
                }
            }
            else if (event.key.key == SDLK_Y)
            {
                if (commandHistory_.Redo())
                {
                    return;
                }
            }
            else if (event.key.key == SDLK_D)
            {
                if (!selectedIds.empty())
                {
                    auto command = std::make_unique<Runtime::Command::DuplicateNodesCommand>(GetScene(), selectedIds);
                    if (commandHistory_.Execute(std::move(command)))
                    {
                        return;
                    }
                }
            }
        }

        if (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE)
        {
            std::vector<uint32_t> selectedIds;
            const auto& currentSelection = GetScene().GetSelectedIds();
            selectedIds.reserve(currentSelection.size() + 1);
            for (uint32_t id : currentSelection)
            {
                selectedIds.push_back(id);
            }
            if (selectedIds.empty())
            {
                const uint32_t selectedId = GetScene().GetSelectedId();
                if (selectedId != static_cast<uint32_t>(-1))
                {
                    selectedIds.push_back(selectedId);
                }
            }

            if (!selectedIds.empty())
            {
                auto command = std::make_unique<Runtime::Command::DeleteNodesCommand>(GetScene(), selectedIds);
                if (commandHistory_.Execute(std::move(command)))
                {
                    return;
                }
            }
        }
    }

    if (gameInstance_->OnKey(event))
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (Runtime::GraphicsDebugPanel::TryHandleViewModeShortcut(
                event.key.key, true, config_.showFlags.DebugGraphicsPanel, config_.showFlags))
        {
            return;
        }
    }
}

bool NextEngine::HandleDebugShortcut(SDL_Keycode key)
{
    struct FDebugShortcutOps
    {
        std::function<bool()> IsActive;
        std::function<void(bool)> SetActive;
    };

    if (key < SDLK_F1 || key > SDLK_F10)
    {
        return false;
    }

    std::optional<FDebugShortcutOps> shortcutOps;
    switch (key)
    {
    case SDLK_F1:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugPhysicsOverlay; },
            .SetActive = [this](bool active) { config_.showFlags.DebugPhysicsOverlay = active; },
        };
        break;
    case SDLK_F2:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugGraphicsPanel; },
            .SetActive = [this](bool active) { config_.showFlags.DebugGraphicsPanel = active; },
        };
        break;
    case SDLK_F3:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugProfileOverlay; },
            .SetActive = [this](bool active) { config_.showFlags.DebugProfileOverlay = active; },
        };
        break;
    default:
        if (gameInstance_ && gameInstance_->SupportsAppDebugShortcut(key))
        {
            shortcutOps = FDebugShortcutOps{
                .IsActive = [this, key]() { return gameInstance_->IsAppDebugShortcutActive(key); },
                .SetActive = [this, key](bool active) { (void)gameInstance_->SetAppDebugShortcutActive(key, active); },
            };
        }
        break;
    }

    if (!shortcutOps.has_value())
    {
        return false;
    }

    const bool isActive = shortcutOps->IsActive();
    const bool engineOwnsShortcut = key == SDLK_F1 || key == SDLK_F2 || key == SDLK_F3;

    if (engineOwnsShortcut)
    {
        config_.showFlags.DebugPhysicsOverlay = false;
        config_.showFlags.DebugGraphicsPanel = false;
        config_.showFlags.DebugProfileOverlay = false;
        if (!isActive)
        {
            shortcutOps->SetActive(true);
        }
        return true;
    }

    shortcutOps->SetActive(!isActive);
    return true;
}

void NextEngine::OnTouch(bool down, double xpos, double ypos)
{
    // OnMouseButton(GLFW_MOUSE_BUTTON_RIGHT, down ? GLFW_PRESS : GLFW_RELEASE, 0);
}

void NextEngine::OnTouchMove(double xpos, double ypos) { OnCursorPosition(xpos, ypos); }

void NextEngine::OnCursorPosition(const double xpos, const double ypos)
{
    if (!renderer_->HasSwapChain() || userInterface_->WantsToCaptureKeyboard() ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    if (gameInstance_->OnCursorPosition(xpos, ypos))
    {
        return;
    }
}

void NextEngine::OnMouseButton(SDL_Event& event)
{
    if (!renderer_->HasSwapChain() || userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    if (gameInstance_->OnMouseButton(event))
    {
        return;
    }
}

void NextEngine::OnScroll(const double xoffset, const double yoffset)
{
    if (!renderer_->HasSwapChain() || userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    gameInstance_->OnScroll(xoffset, yoffset);
}

void NextEngine::OnDropFile(const char* dropPath)
{
    const std::string path(dropPath);
    const std::filesystem::path droppedPath(path);

    if (Runtime::Scene::SceneList::IsSupportedScenePath(droppedPath))
    {
        RequestLoadScene({.filename = path});
        return;
    }

    std::string ext = droppedPath.has_extension() ? droppedPath.extension().string() : std::string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".hdr")
    {
        uint32_t newTextureId = Assets::GlobalTexturePool::GetInstance()->LoadHDRTexture(path);
        scene_->GetEnvSettings().SkyIdx = newTextureId;
    }
}
void NextEngine::TickAgentValidation()
{
    // Only act once a scene is live and rendering. GetTotalFrames() resets to 0 on every scene
    // load, so this naturally waits for the loaded scene to settle before capturing.
    if (status_ != NextRenderer::EApplicationStatus::Running)
    {
        return;
    }

    if (!agentValidation_.captured)
    {
        if (GetTotalFrames() < agentValidation_.waitFrames)
        {
            return;
        }

        // Resolve against the runtime root so both the directory we create and the file the
        // screenshot writer emits land in the same place (matches RequestScreenshot convention).
        const std::string resolvedPath =
            Utilities::FileHelper::GetPlatformFilePath(agentValidation_.outputPath.c_str());
        const std::string outputDir = std::filesystem::path(resolvedPath).parent_path().string();
        if (!outputDir.empty())
        {
            Utilities::FileHelper::EnsureDirectoryExists(outputDir);
        }

        RequestScreenShot({.filename = resolvedPath});
        agentValidation_.captured = true;
        SPDLOG_INFO("[AgentValidation] capturing screenshot -> {}.jpg ({} frames)",
                    resolvedPath, GetTotalFrames());
        return;
    }

    // The pending screenshot is flushed at the top of the next frame; give it a couple of frames
    // to land on disk before closing so the file is guaranteed to exist for the agent to inspect.
    if (++agentValidation_.postCaptureFrames >= 3)
    {
        SPDLOG_INFO("[AgentValidation] screenshot saved -> {}.jpg, exiting", agentValidation_.outputPath);
        RequestClose();
    }
}

void NextEngine::TickGamepadInput()
{
    int gamepadCount = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);

    if (gamepadCount > 0)
    {
        SDL_Gamepad* masterGamepad = SDL_GetGamepadFromID(*gamepads);

        gameInstance_->OnGamepadInput(SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFTX),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFTY),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHTY),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }

    SDL_free(gamepads);
}

void NextEngine::OnRendererBeforeNextFrame()
{
    SCOPED_CPU_TIMER("task coordinator");
    Tasks::TaskCoordinator::GetInstance()->Tick();
}

void NextEngine::RequestLoadScene(FSceneLoadRequest request)
{
    AddTickedTask(
        [this, request = std::move(request)](double deltaSeconds) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            LoadScene(request);
            return true;
        });
}

void NextEngine::LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad)
{
    // wait all task finish
    Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    status_ = NextRenderer::EApplicationStatus::Loading;

    SceneLoadContext ctx;
    ctx.models = std::make_shared<std::vector<Assets::Model>>();
    ctx.nodes = std::make_shared<std::vector<std::shared_ptr<Assets::Node>>>();
    ctx.materials = std::make_shared<std::vector<Assets::FMaterial>>();
    ctx.lights = std::make_shared<std::vector<Assets::LightObject>>();
    ctx.tracks = std::make_shared<std::vector<Assets::AnimationTrack>>();
    ctx.skeletons = std::make_shared<std::vector<Assets::Skeleton>>();
    ctx.cameraState = std::make_shared<Assets::EnvironmentSetting>();

    // dispatch in thread task and reset in main thread
    Tasks::TaskCoordinator::GetInstance()->AddTask(
        [ctx, sceneFileName](Tasks::ResTask& task)
        {
            SceneTaskContext taskContext{};
            const auto timer = std::chrono::high_resolution_clock::now();

            taskContext.success = Runtime::Scene::SceneList::LoadScene(sceneFileName, *ctx.cameraState, *ctx.nodes, *ctx.models,
                                                       *ctx.materials, *ctx.lights, *ctx.tracks, *ctx.skeletons);

            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                      std::chrono::high_resolution_clock::now() - timer)
                                      .count();

            std::string info =
                fmt::format("parsed scene [{}] on cpu in {:.2f}ms",
                            std::filesystem::path(sceneFileName).filename().string(), taskContext.elapsed * 1000.f);
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext(taskContext);
        },
        [this, ctx, sceneFileName, onGpuLoad](Tasks::ResTask& task) mutable
        {
            SceneTaskContext taskContext{};
            task.GetContext(taskContext);
            if (taskContext.success)
            {
                SPDLOG_INFO("{}", taskContext.outputInfo.data());

                renderer_->Device().WaitIdle();
                renderer_->DeleteSwapChain();

                // Execute the specific GPU load logic
                onGpuLoad(ctx);

                frameState_.totalFrames = 0;
                renderer_->OnPostLoadScene();
                renderer_->CreateSwapChain();
            }
            else
            {
                SPDLOG_ERROR("failed to load scene [{}]", std::filesystem::path(sceneFileName).filename().string());
            }

            status_ = NextRenderer::EApplicationStatus::Running;
        },
        1);
}

void NextEngine::LoadScene(const FSceneLoadRequest& request)
{
    if (!request.append)
    {
        scene_->CleanUp();
        services_.physics->OnSceneDestroyed();
        Assets::GlobalTexturePool::GetInstance()->FreeTransientTextures();
    }

    LaunchLoadSceneTask(
        request.filename,
        [this, request](SceneLoadContext& ctx)
        {
            const auto timer = std::chrono::high_resolution_clock::now();
            renderer_->OnPreLoadScene();
            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);

            if (!request.append)
            {
                scene_->GetEnvSettings().Reset();
                scene_->SetEnvSettings(*ctx.cameraState);
                gameInstance_->OnSceneUnloaded();
                services_.physics->OnSceneStarted();

                scene_->Reload(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
                scene_->PostLoad(*ctx.skeletons);
                scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->SupportsRayTracing());
                renderer_->SetScene(scene_);

                config_.userSettings.CameraIdx = 0;
                assert(!scene_->GetEnvSettings().cameras.empty());
                scene_->SetRenderCamera(scene_->GetEnvSettings().cameras[0]);
                gameInstance_->OnSceneLoaded();
            }
            else
            {
                std::string name = std::filesystem::path(request.filename).stem().string();
                std::shared_ptr<Assets::Node> rootNode =
                    scene_->Append(name, *ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks,
                                   *ctx.skeletons);
                if (request.placeOnHit && rootNode)
                {
                    rootNode->SetTranslation(request.hitPosition);
                }
                scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->SupportsRayTracing());
                renderer_->SetScene(scene_);
            }

            const float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                      std::chrono::high_resolution_clock::now() - timer)
                                      .count();
            SPDLOG_INFO("uploaded scene [{}] to gpu in {:.2f}ms",
                        std::filesystem::path(request.filename).filename().string(), elapsed * 1000.f);
        });
}

void NextEngine::InitPhysics() {}
