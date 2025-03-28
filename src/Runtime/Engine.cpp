#include "Engine.hpp"
#include "UserInterface.hpp"
#include "UserSettings.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Texture.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Device.hpp"
#include "ScreenShot.hpp"

#include <iostream>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <Utilities/FileHelper.hpp>
#include <filesystem>

#include "Options.hpp"
#include "TaskCoordinator.hpp"
#include "Utilities/Localization.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredRenderer.hpp"
#include "Platform/PlatformCommon.h"

#include <ThirdParty/quickjs-ng/quickjspp.hpp>

#define MINIAUDIO_IMPLEMENTATION
#include "ThirdParty/miniaudio/miniaudio.h"

#define _USE_MATH_DEFINES
#include <math.h>

#define BUILDVER(X) std::string buildver(#X);
#include "build.version"
#include "NextPhysics.h"

#if ANDROID

#define GLFW_MOUSE_BUTTON_1         0
#define GLFW_MOUSE_BUTTON_2         1
#define GLFW_MOUSE_BUTTON_3         2
#define GLFW_MOUSE_BUTTON_4         3
#define GLFW_MOUSE_BUTTON_5         4
#define GLFW_MOUSE_BUTTON_6         5
#define GLFW_MOUSE_BUTTON_7         6
#define GLFW_MOUSE_BUTTON_8         7
#define GLFW_MOUSE_BUTTON_LAST      GLFW_MOUSE_BUTTON_8
#define GLFW_MOUSE_BUTTON_LEFT      GLFW_MOUSE_BUTTON_1
#define GLFW_MOUSE_BUTTON_RIGHT     GLFW_MOUSE_BUTTON_2
#define GLFW_MOUSE_BUTTON_MIDDLE    GLFW_MOUSE_BUTTON_3

#define GLFW_RELEASE                0
#define GLFW_PRESS                  1
#define GLFW_REPEAT                 2

#endif

ENGINE_API Options* GOption = nullptr;

namespace NextRenderer
{
    std::string GetBuildVersion()
    {
        return buildver;
    }

    Vulkan::VulkanBaseRenderer* CreateRenderer(uint32_t rendererType, Vulkan::Window* window, const VkPresentModeKHR presentMode, const bool enableValidationLayers)
    {
        switch(rendererType)
        {
            case 0:
            case 1:
            case 2:
            case 3:
                {
                    auto ptr = new Vulkan::RayTracing::RayTraceBaseRenderer(window, presentMode, enableValidationLayers);
                    if(!ptr->supportRayTracing_) {
                        break;
                    }
#if !ANDROID
                    ptr->RegisterLogicRenderer(Vulkan::ERT_PathTracing);
#endif
                    ptr->RegisterLogicRenderer(Vulkan::ERT_Hybrid);
                    ptr->RegisterLogicRenderer(Vulkan::ERT_ModernDeferred);
                    ptr->RegisterLogicRenderer(Vulkan::ERT_LegacyDeferred);
                    ptr->SwitchLogicRenderer(static_cast<Vulkan::ERendererType>(rendererType));
                    return ptr;    
                }
            default: break;
        }
        // fallback renderer
        auto fallbackptr =  new Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers);
        fallbackptr->RegisterLogicRenderer(Vulkan::ERT_ModernDeferred);
        fallbackptr->RegisterLogicRenderer(Vulkan::ERT_LegacyDeferred);
        fallbackptr->SwitchLogicRenderer(Vulkan::ERT_ModernDeferred);
        return fallbackptr;
    }

}

namespace
{
    const bool EnableValidationLayers =
#if defined(NDEBUG) ||  defined(ANDROID)
        false;
#else
        true;
#endif

    struct SceneTaskContext
    {
        bool success;
        float elapsed;
        std::array<char, 256> outputInfo;
    };
}

UserSettings CreateUserSettings(const Options& options)
{
    SceneList::ScanScenes();
    
    UserSettings userSettings{};

    userSettings.RendererType = options.RendererType;
    userSettings.SceneIndex = 0;
        
    userSettings.NumberOfSamples = options.Samples;
    userSettings.NumberOfBounces = options.Bounces;
    userSettings.MaxNumberOfBounces = options.MaxBounces;

    userSettings.AdaptiveSample = options.AdaptiveSample;
    userSettings.AdaptiveVariance = 6.0f;
    userSettings.AdaptiveSteps = 4;
    userSettings.TAA = true;

    userSettings.ShowSettings = true;
    userSettings.ShowOverlay = true;

    userSettings.ShowVisualDebug = false;
    userSettings.HeatmapScale = 0.5f;

    userSettings.UseCheckerBoardRendering = false;
    userSettings.TemporalFrames = options.Temporal;

    userSettings.Denoiser = !options.NoDenoiser;

    userSettings.PaperWhiteNit = 600.f;
    
    userSettings.RequestRayCast = false;

    userSettings.DenoiseSigma = 0.5f;
    userSettings.DenoiseSigmaLum = 25.0f;
    userSettings.DenoiseSigmaNormal = 0.005f;
    userSettings.DenoiseSize = 5;

    userSettings.ShowEdge = false;

    userSettings.FastGather = false;
    
#if ANDROID
    userSettings.NumberOfSamples = 1;
    userSettings.Denoiser = false;
    userSettings.FastGather = true;
#endif

    return userSettings;
}

