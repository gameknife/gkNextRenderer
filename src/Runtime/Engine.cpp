#include "Engine.hpp"
#include "Assets/Core/Model.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Assets/GPU/Texture.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Config/EngineCVars.hpp"
#include "Runtime/Subsystems/QuickJSEngine.hpp"
#include "Runtime/Subsystems/AIService.hpp"
#include "Runtime/Subsystems/VoiceInputService.hpp"
#include "Runtime/Command/DeleteNodesCommand.hpp"
#include "Runtime/Command/DuplicateNodesCommand.hpp"
#include "Runtime/ScreenShot.hpp"
#include "Runtime/Editor/UserInterface.hpp"
#include "Runtime/Editor/ConsoleLogBuffer.hpp"
#include "Runtime/Config/UserSettings.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Instance.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/WindowSurface.hpp"

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

#include "Runtime/Subsystems/NextAudio.h"
#include "Options.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"
#include "Runtime/Subsystems/TaskCoordinator.hpp"
#include "Utilities/Localization.hpp"

#define _USE_MATH_DEFINES
#include <math.h>

#include <entt/meta/factory.hpp>

#define BUILDVER(X) std::string buildver(#X);
#include "Runtime/Subsystems/NextAnimation.h"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Platform/PlatformCommon.h"
#include "build.version"

#include "Common/CoreMinimal.hpp"
#include "Reflection/ReflectionRegistry.h"

// spdlog logging
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#if ANDROID
#include <spdlog/sinks/android_sink.h>
#endif

ENGINE_API Options* GOption = nullptr;

void NextEngine::RegisterReflection()
{
    using namespace entt::literals;

    entt::meta_factory<NextEngine>()
        .type("NextEngine"_hs)
        .func<&NextEngine::GetTotalFrames>("GetTotalFrames")
        .func<&NextEngine::GetTestNumber>("GetTestNumber")
        .func<&NextEngine::RegisterJSCallback>("RegisterJSCallback")
        .func<&NextEngine::GetScenePtr>("GetScenePtr");
}

namespace
{
    Vulkan::ERendererType ResolveRendererType(Vulkan::ERendererType requestedType, bool supportsRayTracing)
    {
        if (!supportsRayTracing && requestedType == Vulkan::ERT_PathTracing)
        {
            return Vulkan::ERT_ModernDeferred;
        }
        return requestedType;
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
        Vulkan::Instance* instance = new Vulkan::Instance(*window, validationLayers, VK_API_VERSION_1_2);

        const bool useRayTracingRenderer = !GOption->ForceNoRT && instance->SupportsRayQuery();

        std::vector supportedTypes = {Vulkan::ERT_ModernDeferred, Vulkan::ERT_LegacyDeferred, Vulkan::ERT_VoxelTracing};
        Vulkan::VulkanBaseRenderer* renderer = nullptr;
        if (useRayTracingRenderer)
        {
            renderer =
                new Vulkan::RayTracing::RayTraceBaseRenderer(window, presentMode, enableValidationLayers, instance);
            supportedTypes.emplace_back(Vulkan::ERT_PathTracing);
        }
        else
        {
            renderer = new Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers, instance);
        }

        for (auto type : supportedTypes)
        {
            renderer->RegisterLogicRenderer(type);
        }

