#include "Application.hpp"
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
#include <fstream>
#include <iostream>
#include <ctime>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <Utilities/FileHelper.hpp>
#include <Utilities/Math.hpp>

#include <filesystem>

#include "Options.hpp"
#include "curl/curl.h"
#include "cpp-base64/base64.cpp"
#include "ThirdParty/json11/json11.hpp"

#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "TaskCoordinator.hpp"
#include "Editor/EditorCommand.hpp"
#include "Utilities/Localization.hpp"

#include "Vulkan/RayQuery/RayQueryRenderer.hpp"
#include "Vulkan/RayTrace/RayTracingRenderer.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredRenderer.hpp"
#include "Vulkan/LegacyDeferred/LegacyDeferredRenderer.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredRenderer.hpp"

#if WITH_AVIF
#include "avif/avif.h"
#endif

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
        float elapsed;
        std::array<char, 256> outputInfo;
    };
}

template <typename Renderer>
NextRendererApplication<Renderer>::NextRendererApplication(const UserSettings& userSettings,
                                                           const Vulkan::WindowConfig& windowConfig,
                                                           const VkPresentModeKHR presentMode) :
    Renderer(Renderer::StaticClass(), windowConfig, presentMode, EnableValidationLayers),
    userSettings_(userSettings)
{
    CheckFramebufferSize();

    status_ = NextRenderer::EApplicationStatus::Starting;

#if !ANDROID
    Utilities::Localization::ReadLocTexts(fmt::format("assets/locale/{}.txt", GOption->locale).c_str());
    
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestExit, [this](std::string& args)->bool {
        GetWindow().Close();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestMaximum, [this](std::string& args)->bool {
        GetWindow().Maximum();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestMinimize, [this](std::string& args)->bool {
        GetWindow().Minimize();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdIO_LoadScene, [this](std::string& args)->bool {
        userSettings_.SceneIndex = SceneList::AddExternalScene(args);
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdIO_LoadHDRI, [this](std::string& args)->bool {
        Assets::GlobalTexturePool::UpdateHDRTexture(0, args.c_str(), Vulkan::SamplerConfig());
        userSettings_.SkyIdx = 0;
        return true;
    });
#endif
}

template <typename Renderer>
NextRendererApplication<Renderer>::~NextRendererApplication()
{
#if !ANDROID
    Utilities::Localization::SaveLocTexts(fmt::format("assets/locale/{}.txt", GOption->locale).c_str());
#endif
    scene_.reset();
}

template <typename Renderer>
Assets::UniformBufferObject NextRendererApplication<Renderer>::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const
{
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);

    //if (pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
        pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
    //}

    if(userSettings_.CameraIdx >= 0 && previousSettings_.CameraIdx != userSettings_.CameraIdx)
    {
		modelViewController_.Reset((userSettings_.cameras[userSettings_.CameraIdx]).ModelView);
    }

    const auto& init = cameraInitialSate_;

    Assets::UniformBufferObject ubo = {};

    ubo.ModelView = modelViewController_.ModelView();
    ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView),
                                      extent.width / static_cast<float>(extent.height), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
#if ANDROID
    ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView),
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

    glm::vec2 pixel = mousePos_ - glm::vec2(offset.x, offset.y);
    glm::vec2 uv = pixel / glm::vec2(extent.width, extent.height) * glm::vec2(2.0,2.0) - glm::vec2(1.0,1.0);
    glm::vec4 origin = ubo.ModelViewInverse * glm::vec4(0, 0, 0, 1);
    glm::vec4 target = ubo.ProjectionInverse * (glm::vec4(uv.x, uv.y, 1, 1));

    glm::vec3 raydir = ubo.ModelViewInverse * glm::vec4(normalize((glm::vec3(target) - glm::vec3(0.0,0.0,0.0))), 0.0);
    glm::vec3 rayorg = glm::vec3(origin);

    Renderer::SetRaycastRay(rayorg, raydir);
    
    Assets::RayCastResult rayResult;
    Renderer::GetLastRaycastResult(rayResult);
    
    if( userSettings_.AutoFocus )
    {
        if(rayResult.Hitted )
        {
            userSettings_.FocusDistance = rayResult.T;
            scene_->SetSelectedId(rayResult.InstanceId);
        }
        else
        {
            scene_->SetSelectedId(-1);
        }
    }

    ubo.SelectedId = scene_->GetSelectedId();
    
    userSettings_.HitResult = rayResult;
    
    ubo.Aperture = userSettings_.Aperture;
    ubo.FocusDistance = userSettings_.FocusDistance;
    

    ubo.SkyRotation = userSettings_.SkyRotation;
    ubo.MaxNumberOfBounces = userSettings_.MaxNumberOfBounces;
    ubo.TotalFrames = totalFrames_;
    ubo.NumberOfSamples = userSettings_.NumberOfSamples;
    ubo.NumberOfBounces = userSettings_.NumberOfBounces;
    ubo.RR_MIN_DEPTH = userSettings_.RR_MIN_DEPTH;
    ubo.AdaptiveSample = userSettings_.AdaptiveSample;
    ubo.AdaptiveVariance = userSettings_.AdaptiveVariance;
    ubo.AdaptiveSteps = userSettings_.AdaptiveSteps;
    ubo.TAA = userSettings_.TAA;
    ubo.RandomSeed = rand();
    ubo.SunDirection = glm::vec4( glm::normalize(glm::vec3( sinf(float( userSettings_.SunRotation * M_PI )), 0.75f, cosf(float( userSettings_.SunRotation * M_PI )) )), 0.0f );
    ubo.SunColor = glm::vec4(1,1,1, 0) * userSettings_.SunLuminance;
    ubo.SkyIntensity = userSettings_.SkyIntensity;
    ubo.SkyIdx = userSettings_.SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * userSettings_.SkyIntensity;
    ubo.HasSky = init.HasSky;
    ubo.HasSun = init.HasSun && userSettings_.SunLuminance > 0;
    ubo.ShowHeatmap = false;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = userSettings_.TemporalFrames;
    ubo.HDR = Renderer::SwapChain().IsHDR();
    
    ubo.PaperWhiteNit = userSettings_.PaperWhiteNit;

    ubo.LightCount = scene_->GetLightCount();

    prevUBO_ = ubo;

    return ubo;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::SetPhysicalDeviceImpl(
    VkPhysicalDevice physicalDevice,
    std::vector<const char*>& requiredExtensions,
    VkPhysicalDeviceFeatures& deviceFeatures,
    void* nextDeviceFeatures)
{
    // Required extensions. windows only
#if WIN32
    requiredExtensions.insert(requiredExtensions.end(),
                              {
                                  // VK_KHR_SHADER_CLOCK is required for heatmap
                                  VK_KHR_SHADER_CLOCK_EXTENSION_NAME
                              });
#endif

    // Opt-in into mandatory device features.
    VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
    shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
    shaderClockFeatures.pNext = nextDeviceFeatures;
    shaderClockFeatures.shaderSubgroupClock = true;

    deviceFeatures.fillModeNonSolid = true;
    deviceFeatures.samplerAnisotropy = true;
    
#if WIN32
    deviceFeatures.shaderInt64 = true;
    Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &shaderClockFeatures);
#else
    Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nextDeviceFeatures);