NextEngine* NextEngine::instance_ = nullptr;

NextEngine::NextEngine(Options& options, void* userdata)
{
    instance_ = this;

    status_ = NextRenderer::EApplicationStatus::Starting;

    packageFileSystem_.reset(new Utilities::Package::FPackageFileSystem(Utilities::Package::EPM_OsFile));

    Vulkan::Window::InitGLFW();
    // Create Window
    Vulkan::WindowConfig windowConfig
    {
        "gkNextRenderer " + NextRenderer::GetBuildVersion(),
        options.Width,
        options.Height,
        options.Fullscreen,
        options.Fullscreen,
        !options.Fullscreen,
        options.SaveFile,
        userdata,
        options.ForceSDR
    };
    gameInstance_ = CreateGameInstance(windowConfig, options, this);
    userSettings_ = CreateUserSettings(options);
    window_.reset( new Vulkan::Window(windowConfig));
        
    // Initialize Renderer
    renderer_.reset( NextRenderer::CreateRenderer(options.RendererType, window_.get(), static_cast<VkPresentModeKHR>(options.PresentMode), EnableValidationLayers) );
    rendererType = options.RendererType;
    
    renderer_->DelegateOnDeviceSet = [this]()->void{OnRendererDeviceSet();};
    renderer_->DelegateCreateSwapChain = [this]()->void{OnRendererCreateSwapChain();};
    renderer_->DelegateDeleteSwapChain = [this]()->void{OnRendererDeleteSwapChain();};
    renderer_->DelegateBeforeNextTick = [this]()->void{OnRendererBeforeNextFrame();};
    renderer_->DelegateGetUniformBufferObject = [this](VkOffset2D offset, VkExtent2D extend)->Assets::UniformBufferObject{ return GetUniformBufferObject(offset, extend);};
    renderer_->DelegatePostRender = [this](VkCommandBuffer commandBuffer, uint32_t imageIndex)->void{OnRendererPostRender(commandBuffer, imageIndex);};

    // Initialize IO
    window_->OnKey = [this](const int key, const int scancode, const int action, const int mods) { OnKey(key, scancode, action, mods); };
    window_->OnCursorPosition = [this](const double xpos, const double ypos) { OnCursorPosition(xpos, ypos); };
    window_->OnMouseButton = [this](const int button, const int action, const int mods) { OnMouseButton(button, action, mods); };
    window_->OnScroll = [this](const double xoffset, const double yoffset) { OnScroll(xoffset, yoffset); };
    window_->OnDropFile = [this](int path_count, const char* paths[]) { OnDropFile(path_count, paths); };

    // Initialize Localization
    Utilities::Localization::ReadLocTexts(fmt::format("assets/locale/{}.txt", GOption->locale).c_str());

    // Initialize JS Engine
    JSRuntime_.reset(new qjs::Runtime());
    JSContext_.reset(new qjs::Context(*JSRuntime_));
}

NextEngine::~NextEngine()
{
    Utilities::Localization::SaveLocTexts(fmt::format("assets/locale/{}.txt", GOption->locale).c_str());

    scene_.reset();
    renderer_.reset();
    window_.reset();

    Vulkan::Window::TerminateGLFW();
}

void NextEngine::Start()
{
    renderer_->Start();

    ma_result result;
    audioEngine_.reset( new ma_engine() );

    result = ma_engine_init(NULL, audioEngine_.get());
    if (result != MA_SUCCESS) {
        return;
    }

    physicsEngine_.reset(new NextPhysics());
    physicsEngine_->Start();
    
    gameInstance_->OnInit();
    
    // init js engine
    InitJSEngine();
}

