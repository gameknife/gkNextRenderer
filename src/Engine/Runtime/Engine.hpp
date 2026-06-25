#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Command/CommandHistory.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/ScriptRuntime.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
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

    struct FHotReloadStatus
    {
        bool shaderHotReloadEnabled = false;
        bool shaderInitialized = false;
        double shaderPollIntervalSeconds = 0.5;
        std::filesystem::path shaderSourceRoot;
        std::filesystem::path shaderOutputRoot;
        std::filesystem::path shaderCompiler;
    };

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
    VulkanGpuTimer* GpuTimer() const { return renderer_ ? renderer_->GpuTimer() : nullptr; }
    Assets::Scene& GetScene() { return *scene_; }
    Vulkan::Window& GetWindow() const { return *window_; }

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
    void RequestMinimize();
    bool IsMaximized();
    void ToggleMaximize();
    bool IsBorderlessFullscreen() const;
    bool SetBorderlessFullscreen(bool enable);
    bool ToggleBorderlessFullscreen();
    void ConfigureCustomTitleBarDrag(bool enabled, float titleBarHeight, float leftReservedWidth,
                                     float rightReservedWidth);

    // Input forwarding
    void OnTouch(bool down, double xpos, double ypos);
    void OnTouchMove(double xpos, double ypos);

    // Screen capture
    void RequestScreenShot(FScreenShotSpec spec);
    bool IsCapturingScreenShot() const { return screenShot_.IsCapturing(); }

    // Scene and command operations
    void RequestLoadScene(FSceneLoadRequest request);
    Runtime::Command::CommandHistory& GetCommandHistory() { return commandHistory_; }
    const Runtime::Command::CommandHistory& GetCommandHistory() const { return commandHistory_; }

    // Rendering helpers
    void RayCastGPU(glm::vec3 rayOrigin, glm::vec3 rayDir,
                    std::function<bool(Assets::RayCastResult rayResult)> callback);
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
    Assets::UniformBufferObject& GetLastUniformBufferObject() { return renderState_.previousUniformBuffer; }
    uint32_t GetSunShadowCascadeUpdateMask() const { return renderState_.sunShadowCascadeUpdateMask; }
    VkDeviceAddress TryGetGPUAccelerationStructureAddress() const;
    VkAccelerationStructureKHR TryGetGPUAccelerationStructureHandle() const;

    // Hot reload
    FHotReloadStatus GetHotReloadStatus() const;
    void RequestShaderHotReload();

    // Main-thread tasks
    void AddTickedTask(TickedTask task) { taskQueues_.ticked.push_back(task); }
    void AddTimerTask(double delay, DelayedTask task);

    // Developer debug UI hook (implementation lives in Modules/DevTools,
    // registered by the application entry point; nullptr disables overlays)
    void SetDebugUiProvider(Runtime::IDebugUiProvider* provider) { debugUiProvider_ = provider; }
    Runtime::IDebugUiProvider* GetDebugUiProvider() const { return debugUiProvider_; }

    // Optional frame consumers (remote play, terminal presenter, recording, etc.)
    // are assembled by the application entry before Start().
    void AddRenderFrameConsumer(std::unique_ptr<Runtime::IRenderFrameConsumer> consumer);

    void SetScriptRuntimeFactory(Runtime::ScriptRuntimeFactory factory)
    {
        scriptRuntimeFactory_ = std::move(factory);
    }
    Runtime::IScriptRuntime* GetScriptRuntime() { return scriptRuntime_.get(); }
    const Runtime::IScriptRuntime* GetScriptRuntime() const { return scriptRuntime_.get(); }

    // Optional UI overlay (implementation in Modules/NextRmlUi); the factory is
    // installed by the application entry and instantiated with the renderer.
    void SetUiOverlayFactory(std::function<std::unique_ptr<Runtime::IUiOverlay>(NextEngine&)> factory)
    {
        uiOverlayFactory_ = std::move(factory);
    }

    // Type-erased service slots for optional modules (e.g. Modules/NextAI).
    // Modules attach their engine-scoped singletons here so the core stays
    // free of module types; lifetime ends with the engine.
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
        std::shared_ptr<std::vector<Assets::FGaussianSplatData>> splats;
        std::shared_ptr<Assets::EnvironmentSetting> cameraState;
    };

    // Renderer callbacks
    Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent);
    void OnRendererDeviceSet();
    void OnRendererCreateSwapChain();
    void OnRendererDeleteSwapChain();
    void OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void OnRendererBeforeNextFrame();

    // Scene helpers
    const Assets::Scene& GetScene() const { return *scene_; }
    void LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad);
    void LoadScene(const FSceneLoadRequest& request);

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

    // Renderer-derived transient state
    struct FRenderState
    {
        mutable Assets::UniformBufferObject previousUniformBuffer{};
        mutable Assets::CascadeShadowSetup cachedSunCascades{};
        mutable bool cachedSunCascadesValid = false;
        mutable uint32_t sunShadowCascadeUpdateMask = 0;
        mutable uint32_t sunShadowInitializedMask = 0;
        mutable uint32_t sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
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
        std::unique_ptr<Vulkan::ShaderHotReloader> shaderHotReloader;
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
    FRenderState renderState_{};
    FFrameState frameState_{};
    FInputState inputState_{};
    FProgressiveRenderState progressiveRender_{};
    FScreenShotState screenShot_{};
    FAgentValidationState agentValidation_{};
    FTaskQueues taskQueues_{};
    NextRenderer::EApplicationStatus status_{};

    // Runtime services and UI
    std::unique_ptr<NextUI::UserInterface> userInterface_;
    std::unique_ptr<Runtime::IUiOverlay> uiOverlay_;
    std::function<std::unique_ptr<Runtime::IUiOverlay>(NextEngine&)> uiOverlayFactory_;
    std::vector<std::unique_ptr<Runtime::IRenderFrameConsumer>> renderFrameConsumers_{};
    Runtime::ScriptRuntimeFactory scriptRuntimeFactory_;
    std::unique_ptr<Runtime::IScriptRuntime> scriptRuntime_;
    FRuntimeServices services_{};
    Runtime::IDebugUiProvider* debugUiProvider_ = nullptr;

    // Editor and tooling state
    Runtime::Command::CommandHistory commandHistory_{};
};
