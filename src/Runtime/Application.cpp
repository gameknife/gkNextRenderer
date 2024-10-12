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
#include "BenchMark.hpp"

#include <fmt/format.h>
#include <fmt/chrono.h>
#include <Utilities/FileHelper.hpp>
#include <filesystem>

#include "Options.hpp"
#include "TaskCoordinator.hpp"
#include "Utilities/Localization.hpp"
#include "Vulkan/RayQuery/RayQueryRenderer.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredRenderer.hpp"
#include "Vulkan/LegacyDeferred/LegacyDeferredRenderer.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredRenderer.hpp"

#if WITH_EDITOR
#include "Editor/EditorCommand.hpp"
#include "Editor/EditorInterface.hpp"
#endif

#define _USE_MATH_DEFINES
#include <math.h>

#define BUILDVER(X) std::string buildver(#X);
#include "build.version"

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
                    ptr->RegisterLogicRenderer(Vulkan::ERT_PathTracing);
                    ptr->RegisterLogicRenderer(Vulkan::ERT_Hybrid);
                    ptr->RegisterLogicRenderer(Vulkan::ERT_ModernDeferred);
                    ptr->RegisterLogicRenderer(Vulkan::ERT_LegacyDeferred);
                    ptr->SwitchLogicRenderer(static_cast<Vulkan::ERendererType>(rendererType));
                    return ptr;    
                }
            default:
                return new Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers);
        }
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
        float elapsed;
        std::array<char, 256> outputInfo;
    };
}

UserSettings CreateUserSettings(const Options& options)
{
    SceneList::ScanScenes();
    
    UserSettings userSettings{};

    userSettings.RendererType = options.RendererType;
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

    userSettings.AccumulateRays = false;
    
    userSettings.NumberOfSamples = options.Benchmark ? 1 : options.Samples;
    userSettings.NumberOfBounces = options.Benchmark ? 4 : options.Bounces;
    userSettings.MaxNumberOfBounces = options.MaxBounces;

    userSettings.AdaptiveSample = options.AdaptiveSample;
    userSettings.AdaptiveVariance = 6.0f;
    userSettings.AdaptiveSteps = 8;
    userSettings.TAA = true;

    userSettings.ShowSettings = !options.Benchmark;
    userSettings.ShowOverlay = true;

    userSettings.ShowVisualDebug = false;
    userSettings.HeatmapScale = 0.5f;

    userSettings.UseCheckerBoardRendering = false;
    userSettings.TemporalFrames = options.Benchmark ? 256 : options.Temporal;

    userSettings.Denoiser = options.Benchmark ? false : !options.NoDenoiser;

    userSettings.PaperWhiteNit = 600.f;

    userSettings.SunRotation = 0.5f;
    userSettings.SunLuminance = 500.f;
    userSettings.SkyIntensity = 100.f;
    
    userSettings.RequestRayCast = false;

    userSettings.DenoiseSigma = 0.5f;
    userSettings.DenoiseSigmaLum = 10.0f;
    userSettings.DenoiseSigmaNormal = 0.005f;
    userSettings.DenoiseSize = 5;

#if ANDROID
    userSettings.NumberOfSamples = 1;
    userSettings.Denoiser = false;
#endif

    return userSettings;
}

