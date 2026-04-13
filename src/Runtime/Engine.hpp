#pragma once

#include "Assets/Core/Model.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include "Common/CoreMinimal.hpp"
#include "Options.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Runtime/Command/CommandHistory.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Config/ShowFlags.hpp"
#include "Runtime/Config/UserSettings.hpp"
#include "Options.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/RenderingPipeline.hpp"
#include "Vulkan/WindowSurface.hpp"

class NextPhysics;
class QuickJSEngine;
class NextAudio;

class NextEngine;

namespace NextAI
{
    class FAIService;
    class VoiceInputService;
}

namespace NextCVar
{
    class FCVarSystem;
}

namespace NextCVar
{
    class FCVarSystem;
}
class NextAnimation;

class NextGameInstanceBase
{
public:
    NextGameInstanceBase(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) {}
    virtual ~NextGameInstanceBase() {}
    virtual void OnInit() = 0;
    virtual void OnTick(double deltaSeconds) = 0;
    virtual void OnDestroy() = 0;
    virtual bool OnRenderUI() = 0;
    virtual void OnPreConfigUI() {}
    virtual void OnInitUI() {}
    virtual void OnRayHitResponse(Assets::RayCastResult& result) {}
    virtual void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) {}
    virtual float GetGraphicsDebugPanelTopOffset() const { return 0.0f; }
    virtual void DrawAdditionalPhysicsDebugOverlay(const Assets::Camera& camera) const {}

    // camera
    virtual bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const { return false; }

    // scene
    virtual void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                    std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                    std::vector<Assets::LightObject>& lights,
                                    std::vector<Assets::AnimationTrack>& tracks)
    {
    }
    virtual void OnSceneLoaded() {}
    virtual void OnSceneUnloaded() {}

    // application-owned debug shortcuts; engine-owned F1/F2 are handled by NextEngine
    virtual bool SupportsAppDebugShortcut(SDL_Keycode key) const { return false; }
    virtual bool IsAppDebugShortcutActive(SDL_Keycode key) const { return false; }
    virtual bool SetAppDebugShortcutActive(SDL_Keycode key, bool active) { return false; }
    virtual bool OnKey(SDL_Event& event) { return false; }
    virtual bool OnCursorPosition(double xpos, double ypos) { return false; }
    virtual bool OnMouseButton(SDL_Event& event) { return false; }
    virtual bool OnScroll(double xoffset, double yoffset) { return false; }
    virtual bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
                                int16_t leftTrigger, int16_t rightTrigger)
    {
        return false;
    }
};

class NextGameInstanceVoid : public NextGameInstanceBase
{
public:
    NextGameInstanceVoid(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
        NextGameInstanceBase(config, options, engine)
    {
    }
    ~NextGameInstanceVoid() override = default;

    void OnInit() override {}
    void OnTick(double deltaSeconds) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }
};

extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                                NextEngine* engine);

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

class NextComponent : std::enable_shared_from_this<NextComponent>
{
public:
    NextComponent() = default;
    std::string name_;
    int id_;
};

class NextActor
{
public:
    std::vector<NextComponent*> components;
};

class NextEngine final
{
public:
    VULKAN_NON_COPIABLE(NextEngine)

    static void RegisterReflection();

    NextEngine(Options& options, void* userdata = nullptr);
    ~NextEngine();

