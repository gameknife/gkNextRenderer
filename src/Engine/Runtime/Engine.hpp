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
    NextAudio* GetAudio() { return services_.audio.get(); }
    const NextAudio* GetAudio() const { return services_.audio.get(); }
    NextLocalization* GetLocalization() { return services_.localization.get(); }
    const NextLocalization* GetLocalization() const { return services_.localization.get(); }
    NextAI::FAIService* GetAIService() { return services_.aiService.get(); }
    const NextAI::FAIService* GetAIService() const { return services_.aiService.get(); }
    NextAI::VoiceInputService* GetVoiceInputService() { return services_.voiceInputService.get(); }
    const NextAI::VoiceInputService* GetVoiceInputService() const { return services_.voiceInputService.get(); }
    NextCVar::FCVarSystem& GetCVarSystem() { return *services_.cvarSystem; }
    const NextCVar::FCVarSystem& GetCVarSystem() const { return *services_.cvarSystem; }
    QuickJSEngine* GetQuickJSEngine() { return services_.quickJSEngine.get(); }
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
    Assets::UniformBufferObject& GetLastUniformBufferObject() { return renderState_.previousUniformBuffer; }
    uint32_t GetSunShadowCascadeUpdateMask() const { return renderState_.sunShadowCascadeUpdateMask; }
    VkDeviceAddress TryGetGPUAccelerationStructureAddress() const;
    VkAccelerationStructureKHR TryGetGPUAccelerationStructureHandle() const;

    // Hot reload
    FHotReloadStatus GetHotReloadStatus() const;
    void RequestShaderHotReload();

    // Main-thread tasks and scripting callbacks
    void RegisterJSCallback(std::function<void(double)> callback);
    void AddTickedTask(TickedTask task) { taskQueues_.ticked.push_back(task); }
    void AddTimerTask(double delay, DelayedTask task);

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

    // Progressive rendering warmup state
    struct FProgressiveRenderState
    {
        bool enabled = false;
        uint32_t warmupFramesRemaining = 0;
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
        std::unique_ptr<NextAI::FAIService> aiService;
        std::unique_ptr<NextAI::VoiceInputService> voiceInputService;
        std::unique_ptr<NextCVar::FCVarSystem> cvarSystem;
        std::unique_ptr<NextAudio> audio;
        std::unique_ptr<NextPhysics> physics;
        std::unique_ptr<Utilities::Package::FPackageFileSystem> packageFileSystem;
        std::unique_ptr<QuickJSEngine> quickJSEngine;
        std::unique_ptr<Vulkan::ShaderHotReloader> shaderHotReloader;
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
    FProgressiveRenderState progressiveRender_{};
    FScreenShotState screenShot_{};
    FTaskQueues taskQueues_{};
    NextRenderer::EApplicationStatus status_{};

    // Runtime services and UI
    std::unique_ptr<NextUI::UserInterface> userInterface_;
    FRuntimeServices services_{};

    // Editor and tooling state
    Runtime::Command::CommandHistory commandHistory_{};
};