        auto requestedType =
            ResolveRendererType(static_cast<Vulkan::ERendererType>(rendererType), useRayTracingRenderer);
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

UserSettings CreateUserSettings(const Options& options)
{
    (void)options;
    SceneList::ScanScenes();

    UserSettings userSettings{};

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

NextEngine::NextEngine(Options& options, void* userdata)
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

    packageFileSystem_.reset(new Utilities::Package::FPackageFileSystem(Utilities::Package::EPM_OsFile));
    aiService_ = std::make_unique<NextAI::FAIService>();

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
    userSettings_ = CreateUserSettings(options);
    cvarSystem_ = std::make_unique<NextCVar::FCVarSystem>();
    NextCVar::RegisterEngineCVars(*cvarSystem_, userSettings_, showFlags_, this);
    cvarSystem_->LoadDefaultFile("assets/configs/cvar_default.json");
    gameInstance_->ApplyDefaultCVars(*cvarSystem_);
    cvarSystem_->LoadUserFile("assets/configs/cvar_user.json");
    windowConfig.Fullscreen = userSettings_.BorderlessFullscreen;
    window_.reset(new Vulkan::Window(windowConfig));
    SetBorderlessFullscreen(userSettings_.BorderlessFullscreen);
    quickJSEngine_ = std::make_unique<QuickJSEngine>();

    // Initialize Localization
    Utilities::Localization::ReadLocTexts(fmt::format("assets/locale/{}.txt", options_->locale).c_str());

    SPDLOG_INFO("---- Next Engine Initialized in {}", stopwatch.elapsed_ms());
}

NextEngine::~NextEngine()
{
    if (cvarSystem_)
    {
        cvarSystem_->SaveUserFile("assets/configs/cvar_user.json");
    }

    Utilities::Localization::SaveLocTexts(fmt::format("assets/locale/{}.txt", options_->locale).c_str());

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
#ifndef NDEBUG
    shouldEnableValidation = true;
#endif

    renderer_.reset(NextRenderer::CreateRenderer(static_cast<uint32_t>(userSettings_.RendererType), window_.get(),
                                                 static_cast<VkPresentModeKHR>(options_->PresentMode),
                                                 shouldEnableValidation));
    userSettings_.RendererType = static_cast<int32_t>(renderer_->CurrentLogicRendererType());

    renderer_->DelegateOnDeviceSet = [this]() -> void { OnRendererDeviceSet(); };
    renderer_->DelegateCreateSwapChain = [this]() -> void { OnRendererCreateSwapChain(); };
    renderer_->DelegateDeleteSwapChain = [this]() -> void { OnRendererDeleteSwapChain(); };
    renderer_->DelegateBeforeNextTick = [this]() -> void { OnRendererBeforeNextFrame(); };
    renderer_->DelegateGetUniformBufferObject = [this](VkOffset2D offset,
                                                       VkExtent2D extend) -> Assets::UniformBufferObject
    { return GetUniformBufferObject(offset, extend); };
    renderer_->DelegatePostRender = [this](VkCommandBuffer commandBuffer, uint32_t imageIndex) -> void
    { OnRendererPostRender(commandBuffer, imageIndex); };

    renderer_->Start();

    physicsEngine_.reset(new NextPhysics());
    physicsEngine_->Start();

    animationEngine_ = std::make_unique<NextAnimation>();
    animationEngine_->Start();

    audioEngine_ = std::make_unique<NextAudio>();
    audioEngine_->Start();

    voiceInputService_ = std::make_unique<NextAI::VoiceInputService>();
    NextAI::FVoiceInputConfig voiceConfig;
    if (aiService_)
    {
        aiService_->TryGetVoiceInputConfig(voiceConfig);
    }
    voiceInputService_->Initialize(voiceConfig);

    if (quickJSEngine_)
    {
        quickJSEngine_->Initialize();
    }

    gameInstance_->OnInit();

    SPDLOG_INFO("---- Next Engine Started in {}", stopwatch.elapsed_ms());
}

bool NextEngine::HandleEvent(SDL_Event& event)
{
    userInterface_->HandleEvent(&event);

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
        OnCursorPosition(event.motion.x, event.motion.y);
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

    // make sure the output is flushed
    std::cout << std::flush;

    // Hot change renderer
    auto requestedRendererType = ResolveRendererType(static_cast<Vulkan::ERendererType>(userSettings_.RendererType),
                                                     renderer_->SupportsRayTracing());
    if (requestedRendererType != static_cast<Vulkan::ERendererType>(userSettings_.RendererType))
    {
        userSettings_.RendererType = static_cast<int32_t>(requestedRendererType);
    }

    if (renderer_->CurrentLogicRendererType() != requestedRendererType)
    {
        renderer_->SwitchLogicRenderer(requestedRendererType);
    }

    // delta time calc
    const auto prevTime = time_;
    time_ = GetWindow().GetTime();
    deltaSeconds_ = time_ - prevTime;
    if (forcingDelta)
        deltaSeconds_ = 1.0 / 30.0;
    float invDelta = static_cast<float>(deltaSeconds_) / 60.0f;
    smoothedDeltaSeconds_ = glm::mix(smoothedDeltaSeconds_, deltaSeconds_, invDelta * 100.0f);

    // Scene Update
    if (scene_)
    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickScene", "");
        scene_->Tick(static_cast<float>(deltaSeconds_));
    }

#if WITH_PHYSIC
    if (userSettings_.TickPhysics && physicsEngine_)
        physicsEngine_->Tick(deltaSeconds_);
#endif

