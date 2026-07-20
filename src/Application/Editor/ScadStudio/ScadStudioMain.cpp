#include "ScadStudioMain.h"
#include "ScadStudioInterface.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include "Modules/ScadLoader/ScadModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<ScadStudioGameInstance>(config, options, engine);
}

ScadStudioGameInstance::ScadStudioGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                               NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ui_ = std::make_unique<ScadStudio::ScadStudioInterface>(*engine);

    const glm::ivec2 monitorSize = GetEngine().GetMonitorSize();
    config.Title = "SCAD Studio";
    config.Width = static_cast<uint32_t>(monitorSize.x * 0.75f);
    config.Height = static_cast<uint32_t>(monitorSize.y * 0.75f);
    config.ForceSDR = true;

    options.ForceSDR = true;
}

ScadStudioGameInstance::~ScadStudioGameInstance() = default;

void ScadStudioGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    // Favour responsiveness over final quality for iterative authoring.
    std::string error;
    cvars.SetDefaultFromString("r.samples", "4", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
}

void ScadStudioGameInstance::OnInit() {}

void ScadStudioGameInstance::OnTick(double deltaSeconds)
{
    cameraController_.UpdateCamera(1.0, deltaSeconds);
    // Agent validation (capture + auto-exit) is handled centrally by NextEngine.
}

void ScadStudioGameInstance::OnDestroy() {}

void ScadStudioGameInstance::OnSceneLoaded()
{
    // Re-frame the freshly loaded SCAD model and make right-drag orbit around it
    // by default. Module preview loads a temporary scene, so the same path focuses
    // the selected module without needing module-level render nodes.
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

void ScadStudioGameInstance::OnPreConfigUI() { ui_->Config(); }

void ScadStudioGameInstance::OnInitUI() { ui_->Init(); }

bool ScadStudioGameInstance::OnRenderUI()
{
    ui_->Render();
    return true;
}

bool ScadStudioGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = cameraController_.ModelView();
    outRenderCamera.FieldOfView = cameraController_.FieldOfView();
    return true;
}

bool ScadStudioGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE move only engages while the right mouse button is held, so this is
    // safe to forward even while the user is typing in the chat box.
    cameraController_.OnKey(event);
    return true;
}

bool ScadStudioGameInstance::OnCursorPosition(double xpos, double ypos)
{
    cameraController_.OnCursorPosition(xpos, ypos);
    return true;
}

bool ScadStudioGameInstance::OnMouseButton(SDL_Event& event)
{
    // Don't start an orbit drag when the cursor is over a panel/widget.
    if (ImGui::GetIO().WantCaptureMouse && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        return true;
    }
    cameraController_.OnMouseButton(event);
    return true;
}

bool ScadStudioGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        return true;
    }
    cameraController_.OnScroll(xoffset, yoffset);
    return true;
}