bool NextEngine::Tick()
{
    PERFORMANCEAPI_INSTRUMENT_FUNCTION();
    
    // make sure the output is flushed
    std::cout << std::flush;
    
    if(rendererType != userSettings_.RendererType)
    {
        rendererType = userSettings_.RendererType;
        renderer_->SwitchLogicRenderer(static_cast<Vulkan::ERendererType>(rendererType));
    }
    
    // delta time calc
    const auto prevTime = time_;
    time_ = GetWindow().GetTime();
    deltaSeconds_ = time_ - prevTime;
    float invDelta = static_cast<float>(deltaSeconds_) / 60.0f;
    smoothedDeltaSeconds_ = glm::mix(smoothedDeltaSeconds_, deltaSeconds_, invDelta * 100.0f);
    
    // Scene Update
    if(scene_)
    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickScene", "");
        scene_->Tick(static_cast<float>(deltaSeconds_));
    }

    physicsEngine_->Tick(deltaSeconds_);

    if (JSTickCallback_)
    {
        JSTickCallback_(deltaSeconds_);
    }
    
    // Renderer Tick
#if !ANDROID
    glfwPollEvents();
#endif
    // tick
    if (status_ == NextRenderer::EApplicationStatus::Running)
    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickGameInstance", "");
        gameInstance_->OnTick(deltaSeconds_);
    }

    {
        PERFORMANCEAPI_INSTRUMENT_DATA("Engine::TickTasks", "");

        // iterate the tickedTasks_, if return true, remove it
        for( auto it = tickedTasks_.begin(); it != tickedTasks_.end(); )
        {
            if( (*it)(deltaSeconds_) )
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
    for( auto it = delayedTasks_.begin(); it != delayedTasks_.end(); )
    {
        if( time_ > it->triggerTime )
        {
            // update the next trigger time
            it->triggerTime = time_ + it->loopTime;

            // execute
            if( it->task() )
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
#if ANDROID
    return false;
#else
    window_->attemptDragWindow();
    return glfwWindowShouldClose( window_->Handle() ) != 0;
#endif
}

void NextEngine::End()
{
    physicsEngine_->Stop();
    ma_engine_uninit(audioEngine_.get());
    gameInstance_->OnDestroy();
    renderer_->End();
    userInterface_.reset();
}

void NextEngine::RegisterJSCallback(std::function<void(double)> callback)
{
    JSTickCallback_ = callback;
}

void NextEngine::AddTimerTask(double delay, DelayedTask task)
{
    delayedTasks_.push_back( { time_ + delay, delay, task} );
}

void NextEngine::PlaySound(const std::string& soundName, bool loop, float volume)
{
    if( soundMaps_.find(soundName) == soundMaps_.end() )
    {
        auto sound = new ma_sound();
        ma_sound_init_from_file(audioEngine_.get(), Utilities::FileHelper::GetPlatformFilePath(soundName.c_str()).c_str(), 0, NULL, NULL, sound);
        soundMaps_[soundName].reset(sound);
    }

    ma_sound* sound = soundMaps_[soundName].get();

    // restart the sound
    ma_sound_stop(sound);
    ma_sound_set_looping(sound, loop);
    ma_sound_set_volume(sound, volume);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void NextEngine::PauseSound(const std::string& soundName, bool pause)
{
    if( soundMaps_.find(soundName) == soundMaps_.end() )
    {
        return;
    }

    ma_sound* sound = soundMaps_[soundName].get();
    pause ? ma_sound_stop(sound) : ma_sound_start(sound);
}

bool NextEngine::IsSoundPlaying(const std::string& soundName)
{
    if( soundMaps_.find(soundName) == soundMaps_.end() )
    {
        return false;
    }
    ma_sound* sound = soundMaps_[soundName].get();
    return ma_sound_is_playing(sound);
}

void NextEngine::SaveScreenShot(const std::string& filename, int x, int y, int width, int height)
{
    ScreenShot::SaveSwapChainToFileFast(renderer_.get(), filename, x, y, width, height);
}

glm::vec3 NextEngine::ProjectScreenToWorld(glm::vec2 locationSS)
{
    glm::vec3 org;
    glm::vec3 dir;
    GetScreenToWorldRay(locationSS, org, dir );
    return dir;
}

glm::vec3 NextEngine::ProjectWorldToScreen(glm::vec3 locationWS)
{
    auto vkoffset = GetRenderer().SwapChain().RenderOffset();
    auto vkextent = GetRenderer().SwapChain().RenderExtent();
    
    glm::vec4 transformed = prevUBO_.ViewProjection * glm::vec4(locationWS, 1.0f);
    transformed = transformed / transformed.w;
    // from ndc to screenspace
    transformed.x += 1.0f;
    transformed.x *= vkextent.width / 2;
    transformed.y += 1.0f;
    transformed.y *= vkextent.height / 2;
    
    transformed.x += vkoffset.x;
    transformed.y += vkoffset.y;
    
    return transformed;
}

void NextEngine::GetScreenToWorldRay(glm::vec2 locationSS, glm::vec3& org, glm::vec3& dir)
{
    // should consider rt offset
    auto vkoffset = GetRenderer().SwapChain().RenderOffset();
    auto vkextent = GetRenderer().SwapChain().RenderExtent();
    glm::vec2 offset = {vkoffset.x, vkoffset.y};
    glm::vec2 extent = {vkextent.width, vkextent.height};
    glm::vec2 pixel = locationSS - glm::vec2(offset.x, offset.y);
    glm::vec2 uv = pixel / extent * glm::vec2(2.0,2.0) - glm::vec2(1.0,1.0);
    glm::vec4 origin = prevUBO_.ModelViewInverse * glm::vec4(0, 0, 0, 1);
    glm::vec4 target = prevUBO_.ProjectionInverse * (glm::vec4(uv.x, uv.y, 1, 1));
    glm::vec3 raydir = prevUBO_.ModelViewInverse * glm::vec4(normalize((glm::vec3(target) - glm::vec3(0.0,0.0,0.0))), 0.0);
    org = glm::vec3(origin);
    dir = raydir;
}

void NextEngine::DrawAuxLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size)
{
    auto transformedFrom = ProjectWorldToScreen(from);
    auto transformedTo = ProjectWorldToScreen(to);

    // should clip with z == 1, clip to new point
    if(transformedFrom.z < 1 && transformedTo.z < 1)
    {
        userInterface_->DrawLine(transformedFrom.x, transformedFrom.y, transformedTo.x, transformedTo.y, size, color );
    }
}

void NextEngine::DrawAuxBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size)
{
    // Draw the box with 12 lines
    DrawAuxLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color, size);
    DrawAuxLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color, size);
    DrawAuxLine(glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z), color, size);
    DrawAuxLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, min.y, min.z), color, size);

    DrawAuxLine(glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z), color, size);
    DrawAuxLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color, size);
    DrawAuxLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color, size);
    DrawAuxLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, min.y, max.z), color, size);

    DrawAuxLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z), color, size);
    DrawAuxLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color, size);
    DrawAuxLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color, size);
    DrawAuxLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, max.y, max.z), color, size);
}