NextRendererApplication::NextRendererApplication(const Options& options, void* userdata)
{
    status_ = NextRenderer::EApplicationStatus::Starting;

    // Create Window
    const Vulkan::WindowConfig windowConfig
    {
        "gkNextRenderer " + NextRenderer::GetBuildVersion(),
        options.Width,
        options.Height,
        options.Benchmark && options.Fullscreen,
        options.Fullscreen,
        !options.Fullscreen,
        options.SaveFile,
        userdata,
        options.ForceSDR
    };
    
    userSettings_ = CreateUserSettings(options);
    window_.reset( new Vulkan::Window(windowConfig));

    // Initialize BenchMarker
    if(options.Benchmark)
    {
        benchMarker_ = std::make_unique<BenchMarker>();
    }
    
    // Initialize Renderer
    renderer_.reset( NextRenderer::CreateRenderer(options.RendererType, window_.get(), static_cast<VkPresentModeKHR>(options.Benchmark ? 0 : options.PresentMode), EnableValidationLayers) );
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

    // EditorCommand, need Refactoring
#if WITH_EDITOR    
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

NextRendererApplication::~NextRendererApplication()
{
    Utilities::Localization::SaveLocTexts(fmt::format("assets/locale/{}.txt", GOption->locale).c_str());

    scene_.reset();
    renderer_.reset();
    window_.reset();
    benchMarker_.reset();
}

void NextRendererApplication::Start()
{
    renderer_->Start();
}

bool NextRendererApplication::Tick()
{
    if(rendererType != userSettings_.RendererType)
    {
        rendererType = userSettings_.RendererType;
        renderer_->SwitchLogicRenderer(static_cast<Vulkan::ERendererType>(rendererType));
    }
    
    // delta time calc
    const auto prevTime = time_;
    time_ = GetWindow().GetTime();
    const auto timeDelta = time_ - prevTime;

    // Camera Update
    userSettings_.FieldOfView = glm::mix( userSettings_.FieldOfView, userSettings_.RawFieldOfView, 0.1);
    modelViewController_.UpdateCamera(cameraInitialSate_.ControlSpeed, timeDelta);

    // Handle Scene Switching
    if (status_ == NextRenderer::EApplicationStatus::Running && sceneIndex_ != static_cast<uint32_t>(userSettings_.SceneIndex))
    {
        LoadScene(userSettings_.SceneIndex);
    }

    // Setting Update
    previousSettings_ = userSettings_;

    // Benchmark Update
    if(status_ == NextRenderer::EApplicationStatus::Running)
    {
        TickBenchMarker();
    }

    // Renderer Tick
#if ANDROID
    renderer_->DrawFrame();
    totalFrames_ += 1;
    return false;
#else
    glfwPollEvents();
    renderer_->DrawFrame();
    window_->attemptDragWindow();
    totalFrames_ += 1;
    return glfwWindowShouldClose( window_->Handle() ) != 0;
#endif
}

void NextRendererApplication::End()
{
    renderer_->End();
    userInterface_.reset();
}

Assets::UniformBufferObject NextRendererApplication::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const
{
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

    // handle android vulkan pre rotation
#if ANDROID
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
    
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

    // Raycasting
    {
        glm::vec2 pixel = mousePos_ - glm::vec2(offset.x, offset.y);
        glm::vec2 uv = pixel / glm::vec2(extent.width, extent.height) * glm::vec2(2.0,2.0) - glm::vec2(1.0,1.0);
        glm::vec4 origin = ubo.ModelViewInverse * glm::vec4(0, 0, 0, 1);
        glm::vec4 target = ubo.ProjectionInverse * (glm::vec4(uv.x, uv.y, 1, 1));
        glm::vec3 raydir = ubo.ModelViewInverse * glm::vec4(normalize((glm::vec3(target) - glm::vec3(0.0,0.0,0.0))), 0.0);
        glm::vec3 rayorg = glm::vec3(origin);

        // Send new
        renderer_->SetRaycastRay(rayorg, raydir);
        Assets::RayCastResult rayResult {};
        renderer_->GetLastRaycastResult(rayResult);
    
        if( userSettings_.RequestRayCast )
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

            // only active one frame
            userSettings_.RequestRayCast = false;
        }

        userSettings_.HitResult = rayResult;
    }


    ubo.SelectedId = scene_->GetSelectedId();

    // Camera Stuff
    ubo.Aperture = userSettings_.Aperture;
    ubo.FocusDistance = userSettings_.FocusDistance;

    // SceneStuff
    ubo.SkyRotation = userSettings_.SkyRotation;
    ubo.MaxNumberOfBounces = userSettings_.MaxNumberOfBounces;
    ubo.TotalFrames = totalFrames_;
    ubo.NumberOfSamples = userSettings_.NumberOfSamples;
    ubo.NumberOfBounces = userSettings_.NumberOfBounces;
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
    ubo.ShowHeatmap = userSettings_.ShowVisualDebug;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = userSettings_.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();
    
    ubo.PaperWhiteNit = userSettings_.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    ubo.BFSigma = userSettings_.DenoiseSigma;
    ubo.BFSigmaLum = userSettings_.DenoiseSigmaLum;
    ubo.BFSigmaNormal = userSettings_.DenoiseSigmaNormal;
    ubo.BFSize = userSettings_.Denoiser ? userSettings_.DenoiseSize : 0;

#if WITH_EDITOR
    ubo.ShowEdge = true;
#endif
    

    // Other Setup
    renderer_->supportDenoiser_ = userSettings_.Denoiser;
    renderer_->visualDebug_ = userSettings_.ShowVisualDebug;
    
    // UBO Backup, for motion vector calc
    prevUBO_ = ubo;

    return ubo;
}

void NextRendererApplication::OnRendererDeviceSet()
{
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
    
    scene_.reset(new Assets::Scene(renderer_->CommandPool(), nodes, models,
                                  materials, lights, renderer_->supportRayTracing_));

    renderer_->SetScene(scene_);
    renderer_->OnPostLoadScene();

    status_ = NextRenderer::EApplicationStatus::Running;
}

void NextRendererApplication::OnRendererCreateSwapChain()
{
    if(userInterface_.get() == nullptr)
    {
#if WITH_EDITOR
        userInterface_.reset(new EditorInterface(renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer()));
#else
        userInterface_.reset(new UserInterface(renderer_->CommandPool(), renderer_->SwapChain(), renderer_->DepthBuffer(),
                                   userSettings_));
#endif
    }
    userInterface_->OnCreateSurface(renderer_->SwapChain(), renderer_->DepthBuffer());
}

void NextRendererApplication::OnRendererDeleteSwapChain()
{
    if(userInterface_.get() != nullptr)
    {
        userInterface_->OnDestroySurface();
    }
}