#endif
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnDeviceSet()
{
    Renderer::OnDeviceSet();

    // global textures
    // texture id 0: dynamic hdri sky
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/std_env.hdr"), Vulkan::SamplerConfig());
    
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/canary_wharf_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/kloppenheim_01_puresky_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/kloppenheim_07_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/river_road_2.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/rainforest_trail_1k.hdr"), Vulkan::SamplerConfig());

    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/studio_small_03_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/studio_small_09_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/sunset_fairway_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/umhlanga_sunrise_1k.hdr"), Vulkan::SamplerConfig());
    Assets::GlobalTexturePool::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/shanghai_bund_1k.hdr"), Vulkan::SamplerConfig());

    if(GOption->HDRIfile != "") Assets::GlobalTexturePool::UpdateHDRTexture(0, GOption->HDRIfile.c_str(), Vulkan::SamplerConfig());
    
    std::vector<Assets::Model> models;
    std::vector<Assets::Node> nodes;
    std::vector<Assets::Material> materials;
    std::vector<Assets::LightObject> lights;
    Assets::CameraInitialSate cameraState;
    
    scene_.reset(new Assets::Scene(Renderer::CommandPool(), nodes, models,
                                  materials, lights, Renderer::supportRayTracing_));

    Renderer::OnPostLoadScene();

    status_ = NextRenderer::EApplicationStatus::Running;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CreateSwapChain()
{
    Renderer::CreateSwapChain();

    userInterface_.reset(new UserInterface(Renderer::CommandPool(), Renderer::SwapChain(), Renderer::DepthBuffer(),
                                           userSettings_, Renderer::GetRenderImage()));

    CheckFramebufferSize();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DeleteSwapChain()
{
    userInterface_.reset();

    Renderer::DeleteSwapChain();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DrawFrame()
{
    // Check if the scene has been changed by the user.
    if (status_ == NextRenderer::EApplicationStatus::Running && sceneIndex_ != static_cast<uint32_t>(userSettings_.SceneIndex))
    {
        LoadScene(userSettings_.SceneIndex);
        //return;
    }
    
    previousSettings_ = userSettings_;

    Renderer::DrawFrame();

    totalFrames_ += 1;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
{
    const auto prevTime = time_;
    time_ = Renderer::Window().GetTime();
    const auto timeDelta = time_ - prevTime;

    // Update the camera position / angle.
    modelViewController_.UpdateCamera(cameraInitialSate_.ControlSpeed, timeDelta);
    
    Renderer::supportDenoiser_ = userSettings_.Denoiser;
    Renderer::checkerboxRendering_ = userSettings_.UseCheckerBoardRendering;

    // Render the scene if scene loaded
    if(cameraInitialSate_.CameraIdx != -1)
    {
        Renderer::Render(commandBuffer, imageIndex);
    }

    // Check the current state of the benchmark, update it for the new frame.
    if(status_ == NextRenderer::EApplicationStatus::Running)
    {
        CheckAndUpdateBenchmarkState(prevTime);
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::RenderUI(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    static float frameRate = 0.0;
    static double lastTime = 0.0;
    static double lastTimestamp = 0.0;
    double now = Renderer::Window().GetTime();
    
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
    
    stats.Stats["gpu"] = Renderer::Device().DeviceProperties().deviceName;
    
    stats.FramebufferSize = Renderer::Window().FramebufferSize();
    stats.FrameRate = frameRate;
    

    stats.TotalFrames = totalFrames_;
    stats.RenderTime = time_ - sceneInitialTime_;

    stats.CamPosX = modelViewController_.Position()[0];
    stats.CamPosY = modelViewController_.Position()[1];
    stats.CamPosZ = modelViewController_.Position()[2];

    stats.InstanceCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    Renderer::visualDebug_ = userSettings_.ShowVisualDebug;
    
    userInterface_->Render(commandBuffer, imageIndex, stats, Renderer::GpuTimer(), scene_.get());
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnKey(int key, int scancode, int action, int mods)
{
    if (userInterface_->WantsToCaptureKeyboard())
    {
        return;
    }
#if !ANDROID
    if (action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_ESCAPE: Renderer::Window().Close();
            break;
        default: break;
        }

        // Settings (toggle switches)
        if (!userSettings_.Benchmark)
        {
            switch (key)
            {
            case GLFW_KEY_F1: userSettings_.ShowSettings = !userSettings_.ShowSettings;
                break;
            case GLFW_KEY_F2: userSettings_.ShowOverlay = !userSettings_.ShowOverlay;
                break;
            case GLFW_KEY_SPACE: userSettings_.AutoFocus = true;
                break;
            default: break;
            }
        }
    }
    else if(action == GLFW_RELEASE)
    {
        if (!userSettings_.Benchmark)
        {
            switch (key)
            {
            case GLFW_KEY_SPACE: userSettings_.AutoFocus = false;
                break;
            default: break;
            }
        }
    }
#endif

    // Camera motions
    if (!userSettings_.Benchmark)
    {
        modelViewController_.OnKey(key, scancode, action, mods);
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnCursorPosition(const double xpos, const double ypos)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureKeyboard() ||
        userInterface_->WantsToCaptureMouse()
        )
    {
        return;
    }

    // Camera motions
    modelViewController_.OnCursorPosition(xpos, ypos);

    mousePos_ = glm::vec2(xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnMouseButton(const int button, const int action, const int mods)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    // Camera motions
    modelViewController_.OnMouseButton(button, action, mods);
#if !ANDROID
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        userSettings_.AutoFocus = true;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        userSettings_.AutoFocus = false;
    }
#endif
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnScroll(const double xoffset, const double yoffset)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }
    const auto prevFov = userSettings_.FieldOfView;
    userSettings_.FieldOfView = std::clamp(
        static_cast<float>(prevFov - yoffset),
        UserSettings::FieldOfViewMinValue,
        UserSettings::FieldOfViewMaxValue);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnDropFile(int path_count, const char* paths[])
{
    // add glb to the last, and loaded
    if (path_count > 0)
    {
        std::string path = paths[path_count - 1];
        std::string ext = path.substr(path.find_last_of(".") + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "glb")
        {
            userSettings_.SceneIndex = SceneList::AddExternalScene(path);
        }

        if( ext == "hdr")
        {
            Assets::GlobalTexturePool::UpdateHDRTexture(0, path, Vulkan::SamplerConfig());
            userSettings_.SkyIdx = 0;
        }
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::BeforeNextFrame()
{
    TaskCoordinator::GetInstance()->Tick();
    Renderer::BeforeNextFrame();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnTouch(bool down, double xpos, double ypos)
{
    modelViewController_.OnTouch(down, xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnTouchMove(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::LoadScene(const uint32_t sceneIndex)
{
    status_ = NextRenderer::EApplicationStatus::Loading;
    
    std::shared_ptr< std::vector<Assets::Model> > models = std::make_shared< std::vector<Assets::Model> >();
    std::shared_ptr< std::vector<Assets::Node> > nodes = std::make_shared< std::vector<Assets::Node> >();
    std::shared_ptr< std::vector<Assets::Material> > materials = std::make_shared< std::vector<Assets::Material> >();
    std::shared_ptr< std::vector<Assets::LightObject> > lights = std::make_shared< std::vector<Assets::LightObject> >();
    std::shared_ptr< Assets::CameraInitialSate > cameraState = std::make_shared< Assets::CameraInitialSate >();
    
    cameraInitialSate_.cameras.clear();
    cameraInitialSate_.CameraIdx = -1;

    // dispatch in thread task and reset in main thread
    TaskCoordinator::GetInstance()->AddTask( [this, cameraState, sceneIndex, models, nodes, materials, lights](ResTask& task)
    {
        SceneTaskContext taskContext {};
        const auto timer = std::chrono::high_resolution_clock::now();
        
        SceneList::AllScenes[sceneIndex].second(*cameraState, *nodes, *models, *materials, *lights);
        
        taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();

        std::string info = fmt::format("parsed scene #{} on cpu in {:.2f}ms", sceneIndex, taskContext.elapsed * 1000.f);
        std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
        task.SetContext( taskContext );
    },
    [this, cameraState, sceneIndex, models, nodes, materials, lights](ResTask& task)
    {
        SceneTaskContext taskContext {};
        task.GetContext( taskContext );
        fmt::print("{} {}{}\n", CONSOLE_GREEN_COLOR, taskContext.outputInfo.data(), CONSOLE_DEFAULT_COLOR);
        
        const auto timer = std::chrono::high_resolution_clock::now();
        
        cameraInitialSate_ = *cameraState;
        
        Renderer::Device().WaitIdle();
        DeleteSwapChain();
        Renderer::OnPreLoadScene();

        scene_.reset(new Assets::Scene(Renderer::CommandPool(), *nodes, *models,
                                *materials, *lights, Renderer::supportRayTracing_));

        sceneIndex_ = sceneIndex;

        userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
        userSettings_.Aperture = cameraInitialSate_.Aperture;
        userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;
        userSettings_.SkyIdx = cameraInitialSate_.SkyIdx;
        userSettings_.SunRotation = cameraInitialSate_.SunRotation;

        userSettings_.cameras = cameraInitialSate_.cameras;
        userSettings_.CameraIdx = cameraInitialSate_.CameraIdx;

        modelViewController_.Reset(cameraInitialSate_.ModelView);

        periodTotalFrames_ = 0;
        totalFrames_ = 0;
        benchmarkTotalFrames_ = 0;
        sceneInitialTime_ = Renderer::Window().GetTime();

        Renderer::OnPostLoadScene();
        CreateSwapChain();

        float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("{} uploaded scene #{} to gpu in {:.2f}ms{}\n", CONSOLE_GREEN_COLOR, sceneIndex, elapsed * 1000.f, CONSOLE_DEFAULT_COLOR);
        
        sceneIndex_ = sceneIndex;
        status_ = NextRenderer::EApplicationStatus::Running;
    },
    1);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckAndUpdateBenchmarkState(double prevTime)
{
    if (!userSettings_.Benchmark)
    {
        return;
    }

    // Initialise scene benchmark timers
    if (periodTotalFrames_ == 0)
    {
        if (benchmarkNumber_ == 0)
        {
            std::time_t now = std::time(nullptr);
            std::string report_filename = fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", fmt::localtime(now));
            benchmarkCsvReportFile.open(report_filename);
            benchmarkCsvReportFile << fmt::format("#;scene;FPS\n");
        }

        fmt::print("\n\nRenderer: {}\n", Renderer::StaticClass());
        fmt::print("Benchmark: Start scene #{} '{}'\n", sceneIndex_, SceneList::AllScenes[sceneIndex_].first);
        periodInitialTime_ = time_;
    }

    // Print out the frame rate at regular intervals.
    {
        const double period = 5;
        const double prevTotalTime = prevTime - periodInitialTime_;
        const double totalTime = time_ - periodInitialTime_;

        if (periodTotalFrames_ != 0 && static_cast<uint64_t>(prevTotalTime / period) != static_cast<uint64_t>(totalTime
            / period))
        {
            fmt::print("Benchmark: {:.3f}\n", float(periodTotalFrames_) / float(totalTime));
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
            benchmarkNumber_++;
        }

        periodTotalFrames_++;
        benchmarkTotalFrames_++;
    }

    // If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
    {
        const bool timeLimitReached = userSettings_.BenchmarkMaxFrame == 0 && periodTotalFrames_ != 0 && time_ - sceneInitialTime_ >
            userSettings_.BenchmarkMaxTime;
        const bool frameLimitReached = userSettings_.BenchmarkMaxFrame > 0 && benchmarkTotalFrames_ >= userSettings_.BenchmarkMaxFrame;
        if (timeLimitReached || frameLimitReached)
        {
            {
                const double totalTime = time_ - sceneInitialTime_;
                std::string SceneName = SceneList::AllScenes[userSettings_.SceneIndex].first;
                double fps = benchmarkTotalFrames_ / totalTime;

                fmt::print("\n*** totalTime {:%H:%M:%S} fps {:.3f}\n", std::chrono::seconds(static_cast<long long>(totalTime)), fps);

                Report(static_cast<int>(floor(fps)), SceneName, false, GOption->SaveFile);
                benchmarkCsvReportFile << fmt::format("{};{};{:.3f}\n", sceneIndex_, SceneList::AllScenes[sceneIndex_].first, fps);
            }

            if (!userSettings_.BenchmarkNextScenes || static_cast<size_t>(userSettings_.SceneIndex) ==
                SceneList::AllScenes.size() - 1)
            {
                Renderer::Window().Close();
            }

            puts("");
            userSettings_.SceneIndex += 1;
        }
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckFramebufferSize() const
{
    // Check the framebuffer size when requesting a fullscreen window, as it's not guaranteed to match.
    const auto& cfg = Renderer::Window().Config();
    const auto fbSize = Renderer::Window().FramebufferSize();

    if (userSettings_.Benchmark && cfg.Fullscreen && (fbSize.width != cfg.Width || fbSize.height != cfg.Height))
    {
        std::string out = fmt::format("framebuffer fullscreen size mismatch (requested: {}x{}, got: {}x{})", cfg.Width, cfg.Height, fbSize.width, fbSize.height);

        Throw(std::runtime_error(out));
    }
}

inline const std::string versionToString(const uint32_t version)
{
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Report(int fps, const std::string& sceneName, bool upload_screen, bool save_screen)
{
    VkPhysicalDeviceProperties deviceProp1{};
    VkPhysicalDeviceDriverProperties driverProp{};
    driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 deviceProp{};
    deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProp.pNext = &driverProp;
    
    vkGetPhysicalDeviceProperties(Renderer::Device().PhysicalDevice(), &deviceProp1);

    std::string img_encoded {};
    if (upload_screen || save_screen)
    {
#if WITH_AVIF
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = Renderer::SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        Renderer::CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        avifImage* image = avifImageCreate(extent.width, extent.height, swapChain.IsHDR() ? 10 : 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = swapChain.IsHDR() ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
        image->transferCharacteristics = swapChain.IsHDR() ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 : AVIF_TRANSFER_CHARACTERISTICS_BT709;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->clli.maxCLL = static_cast<uint16_t>(userSettings_.PaperWhiteNit); //maxCLLNits;
        image->clli.maxPALL = 0; //maxFALLNits;

        avifEncoder* encoder = NULL;
        avifRWData avifOutput = AVIF_DATA_EMPTY;

        avifRGBImage rgbAvifImage{};
        avifRGBImageSetDefaults(&rgbAvifImage, image);
        rgbAvifImage.format = AVIF_RGB_FORMAT_RGB;
        rgbAvifImage.ignoreAlpha = AVIF_TRUE;

        void* data = nullptr;
        if(swapChain.IsHDR())
        {
            size_t hdrsize = rgbAvifImage.width * rgbAvifImage.height * 3 * 2;
            data = malloc(hdrsize);
            uint16_t* dataview = (uint16_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);

                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t idxDelta = extent.width * 4;
                uint32_t yy = 0;
                uint32_t xx = 0, idx = 0;
                for (uint32_t y = 0; y < extent.height; y++)
                {
                    xx = 0;
                    for (uint32_t x = 0; x < extent.width; x++)
                    {
                        uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                        uint32_t uInPixel = *pInPixel;
                        dataview[yy + xx + 2]     = (uInPixel & (0b1111111111 << 20)) >> 20;
                        dataview[yy + xx + 1] = (uInPixel & (0b1111111111 << 10)) >> 10;
                        dataview[yy + xx + 0] = (uInPixel & (0b1111111111 << 0)) >> 0;
                        
                        idx += 4;
                        xx += xDelta;
                    }
                    //idx += idxDelta;
                    yy += yDelta;
                }
                vkMemory->Unmap();
            }

            rgbAvifImage.pixels = (uint8_t*)data;
            rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint16_t);
        }
        else
        {
            data = malloc(imageSize);
            uint8_t* dataview = (uint8_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);

                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t idxDelta = extent.width * 4;
                uint32_t yy = 0;
                uint32_t xx = 0, idx = 0;
                for (uint32_t y = 0; y < extent.height; y++)
                {
                    xx = 0;
                    for (uint32_t x = 0; x < extent.width; x++)
                    {
                        uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                        uint32_t uInPixel = *pInPixel;

                        dataview[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                        dataview[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                        dataview[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                        idx += 4;
                        xx += xDelta;
                    }
                    yy += yDelta;
                }
                vkMemory->Unmap();
            }

            rgbAvifImage.pixels = (uint8_t*)data;
            rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint8_t);
        }
       
       

        avifResult convertResult = avifImageRGBToYUV(image, &rgbAvifImage);
        if (convertResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to convert RGB to YUV: " + std::string(avifResultToString(convertResult))));
        }
        encoder = avifEncoderCreate();
        if (!encoder)
        {
            Throw(std::runtime_error("Failed to create encoder"));
        }
        encoder->quality = 80;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
        encoder->speed = AVIF_SPEED_FASTEST;

        avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
        if (addImageResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to add image: " + std::string(avifResultToString(addImageResult))));
        }
        avifResult finishResult = avifEncoderFinish(encoder, &avifOutput);
        if (finishResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to finish encoding: " + std::string(avifResultToString(finishResult))));
        }

        // save to file with scenename
        std::string filename = sceneName + ".avif";
        std::wfstream file(filename.c_str());
        file << avifOutput.data;
        file.close();
		
        free(data);

        // send to server
        img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
#else
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = Renderer::SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        Renderer::CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        uint8_t* data = (uint8_t*)malloc(imageSize);
        {
            Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

            uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, imageSize);

            uint32_t yDelta = extent.width * kCompCnt;
            uint32_t xDelta = kCompCnt;
            uint32_t idxDelta = extent.width * 4;
            uint32_t yy = 0;
            uint32_t xx = 0, idx = 0;
            for (uint32_t y = 0; y < extent.height; y++)
            {
                xx = 0;
                for (uint32_t x = 0; x < extent.width; x++)
                {
                    uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                    uint32_t uInPixel = *pInPixel;

                    data[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                    data[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                    data[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                    idx += 4;
                    xx += xDelta;
                }
                yy += yDelta;
            }
            vkMemory->Unmap();
        }

        // save to file with scenename
        std::string filename = sceneName + ".jpg";
        stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, (const void*)data, 91);
        fmt::print("screenshot saved to {}\n", filename);
        std::uintmax_t img_file_size = std::filesystem::file_size(filename);
        fmt::print("file size: {}\n", Utilities::metricFormatter(static_cast<double>(img_file_size), "b", 1024));
        // send to server
        //img_encoded = base64_encode(data, img_file_size, false);
        free(data);
#endif
    }

    if( NextRenderer::GetBuildVersion() != "v0.0.0.0" )
    {
        json11::Json my_json = json11::Json::object{
            {"renderer", Renderer::StaticClass()},
            {"scene", sceneName},
            {"gpu", std::string(deviceProp1.deviceName)},
            {"driver", versionToString(deviceProp1.driverVersion)},
            {"fps", fps},
            {"version", NextRenderer::GetBuildVersion()},
            {"screenshot", img_encoded}
        };
        std::string json_str = my_json.dump();

        fmt::print("Sending benchmark to perf server...\n");
        // upload from curl
        CURL* curl;
        CURLcode res;
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();
        if (curl)
        {
            curl_slist* slist1 = nullptr;
            slist1 = curl_slist_append(slist1, "Content-Type: application/json");
            slist1 = curl_slist_append(slist1, "Accept: application/json");

            /* set custom headers */
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);
            curl_easy_setopt(curl, CURLOPT_URL, "http://gameknife.site:60010/rt_benchmark");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        
            /* Perform the request, res gets the return code */
            res = curl_easy_perform(curl);
            /* Check for errors */
            if (res != CURLE_OK)
                fmt::print(stderr, "curl_easy_perform() failed: {}\n", curl_easy_strerror(res));

            /* always cleanup */
            curl_easy_cleanup(curl);
        }
    }
}

// export it 
template class NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>;
template class NextRendererApplication<Vulkan::RayTracing::RayQueryRenderer>;
template class NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>;
template class NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>;
template class NextRendererApplication<Vulkan::HybridDeferred::HybridDeferredRenderer>;
template class NextRendererApplication<Vulkan::VulkanBaseRenderer>;
