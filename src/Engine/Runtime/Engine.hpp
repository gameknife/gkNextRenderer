#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Command/CommandHistory.hpp"
#include "Engine/Runtime/Interface/AgentDriver.hpp"
#include "Engine/Runtime/Interface/DebugDraw.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Interface/ScriptRuntime.hpp"
#include "Engine/Runtime/Interface/ShaderHotReload.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

namespace NextRenderer
{
    enum class EApplicationStatus
    {
        Starting,
        Running,
        Loading,
        AsyncPreparing,
    };
    std::string GetBuildVersion();
    Vulkan::VulkanBaseRenderer* CreateRenderer(uint32_t rendererType, Vulkan::Window* window,
                                               const VkPresentModeKHR presentMode, const bool enableValidationLayers);
} // namespace NextRenderer

typedef std::function<bool(double DeltaSeconds)> TickedTask;
typedef std::function<bool()> DelayedTask;

struct FDelayTaskContext
{
    double triggerTime;
    double loopTime;
    DelayedTask task;
};

class NextEngine final
{
public:
    GK_NON_COPIABLE(NextEngine)

    // Request and status payloads
    struct FSceneLoadRequest
    {
        std::string filename;
        bool append = false;
        bool placeOnHit = false;
        glm::vec3 hitPosition{0.0f, 0.0f, 0.0f};
    };

    struct FScreenShotSpec
    {
        std::string filename;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        uint32_t accumulateFrames = 0;
        bool sync = false;
        bool includeUi = false;
    };

    using FHotReloadStatus = Runtime::FShaderHotReloadStatus;

    // Construction and global access
    static void RegisterReflection();
    static NextEngine* GetInstance() { return instance_; }

    NextEngine(Runtime::Config::Options& options, void* userdata = nullptr);
    ~NextEngine();

    static NextEngine* instance_;

    // Main lifecycle
    void Start();
    bool HandleEvent(SDL_Event& event);
    bool Tick(bool forcingDelta = false);
    void End();