    if (userSettings_.TickAnimation && animationEngine_)
        animationEngine_->Tick(deltaSeconds_); // pause dev, wait next

    if (quickJSEngine_)
    {
        quickJSEngine_->Tick(deltaSeconds_);
    }

    // tick
    if (status_ == NextRenderer::EApplicationStatus::Running)
    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickGameInstance", "");
        gameInstance_->OnTick(deltaSeconds_);
    }

    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickTasks", "");

        // iterate the tickedTasks_, if return true, remove it
        for (auto it = tickedTasks_.begin(); it != tickedTasks_.end();)
        {
            if ((*it)(deltaSeconds_))
            {
                it = tickedTasks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }


    // iterate the delayedTasks_ , if Time is up, execute it, if return true, remove it
    for (auto it = delayedTasks_.begin(); it != delayedTasks_.end();)
    {
        if (time_ > it->triggerTime)
        {
            // update the next trigger time
            it->triggerTime = time_ + it->loopTime;

            // execute
            if (it->task())
            {
                it = delayedTasks_.erase(it);
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

    {
        PERFORMANCEAPI_INSTRUMENT_COLOR("Engine::TickRenderer", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
        renderer_->DrawFrame();
    }
    totalFrames_ = renderer_->FrameCount();


    if (progressivePreFrames_ > 0)
    {
        progressivePreFrames_--;
        if (progressivePreFrames_ == 0)
        {
            progressiveRendering_ = true;
        }
    }

    // High quality capture: count down accumulated frames after DrawFrame
    if (hqCaptureFramesRemaining_ > 0)
    {
        hqCaptureFramesRemaining_--;
        if (hqCaptureFramesRemaining_ == 0)
        {
            // All frames accumulated, capture now
            renderer_->Device().WaitIdle();
            ScreenShot::SaveSwapChainToFile(renderer_.get(), hqCaptureFilename_, 0, 0, 0, 0);
            spdlog::info("High quality capture saved: {} ({} frames accumulated)",
                         hqCaptureFilename_, hqCaptureTotalFrames_);

            // Restore previous state
            progressiveRendering_ = hqCapturePrevProgressive_;
            progressivePreFrames_ = hqCapturePrevPreFrames_;
        }
    }

    // sample gamepad stats

    TickGamepadInput();
    return false;
}

void NextEngine::End()
{
    if (!GOption->FastExit)
    {
        TaskCoordinator::GetInstance()->CancelAllParralledTasks();
        TaskCoordinator::GetInstance()->WaitForAllParralledTask();
        TaskCoordinator::DestroyInstance();
    }

    if (audioEngine_)
    {
        audioEngine_->Stop();
    }

    physicsEngine_->Stop();
    animationEngine_->Stop();
    gameInstance_->OnDestroy();
    renderer_->End();
    userInterface_.reset();

    Utilities::Localization::SaveLocTexts(fmt::format("assets/locale/{}.txt", options_->locale).c_str());
}

void NextEngine::RegisterJSCallback(std::function<void(double)> callback)
{
    if (quickJSEngine_)
    {
        quickJSEngine_->RegisterTickCallback(std::move(callback));
    }
}

void NextEngine::AddTimerTask(double delay, DelayedTask task) { delayedTasks_.push_back({time_ + delay, delay, task}); }

void NextEngine::PlaySound(const std::string& soundName, bool loop, float volume)
{
    if (audioEngine_)
    {
        audioEngine_->PlaySound(soundName, loop, volume);
    }
}

void NextEngine::PauseSound(const std::string& soundName, bool pause)
{
    if (audioEngine_)
    {
        audioEngine_->PauseSound(soundName, pause);
    }
}

bool NextEngine::IsSoundPlaying(const std::string& soundName)
{
    if (!audioEngine_)
    {
        return false;
    }

    return audioEngine_->IsSoundPlaying(soundName);
}

void NextEngine::SaveScreenShot(const std::string& filename, int x, int y, int width, int height)
{
    ScreenShot::SaveSwapChainToFile(renderer_.get(), filename, x, y, width, height);
}

glm::dvec2 NextEngine::GetMousePos()
{
    float fx{}, fy{};
    SDL_GetMouseState(&fx, &fy);
    return glm::dvec2(fx, fy);
}

void NextEngine::RequestClose() { window_->Close(); }

void NextEngine::RequestMinimize() { window_->Minimize(); }

bool NextEngine::IsBorderlessFullscreen() const
{
    if (!window_)
    {
        return userSettings_.BorderlessFullscreen;
    }

    return window_->IsBorderlessFullscreen();
}

bool NextEngine::SetBorderlessFullscreen(bool enable)
{
    userSettings_.BorderlessFullscreen = enable;
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
        userSettings_.BorderlessFullscreen = !userSettings_.BorderlessFullscreen;
        return true;
    }

    const bool success = window_->ToggleBorderlessFullscreen();
    if (success)
    {
        userSettings_.BorderlessFullscreen = window_->IsBorderlessFullscreen();
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

bool NextEngine::IsMaximumed() { return window_->IsMaximumed(); }

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

void NextEngine::RequestScreenShot(std::string filename)
{
    auto time = std::time(nullptr);
    std::string screenshotFilename =
        filename.empty() ? fmt::format("screenshot_{:%Y-%m-%d-%H-%M-%S}", *std::localtime(&time)) : filename;
    SaveScreenShot(screenshotFilename, 0, 0, 0, 0);
}

void NextEngine::RequestHighQualityScreenShot(const std::string& filename, uint32_t accumulateFrames)
{
    if (hqCaptureFramesRemaining_ > 0)
    {
        spdlog::warn("High quality capture already in progress, ignoring request");
        return;
    }

    // Save current state
    hqCapturePrevProgressive_ = progressiveRendering_;
    hqCapturePrevPreFrames_ = progressivePreFrames_;

    // Setup capture
    hqCaptureTotalFrames_ = accumulateFrames;
    hqCaptureFramesRemaining_ = accumulateFrames;

    auto time = std::time(nullptr);
    hqCaptureFilename_ =
        filename.empty() ? fmt::format("hq_screenshot_{:%Y-%m-%d-%H-%M-%S}", *std::localtime(&time)) : filename;

    // Enable progressive rendering directly (skip pre-frames warmup)
    progressiveRendering_ = true;
    progressivePreFrames_ = 0;

    spdlog::info("High quality capture started: accumulating {} frames...", accumulateFrames);
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
        progressiveRendering_ = enable;
        return;
    }

    if (enable)
    {
        if (progressivePreFrames_ == 0)
        {
            progressivePreFrames_ = userSettings_.TemporalFrames * 2;
        }
    }
    else
    {
        progressivePreFrames_ = 0;
        progressiveRendering_ = false;
    }
}

VkDeviceAddress NextEngine::TryGetGPUAccelerationStructureAddress() const
{
    Vulkan::RayTracing::RayTraceBaseRenderer* rtRender =
        dynamic_cast<Vulkan::RayTracing::RayTraceBaseRenderer*>(renderer_.get());
    if (rtRender)
    {
        return rtRender->TLAS()[0].GetDeviceAddress();
    }

    return -1;
}

VkAccelerationStructureKHR NextEngine::TryGetGPUAccelerationStructureHandle() const
{
    Vulkan::RayTracing::RayTraceBaseRenderer* rtRender =
        dynamic_cast<Vulkan::RayTracing::RayTraceBaseRenderer*>(renderer_.get());
    if (rtRender)
    {
        return rtRender->TLAS()[0].Handle();
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

    ubo.FastGather = userSettings_.FastGather;
    ubo.SelectedId = scene_->GetSelectedId();
    ubo.SuperResolution = GOption->ReferenceMode ? 2 : userSettings_.SuperResolution;
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

    if (userSettings_.TAA || userSettings_.DLSS)
    {
        std::vector<glm::vec2> haltonSeq = GenerateHaltonSequence(userSettings_.TemporalFrames);
        glm::vec2 jitter = haltonSeq[totalFrames_ % userSettings_.TemporalFrames] - glm::vec2(0.5f, 0.5f);

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

    ubo.PrevViewProjection = prevUBO_.TotalFrames != 0 ? prevUBO_.ViewProjection : ubo.ViewProjection;
    ubo.PrevViewProjectionUnJit = prevUBO_.TotalFrames != 0 ? prevUBO_.ViewProjectionUnJit : ubo.ViewProjectionUnJit;

    ubo.ViewportRect =
        glm::vec4(renderer_->SwapChain().RenderOffset().x, renderer_->SwapChain().RenderOffset().y,
                  renderer_->SwapChain().RenderExtent().width, renderer_->SwapChain().RenderExtent().height);

    ubo.SunViewProjection = scene_->GetEnvSettings().GetSunViewProjection();

    ubo.SelectedId = scene_->GetSelectedId();

    // Camera Stuff
    ubo.Aperture = renderCam.Aperture;
    ubo.FocusDistance = renderCam.FocalDistance;

    // SceneStuff
    ubo.SkyRotation = scene_->GetEnvSettings().SkyRotation;
    ubo.MaxNumberOfBounces = userSettings_.MaxNumberOfBounces;
    ubo.TotalFrames = totalFrames_;
    ubo.NumberOfSamples = userSettings_.NumberOfSamples;
    ubo.NumberOfBounces = userSettings_.NumberOfBounces;
    ubo.AdaptiveSample = userSettings_.AdaptiveSample;
    ubo.AdaptiveVariance = userSettings_.AdaptiveVariance;
    ubo.AdaptiveSteps = userSettings_.AdaptiveSteps;
    ubo.TAA = userSettings_.TAA;
    ubo.RandomSeed = rand();
    ubo.SunDirection = glm::vec4(scene_->GetEnvSettings().SunDirection(), 0.0f);
    ubo.SunColor = glm::vec4(1, 1, 1, 0) * scene_->GetEnvSettings().SunIntensity;
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * scene_->GetEnvSettings().SkyIntensity;
    ubo.HasSky = scene_->GetEnvSettings().HasSky;
    ubo.HasSun = scene_->GetEnvSettings().HasSun && scene_->GetEnvSettings().SunIntensity > 0;

    if (ubo.HasSun != prevUBO_.HasSun || ubo.SunDirection != prevUBO_.SunDirection)
    {
        scene_->MarkEnvDirty();
    }

    ubo.ShowHeatmap = showFlags_.ShowVisualDebug;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.DebugDraw_Lighting = showFlags_.DebugDraw_Lighting;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = progressiveRendering_ ? 256 : userSettings_.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();

    ubo.PaperWhiteNit = userSettings_.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    ubo.BFSigma = userSettings_.DenoiseSigma;
    ubo.BFSigmaLum = userSettings_.DenoiseSigmaLum;
    ubo.BFSigmaNormal = userSettings_.DenoiseSigmaNormal;

    ubo.BFSize = userSettings_.Denoiser ? userSettings_.DenoiseSize : 0;

#if WITH_OIDN
    ubo.BFSize = 0;
#endif

    ubo.ShowEdge = showFlags_.ShowEdge;
    ubo.ProgressiveRender = progressiveRendering_;
    ubo.SceneEpsilonScale = userSettings_.SceneEpsilonScale;
    const float ambientCubeUnit = Assets::SanitizeAmbientCubeUnit(userSettings_.AmbientCubeUnit);
    const glm::vec3 ambientCubeOffsetBias =
        glm::vec3(userSettings_.AmbientCubeOffsetX, userSettings_.AmbientCubeOffsetY, userSettings_.AmbientCubeOffsetZ);
    const uint32_t ambientCubeCascadeCount =
        Assets::SanitizeAmbientCubeCascadeCount(userSettings_.AmbientCubeCascadeCount);
    const float ambientCubeCascadeRatio =
        Assets::SanitizeAmbientCubeCascadeRatio(userSettings_.AmbientCubeCascadeRatio);
    ubo.AmbientCubeUnit = ambientCubeUnit;
    ubo.AmbientCubeOffset = Assets::CalculateAmbientCubeOffset(ambientCubeUnit, ambientCubeOffsetBias);
    ubo.AmbientCubeCascadeParams = glm::vec4(float(ambientCubeCascadeCount), ambientCubeCascadeRatio, 0.0f, 0.0f);

    // Other Setup
    renderer_->supportDenoiser_ = userSettings_.Denoiser;
    renderer_->visualDebug_ = showFlags_.ShowVisualDebug;
    // UBO Backup, for motion vector calc
    prevUBO_ = ubo;

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

    scene_.reset(new Assets::Scene(renderer_->CommandPool(), renderer_->supportRayTracing_));
    renderer_->SetScene(scene_);
    renderer_->OnPostLoadScene();

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextEngine::OnRendererCreateSwapChain()
{
    if (userInterface_.get() == nullptr)
    {
        userInterface_.reset(new UserInterface(
            this, renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer(), userSettings_,
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
    static double lastTimestamp = 0.0;
    double now = GetWindow().GetTime();

    // Record delta time between calls to Render.
    if (totalFrames_ % 30 == 0)
    {
        const auto timeDelta = now - lastFrameTime_;
        lastFrameTime_ = now;
        frameRate_ = static_cast<float>(30 / timeDelta);
    }

    // Render the UI
    Statistics stats = {};

    stats.FrameTime = static_cast<float>((now - lastTimestamp) * 1000.0);
    lastTimestamp = now;

    stats.Stats["gpu"] = renderer_->Device().DeviceProperties().deviceName;

    stats.FramebufferSize = GetWindow().FramebufferSize();
    stats.RenderSize = renderer_->SwapChain().RenderExtent();
    stats.FrameRate = frameRate_;
    stats.RenderTime = GetTime();

    stats.TotalFrames = totalFrames_;
    stats.InstanceCount = static_cast<uint32_t>(scene_->GetNodeProxys().size());
    stats.NodeCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    // Renderer::visualDebug_ = userSettings_.ShowVisualDebug;
    userInterface_->PreRender();
    if (!gameInstance_->OnRenderUI())
    {
        userInterface_->Render(stats, renderer_->GpuTimer(), scene_.get());
    }
    userInterface_->PostRender(commandBuffer, renderer_->SwapChain(), imageIndex);
}

void NextEngine::OnKey(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const bool altPressed = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
        const bool isAltEnter =
            altPressed && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER);
        const bool isF11 = event.key.key == SDLK_F11;

        if (isAltEnter || isF11)
        {
            if (cvarSystem_)
            {
                auto result = cvarSystem_->ExecuteCommand("cvar.toggle sys.fullscreen");
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
                    auto command = std::make_unique<DuplicateNodesCommand>(GetScene(), selectedIds);
                    if (ExecuteCommand(std::move(command)))
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
                auto command = std::make_unique<DeleteNodesCommand>(GetScene(), selectedIds);
                if (ExecuteCommand(std::move(command)))
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
}

bool NextEngine::ExecuteCommand(std::unique_ptr<ICommand> command)
{
    return commandHistory_.Execute(std::move(command));
}

bool NextEngine::UndoCommand() { return commandHistory_.Undo(); }

bool NextEngine::RedoCommand() { return commandHistory_.Redo(); }

bool NextEngine::CanUndo() const { return commandHistory_.CanUndo(); }

bool NextEngine::CanRedo() const { return commandHistory_.CanRedo(); }

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

    if (SceneList::IsSupportedScenePath(droppedPath))
    {
        RequestLoadScene(path);
        return;
    }

    std::string ext = droppedPath.has_extension() ? droppedPath.extension().string() : std::string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".hdr")
    {
        uint32_t newTextureId = Assets::GlobalTexturePool::GetInstance()->LoadHDRTexture(path);
        scene_->GetEnvSettings().SkyIdx = newTextureId;
        // userSettings_. = 0;
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

void NextEngine::OnRendererBeforeNextFrame() { TaskCoordinator::GetInstance()->Tick(); }

void NextEngine::RequestLoadScene(std::string sceneFileName)
{
    AddTickedTask(
        [this, sceneFileName](double deltaSeconds) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            LoadScene(sceneFileName);
            return true;
        });
}

void NextEngine::RequestLoadSceneAdd(std::string sceneFileName)
{
    SceneAppendOptions options{};
    RequestLoadSceneAdd(std::move(sceneFileName), options);
}

void NextEngine::RequestLoadSceneAdd(std::string sceneFileName, const SceneAppendOptions& options)
{
    AddTickedTask(
        [this, sceneFileName, options](double deltaSeconds) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            LoadSceneAdd(sceneFileName, options);
            return true;
        });
}

void NextEngine::LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad)
{
    // wait all task finish
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    TaskCoordinator::GetInstance()->WaitForAllParralledTask();

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
    TaskCoordinator::GetInstance()->AddTask(
        [ctx, sceneFileName](ResTask& task)
        {
            SceneTaskContext taskContext{};
            const auto timer = std::chrono::high_resolution_clock::now();

            taskContext.success = SceneList::LoadScene(sceneFileName, *ctx.cameraState, *ctx.nodes, *ctx.models,
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
        [this, ctx, sceneFileName, onGpuLoad](ResTask& task) mutable
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

                totalFrames_ = 0;
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

void NextEngine::LoadScene(std::string sceneFileName)
{
    scene_->CleanUp();
    physicsEngine_->OnSceneDestroyed();
    Assets::GlobalTexturePool::GetInstance()->FreeNonSystemTextures();

    LaunchLoadSceneTask(sceneFileName,
                        [this, sceneFileName](SceneLoadContext& ctx)
                        {
                            const auto timer = std::chrono::high_resolution_clock::now();
                            scene_->GetEnvSettings().Reset();
                            scene_->SetEnvSettings(*ctx.cameraState);

                            gameInstance_->OnSceneUnloaded();
                            physicsEngine_->OnSceneStarted();

                            renderer_->OnPreLoadScene();

                            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights,
                                                              *ctx.tracks);
                            scene_->Reload(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
                            scene_->PostLoad(*ctx.skeletons);
                            scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->supportRayTracing_);
                            renderer_->SetScene(scene_);

                            userSettings_.CameraIdx = 0;
                            assert(!scene_->GetEnvSettings().cameras.empty());
                            scene_->SetRenderCamera(scene_->GetEnvSettings().cameras[0]);

                            gameInstance_->OnSceneLoaded();

                            float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                                std::chrono::high_resolution_clock::now() - timer)
                                                .count();
                            SPDLOG_INFO("uploaded scene [{}] to gpu in {:.2f}ms",
                                        std::filesystem::path(sceneFileName).filename().string(), elapsed * 1000.f);
                        });
}

void NextEngine::LoadSceneAdd(std::string sceneFileName)
{
    SceneAppendOptions options{};
    LoadSceneAdd(std::move(sceneFileName), options);
}

void NextEngine::LoadSceneAdd(std::string sceneFileName, const SceneAppendOptions& options)
{
    LaunchLoadSceneTask(
        sceneFileName,
        [this, sceneFileName, options](SceneLoadContext& ctx)
        {
            const auto timer = std::chrono::high_resolution_clock::now();

            renderer_->OnPreLoadScene();

            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);

            std::string name = std::filesystem::path(sceneFileName).stem().string();
            std::shared_ptr<Assets::Node> rootNode =
                scene_->Append(name, *ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks, *ctx.skeletons);
            if (options.placeOnHit && rootNode)
            {
                rootNode->SetTranslation(options.hitPosition);
                rootNode->RecalcTransform(true);
            }
            scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->supportRayTracing_);
            renderer_->SetScene(scene_);

            // gameInstance_->OnSceneLoaded(); // Maybe trigger this too?

            float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                std::chrono::high_resolution_clock::now() - timer)
                                .count();
            SPDLOG_INFO("uploaded scene [{}] to gpu in {:.2f}ms",
                        std::filesystem::path(sceneFileName).filename().string(), elapsed * 1000.f);
        });
}

void NextEngine::InitPhysics() {}
