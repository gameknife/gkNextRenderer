#pragma once

#include "EditorActionDispatcher.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Engine/Runtime/Camera/ModelViewController.hpp"
#include "Core/EditorSettings.hpp"
#include "Core/EditorUiState.hpp"

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
    void OnDestroy() override {};

    void OnSceneLoaded() override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    void OnInitUI() override;
    std::unique_ptr<NextUI::IMultiViewportBackend> CreateMultiViewportBackend() override;

    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool WantsMouseInputWhenUiCaptures() const override { return true; }

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    EditorActionDispatcher& Actions() { return actions_; }
    const EditorActionDispatcher& Actions() const { return actions_; }
    void DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    EditorInterface& GetEditorInterface() { return *editorUserInterface_; }
    const EditorInterface& GetEditorInterface() const { return *editorUserInterface_; }
    NextUI::GizmoController& GetGizmoController() { return gizmoController_; }
    Editor::EditorSettings& GetEditorSettings() { return settings_; }
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
    void RayCastFromViewport(EViewportInputTarget target, const glm::vec2& mousePos,
                             std::function<bool(Assets::RayCastResult)> callback);

    EditorActionDispatcher actions_{};

    std::unique_ptr<EditorInterface> editorUserInterface_;
    Runtime::Camera::ModelViewController modelViewController_;
    std::array<Runtime::Camera::ModelViewController, Editor::kMaxCameraViewports> cameraViewControllers_;
    EViewportInputTarget activeInputViewport_ = EViewportInputTarget::Scene;
    std::optional<EViewportInputTarget> capturedInputViewport_{};
    NextUI::GizmoController gizmoController_;
    Editor::EditorSettings settings_{};
};

inline bool EditorGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    OutRenderCamera = BuildSceneViewportCamera();
    return true;
}