    // Core runtime objects
    Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }
    Runtime::FrameProfiler* Profiler() const { return renderer_ ? renderer_->Profiler() : nullptr; }
    Assets::Scene& GetScene() { return *scene_; }
    Vulkan::Window& GetWindow() const { return *window_; }
    NextGameInstanceBase* GetGameInstance() { return gameInstance_.get(); }
    const NextGameInstanceBase* GetGameInstance() const { return gameInstance_.get(); }

    // Configuration state
    Runtime::Config::UserSettings& GetUserSettings() { return config_.userSettings; }
    Runtime::Config::ShowFlags& GetShowFlags() { return config_.showFlags; }
    Runtime::Config::Options& GetOptions() { return *options_; }
    const Runtime::Config::Options& GetOptions() const { return *options_; }

    // Runtime services
    NextUI::UserInterface* GetUserInterface() { return userInterface_.get(); }
    Runtime::IUiOverlay* GetUiOverlay() { return uiOverlay_.get(); }
    NextAudio* GetAudio() { return services_.audio.get(); }
    const NextAudio* GetAudio() const { return services_.audio.get(); }
    NextLocalization* GetLocalization() { return services_.localization.get(); }
    const NextLocalization* GetLocalization() const { return services_.localization.get(); }
    NextCVar::FCVarSystem& GetCVarSystem() { return *services_.cvarSystem; }
    const NextCVar::FCVarSystem& GetCVarSystem() const { return *services_.cvarSystem; }
    NextPhysics* GetPhysicsEngine() { return services_.physics.get(); }
    Utilities::Package::FPackageFileSystem& GetPakSystem() { return *services_.packageFileSystem; }

    // Frame state
    double GetTime() const { return frameState_.time; }
    double GetDeltaSeconds() const { return frameState_.deltaSeconds; }
    double GetSmoothDeltaSeconds() const { return frameState_.smoothedDeltaSeconds; }
    float GetFrameRate() const { return frameState_.frameRate; }
    uint32_t GetTotalFrames() const { return frameState_.totalFrames; }
    NextRenderer::EApplicationStatus GetEngineStatus() const { return status_; }

    // Window and pointer state
    glm::dvec2 GetMousePos();
    uint32_t GetMouseButtons() const { return inputState_.mouseButtons; }
    glm::ivec2 GetMonitorSize() const;
    void RequestClose();
    void RequestExit(int exitCode);
    int GetRequestedExitCode() const { return requestedExitCode_; }
    void RequestMinimize();
    bool IsMaximized();
    void ToggleMaximize();
    bool IsBorderlessFullscreen() const;
    bool SetBorderlessFullscreen(bool enable);
    bool ToggleBorderlessFullscreen();
    void ConfigureCustomTitleBarDrag(bool enabled, float titleBarHeight, float leftReservedWidth,
                                     float rightReservedWidth);

    // Input forwarding
    void InjectRelativeMouse(float dx, float dy);
    void OnTouch(bool down, double xpos, double ypos);
    void OnTouchMove(double xpos, double ypos);

    // Screen capture
    void RequestScreenShot(FScreenShotSpec spec);
    bool IsCapturingScreenShot() const { return screenShot_.IsCapturing(); }

    // Scene and command operations
    void RequestLoadScene(FSceneLoadRequest request);
    void RequestAddSceneReference(std::string assetPath, glm::vec3 translation);
    void RequestSceneGpuRefresh();
    Runtime::Command::CommandHistory& GetCommandHistory() { return commandHistory_; }
    const Runtime::Command::CommandHistory& GetCommandHistory() const { return commandHistory_; }

    // RayCast
    void RayCast(glm::vec3 rayOrigin, glm::vec3 rayDir,
                    std::function<bool(Assets::RayCastResult rayResult)> callback);
    
    // Rendering helpers
    void SetProgressiveRendering(bool enable, bool directly);
    bool IsProgressiveRendering() const { return progressiveRender_.enabled; }
    bool IsOfflineProgressivePathTracing() const
    {
        return progressiveRender_.enabled && renderer_ != nullptr &&
            renderer_->CurrentLogicRendererType() == Vulkan::ERT_PathTracing;
    }
    bool IsEffectiveDenoiserEnabled() const
    {
        return config_.userSettings.Denoiser && !IsOfflineProgressivePathTracing();
    }
    bool IsEffectiveSharcEnabled() const
    {
        return config_.userSettings.SharcEnable && !IsOfflineProgressivePathTracing();
    }
    uint32_t GetProgressiveRenderAccumulatedFrames() const { return progressiveRender_.accumulatedFrames; }
    uint32_t GetProgressiveRenderTargetFrames() const { return FProgressiveRenderState::TargetFrames; }
    Assets::UniformBufferObject& GetLastUniformBufferObject() { return renderer_->PrimaryViewState().previousUniformBuffer; }
    VkDeviceAddress TryGetGPUAccelerationStructureAddress() const;
    VkAccelerationStructureKHR TryGetGPUAccelerationStructureHandle() const;

    // Hot reload
    FHotReloadStatus GetHotReloadStatus() const;
    void RequestShaderHotReload();

    // Main-thread tasks
    void AddTickedTask(TickedTask task) { taskQueues_.ticked.push_back(task); }
    void AddTimerTask(double delay, DelayedTask task);
    
    // Optional Modules
    
    void SetDebugUiProvider(Runtime::IDebugUiProvider* provider) { debugUiProvider_ = provider; }
    Runtime::IDebugUiProvider* GetDebugUiProvider() const { return debugUiProvider_; }
    void SetDebugDraw(std::shared_ptr<Runtime::IDebugDraw> debugDraw) { debugDraw_ = std::move(debugDraw); }
    Runtime::IDebugDraw* GetDebugDraw() const { return debugDraw_.get(); }
    
    void AddRenderFrameConsumer(std::unique_ptr<Runtime::IRenderFrameConsumer> consumer);

    
    void SetScriptRuntimeFactory(Runtime::ScriptRuntimeFactory factory)
    {
        scriptRuntimeFactory_ = std::move(factory);
    }
    Runtime::IScriptRuntime* GetScriptRuntime() { return scriptRuntime_.get(); }
    const Runtime::IScriptRuntime* GetScriptRuntime() const { return scriptRuntime_.get(); }
    
    void SetShaderHotReloaderFactory(Runtime::ShaderHotReloaderFactory factory)
    {
        shaderHotReloaderFactory_ = std::move(factory);
    }
    
    void SetAgentDriverFactory(Runtime::Agent::AgentDriverFactory factory)
    {
        agentDriverFactory_ = std::move(factory);
    }
    
    void SetUiOverlayFactory(std::function<std::unique_ptr<Runtime::IUiOverlay>(NextEngine&)> factory)
    {
        uiOverlayFactory_ = std::move(factory);
    }

    void SetAudioFactory(std::function<std::unique_ptr<NextAudio>()> factory)
    {
        audioFactory_ = std::move(factory);
    }

    void SetPhysicsFactory(std::function<std::unique_ptr<NextPhysics>()> factory)
    {
        physicsFactory_ = std::move(factory);
    }
    
    void SetExternalService(const std::string& key, std::shared_ptr<void> service)
    {
        services_.externalServices[key] = std::move(service);
    }
    
    std::shared_ptr<void> GetExternalService(const std::string& key) const
    {
        auto it = services_.externalServices.find(key);
        return it != services_.externalServices.end() ? it->second : nullptr;
    }

