#include "ScadLibraryMain.h"
#include "ScadLibraryInterface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<ScadLibraryGameInstance>(config, options, engine);
}

ScadLibraryGameInstance::ScadLibraryGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                 NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ui_ = std::make_unique<ScadLibrary::ScadLibraryInterface>(*engine);

    const glm::ivec2 monitorSize = GetEngine().GetMonitorSize();
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
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
}

void ScadLibraryGameInstance::OnInit() {}

void ScadLibraryGameInstance::OnTick(double deltaSeconds)
{
    cameraController_.UpdateCamera(1.0, deltaSeconds);
    ui_->RigPreview().Tick(deltaSeconds);
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

void ScadLibraryGameInstance::OnSceneUnloaded()
{
    ui_->RigPreview().OnSceneUnloaded();
}

void ScadLibraryGameInstance::OnSceneLoaded()
{
    ui_->RigPreview().OnSceneLoaded(GetEngine().GetScene());

    // Re-frame whatever was just previewed/composed and orbit around it.
    Assets::Scene& scene = GetEngine().GetScene();
    cameraController_.Reset(scene.GetRenderCamera());

    const glm::vec3 minBounds = scene.GetSceneAABBMin();
    const glm::vec3 maxBounds = scene.GetSceneAABBMax();
    if (glm::all(glm::lessThan(minBounds, maxBounds)))
    {
        const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
        const float radius = std::max(glm::length(maxBounds - minBounds) * 0.5f, 0.5f);
        cameraController_.SetOrbitTarget(center);
        cameraController_.SetAltPressed(true);
        cameraController_.Focus(center, radius);
    }
}

void ScadLibraryGameInstance::OnPreConfigUI() { ui_->Config(); }

void ScadLibraryGameInstance::OnInitUI() { ui_->Init(); }

bool ScadLibraryGameInstance::OnRenderUI()
{
    ui_->Render();
    return true;
}

bool ScadLibraryGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = cameraController_.ModelView();
    outRenderCamera.FieldOfView = cameraController_.FieldOfView();
    return true;
}

bool ScadLibraryGameInstance::OnKey(SDL_Event& event)
{
    cameraController_.OnKey(event);
    return true;
}

bool ScadLibraryGameInstance::OnCursorPosition(double xpos, double ypos)
{
    cameraController_.OnCursorPosition(xpos, ypos);
    return true;
}

bool ScadLibraryGameInstance::OnMouseButton(SDL_Event& event)
{
    // Don't start an orbit drag when the cursor is over a panel/widget.
    if (ImGui::GetIO().WantCaptureMouse && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        return true;
    }
    cameraController_.OnMouseButton(event);
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
