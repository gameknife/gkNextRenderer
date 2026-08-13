#include "CitySolSimGameInstance.hpp"

#include "CitySolSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

namespace
{
    constexpr float kCityFov = 48.0f;
    constexpr glm::vec3 kOverviewTarget{0.0f, 0.0f, -105.0f};
    constexpr glm::vec3 kOverviewOffset{0.0f, 390.0f, 340.0f};
    constexpr glm::vec3 kFollowOffset{0.0f, 17.0f, 19.0f};
    constexpr float kCameraSharpness = 7.0f;

    bool ProjectForPick(const glm::mat4& viewProjection, const ImGuiViewport& viewport,
                        const glm::vec3& world, glm::vec2& outScreen)
    {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)
        {
            return false;
        }
        outScreen = {viewport.Pos.x + (ndc.x * 0.5f + 0.5f) * viewport.Size.x,
                     viewport.Pos.y + (-ndc.y * 0.5f + 0.5f) * viewport.Size.y};
        return true;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<CitySolSimGameInstance>(config, options, engine);
}

CitySolSimGameInstance::CitySolSimGameInstance(Vulkan::WindowConfig& config,
                                               Runtime::Config::Options& options,
                                               NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "CitySolSim", 1920, 1080, false);
}

void CitySolSimGameInstance::OnInit()
{
    GOption->KeepCPUMeshData = true;
    const std::string sceneName = GOption->SceneName.empty() ? CitySolSim::Config::kScenePath : GOption->SceneName;
    SPDLOG_INFO("CitySolSim: loading procedural city '{}'", sceneName);
    GetEngine().RequestLoadScene({.filename = sceneName});
}

void CitySolSimGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                std::vector<Assets::Model>& models,
                                                std::vector<Assets::FMaterial>& materials,
                                                std::vector<Assets::LightObject>& /*lights*/,
                                                std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    traffic_.InjectAssets(nodes, models, materials);
    citizens_.InjectAssets(models, materials);
    BuildStreetLights(nodes, models, materials);
}

void CitySolSimGameInstance::BuildStreetLights(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials)
{
    streetLightNodes_.clear();
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.45f));
    const uint32_t bulbModel = static_cast<uint32_t>(models.size() - 1);
    const uint32_t bulbMaterial =
        Assets::SceneBuilder::AddDiffuseLightMaterial(materials, {1.0f, 0.74f, 0.38f}, 12.0f);
    const std::array<float, 4> lampRows = {25.1f, -24.9f, -124.9f, -224.9f};
    for (size_t row = 0; row < lampRows.size(); ++row)
    {
        for (size_t column = row % 2; column < CitySolSim::Config::kBlockX.size(); column += 2)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("city_night_light_{}_{}", row, column),
                {CitySolSim::Config::kBlockX[column], 4.7f, lampRows[row]}, glm::vec3(1.0f),
                static_cast<uint32_t>(nodes.size()), bulbModel, bulbMaterial, false,
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            nodes.push_back(node);
            streetLightNodes_.push_back(std::move(node));
        }
    }
}

void CitySolSimGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    time_.Reset(GOption->AgentValidation);
    citizens_.OnSceneLoaded(scene, time_.GameMinutes());
    ui_.State() = {};
    zoom_ = GOption->AgentValidation ? 0.76f : 1.0f;
    panOffset_ = GOption->AgentValidation ? glm::vec2(0.0f, 28.0f) : glm::vec2(0.0f);
    lightsVisible_ = true;
    UpdateNightLights(scene);
    cameraTarget_ = DesiredCameraTarget();
    cameraEye_ = DesiredCameraEye();
    cameraInitialized_ = true;
    sceneReady_ = true;
    SPDLOG_INFO("CitySolSim: city online ({} scene nodes, {} vehicles, {} citizens)",
                scene.Nodes().size(), traffic_.Vehicles().size(), citizens_.Citizens().size());
}

void CitySolSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    cameraInitialized_ = false;
    citizens_.Clear();
    // BeforeSceneRebuild has already injected the next scene's vehicle/light nodes when this
    // callback is issued during scene replacement. Their runtime handles must survive until
    // OnSceneLoaded; the next injection clears and replaces them deterministically.
}

void CitySolSimGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        return;
    }
    Assets::Scene& scene = GetEngine().GetScene();
    time_.Tick(deltaSeconds, scene);
    UpdateNightLights(scene);
    if (!time_.PausedRef())
    {
        const float simulationSpeed = time_.TimeScaleRef() / CitySolSim::Config::kDefaultTimeScale;
        traffic_.Tick(static_cast<float>(deltaSeconds), simulationSpeed, scene);
        citizens_.Tick(static_cast<float>(deltaSeconds), simulationSpeed, time_.GameMinutes(),
                       time_.DayMinutes(), time_.DayIndex(), scene);
    }
    UpdateCamera(deltaSeconds);
}

void CitySolSimGameInstance::UpdateNightLights(Assets::Scene& scene)
{
    const bool visible = time_.LightsOn();
    if (visible == lightsVisible_)
    {
        return;
    }
    lightsVisible_ = visible;
    for (const auto& node : streetLightNodes_)
    {
        Assets::NodeUtils::SetVisible(node, visible);
    }
    scene.MarkDirty();
}

glm::vec3 CitySolSimGameInstance::DesiredCameraTarget() const
{
    if (const auto* vehicle = traffic_.FindById(ui_.State().followVehicleId))
    {
        return vehicle->position;
    }
    if (const auto* citizen = citizens_.FindById(ui_.State().followCitizenId))
    {
        return citizens_.PositionOf(*citizen) + glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return kOverviewTarget + glm::vec3(panOffset_.x, 0.0f, panOffset_.y);
}

glm::vec3 CitySolSimGameInstance::DesiredCameraEye() const
{
    const bool following = ui_.State().followVehicleId >= 0 || ui_.State().followCitizenId >= 0;
    return DesiredCameraTarget() + (following ? kFollowOffset : kOverviewOffset * zoom_);
}

void CitySolSimGameInstance::UpdateCamera(double deltaSeconds)
{
    const glm::vec3 target = DesiredCameraTarget();
    const glm::vec3 eye = DesiredCameraEye();
    if (!cameraInitialized_)
    {
        cameraTarget_ = target;
        cameraEye_ = eye;
        cameraInitialized_ = true;
        return;
    }
    const float factor = 1.0f - std::exp(-kCameraSharpness * std::max(0.0f, static_cast<float>(deltaSeconds)));
    cameraTarget_ = glm::mix(cameraTarget_, target, factor);
    cameraEye_ = glm::mix(cameraEye_, eye, factor);
}

glm::mat4 CitySolSimGameInstance::ViewMatrix() const
{
    return glm::lookAt(cameraEye_, cameraTarget_, glm::vec3(0.0f, 1.0f, 0.0f));
}

bool CitySolSimGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = ViewMatrix();
    outRenderCamera.FieldOfView = kCityFov;
    outRenderCamera.NearPlane = 0.1f;
    outRenderCamera.FarPlane = 1800.0f;
    return true;
}

bool CitySolSimGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return true;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float aspect = viewport->Size.y > 1.0f ? viewport->Size.x / viewport->Size.y : 16.0f / 9.0f;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kCityFov), aspect, 0.1f, 1800.0f) * ViewMatrix();
    ui_.Draw(viewProjection, cameraEye_, time_, traffic_, citizens_);
    return true;
}