void NextRendererApplication::OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
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
    //stats.RenderTime = time_ - sceneInitialTime_;

    stats.CamPosX = modelViewController_.Position()[0];
    stats.CamPosY = modelViewController_.Position()[1];
    stats.CamPosZ = modelViewController_.Position()[2];

    stats.InstanceCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    stats.ComputePassCount = 0;
    stats.LoadingStatus = status_ == NextRenderer::EApplicationStatus::Loading;

    //Renderer::visualDebug_ = userSettings_.ShowVisualDebug;
    
    userInterface_->Render(commandBuffer, renderer_->SwapChain(), imageIndex, stats, renderer_->GpuTimer(), scene_.get());
}

void NextRendererApplication::OnKey(int key, int scancode, int action, int mods)
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
        case GLFW_KEY_ESCAPE: scene_->SetSelectedId(-1);;
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
            default: break;
            }
        }
    }
    else if(action == GLFW_RELEASE)
    {
        if (!userSettings_.Benchmark)
        {
            // switch (key)
            // {
            //     default: break;
            // }
        }
    }
#endif

    // Camera motions
    if (!userSettings_.Benchmark)
    {
        modelViewController_.OnKey(key, scancode, action, mods);
    }
}

void NextRendererApplication::OnCursorPosition(const double xpos, const double ypos)
{
    if (!renderer_->HasSwapChain() ||
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

void NextRendererApplication::OnMouseButton(const int button, const int action, const int mods)
{
    if (!renderer_->HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    // Camera motions
    modelViewController_.OnMouseButton(button, action, mods);
#if !ANDROID
    static glm::vec2 pressMousePos_ {};
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
       pressMousePos_ = mousePos_;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        if( glm::distance(pressMousePos_, mousePos_) < 1.0f )
        {
            userSettings_.RequestRayCast = true;
        }
    }
#endif
}

void NextRendererApplication::OnScroll(const double xoffset, const double yoffset)
{
    if (!renderer_->HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }
    const auto prevFov = userSettings_.RawFieldOfView;
    userSettings_.RawFieldOfView = std::clamp(
        static_cast<float>(prevFov - yoffset),
        UserSettings::FieldOfViewMinValue,
        UserSettings::FieldOfViewMaxValue);
}

void NextRendererApplication::OnDropFile(int path_count, const char* paths[])
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

void NextRendererApplication::OnRendererBeforeNextFrame()
{
    TaskCoordinator::GetInstance()->Tick();
}

void NextRendererApplication::OnTouch(bool down, double xpos, double ypos)
{
    modelViewController_.OnTouch(down, xpos, ypos);
}

void NextRendererApplication::OnTouchMove(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(xpos, ypos);
}

void NextRendererApplication::LoadScene(const uint32_t sceneIndex)
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
    TaskCoordinator::GetInstance()->AddTask( [cameraState, sceneIndex, models, nodes, materials, lights](ResTask& task)
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
        
        renderer_->Device().WaitIdle();
        renderer_->DeleteSwapChain();
        renderer_->OnPreLoadScene();

        scene_.reset(new Assets::Scene(renderer_->CommandPool(), *nodes, *models,
                                *materials, *lights, renderer_->supportRayTracing_));
        renderer_->SetScene(scene_);
        
        sceneIndex_ = sceneIndex;

        userSettings_.RawFieldOfView = cameraInitialSate_.FieldOfView;
        userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
        userSettings_.Aperture = cameraInitialSate_.Aperture;
        userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;
        if(cameraInitialSate_.HasSky)
        {
            userSettings_.SkyIdx = cameraInitialSate_.SkyIdx;
            userSettings_.SkyIntensity = cameraInitialSate_.SkyIntensity;
        }
        if(cameraInitialSate_.HasSun)
        {
            userSettings_.SunRotation = cameraInitialSate_.SunRotation;
            userSettings_.SunLuminance = cameraInitialSate_.SunIntensity;
        }
        userSettings_.cameras = cameraInitialSate_.cameras;
        userSettings_.CameraIdx = cameraInitialSate_.CameraIdx;

        modelViewController_.Reset(cameraInitialSate_.ModelView);

        
        totalFrames_ = 0;

        if(benchMarker_)
        {
            benchMarker_->OnSceneStart(GetWindow().GetTime());
        }

        renderer_->OnPostLoadScene();
        renderer_->CreateSwapChain();

        float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("{} uploaded scene #{} to gpu in {:.2f}ms{}\n", CONSOLE_GREEN_COLOR, sceneIndex, elapsed * 1000.f, CONSOLE_DEFAULT_COLOR);
        
        sceneIndex_ = sceneIndex;
        status_ = NextRenderer::EApplicationStatus::Running;
    },
    1);
}

void NextRendererApplication::TickBenchMarker()
{
    if( benchMarker_ && benchMarker_->OnTick( GetWindow().GetTime(), renderer_.get() ))
    {
        // Benchmark is done, report the results.
        benchMarker_->OnReport(renderer_.get(), SceneList::AllScenes[userSettings_.SceneIndex].first);
        
        if (!userSettings_.BenchmarkNextScenes || static_cast<size_t>(userSettings_.SceneIndex) ==
            SceneList::AllScenes.size() - 1)
        {
            GetWindow().Close();
        }
        
        userSettings_.SceneIndex += 1;
    }
}
