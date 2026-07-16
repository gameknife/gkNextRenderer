#include "Engine/Runtime/Engine.hpp"
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
#include "Engine/Runtime/ScreenShot.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Interface/UiOverlay.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Input/SyntheticInput.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
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
#include <nlohmann/json.hpp>

#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Localization.hpp"

#define _USE_MATH_DEFINES

#include <math.h>

#include <entt/meta/factory.hpp>

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

void NextEngine::RegisterReflection()
{
    using namespace entt::literals;

    entt::meta_factory<NextEngine>()
        .type("NextEngine"_hs)
        .func<&NextEngine::GetTotalFrames>("GetTotalFrames");
}

namespace
{
    // Older Android NDK Vulkan headers do not name this newer registry value yet.
    constexpr VkDriverId kMesaKosmicKrispDriverId = static_cast<VkDriverId>(28);

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

    Vulkan::ERendererType ResolveRendererType(
        Vulkan::ERendererType requestedType,
        bool supportsRayTracing,
        bool hasFullAmbientCubeBudget)
    {
        if (!supportsRayTracing && Vulkan::GetRendererRequirements(requestedType).requestRayTracing)
        {
            requestedType = Vulkan::ERT_SoftwareTracing;
        }
        if (!hasFullAmbientCubeBudget && Vulkan::GetRendererRequirements(requestedType).requestAmbientCube)
        {
            return Vulkan::ERT_SoftwareModernNoAmbient;
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

        Vulkan::Instance* instance = new Vulkan::Instance(
            *window, validationLayers, VK_API_VERSION_1_2, GOption->SyncValidation);

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
} // namespace

Runtime::Config::UserSettings CreateUserSettings(const Runtime::Config::Options& options)
{
    Runtime::Scene::SceneList::ScanScenes();

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

#if ANDROID
    std::string tag = "gknext";
    auto android_logger = spdlog::android_logger_mt("android", tag);
    android_logger->critical("Use \"adb shell logcat\" to view this message.");
    spdlog::set_default_logger(android_logger);
#endif

    SPDLOG_INFO("---- Next Engine Initializing...");
    spdlog::stopwatch stopwatch;

    instance_ = this;
    
    // Initialize reflection system first
    Reflection::RegisterAllReflection();

    status_ = NextRenderer::EApplicationStatus::Starting;

    services_.packageFileSystem.reset(new Utilities::Package::FPackageFileSystem(Utilities::Package::EPM_OsFile));
    {
        const std::string optionalPakPath = Utilities::FileHelper::GetPlatformFilePath("assets/paks/optional.pak");
        std::error_code ec;
        if (std::filesystem::exists(optionalPakPath, ec))
        {
            services_.packageFileSystem->MountPak(optionalPakPath);
        }
    }

    Vulkan::Window::InitSDL();
    
    Vulkan::WindowConfig windowConfig{"gkNextEngine " + NextRenderer::GetBuildVersion(),
                                      options.Width,options.Height,
                                      false, options.Fullscreen,!options.Fullscreen,
                                      options.SaveFile,userdata,options.ForceSDR};
    
    gameInstance_ = CreateGameInstance(windowConfig, options, this);
    
    config_.userSettings = CreateUserSettings(options);
    
    // cvars
    services_.cvarSystem = std::make_unique<NextCVar::FCVarSystem>();
    NextCVar::RegisterEngineCVars(*services_.cvarSystem, config_.userSettings, config_.showFlags, this);
    services_.cvarSystem->LoadDefaultFile("assets/configs/cvar_default.json");
    gameInstance_->ConfigureCVars(*services_.cvarSystem);
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
    windowConfig.HiddenWindow =
        (options.AgentValidation && !options.AgentVisibleWindow) || options.HiddenWindow || options.Tui;
    
    // create windows
    window_.reset(new Vulkan::Window(windowConfig));
    SetBorderlessFullscreen(config_.userSettings.BorderlessFullscreen);
    
    // localization
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
    if (services_.cvarSystem)
    {
        services_.cvarSystem->SaveUserFile("assets/configs/cvar_user.json");
    }

    if (services_.localization)
    {
        services_.localization->SaveToTxt(fmt::format("assets/locale/{}.txt", options_->locale));
    }

    uiOverlay_.reset();
    userInterface_.reset();
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

    auto resolvedRendererType = ResolveRendererType(
        renderer_->CurrentLogicRendererType(), renderer_->SupportsRayTracing(), renderer_->HasFullAmbientCubeBudget());
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
    
    if (!options_->AgentControl.empty())
    {
        agentControl_ = std::make_unique<Runtime::Agent::FAgentControlServer>();
        std::string error;
        if (!agentControl_->Start(options_->AgentControl, options_->AgentControlToken, error))
        {
            SPDLOG_ERROR("[AgentControl] failed to start: {}", error); RequestExit(3);
        }
        else
        {
            gameInstance_->RegisterAgentQueries(agentQueries_);
            SPDLOG_INFO("[AgentControl] listening on {}", options_->AgentControl);
        }
    }

    SPDLOG_INFO("---- Next Engine Started in {}", stopwatch.elapsed_ms());
}

bool NextEngine::HandleEvent(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        SDL_SetModState(static_cast<SDL_Keymod>(event.key.mod));
    }

    userInterface_->HandleEvent(&event);
    const bool rmlUiConsumed = uiOverlay_ && uiOverlay_->HandleEvent(event);

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
        if (!rmlUiConsumed)
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

    if (Profiler())
    {
        Profiler()->BeginCpuFrame();
    }

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

        if (config_.userSettings.TickPhysics && services_.physics)
        {
            SCOPED_CPU_TIMER("physics");
            services_.physics->Tick(frameState_.deltaSeconds);
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

        {
            SCOPED_CPU_TIMER("draw frame");
            if (screenShot_.hasPending || screenShot_.captureFramesRemaining == 1)
            {
                renderer_->RequestScreenShotCapture();
            }
            renderer_->DrawFrame();
        }
        frameState_.totalFrames = renderer_->FrameCount();

        if (screenShot_.hasPending)
        {
            renderer_->Device().WaitIdle();
            if (screenShot_.pending.fast)
            {
                Runtime::ScreenShot::SaveSwapChainToFileFast(renderer_.get(),
                                               screenShot_.pending.filename,
                                               screenShot_.pending.x,
                                               screenShot_.pending.y,
                                               screenShot_.pending.width,
                                               screenShot_.pending.height);
            }
            else
            {
                Runtime::ScreenShot::SaveSwapChainToFile(renderer_.get(),
                                               screenShot_.pending.filename,
                                               screenShot_.pending.x,
                                               screenShot_.pending.y,
                                               screenShot_.pending.width,
                                               screenShot_.pending.height);
            }
            screenShot_.hasPending = false;
            screenShot_.pending = {};
        }

        if (progressiveRender_.warmupFramesRemaining > 0)
        {
            progressiveRender_.warmupFramesRemaining--;
            if (progressiveRender_.warmupFramesRemaining == 0)
            {
                progressiveRender_.enabled = true;
                progressiveRender_.accumulatedFrames = 0;
            }
        }

        if (progressiveRender_.enabled)
        {
            progressiveRender_.accumulatedFrames =
                std::min(progressiveRender_.accumulatedFrames + 1, FProgressiveRenderState::TargetFrames);
        }

        // High quality capture: count down accumulated frames after DrawFrame
        if (screenShot_.captureFramesRemaining > 0)
        {
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
            TickGamepadInput();
        }

        if (agentControl_)
        {
            agentControl_->Pump([this](const std::string& method, const nlohmann::json& params) { return HandleAgentControlCommand(method, params); });
        }

        if (!renderFrameConsumers_.empty())
        {
            for (const auto& consumer : renderFrameConsumers_)
            {
                consumer->Tick();
            }
        }
    }

    if (Profiler())
    {
        Profiler()->EndCpuFrame();
    }
    return false;
}

void NextEngine::End()
{
    if (agentControl_) { agentControl_->Stop(); agentControl_.reset(); }

    if (!GOption->FastExit)
    {
        Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();
        Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
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
    ++screenShot_.queuedRequests;
    AddTickedTask([this, spec = std::move(spec)](double) mutable {
        if (screenShot_.hasPending || screenShot_.captureFramesRemaining > 0)
        {
            return false;
        }

        --screenShot_.queuedRequests;
        if (spec.accumulateFrames > 0)
        {
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
            return true;
        }

        spec.filename = ResolveScreenShotFilename(spec.filename, "screenshot");
        screenShot_.pending = std::move(spec);
        screenShot_.hasPending = true;
        return true;
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

void NextEngine::SetProgressiveRendering(bool enable, bool directly)
{
    if (directly)
    {
        if (enable && !progressiveRender_.enabled)
        {
            progressiveRender_.accumulatedFrames = 0;
            progressiveRender_.warmupFramesRemaining = 0;
        }
        else if (!enable)
        {
            progressiveRender_.accumulatedFrames = 0;
            progressiveRender_.warmupFramesRemaining = 0;
        }
        progressiveRender_.enabled = enable;
        return;
    }

    if (enable)
    {
        if (!progressiveRender_.enabled && progressiveRender_.warmupFramesRemaining == 0)
        {
            progressiveRender_.accumulatedFrames = 0;
            progressiveRender_.warmupFramesRemaining = config_.userSettings.TemporalFrames * 2;
        }
    }
    else
    {
        progressiveRender_.warmupFramesRemaining = 0;
        progressiveRender_.enabled = false;
        progressiveRender_.accumulatedFrames = 0;
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

    scene_.reset(new Assets::Scene(renderer_->CommandPool(), renderer_->SupportsRayTracing()));
    CommitSceneToRenderer({.rebuildMeshBuffer = false,
                           .resetFrameCounter = false,
                           .refreshSwapChainResources = false});

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextEngine::OnRendererCreateSwapChain()
{
    if (userInterface_.get() == nullptr)
    {
        userInterface_.reset(new NextUI::UserInterface(
            this, renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer(), config_.userSettings,
            [this]() -> void { gameInstance_->OnPreConfigUI(); }, [this]() -> void { gameInstance_->OnInitUI(); },
            gameInstance_->CreateMultiViewportBackend()));
    }
    if (uiOverlay_.get() == nullptr && uiOverlayFactory_)
    {
        uiOverlay_ = uiOverlayFactory_(*this);
    }
    userInterface_->OnCreateSurface(renderer_->SwapChain(), renderer_->DepthBuffer());
}

void NextEngine::OnRendererDeleteSwapChain()
{
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
    const bool suppressAllUi = screenShot_.hasPending && !screenShot_.pending.includeUi &&
        (!gameInstance_ || !gameInstance_->ShouldRenderUiDuringScreenshot());

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

    // Renderer::visualDebug_ = config_.userSettings.ShowVisualDebug;
    {
        SCOPED_CPU_TIMER("pre render");
        userInterface_->PreRender();
        if (uiOverlay_)
        {
            uiOverlay_->BeginFrame();
        }
    }
    bool uiHandled = false;
    {
        SCOPED_CPU_TIMER("game ui");
        uiHandled = gameInstance_->OnRenderUI();
    }
    if (uiOverlay_)
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
            debugUiProvider_->DrawProfileOverlay(*this, stats, renderer_->Profiler(),
                                                 gameInstance_->GetGraphicsDebugPanelTopOffset());
        }
    }
    if (!uiHandled)
    {
        SCOPED_CPU_TIMER("overlay ui");
        userInterface_->Render(stats, renderer_->Profiler(), scene_.get(), config_.showFlags.DebugProfileOverlay);
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

nlohmann::json NextEngine::HandleAgentControlCommand(const std::string& method, const nlohmann::json& params)
{
    using Runtime::Input::Synthetic::FPoint;
    SDL_Window* window = GetWindow().Handle();
    const SDL_WindowID windowId = SDL_GetWindowID(window);
    const FPoint current{static_cast<float>(GetMousePos().x), static_cast<float>(GetMousePos().y)};
    auto point = [](const nlohmann::json& value) -> FPoint
    {
        if (!value.is_array() || value.size() < 2) throw std::runtime_error("point must be [x,y]");
        return {value[0].get<float>(), value[1].get<float>()};
    };
    if (method == "handshake") return {{"protocolVersion", 1}, {"capabilities", {"input", "query", "cvar", "exec", "screenshot", "quit"}}};
    if (method == "query")
    {
        const auto value = QueryAgentControl(params.value("query", ""));
        if (!value) throw std::runtime_error("query not found");
        return std::visit([](const auto& item) { return nlohmann::json(item); }, *value);
    }
    if (method == "key")
    {
        const std::string code = params.value("code", ""); const auto key = Runtime::Input::Synthetic::ResolveKeyCode(code);
        if (key == SDLK_UNKNOWN) throw std::runtime_error("unknown key");
        const auto scan = Runtime::Input::Synthetic::ResolveScanCode(key, code);
        const auto modifiers = Runtime::Input::Synthetic::ResolveModifiers(params.value("mods", std::vector<std::string>{}));
        const std::string action = params.value("action", "press");
        if (action == "down") Runtime::Input::Synthetic::PushKey(windowId, key, scan, modifiers, true);
        else if (action == "up") Runtime::Input::Synthetic::PushKey(windowId, key, scan, modifiers, false);
        else Runtime::Input::Synthetic::PushKeyPress(windowId, key, scan, modifiers);
        return {{"ok", true}};
    }
    if (method == "text") { Runtime::Input::Synthetic::PushText(windowId, params.value("value", "")); return {{"ok", true}}; }
    if (method == "mouse-move")
    {
        const auto to = point(params.at("to"));
        if (params.value("relative", false)) InjectRelativeMouse(to.x, to.y); else Runtime::Input::Synthetic::PushMouseMove(window, current, to);
        return {{"ok", true}};
    }
    if (method == "mouse-button" || method == "click")
    {
        const auto at = params.contains("at") ? point(params["at"]) : current;
        if (params.contains("at")) Runtime::Input::Synthetic::PushMouseMove(window, current, at);
        const auto button = Runtime::Input::Synthetic::ResolveMouseButton(params.value("button", "left"));
        const auto clicks = static_cast<Uint8>(std::max(1, params.value("count", 1)));
        const std::string action = method == "click" ? "press" : params.value("action", "press");
        if (action != "up") Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, true, clicks);
        if (action != "down") Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, false, clicks);
        return {{"ok", true}};
    }
    if (method == "drag")
    {
        const auto from = point(params.at("from")); const auto to = point(params.at("to"));
        const auto button = Runtime::Input::Synthetic::ResolveMouseButton(params.value("button", "left"));
        Runtime::Input::Synthetic::PushMouseMove(window, current, from); Runtime::Input::Synthetic::PushMouseButton(windowId, from, button, true);
        Runtime::Input::Synthetic::PushMouseMove(window, from, to); Runtime::Input::Synthetic::PushMouseButton(windowId, to, button, false); return {{"ok", true}};
    }
    if (method == "scroll") { Runtime::Input::Synthetic::PushMouseWheel(windowId, current, params.value("x", 0.0f), params.value("y", 0.0f)); return {{"ok", true}}; }
    if (method == "cvar")
    {
        const std::string name = params.value("name", "");
        if (params.contains("set")) { std::string error; const std::string value = params["set"].is_string() ? params["set"].get<std::string>() : params["set"].dump(); if (!GetCVarSystem().SetValueFromString(name, value, NextCVar::ECVarSetBy::Console, &error)) throw std::runtime_error(error); return {{"value", value}}; }
        bool found = false; const auto value = GetCVarSystem().GetValueString(name, &found); if (!found) throw std::runtime_error("cvar not found"); return {{"value", value}};
    }
    if (method == "exec") { const auto result = GetCVarSystem().ExecuteCommand(params.value("line", "")); if (!result.success) throw std::runtime_error(result.message); return {{"message", result.message}, {"output", result.output}}; }
    if (method == "screenshot")
    {
        const std::string path = Utilities::FileHelper::GetPlatformFilePath(params.value("out", "screenshots/agent_validation").c_str());
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(path).parent_path().string());
        RequestScreenShot({.filename = path, .includeUi = params.value("ui", false)}); return {{"path", path + ".jpg"}};
    }
    if (method == "quit") { RequestExit(params.value("exitCode", 0)); return {{"ok", true}}; }
    throw std::runtime_error("unknown agent control method: " + method);
}

std::optional<Runtime::Agent::FAgentQueryValue> NextEngine::QueryAgentControl(const std::string& query) const
{
    if (query == "engine.totalFrames") return static_cast<int64_t>(GetTotalFrames());
    if (query == "engine.frameRate") return static_cast<double>(GetFrameRate());
    if (query == "engine.time") return GetTime();
    if (query == "engine.status")
    {
        switch (GetEngineStatus()) { case NextRenderer::EApplicationStatus::Starting: return std::string("Starting"); case NextRenderer::EApplicationStatus::Running: return std::string("Running"); case NextRenderer::EApplicationStatus::Loading: return std::string("Loading"); default: return std::string("AsyncPreparing"); }
    }
    if (query == "engine.rendererType") return static_cast<int64_t>(renderer_->CurrentLogicRendererType());
    if (query == "scene.nodeCount") return static_cast<int64_t>(GetScene().Nodes().size());
    if (query == "scene.selectedId") return static_cast<int64_t>(GetScene().GetSelectedId());
    if (query == "scene.selectedCount") return static_cast<int64_t>(GetScene().GetSelectedIds().size());
    if (query.rfind("cvar.", 0) == 0) { bool found = false; auto value = GetCVarSystem().GetValueString(query.substr(5), &found); if (found) return value; return std::nullopt; }
    if (query.rfind("game.", 0) == 0) return agentQueries_.Query(query.substr(5));
    return std::nullopt;
}

void NextEngine::OnRendererBeforeNextFrame()
{
    SCOPED_CPU_TIMER("task coordinator");
    Tasks::TaskCoordinator::GetInstance()->Tick();
}