static std::vector<int32_t> auxCounter;
void NextEngine::DrawAuxPoint(glm::vec3 location, glm::vec4 color, float size, int32_t durationInTick)
{
    if (durationInTick > 0)
    {
        auxCounter.push_back(durationInTick);
        int32_t id = static_cast<int32_t>(auxCounter.size()) - 1;
        AddTickedTask( [this, location, color, size, id](double deltaSeconds)->bool
        {
            auto transformed = ProjectWorldToScreen(location);
            if(transformed.z < 1)
            {
                userInterface_->DrawPoint(transformed.x, transformed.y, size, color);
            }
            return (auxCounter[id] -= 1) <= 0;
        });
    }
    else
    {
        auto transformed = ProjectWorldToScreen(location);
        if(transformed.z < 1)
        {
            userInterface_->DrawPoint(transformed.x, transformed.y, size, color);
        }
    }
}

glm::dvec2 NextEngine::GetMousePos()
{
    double x{},y{};
#if !ANDROID
    glfwGetCursorPos( window_->Handle(), &x, &y );
#endif
    return glm::dvec2(x,y);
}

void NextEngine::RequestClose()
{
    window_->Close();
}

void NextEngine::RequestMinimize()
{
    window_->Minimize();
}

bool NextEngine::IsMaximumed()
{
    return window_->IsMaximumed();
}

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
    std::string screenshot_filename = filename.empty() ? fmt::format("screenshot_{:%Y-%m-%d-%H-%M-%S}", fmt::localtime(std::time(nullptr))) : filename;
    SaveScreenShot(screenshot_filename, 0, 0, 0, 0);
}

// 生成一个随机抖动偏移
glm::vec2 GenerateJitter(float screenWidth, float screenHeight) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-0.5f, 0.5f);

    float jitterX = static_cast<float>(dis(gen)) / screenWidth;
    float jitterY = static_cast<float>(dis(gen)) / screenHeight;

    return glm::vec2(jitterX, jitterY);
}

// 创建抖动矩阵
glm::mat4 CreateJitterMatrix(float jitterX, float jitterY) {
    glm::mat4 jitterMatrix = glm::mat4(1.0f);
    jitterMatrix[3][0] = jitterX;
    jitterMatrix[3][1] = jitterY;
    return jitterMatrix;
}

// 调制投影矩阵
glm::mat4 RandomJitterProjectionMatrix(const glm::mat4& projectionMatrix, float screenWidth, float screenHeight) {
    glm::vec2 jitter = GenerateJitter(screenWidth, screenHeight);
    glm::mat4 jitterMatrix = CreateJitterMatrix(jitter.x, jitter.y);
    return jitterMatrix * projectionMatrix;
}

