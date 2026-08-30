#include "ScadLibraryMain.h"
#include "ScadLibraryInterface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <limits>

namespace
{
    constexpr float kScadLibraryDefaultNearPlane = 0.5f;
    constexpr float kScadLibrarySmallSceneMinNearPlane = 0.02f;
    constexpr float kScadLibrarySmallSceneNearScale = 0.025f;
    constexpr float kScadLibrarySmallSceneDiagonal = 20.0f;

    float ScadLibraryNearPlaneForScene(const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        const float sceneDiagonal = glm::length(boundsMax - boundsMin);
        if (sceneDiagonal <= 0.0f)
        {
            return kScadLibraryDefaultNearPlane;
        }
        if (sceneDiagonal >= kScadLibrarySmallSceneDiagonal)
        {
            return kScadLibraryDefaultNearPlane;
        }

        // Reverse-Z makes the far plane cheap, so spend the depth precision at
        // the near end. Keep ordinary scenes at 0.5m; only tiny kit previews
        // are allowed to lower it so their parts are not clipped immediately.
        return std::clamp(sceneDiagonal * kScadLibrarySmallSceneNearScale,
                          kScadLibrarySmallSceneMinNearPlane, kScadLibraryDefaultNearPlane);
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<ScadLibraryGameInstance>(config, options, engine);
}

ScadLibraryGameInstance::ScadLibraryGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                 NextEngine* engine) : NextGameInstanceBase(config, options, engine)
{
    ui_ = std::make_unique<ScadLibrary::ScadLibraryInterface>(*engine, options.SceneName);
    cameraController_.SetLeftDragPans(true);

    const glm::ivec2 monitorSize = GetEngine().GetMonitorSize();
    config.HideTitleBar = true;
    config.Title = "SCAD Library";
    config.Width = static_cast<uint32_t>(monitorSize.x * 0.75f);
    config.Height = static_cast<uint32_t>(monitorSize.y * 0.75f);
    config.ForceSDR = true;

    options.ForceSDR = true;
}

ScadLibraryGameInstance::~ScadLibraryGameInstance() = default;

void ScadLibraryGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    // Favour responsiveness over final quality for iterative part browsing.
    std::string error;
    cvars.SetDefaultFromString("r.samples", "4", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
#if defined(__APPLE__)
    // MoltenVK has no hardware ray tracing path; use the cheaper lighting-only renderer for
    // ScadLibrary's iterative previews and validation runs on macOS.
    cvars.SetDefaultFromString("r.rendererType", "4", &error);
#endif
}

void ScadLibraryGameInstance::OnInit() {}

void ScadLibraryGameInstance::OnTick(double deltaSeconds)
{
    cameraController_.UpdateCamera(1.0, deltaSeconds);
    ui_->RigPreview().Tick(deltaSeconds);
    ui_->TickKitFileWatch(deltaSeconds);
    // Agent validation (capture + auto-exit) is handled centrally by NextEngine.
}

void ScadLibraryGameInstance::OnDestroy() {}

void ScadLibraryGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                 std::vector<Assets::Model>& models,
                                                 std::vector<Assets::FMaterial>& materials,
                                                 std::vector<Assets::LightObject>& lights,
                                                 std::vector<Assets::AnimationTrack>& tracks)
{
    // Character designer rig preview: part models/materials must ride the rebuild.
    ui_->RigPreview().InjectAssets(models, materials);
}

void ScadLibraryGameInstance::OnSceneUnloaded() { ui_->RigPreview().OnSceneUnloaded(); }

void ScadLibraryGameInstance::OnSceneLoaded()
{
    ui_->RigPreview().OnSceneLoaded(GetEngine().GetScene());

    Assets::Scene& scene = GetEngine().GetScene();
    scene.GetEnvSettings().BackgroundMode = Assets::EBackgroundMode::Studio;
    const glm::vec3 minBounds = scene.GetSceneAABBMin();
    const glm::vec3 maxBounds = scene.GetSceneAABBMax();
    if (glm::all(glm::lessThan(minBounds, maxBounds)))
    {
        const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
        const float radius = std::max(glm::length(maxBounds - minBounds) * 0.5f, 0.5f);
        Assets::Camera& sceneCamera = scene.GetRenderCamera();
        sceneCamera.NearPlane = ScadLibraryNearPlaneForScene(minBounds, maxBounds);
        sceneCamera.FarPlane = GetEngine().GetUserSettings().CameraFarPlane;
        cameraController_.SetNavigationScale(radius);
        if (ui_->ConsumePreserveCameraOnNextSceneLoad())
        {
            return;
        }

        // Re-frame whatever was just previewed/composed immediately. Loading a
        // scene should show the final view without an extra camera zoom.
        cameraController_.Reset(scene.GetRenderCamera());
        cameraController_.SetOrbitTarget(center);
        cameraController_.SetAltPressed(true);
        cameraController_.FocusImmediate(center, radius);
        return;
    }
    if (ui_->ConsumePreserveCameraOnNextSceneLoad())
    {
        return;
    }
    Assets::Camera& sceneCamera = scene.GetRenderCamera();
    sceneCamera.NearPlane = kScadLibraryDefaultNearPlane;
    sceneCamera.FarPlane = GetEngine().GetUserSettings().CameraFarPlane;
    cameraController_.Reset(scene.GetRenderCamera());
}

void ScadLibraryGameInstance::OnPreConfigUI() { ui_->Config(); }

void ScadLibraryGameInstance::OnInitUI() { ui_->Init(); }