private:
    // Scene loading payload
    struct SceneLoadContext
    {
        std::shared_ptr<std::vector<Assets::Model>> models;
        std::shared_ptr<std::vector<std::shared_ptr<Assets::Node>>> nodes;
        std::shared_ptr<std::vector<Assets::FMaterial>> materials;
        std::shared_ptr<std::vector<Assets::LightObject>> lights;
        std::shared_ptr<std::vector<Assets::AnimationTrack>> tracks;
        std::shared_ptr<std::vector<Assets::Skeleton>> skeletons;
        std::shared_ptr<Assets::EnvironmentSetting> cameraState;
    };

    struct SceneRendererSyncOptions
    {
        bool rebuildMeshBuffer = true;
        bool setRendererScene = true;
        bool resetFrameCounter = true;
        bool postLoadRenderer = true;
        bool refreshSwapChainResources = true;
        bool createSwapChainIfMissing = true;
    };

    // Renderer callbacks
    Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent);
    void OnRendererDeviceSet();
    void OnRendererCreateSwapChain();
    void OnRendererDeleteSwapChain();
    void OnRendererPostLoadScene();
    void OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void OnRendererAfterSubmit();
    void OnRendererBeforeNextFrame();

    // Scene helpers
    const Assets::Scene& GetScene() const { return *scene_; }
    void LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad);
    void LoadScene(const FSceneLoadRequest& request);
    void PrepareRendererForSceneMutation();
    void CommitSceneToRenderer(const SceneRendererSyncOptions& options);

    // Input helpers
    void OnKey(SDL_Event& event);
    void OnCursorPosition(double xpos, double ypos);
    void OnMouseButton(SDL_Event& event);
    void OnScroll(double xoffset, double yoffset);
    void OnDropFile(const char* path);
    void TickGamepadInput();
    void TickAgentValidation();
    bool HandleDebugShortcut(SDL_Keycode key);

    // Lifecycle helpers
    void InitPhysics();
    void TickHotReload();

    // Configuration and user-facing toggles
    struct FConfigState
    {
        mutable Runtime::Config::UserSettings userSettings{};
        mutable Runtime::Config::ShowFlags showFlags{};
    };

    // Per-frame timing and statistics
    struct FFrameState
    {
        uint32_t totalFrames = 0;
        double time = 0.0;
        double deltaSeconds = 0.0;
        double smoothedDeltaSeconds = 0.0;
        float frameRate = 0.0f;
        double lastFrameTime = 0.0;
    };

    // Event-driven pointer state. Remote input injects SDL events but does not
    // necessarily update SDL_GetMouseState(), so gameplay/editor picking must
    // read the coordinates we last observed on the engine event path.
    struct FInputState
    {
        glm::dvec2 mousePos{0.0, 0.0};
        uint32_t mouseButtons = 0;
    };

    // Progressive rendering warmup state
    struct FProgressiveRenderState
    {
        static constexpr uint32_t TargetFrames = 1024;
        bool enabled = false;
        uint32_t warmupFramesRemaining = 0;
        uint32_t accumulatedFrames = 0;
    };

    // Deferred and accumulated screenshot state
    struct FScreenShotState
    {
        bool hasPending = false;
        FScreenShotSpec pending{};
        uint32_t captureFramesRemaining = 0;
        uint32_t captureTotalFrames = 0;
        FScreenShotSpec captureSpec{};
        bool previousProgressiveEnabled = false;
        uint32_t previousProgressiveWarmupFrames = 0;

        bool IsCapturing() const { return hasPending || captureFramesRemaining > 0; }
    };

    // Agent validation: render to a stable frame, capture one screenshot to a fixed path,
    // then auto-exit. Centralized here so every target behaves identically.
    struct FAgentValidationState
    {
        bool active = false;
        bool captured = false;
        bool includeUi = false;
        uint32_t waitFrames = 90;
        uint32_t postCaptureFrames = 0;
        std::string outputPath = "screenshots/agent_validation";
    };

    // Main-thread task queues
    struct FTaskQueues
    {
        std::vector<TickedTask> ticked;
        std::vector<FDelayTaskContext> delayed;
    };

    // Runtime subsystems owned by the engine
    struct FRuntimeServices
    {
        FRuntimeServices();
        ~FRuntimeServices();

        std::unique_ptr<NextLocalization> localization;
        std::unique_ptr<NextCVar::FCVarSystem> cvarSystem;
        std::unique_ptr<NextAudio> audio;
        std::unique_ptr<NextPhysics> physics;
        std::unique_ptr<Utilities::Package::FPackageFileSystem> packageFileSystem;
        std::unique_ptr<Runtime::IShaderHotReloader> shaderHotReloader;
        std::unordered_map<std::string, std::shared_ptr<void>> externalServices;
    };

    // Core ownership
    Runtime::Config::Options* options_ = nullptr;
    std::unique_ptr<Vulkan::Window> window_;
    std::unique_ptr<Vulkan::VulkanBaseRenderer> renderer_;
    std::shared_ptr<Assets::Scene> scene_;
    std::unique_ptr<NextGameInstanceBase> gameInstance_;

    // Engine state
    FConfigState config_{};
    FFrameState frameState_{};
    FInputState inputState_{};
    FProgressiveRenderState progressiveRender_{};
    FScreenShotState screenShot_{};
    FAgentValidationState agentValidation_{};
    std::unique_ptr<Runtime::Agent::IAgentDriver> agentDriver_;
    int requestedExitCode_ = 0;
    FTaskQueues taskQueues_{};
    NextRenderer::EApplicationStatus status_{};

    // Runtime services and UI
    std::unique_ptr<NextUI::UserInterface> userInterface_;
    std::unique_ptr<Runtime::IUiOverlay> uiOverlay_;
    std::function<std::unique_ptr<Runtime::IUiOverlay>(NextEngine&)> uiOverlayFactory_;
    std::vector<std::unique_ptr<Runtime::IRenderFrameConsumer>> renderFrameConsumers_{};
    Runtime::ScriptRuntimeFactory scriptRuntimeFactory_;
    Runtime::ShaderHotReloaderFactory shaderHotReloaderFactory_;
    Runtime::Agent::AgentDriverFactory agentDriverFactory_;
    std::function<std::unique_ptr<NextAudio>()> audioFactory_;
    std::function<std::unique_ptr<NextPhysics>()> physicsFactory_;
    std::unique_ptr<Runtime::IScriptRuntime> scriptRuntime_;
    FRuntimeServices services_{};
    Runtime::IDebugUiProvider* debugUiProvider_ = nullptr;
    std::shared_ptr<Runtime::IDebugDraw> debugDraw_{};

    // Editor and tooling state
    Runtime::Command::CommandHistory commandHistory_{};
};