// 生成Halton序列的单一维度
float HaltonSequence(int index, int base) {
    float f = 1.0f;
    float result = 0.0f;
    while (index > 0) {
        f = f / base;
        result = result + f * (index % base);
        index = index / base;
    }
    return result;
}

// 生成2D Halton序列
std::vector<glm::vec2> GenerateHaltonSequence(int count) {
    std::vector<glm::vec2> sequence;
    for (int i = 0; i < count; ++i) {
        float x = HaltonSequence(i + 1, 2);  // 基数2
        float y = HaltonSequence(i + 1, 3);  // 基数3
        sequence.push_back(glm::vec2(x, y));
    }
    return sequence;
}

glm::mat4 HaltonJitterProjectionMatrix(const glm::mat4& projectionMatrix, float screenWidth, float screenHeight) {
    glm::vec2 jitter = GenerateJitter(screenWidth, screenHeight);
    glm::mat4 jitterMatrix = CreateJitterMatrix(jitter.x, jitter.y);
    return jitterMatrix * projectionMatrix;
}

glm::ivec2 NextEngine::GetMonitorSize(int monitorIndex) const
{
    glm::ivec2 pos{0,0};
    glm::ivec2 size{1920,1080};
#if !ANDROID
    glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &pos.x, &pos.y, &size.x, &size.y);
#endif
    return size;
}

void NextEngine::RayCastGPU(glm::vec3 rayOrigin, glm::vec3 rayDir,
    std::function<bool(Assets::RayCastResult rayResult)> callback)
{
    if( renderer_->supportRayTracing_ )
    {
        // set in gpu directly
        renderer_->SetRaycastRay(rayOrigin, rayDir, callback);
    }
    else
    {
        // CPU Raycast in scene
        // Assets::RayCastResult result = scene_->RayCastInCPU(rayOrigin, rayDir);
        // callback(result);
    }
}