bool CitySolSimGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return false;
    }
    switch (event.key.key)
    {
    case SDLK_SPACE:
        time_.PausedRef() = !time_.PausedRef();
        return true;
    case SDLK_1:
        time_.TimeScaleRef() = CitySolSim::Config::kDefaultTimeScale;
        return true;
    case SDLK_2:
        time_.TimeScaleRef() = CitySolSim::Config::kDefaultTimeScale * 4.0f;
        return true;
    case SDLK_3:
        time_.TimeScaleRef() = CitySolSim::Config::kDefaultTimeScale * 16.0f;
        return true;
    case SDLK_C:
    {
        const auto& vehicles = traffic_.Vehicles();
        if (!vehicles.empty())
        {
            int next = ui_.State().followVehicleId + 1;
            if (next <= 0 || next > static_cast<int>(vehicles.size())) next = 1;
            ui_.State().followVehicleId = next;
            ui_.State().followCitizenId = -1;
        }
        return true;
    }
    case SDLK_V:
    {
        const auto& people = citizens_.Citizens();
        if (!people.empty())
        {
            int next = ui_.State().followCitizenId + 1;
            if (next <= 0 || next > static_cast<int>(people.size())) next = 1;
            ui_.State().followCitizenId = next;
            ui_.State().followVehicleId = -1;
        }
        return true;
    }
    case SDLK_R:
    case SDLK_ESCAPE:
        ui_.State().followVehicleId = -1;
        ui_.State().followCitizenId = -1;
        panOffset_ = glm::vec2(0.0f);
        zoom_ = 1.0f;
        return true;
    case SDLK_LEFT:
        panOffset_.x = std::max(-280.0f, panOffset_.x - 22.0f);
        return true;
    case SDLK_RIGHT:
        panOffset_.x = std::min(280.0f, panOffset_.x + 22.0f);
        return true;
    case SDLK_UP:
        panOffset_.y = std::max(-165.0f, panOffset_.y - 18.0f);
        return true;
    case SDLK_DOWN:
        panOffset_.y = std::min(150.0f, panOffset_.y + 18.0f);
        return true;
    default:
        return false;
    }
}

bool CitySolSimGameInstance::SupportsAppDebugShortcut(SDL_Keycode key) const
{
    return key == SDLK_F5;
}

bool CitySolSimGameInstance::IsAppDebugShortcutActive(SDL_Keycode key) const
{
    return key == SDLK_F5 && ui_.State().showPoiMarkers;
}

bool CitySolSimGameInstance::SetAppDebugShortcutActive(SDL_Keycode key, bool active)
{
    if (key != SDLK_F5)
    {
        return false;
    }
    ui_.State().showPoiMarkers = active;
    return true;
}

bool CitySolSimGameInstance::PickAtScreen(const glm::vec2& screenPosition)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr || viewport->Size.y <= 1.0f)
    {
        return false;
    }
    const float aspect = viewport->Size.x / viewport->Size.y;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kCityFov), aspect, 0.1f, 1800.0f) * ViewMatrix();
    float bestDistance = 18.0f;
    int bestVehicle = -1;
    int bestCitizen = -1;
    for (const auto& vehicle : traffic_.Vehicles())
    {
        glm::vec2 projected;
        if (ProjectForPick(viewProjection, *viewport, vehicle.position + glm::vec3(0.0f, 0.8f, 0.0f), projected))
        {
            const float distance = glm::distance(projected, screenPosition);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestVehicle = vehicle.id;
                bestCitizen = -1;
            }
        }
    }
    for (const auto& citizen : citizens_.Citizens())
    {
        glm::vec2 projected;
        if (ProjectForPick(viewProjection, *viewport, citizens_.PositionOf(citizen) + glm::vec3(0.0f, 1.0f, 0.0f), projected))
        {
            const float distance = glm::distance(projected, screenPosition);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestVehicle = -1;
                bestCitizen = citizen.id;
            }
        }
    }
    ui_.State().followVehicleId = bestVehicle;
    ui_.State().followCitizenId = bestCitizen;
    return bestVehicle >= 0 || bestCitizen >= 0;
}

bool CitySolSimGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!sceneReady_ || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
    {
        return false;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    return PickAtScreen({mouse.x, mouse.y});
}

bool CitySolSimGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    zoom_ = std::clamp(zoom_ - static_cast<float>(yoffset) * 0.08f, 0.38f, 1.55f);
    return true;
}

void CitySolSimGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.samples", "2", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "1", &error);
}

void CitySolSimGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("city.hour", [this]() { return time_.DayMinutes() / 60.0; });
    reg.Add("city.dayIndex", [this]() { return static_cast<int64_t>(time_.DayIndex()); });
    reg.Add("city.isNight", [this]() { return time_.IsNight(); });
    reg.Add("city.vehicleCount", [this]() {
        return static_cast<int64_t>(traffic_.Vehicles().size());
    });
    reg.Add("city.citizenCount", [this]() {
        return static_cast<int64_t>(citizens_.Citizens().size());
    });
    reg.Add("city.movingCitizens", [this]() {
        return static_cast<int64_t>(citizens_.MovingCount());
    });
}

void CitySolSimGameInstance::OnDestroy()
{
}
