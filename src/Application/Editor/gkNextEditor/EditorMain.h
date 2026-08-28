#pragma once

#include "EditorActionDispatcher.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Gameplay/Camera/ModelViewController.hpp"
#include "Core/EditorPlaySession.hpp"
#include "Core/EditorSettings.hpp"
#include "Core/EditorUiState.hpp"
#if GK_WITH_VITURE
#include "Modules/NextViture/VitureModule.hpp"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <optional>

class EditorInterface;
struct ImGuiWindow;

namespace Assets
{
    class Scene;
}

class EditorGameInstance : public NextGameInstanceBase
{
public:
    EditorGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~EditorGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;

    void OnSceneLoaded() override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    bool OnGameRequestedClose() override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
                        int16_t leftTrigger, int16_t rightTrigger) override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    bool OnRenderUI(const FGameUiFrameContext& context) override;
    NextUI::FUiFrameResult RenderUiFrame(const FGameUiFrameContext& context) override;
    void OnInitUI() override;
    void OnRemoteUiSessionClosed(std::string_view sessionId) override;
    std::unique_ptr<NextUI::IMultiViewportBackend> CreateMultiViewportBackend() override;

#if GK_WITH_VITURE
    bool HasVitureDebugPanel() const { return headPoseTracker_ != nullptr; }
    bool& GetVitureDebugPanelVisible() { return vitureDebugPanelVisible_; }
    void DrawVitureDebugPanel();
#endif

    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool WantsMouseInputWhenUiCaptures() const override { return true; }

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    Editor::FPlaySession& GetPlaySession() { return playSession_; }
    const Editor::FPlaySession& GetPlaySession() const { return playSession_; }
    /// Starts the given game, remembering the scene currently open so Stop can come back to it.
    void StartPlaySession(const std::string& gameId);

    EditorActionDispatcher& Actions() { return actions_; }
    const EditorActionDispatcher& Actions() const { return actions_; }
    void DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    EditorInterface& GetEditorInterface() { return *editorUserInterface_; }
    const EditorInterface& GetEditorInterface() const { return *editorUserInterface_; }
    NextUI::GizmoController& GetGizmoController() { return gizmoController_; }
    Editor::EditorSettings& GetEditorSettings() { return settings_; }
    void SelectSceneCamera(size_t cameraIndex);
    void ResetToDefaultSceneCamera();
    void SetSceneViewportFieldOfView(float fieldOfView);
    Assets::Camera BuildSceneViewportCamera() const;
    Assets::Camera BuildCameraViewCamera(size_t viewIndex) const;
    void SyncCameraViewRendererCamera(size_t viewIndex, const glm::vec2& viewportSize);
    void DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize,
                   const Assets::UniformBufferObject* viewUbo, ImGuiWindow* alternativeWindow = nullptr);

private:
    enum class EViewportInputTarget
    {
        Scene,
        CameraView0,
        CameraView1,
        CameraView2
    };

    static std::optional<size_t> CameraViewIndex(EViewportInputTarget target);
    static EViewportInputTarget CameraViewTarget(size_t viewIndex);
    Runtime::Camera::ModelViewController& ControllerForViewport(EViewportInputTarget target);
    const Runtime::Camera::ModelViewController& ControllerForViewport(EViewportInputTarget target) const;
    std::optional<EViewportInputTarget> ResolveViewportUnderMouse() const;
    EViewportInputTarget ActiveViewportFromUi() const;
    void SetActiveInputViewport(EViewportInputTarget target);
    bool IsMouseInRect(const glm::vec2& mousePos, const glm::vec2& rectPos, const glm::vec2& rectSize) const;
    void UpdateControllerContext(Runtime::Camera::ModelViewController& controller);
#if GK_WITH_VITURE
    bool UpdateArTracking(double deltaSeconds);
#endif
    void RayCastFromViewport(EViewportInputTarget target, const glm::vec2& mousePos,
                             std::function<bool(Assets::RayCastResult)> callback);
    /// Draws the running game's UI into the viewport panel's rect.
    void RenderPlaySessionUI();

    EditorActionDispatcher actions_{};

    Editor::FPlaySession playSession_;
    /// Control channel for scripted validation and the console: set ed.play to a game id to start
    /// it, empty to stop. Consumed on tick, never inside the cvar callback.
    std::string playCVarValue_;
    std::string pendingPlayRequest_;
    bool hasPendingPlayRequest_ = false;
    bool playEjectCVar_ = false;
    /// Backing store for ed.newProject, which is how a script or the console opens the dialog.
    bool newProjectCVar_ = false;
    std::unique_ptr<EditorInterface> editorUserInterface_;
    Runtime::Camera::ModelViewController modelViewController_;
#if GK_WITH_VITURE
    std::unique_ptr<Modules::Viture::IHeadPoseTracker> headPoseTracker_;
    Modules::Viture::FHeadTrackingCamera arCamera_;
    std::optional<Modules::Viture::FHeadPose> latestArPose_;
    bool vitureDebugPanelVisible_ = true;
#endif
    std::array<Runtime::Camera::ModelViewController, Editor::kMaxCameraViewports> cameraViewControllers_;
    EViewportInputTarget activeInputViewport_ = EViewportInputTarget::Scene;
    std::optional<EViewportInputTarget> capturedInputViewport_{};
    NextUI::GizmoController gizmoController_;
    Editor::EditorSettings settings_{};
    uint32_t progressiveRenderResumeFramesRemaining_ = 0;
};

inline bool EditorGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    // While a game is playing it drives the camera, the same as in its own executable. Ejecting
    // returns the viewport to the editor's camera without stopping the game.
    if (playSession_.TryGetOverrideCamera(OutRenderCamera))
    {
        return true;
    }
    OutRenderCamera = BuildSceneViewportCamera();
    return true;
}