    static NextEngine* instance_;
    static NextEngine* GetInstance() { return instance_; }

    Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }
    Vulkan::VulkanBaseRenderer* GetRendererPtr() { return renderer_.get(); }
    const Vulkan::VulkanBaseRenderer* GetRendererPtr() const { return renderer_.get(); }

    void Start();
    bool HandleEvent(SDL_Event& event);
    bool Tick(bool forcingDelta = false);
    void End();

    void OnTouch(bool down, double xpos, double ypos);
    void OnTouchMove(double xpos, double ypos);

    Assets::Scene& GetScene() { return *scene_; }
    Assets::Scene* GetScenePtr() { return scene_.get(); }
    UserSettings& GetUserSettings() { return userSettings_; }
    ShowFlags& GetShowFlags() { return showFlags_; }
    bool IsPhysicsDebugOverlayVisible() const { return showPhysicsDebugOverlay_; }
    void SetPhysicsDebugOverlayVisible(bool visible) { showPhysicsDebugOverlay_ = visible; }
    bool IsGraphicsDebugPanelVisible() const { return showGraphicsDebugPanel_; }
    void SetGraphicsDebugPanelVisible(bool visible) { showGraphicsDebugPanel_ = visible; }

    float GetTime() const { return static_cast<float>(time_); }
    float GetDeltaSeconds() const { return static_cast<float>(deltaSeconds_); }
    float GetSmoothDeltaSeconds() const { return static_cast<float>(smoothedDeltaSeconds_); }
    float GetFrameRate() const { return frameRate_; }
    uint32_t GetTotalFrames() const { return totalFrames_; }
    uint32_t GetTestNumber() const { return 20; }

    void RegisterJSCallback(std::function<void(double)> callback);

    // remove till return true
    void AddTickedTask(TickedTask task) { tickedTasks_.push_back(task); }
    void AddTimerTask(double delay, DelayedTask task);

    // sound
    void PlaySound(const std::string& soundName, bool loop = false, float volume = 1.0f);
    void PauseSound(const std::string& soundName, bool pause);
    bool IsSoundPlaying(const std::string& soundName);

    // screen shot
    void SaveScreenShot(const std::string& filename, int x, int y, int width, int height);

    // pak
    Utilities::Package::FPackageFileSystem& GetPakSystem() { return *packageFileSystem_; }

    // cursor pos
    glm::dvec2 GetMousePos();

    // life cycle
    void RequestClose();
    void RequestMinimize();
    bool IsMaximumed();
    void ToggleMaximize();
    bool IsBorderlessFullscreen() const;
    bool SetBorderlessFullscreen(bool enable);
    bool ToggleBorderlessFullscreen();
    void ConfigureCustomTitleBarDrag(bool enabled, float titleBarHeight, float leftReservedWidth,
                                     float rightReservedWidth);

    // capture
    void RequestScreenShot(std::string filename);
    void RequestHighQualityScreenShot(const std::string& filename, uint32_t accumulateFrames);
    bool IsCapturingHighQuality() const { return hqCaptureFramesRemaining_ > 0; }

    // scene loading
    void RequestLoadScene(std::string sceneFileName);
    void RequestLoadSceneAdd(std::string sceneFileName);

    struct SceneAppendOptions
    {
        bool placeOnHit = false;
        glm::vec3 hitPosition{0.0f, 0.0f, 0.0f};
    };

    void RequestLoadSceneAdd(std::string sceneFileName, const SceneAppendOptions& options);

    // command system
    bool ExecuteCommand(std::unique_ptr<ICommand> command);
    bool UndoCommand();
    bool RedoCommand();
    bool CanUndo() const;
    bool CanRedo() const;
    CommandHistory& GetCommandHistory() { return commandHistory_; }
    const CommandHistory& GetCommandHistory() const { return commandHistory_; }

    CommandHistory& GetCommandSystem() { return commandHistory_; }
    const CommandHistory& GetCommandSystem() const { return commandHistory_; }

    Vulkan::Window& GetWindow() const { return *window_; }

    class UserInterface* GetUserInterface() { return userInterface_.get(); }

    NextAI::FAIService* GetAIService() { return aiService_.get(); }
    const NextAI::FAIService* GetAIService() const { return aiService_.get(); }
    NextAI::VoiceInputService* GetVoiceInputService() { return voiceInputService_.get(); }
    const NextAI::VoiceInputService* GetVoiceInputService() const { return voiceInputService_.get(); }

    NextCVar::FCVarSystem& GetCVarSystem() { return *cvarSystem_; }
    const NextCVar::FCVarSystem& GetCVarSystem() const { return *cvarSystem_; }

    QuickJSEngine* GetQuickJSEngine() { return quickJSEngine_.get(); }

    // monitor info
    glm::ivec2 GetMonitorSize() const;

    // gpu raycast
    void RayCastGPU(glm::vec3 rayOrigin, glm::vec3 rayDir,
                    std::function<bool(Assets::RayCastResult rayResult)> callback);

    void SetProgressiveRendering(bool enable, bool directly);
    bool IsProgressiveRendering() const { return progressiveRendering_; }

    NextRenderer::EApplicationStatus GetEngineStatus() const { return status_; }

    NextPhysics* GetPhysicsEngine() { return physicsEngine_.get(); }

    Assets::UniformBufferObject& GetUniformBufferObject() { return prevUBO_; }

    VkDeviceAddress TryGetGPUAccelerationStructureAddress() const;
    VkAccelerationStructureKHR TryGetGPUAccelerationStructureHandle() const;

