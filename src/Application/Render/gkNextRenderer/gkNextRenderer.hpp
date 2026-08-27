#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Gameplay/Camera/ModelViewController.hpp"
#if GK_WITH_VITURE
#include "Modules/NextViture/VitureModule.hpp"
#endif

class NextRendererGameInstance : public NextGameInstanceBase
{
public:
    NextRendererGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextRendererGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                       std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    bool OnRenderUI(const FGameUiFrameContext& context) override;
    NextUI::FUiFrameResult RenderUiFrame(const FGameUiFrameContext& context) override;
    void OnInitUI() override;
    void OnRemoteUiSessionClosed(std::string_view sessionId) override;

#if GK_WITH_VITURE
    bool HasVitureDebugPanel() const { return headPoseTracker_ != nullptr; }
    bool& GetVitureDebugPanelVisible() { return vitureDebugPanelVisible_; }
#endif

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    float GetGraphicsDebugPanelTopOffset() const override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnTouch(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY,
                    int16_t rightStickX, int16_t rightStickY,
                    int16_t leftTrigger, int16_t rightTrigger) override;
    bool OnRemoteViewAction(const FRemoteViewActionContext& context, std::string_view action) override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

    void CreateSphereAndPush();
    void CreateBoxAndPush();
    void DropPhysicsSphereGrid();

    enum class EWorkMode : uint8_t
    {
        Render = 0,
        Detail,
        Profile,
        CVar,
        Count,
    };

private:
    struct FRendererUiState
    {
        NextUI::GizmoController gizmoController;
        EWorkMode workMode = EWorkMode::Render;
        EWorkMode lastWorkMode = EWorkMode::Render;
        bool showSettings = false;
        bool showCheatSheet = true;
        bool showAbout = false;
    };

    struct FLaunchView
    {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        std::string debugName{"tempBox"};
    };

    void CreateBoxAndPushFromView(const FLaunchView& view);

    bool DrawRendererUi(const FGameUiFrameContext& context, FRendererUiState& uiState);
    FRendererUiState& GetRemoteUiState(std::string_view sessionId);
    void DrawSettings(FRendererUiState& uiState);
    void DrawTitleBar(const FGameUiFrameContext& context, FRendererUiState& uiState);
    void DrawBottomStatusBar();
    void DrawModeRail(FRendererUiState& uiState);
    void DrawViewportTopBar(const FGameUiFrameContext& context, FRendererUiState& uiState);
    void DrawViewportCheatSheet(FRendererUiState& uiState);
    void RequestScreenshot(bool openFolder, const std::string& tag);
    void DrawVideoCaptureMenuItems();
    void RequestThreeSecondVideo(Runtime::IScreenShotService::EVideoOutputScale outputScale);
#if GK_WITH_VITURE
    bool UpdateArTracking(double deltaSeconds);
    void DrawVitureDebugPanel();
#endif
    Runtime::Camera::ModelViewController modelViewController_;
#if IOS || ANDROID
    uint64_t mobilePanFinger_ = 0;
    uint64_t mobileRotateFinger_ = 0;
    glm::dvec2 mobilePanCenter_{};
#endif
#if GK_WITH_VITURE
    std::unique_ptr<Modules::Viture::IHeadPoseTracker> headPoseTracker_;
    Modules::Viture::FHeadTrackingCamera arCamera_;
    std::optional<Modules::Viture::FHeadPose> latestArPose_;
    bool vitureDebugPanelVisible_ = true;
#endif

    FRendererUiState mainUiState_;
    std::unordered_map<std::string, FRendererUiState> remoteUiStates_;

    uint32_t modelId_;
    uint32_t boxModelId_;
    std::vector<uint32_t> matIds_;
    std::vector<uint32_t> dropSphereMatIds_;
    uint32_t dropSphereSequence_ = 0;

    bool isTakingScreenshot_ = false;
    bool isRecordingVideo_ = false;
    Runtime::IScreenShotService::EVideoOutputScale videoOutputScale_ =
        Runtime::IScreenShotService::EVideoOutputScale::Half;
    bool playbackPaused_ = false;
    bool stepRequested_ = false;
};