Assets::UniformBufferObject NextEngine::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent)
{
    Assets::UniformBufferObject ubo = {};

    // a copy, simple struct
    Assets::Camera renderCam = scene_->GetRenderCamera();
    gameInstance_->OverrideRenderCamera(renderCam);
    ubo.ModelView = renderCam.ModelView;
    
    scene_->OverrideModelView(ubo.ModelView);
    ubo.Projection = glm::perspective(glm::radians(renderCam.FieldOfView),
                                      extent.width / static_cast<float>(extent.height), 0.1f, 10000.0f);

    ubo.BakeWithGPU = userSettings_.BakeWithGPU;
    ubo.FastGather = userSettings_.FastGather;
    ubo.FastInterpole = userSettings_.FastInterpole;
    if (userSettings_.TAA)
    {
        // std::vector<glm::vec2> haltonSeq = GenerateHaltonSequence(userSettings_.TemporalFrames);
        // glm::vec2 jitter = haltonSeq[totalFrames_ % userSettings_.TemporalFrames] - glm::vec2(0.5f,0.5f);
        // glm::mat4 jitterMatrix = CreateJitterMatrix(jitter.x / static_cast<float>(extent.width), jitter.y / static_cast<float>(extent.height));
        // //ubo.Projection = jitterMatrix * ubo.Projection;
        //
        // ubo.Projection[0][2] = jitter.x / static_cast<float>(extent.width) * 2.0f;
        // ubo.Projection[1][2] = jitter.y / static_cast<float>(extent.height) * 2.0f;
    }
    
    ubo.Projection[1][1] *= -1;
    
    // handle android vulkan pre rotation
#if ANDROID
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
    
    ubo.Projection = glm::perspective(glm::radians(renderCam.FieldOfView),
                                      extent.height / static_cast<float>(extent.width), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
    ubo.Projection = pre_rotate_mat * ubo.Projection;
#endif
    
    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    
    ubo.PrevViewProjection = prevUBO_.TotalFrames != 0 ? prevUBO_.ViewProjection : ubo.ViewProjection;

    ubo.ViewportRect = glm::vec4(offset.x, offset.y, extent.width, extent.height);

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
    ubo.SunDirection = glm::vec4( scene_->GetEnvSettings().SunDirection(), 0.0f );
    ubo.SunColor = glm::vec4(1,1,1, 0) * scene_->GetEnvSettings().SunIntensity;
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * scene_->GetEnvSettings().SkyIntensity;
    ubo.HasSky = scene_->GetEnvSettings().HasSky;
    ubo.HasSun =scene_->GetEnvSettings().HasSun && scene_->GetEnvSettings().SunIntensity > 0;
    
    if (ubo.HasSun != prevUBO_.HasSun || ubo.SunDirection != prevUBO_.SunDirection)
    {
        scene_->MarkEnvDirty();
    }

    ubo.ShowHeatmap = userSettings_.ShowVisualDebug;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = progressiveRendering_ ? (1024 / userSettings_.TemporalFrames) : userSettings_.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();
    
    ubo.PaperWhiteNit = userSettings_.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    ubo.BFSigma = userSettings_.DenoiseSigma;
    ubo.BFSigmaLum = userSettings_.DenoiseSigmaLum;
    ubo.BFSigmaNormal = userSettings_.DenoiseSigmaNormal;
    ubo.BFSize = userSettings_.Denoiser ? userSettings_.DenoiseSize : 0;
    
    ubo.ShowEdge = userSettings_.ShowEdge;

    ubo.ProgressiveRender = progressiveRendering_;

    // Other Setup
    renderer_->supportDenoiser_ = userSettings_.Denoiser;
    renderer_->visualDebug_ = userSettings_.ShowVisualDebug;
    
    // UBO Backup, for motion vector calc
    prevUBO_ = ubo;

    return ubo;
}

void NextEngine::OnRendererDeviceSet()
{
    // global textures
    // texture id 0: dynamic hdri sky
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/std_env.hdr");
    
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/canary_wharf_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/kloppenheim_01_puresky_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/kloppenheim_07_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/river_road_2.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/rainforest_trail_1k.hdr");

    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/studio_small_03_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/studio_small_09_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/sunset_fairway_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/umhlanga_sunrise_1k.hdr");
    Assets::GlobalTexturePool::LoadHDRTexture("assets/textures/shanghai_bund_1k.hdr");

    //if(GOption->HDRIfile != "") Assets::GlobalTexturePool::UpdateHDRTexture(0, GOption->HDRIfile.c_str(), Vulkan::SamplerConfig());
        
    scene_.reset(new Assets::Scene(renderer_->CommandPool(), renderer_->supportRayTracing_));
    renderer_->SetScene(scene_);
    renderer_->OnPostLoadScene();

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextEngine::OnRendererCreateSwapChain()
{
    if(userInterface_.get() == nullptr)
    {
        userInterface_.reset(new UserInterface(this, renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer(),
                                   userSettings_, [this]()->void
                                   {
                                       gameInstance_->OnPreConfigUI();
                                   },
                                   [this]()->void{
            gameInstance_->OnInitUI();
        }));
    }
    userInterface_->OnCreateSurface(renderer_->SwapChain(), renderer_->DepthBuffer());
}

void NextEngine::OnRendererDeleteSwapChain()
{
    if(userInterface_.get() != nullptr)
    {
        userInterface_->OnDestroySurface();
    }
}

void NextEngine::OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    static float frameRate = 0.0;
    static double lastTime = 0.0;
    static double lastTimestamp = 0.0;
    double now = GetWindow().GetTime();
    
    // Record delta time between calls to Render.
    if(totalFrames_ % 30 == 0)
    {
        const auto timeDelta = now - lastTime;
        lastTime = now;
        frameRate = static_cast<float>(30 / timeDelta);
    }
    
    // Render the UI
    Statistics stats = {};
    
    stats.FrameTime = static_cast<float>((now - lastTimestamp) * 1000.0);
    lastTimestamp = now;
    
    stats.Stats["gpu"] = renderer_->Device().DeviceProperties().deviceName;
    
    stats.FramebufferSize = GetWindow().FramebufferSize();
    stats.FrameRate = frameRate;
    
    stats.TotalFrames = totalFrames_;
    stats.InstanceCount = static_cast<uint32_t>(scene_->GetNodeProxys().size());
    stats.NodeCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    //Renderer::visualDebug_ = userSettings_.ShowVisualDebug;
    userInterface_->PreRender();
    if( !gameInstance_->OnRenderUI() )
    {
        userInterface_->Render(stats, renderer_->GpuTimer(), scene_.get());
    }
    userInterface_->PostRender(commandBuffer, renderer_->SwapChain(), imageIndex);
}

void NextEngine::OnKey(int key, int scancode, int action, int mods)
{
    if (userInterface_->WantsToCaptureKeyboard())
    {
        return;
    }

    if( gameInstance_->OnKey(key, scancode, action, mods) )
    {
        return;
    }
    
#if !ANDROID
    if (action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_ESCAPE: scene_->SetSelectedId(-1);
            break;
        default: break;
        }

        // Settings (toggle switches)
        switch (key)
        {
        case GLFW_KEY_F1: userSettings_.ShowSettings = !userSettings_.ShowSettings;
            break;
        case GLFW_KEY_F2: userSettings_.ShowOverlay = !userSettings_.ShowOverlay;
            break;
        default: break;
        }
    }
#endif
}

void NextEngine::OnTouch(bool down, double xpos, double ypos)
{
    OnMouseButton(GLFW_MOUSE_BUTTON_RIGHT, down ? GLFW_PRESS : GLFW_RELEASE, 0);
}