protected:
    Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent);
    void OnRendererDeviceSet();
    void OnRendererCreateSwapChain();
    void OnRendererDeleteSwapChain();
    void OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void OnRendererBeforeNextFrame();

    const Assets::Scene& GetScene() const { return *scene_; }

    void OnKey(SDL_Event& event);
    void OnCursorPosition(double xpos, double ypos);
    void OnMouseButton(SDL_Event& event);
    void OnScroll(double xoffset, double yoffset);
    void OnDropFile(const char* path);
    void TickGamepadInput();
    bool HandleDebugShortcut(SDL_Keycode key);

private:
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

    void LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad);

    void LoadScene(std::string sceneFileName);
    void LoadSceneAdd(std::string sceneFileName);
    void LoadSceneAdd(std::string sceneFileName, const SceneAppendOptions& options);

    void InitPhysics();

    // engine stuff
    std::unique_ptr<Vulkan::Window> window_;
    std::unique_ptr<Vulkan::VulkanBaseRenderer> renderer_;

    // settings, may move into scene
    mutable UserSettings userSettings_{};
    mutable ShowFlags showFlags_{};
    mutable Assets::UniformBufferObject prevUBO_{};
    bool showPhysicsDebugOverlay_ = false;
    bool showGraphicsDebugPanel_ = false;

    // scene, maybe multiple at a time
    std::shared_ptr<Assets::Scene> scene_;

    // timing
    uint32_t totalFrames_{};
    double time_{};
    double deltaSeconds_{};
    double smoothedDeltaSeconds_{};
    float frameRate_{};
    double lastFrameTime_{};
    bool progressiveRendering_{};
    uint32_t progressivePreFrames_{};

    // high quality capture state
    uint32_t hqCaptureFramesRemaining_{};
    uint32_t hqCaptureTotalFrames_{};
    std::string hqCaptureFilename_;
    bool hqCapturePrevProgressive_{};
    uint32_t hqCapturePrevPreFrames_{};
    bool screenShotRequested_{};
    std::string screenShotFilename_;

    // game instance
    std::unique_ptr<NextGameInstanceBase> gameInstance_;

    // tasks
    std::vector<TickedTask> tickedTasks_;
    std::vector<FDelayTaskContext> delayedTasks_;

    // internal ui
    std::unique_ptr<class UserInterface> userInterface_;

    std::unique_ptr<NextAI::FAIService> aiService_;
    std::unique_ptr<NextAI::VoiceInputService> voiceInputService_;

    std::unique_ptr<NextCVar::FCVarSystem> cvarSystem_;

    Options* options_ = nullptr;

    // audio
    std::unique_ptr<NextAudio> audioEngine_;

    // physics
    std::unique_ptr<NextPhysics> physicsEngine_;

    // animation
    std::unique_ptr<NextAnimation> animationEngine_;

    // package
    std::unique_ptr<Utilities::Package::FPackageFileSystem> packageFileSystem_;

    std::unique_ptr<QuickJSEngine> quickJSEngine_;

    CommandHistory commandHistory_{};

    // engine status
    NextRenderer::EApplicationStatus status_{};
};
