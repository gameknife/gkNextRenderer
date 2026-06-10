#pragma once

#include "EditorActionDispatcher.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Engine/Runtime/Camera/ModelViewController.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class EditorInterface;

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

    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    EditorActionDispatcher& Actions() { return actions_; }
    const EditorActionDispatcher& Actions() const { return actions_; }
    void DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    EditorInterface& GetEditorInterface() { return *editorUserInterface_; }
    NextUI::GizmoController& GetGizmoController() { return gizmoController_; }

private:
    EditorActionDispatcher actions_{};

    std::unique_ptr<EditorInterface> editorUserInterface_;
    Runtime::Camera::ModelViewController modelViewController_;
    NextUI::GizmoController gizmoController_;
};

inline bool EditorGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    OutRenderCamera.ModelView = modelViewController_.ModelView();
    OutRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}