void NextEngine::OnTouchMove(double xpos, double ypos)
{
    OnCursorPosition(xpos, ypos);
}

void NextEngine::OnCursorPosition(const double xpos, const double ypos)
{
    if (!renderer_->HasSwapChain() ||
        userInterface_->WantsToCaptureKeyboard() ||
        userInterface_->WantsToCaptureMouse()
        )
    {
        return;
    }
    
    if(gameInstance_->OnCursorPosition(xpos, ypos))
    {
        return;
    }
}

void NextEngine::OnMouseButton(const int button, const int action, const int mods)
{
    if (!renderer_->HasSwapChain() ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    if(gameInstance_->OnMouseButton(button, action, mods))
    {
        return;
    }
}

void NextEngine::OnScroll(const double xoffset, const double yoffset)
{
    if (!renderer_->HasSwapChain() ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    gameInstance_->OnScroll(xoffset, yoffset);
}

void NextEngine::OnDropFile(int path_count, const char* paths[])
{
    // add glb to the last, and loaded
    if (path_count > 0)
    {
        std::string path = paths[path_count - 1];
        std::string ext = path.substr(path.find_last_of(".") + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "glb" || ext == "gltf")
        {
            //userSettings_.SceneIndex = SceneList::AddExternalScene(path);
            RequestLoadScene(path);
        }

        if( ext == "hdr")
        {
            //Assets::GlobalTexturePool::UpdateHDRTexture(0, path, Vulkan::SamplerConfig());
            //userSettings_.SkyIdx = 0;
        }
    }
}

void NextEngine::OnRendererBeforeNextFrame()
{
    TaskCoordinator::GetInstance()->Tick();
}

void NextEngine::RequestLoadScene(std::string sceneFileName)
{
    AddTickedTask([this, sceneFileName](double DeltaSeconds)->bool
    {
        if ( status_ != NextRenderer::EApplicationStatus::Running )
        {
            return false;
        }
        
        LoadScene(sceneFileName);
        return true;
    });
}

void NextEngine::LoadScene(std::string sceneFileName)
{
    // wait all task finish
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    
    status_ = NextRenderer::EApplicationStatus::Loading;
    
    std::shared_ptr< std::vector<Assets::Model> > models = std::make_shared< std::vector<Assets::Model> >();
    std::shared_ptr< std::vector< std::shared_ptr<Assets::Node> > > nodes = std::make_shared< std::vector< std::shared_ptr<Assets::Node> > >();
    std::shared_ptr< std::vector<Assets::FMaterial> > materials = std::make_shared< std::vector<Assets::FMaterial> >();
    std::shared_ptr< std::vector<Assets::LightObject> > lights = std::make_shared< std::vector<Assets::LightObject> >();
    std::shared_ptr< std::vector<Assets::AnimationTrack> > tracks = std::make_shared< std::vector<Assets::AnimationTrack> >();
    std::shared_ptr< Assets::EnvironmentSetting > cameraState = std::make_shared< Assets::EnvironmentSetting >();

    physicsEngine_->OnSceneDestroyed();
    
    // dispatch in thread task and reset in main thread
    TaskCoordinator::GetInstance()->AddTask( [cameraState, sceneFileName, models, nodes, materials, lights, tracks](ResTask& task)
    {
        SceneTaskContext taskContext {};
        const auto timer = std::chrono::high_resolution_clock::now();
        
        taskContext.success = SceneList::LoadScene( sceneFileName, *cameraState, *nodes, *models, *materials, *lights, *tracks);
        
        taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();

        std::string info = fmt::format("parsed scene [{}] on cpu in {:.2f}ms", std::filesystem::path(sceneFileName).filename().string(), taskContext.elapsed * 1000.f);
        std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
        task.SetContext( taskContext );
    },
    [this, cameraState, sceneFileName, models, nodes, materials, lights, tracks](ResTask& task)
    {
        SceneTaskContext taskContext {};
        task.GetContext( taskContext );
        if (taskContext.success )
        {
            fmt::print("{} {}{}\n", CONSOLE_GREEN_COLOR, taskContext.outputInfo.data(), CONSOLE_DEFAULT_COLOR);
            const auto timer = std::chrono::high_resolution_clock::now();
            scene_->GetEnvSettings().Reset();
            scene_->SetEnvSettings(*cameraState);

            gameInstance_->OnSceneUnloaded();
            physicsEngine_->OnSceneStarted();

            renderer_->Device().WaitIdle();
            renderer_->DeleteSwapChain();
            renderer_->OnPreLoadScene();
                    
            scene_->Reload(*nodes, *models, *materials, *lights, *tracks);
            scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->supportRayTracing_);
                    
            renderer_->SetScene(scene_);
                    
            userSettings_.CameraIdx = 0;
            assert(!scene_->GetEnvSettings().cameras.empty());
            scene_->SetRenderCamera(scene_->GetEnvSettings().cameras[0]);

            totalFrames_ = 0;
                    
            renderer_->OnPostLoadScene();
            renderer_->CreateSwapChain();

            gameInstance_->OnSceneLoaded();

            float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
            fmt::print("{} uploaded scene [{}] to gpu in {:.2f}ms{}\n", CONSOLE_GREEN_COLOR, std::filesystem::path(sceneFileName).filename().string(), elapsed * 1000.f, CONSOLE_DEFAULT_COLOR);
        }
        else
        {
            fmt::print("{} failed to load scene [{}]{}\n", CONSOLE_RED_COLOR, std::filesystem::path(sceneFileName).filename().string(), CONSOLE_DEFAULT_COLOR);
        }

        status_ = NextRenderer::EApplicationStatus::Running;
    },
    1);
}


class MyClass
{
public:
    MyClass() {}
    MyClass(std::vector<int>) {}

    double member_variable = 5.5;
    std::string member_function(const std::string& s) { return "Hello, " + s; }
};

void println(qjs::rest<std::string> args) {
    for (auto const & arg : args) { fmt::printf(arg); fmt::printf("\n"); }
}

NextEngine* GetEngine() {
    return NextEngine::GetInstance();
}

void NextEngine::InitJSEngine() {
    try
    {
        // export classes as a module
        auto& module = JSContext_->addModule("Engine");
        module.function<&println>("println");
        module.function<&GetEngine>("GetEngine");

        module.class_<NextEngine>("NextEngine")
                .fun<&NextEngine::GetTotalFrames>("GetTotalFrames")
                .fun<&NextEngine::GetTestNumber>("GetTestNumber")
                .fun<&NextEngine::RegisterJSCallback>("RegisterJSCallback")
                .fun<&NextEngine::GetScenePtr>("GetScenePtr");
        module.class_<Assets::Scene>("Scene")
                .fun<&Assets::Scene::GetIndicesCount>("GetIndicesCount");
        module.class_<NextComponent>("NextComponent")
                .constructor<>()
                .fun<&NextComponent::name_> ("name_")
                .fun<&NextComponent::id_> ("id_");

        // TODO use node.exe + tsc to compile the typescript to js realtime
        // NextRenderer::OSProcess(fmt::format("").c_str());    

        // Current load the script from file
        std::vector<uint8_t> scriptBuffer;
        if ( Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/scripts/test.js", scriptBuffer) )
        {
            JSContext_->eval( std::string_view( (char*)scriptBuffer.data()), "<import>", JS_EVAL_TYPE_MODULE);
        }
        else
        {
            fmt::print("Failed to load script\n");
        }
    }
    catch(qjs::exception)
    {
        auto exc = JSContext_->getException();
        std::cerr << (std::string) exc << std::endl;
        if((bool) exc["stack"])
            std::cerr << (std::string) exc["stack"] << std::endl;
    }
}

void NextEngine::TestJSEngine()
{
    try
    {
        // export classes as a module
        auto& module = JSContext_->addModule("MyModule");
        module.function<&println>("println");
        module.class_<MyClass>("MyClass")
                .constructor<>()
                .constructor<std::vector<int>>("MyClassA")
                .fun<&MyClass::member_variable>("member_variable")
                .fun<&MyClass::member_function>("member_function");
        // import module
        JSContext_->eval(R"xxx(
            import * as my from 'MyModule';
            globalThis.my = my;
        )xxx", "<import>", JS_EVAL_TYPE_MODULE);
        // evaluate js code
        JSContext_->eval(R"xxx(
            let v1 = new my.MyClass();
            v1.member_variable = 1;
            let v2 = new my.MyClassA([1,2,3]);
            function my_callback(str) {
              my.println("Call callback from javascript:", v2.member_function(str));
            }
        )xxx");

        // callback
        auto cb = (std::function<void(const std::string&)>) JSContext_->eval("my_callback");
        cb("World from cpp");

        // passing c++ objects to JS
        auto lambda = JSContext_->eval("x=>my.println(x.member_function('Lambda from javascript'))").as<std::function<void(qjs::shared_ptr<MyClass>)>>();
        auto v3 = qjs::make_shared<MyClass>(JSContext_->ctx, std::vector{1,2,3});
        lambda(v3);
    }
    catch(qjs::exception)
    {
        auto exc = JSContext_->getException();
        std::cerr << (std::string) exc << std::endl;
        if((bool) exc["stack"])
            std::cerr << (std::string) exc["stack"] << std::endl;
    }
}

void NextEngine::InitPhysics()
{
    
}