bool ScadLibraryGameInstance::OnRenderUI()
{
    ui_->Render();
    if (ui_->ConsumeFrameAllRequest())
    {
        FrameAllSceneObjects();
    }
    if (ui_->ConsumeFocusSelectedRequest())
    {
        FocusSelectedSceneObject();
    }
    return true;
}

void ScadLibraryGameInstance::FocusSelectedSceneObject()
{
    glm::vec3 center;
    float radius = 0.0f;
    if (!ui_->GetSelectedSceneObjectBounds(center, radius))
    {
        return;
    }

    cameraController_.SetNavigationScale(radius);
    cameraController_.SetOrbitTarget(center);
    cameraController_.SetAltPressed(true);
    cameraController_.Focus(center, radius);
}

void ScadLibraryGameInstance::FrameAllSceneObjects()
{
    Assets::Scene& scene = GetEngine().GetScene();
    const glm::vec3 minBounds = scene.GetSceneAABBMin();
    const glm::vec3 maxBounds = scene.GetSceneAABBMax();
    if (!glm::all(glm::lessThan(minBounds, maxBounds)))
    {
        cameraController_.Reset(scene.GetRenderCamera());
        cameraController_.SetOrbitTarget(std::nullopt);
        return;
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const float radius = std::max(glm::length(maxBounds - minBounds) * 0.5f, 0.5f);
    cameraController_.Reset(scene.GetRenderCamera());
    cameraController_.SetNavigationScale(radius);
    cameraController_.SetOrbitTarget(center);
    cameraController_.SetAltPressed(true);
    cameraController_.Focus(center, radius);
}

bool ScadLibraryGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = cameraController_.ModelView();
    outRenderCamera.FieldOfView = cameraController_.FieldOfView();
    return true;
}

bool ScadLibraryGameInstance::OnKey(SDL_Event& event)
{
    const bool modeShortcut = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 && !ImGui::GetIO().WantTextInput;
    if (modeShortcut)
    {
        if (event.key.key == SDLK_S && ui_->WorkspaceMode() == ScadLibrary::EWorkspaceMode::SceneAssembly)
        {
            ui_->SaveCurrentAssembly();
            return true;
        }
        if (event.key.key == SDLK_1)
        {
            ui_->SetWorkspaceMode(ScadLibrary::EWorkspaceMode::SceneAssembly);
            return true;
        }
        if (event.key.key == SDLK_2)
        {
            ui_->SetWorkspaceMode(ScadLibrary::EWorkspaceMode::CharacterDesigner);
            return true;
        }
        if (event.key.key == SDLK_3)
        {
            ui_->SetWorkspaceMode(ScadLibrary::EWorkspaceMode::CharacterWorkbench);
            return true;
        }
        if (event.key.key == SDLK_4)
        {
            ui_->SetWorkspaceMode(ScadLibrary::EWorkspaceMode::KitBrowser);
            return true;
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !ImGui::GetIO().WantTextInput)
    {
        if (event.key.key == SDLK_F && ui_->WorkspaceMode() == ScadLibrary::EWorkspaceMode::SceneAssembly)
        {
            FocusSelectedSceneObject();
            return true;
        }
        if (event.key.key == SDLK_ESCAPE)
        {
            GetEngine().GetScene().ClearSelection();
            GetEngine().GetShowFlags().ShowEdge = false;
            return true;
        }
    }

    cameraController_.OnKey(event);
    return true;
}

bool ScadLibraryGameInstance::OnCursorPosition(double xpos, double ypos)
{
    glm::vec3 center;
    float radius = 0.0f;
    const bool hasSelectedSceneObject = ui_->WorkspaceMode() == ScadLibrary::EWorkspaceMode::SceneAssembly &&
        ui_->GetSelectedSceneObjectBounds(center, radius);
    if (hasSelectedSceneObject || GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        cameraController_.SetOrbitTarget(center);
    }

    if (cameraController_.IsRightMousePressed() || !ui_->IsTerrainFeatureDragging())
    {
        cameraController_.OnCursorPosition(xpos, ypos);
    }
    return true;
}

bool ScadLibraryGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_RIGHT)
    {
        cameraController_.OnMouseButton(event);
        return true;
    }

    const glm::vec2 mousePos = GetEngine().GetMousePos();
    const NextUI::IUserInterface* userInterface = GetEngine().GetUserInterface();
    const float uiScale = userInterface != nullptr ? userInterface->UiScale() : 1.0f;
    // SDL mouse coordinates are framebuffer pixels on DPI-aware Windows, while
    // IsViewportPoint stores the viewport and toolbar in ImGui logical pixels.
    // Keep mousePos unchanged for GetScreenToWorldRay, which consumes framebuffer pixels.
    const glm::vec2 uiMousePos = mousePos / std::max(uiScale, 1.0f);

    // Don't start a pan drag or a scene pick when the cursor is over a panel/widget.
    if (ImGui::GetIO().WantCaptureMouse && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        !ui_->IsViewportPoint(uiMousePos.x, uiMousePos.y))
    {
        return true;
    }
    if (event.button.button == SDL_BUTTON_LEFT && ui_->TerrainFeatureConsumesMouse(mousePos.x, mousePos.y))
    {
        return true;
    }
    cameraController_.OnMouseButton(event);
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        ui_->WorkspaceMode() == ScadLibrary::EWorkspaceMode::SceneAssembly && !ui_->HasActiveProceduralHandles() &&
        ui_->IsViewportPoint(uiMousePos.x, uiMousePos.y))
    {
        glm::vec3 origin;
        glm::vec3 direction;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, origin, direction);
        ui_->SelectSceneObjectFromViewport(origin, direction);
    }
    return true;
}

bool ScadLibraryGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        return true;
    }
    cameraController_.OnScroll(xoffset, yoffset);
    return true;
}
