#include "TruckerDemoGameInstance.hpp"

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
    Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<TruckerDemoGameInstance>(config, options, engine);
}

TruckerDemoGameInstance::TruckerDemoGameInstance(Vulkan::WindowConfig& config,
    Runtime::Config::Options& options, NextEngine* engine) : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "Trucker Demo", 1280, 720, false);
}

void TruckerDemoGameInstance::OnInit()
{
    Modules::Scad::Register();
    GetEngine().RequestLoadScene({.filename = "assets/scad/overhill_mission.scad"});
}

void TruckerDemoGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>&, std::vector<Assets::FMaterial>&, std::vector<Assets::LightObject>&,
    std::vector<Assets::AnimationTrack>&)
{
    for (const auto& node : nodes)
    {
        bool playerHierarchy = false;
        for (Assets::Node* current = node.get(); current; current = current->GetParent())
        {
            const std::string& name = current->GetName();
            if (name.starts_with("player_") || name == "cargo_crate")
            {
                playerHierarchy = true;
                break;
            }
        }
        if (playerHierarchy)
        {
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            physics->SetSimulatePhysics(false);
            node->AddComponent(physics);
        }
    }
}

void TruckerDemoGameInstance::OnSceneLoaded()
{
    int wheel = 0;
    for (const auto& node : GetEngine().GetScene().Nodes())
    {
        const std::string& name = node->GetName();
        if (name == "player_truck_body") bodyNode_ = node;
        else if (name == "player_wheel" && wheel < 6) wheelNodes_[wheel++] = node;
        else if (name == "cargo_crate") cargoNode_ = node;
        auto* physics = node->GetComponentPtr<Runtime::PhysicsComponent>();
        if (!physics || physics->GetPhysicsBody().IsInvalid()) continue;
        ESurface type = ESurface::Grass;
        if (name.starts_with("oh_ground_mud")) type = ESurface::Mud;
        else if (name.starts_with("oh_ground_sand")) type = ESurface::Sand;
        else if (name.starts_with("oh_ground_river")) type = ESurface::Water;
        else if (name.starts_with("oh_prop_bridge")) type = ESurface::Bridge;
        else if (name.starts_with("oh_ground_trail")) type = ESurface::Trail;
        surfaces_[physics->GetPhysicsBody()] = type;
    }
    CreateVehicle();
}

void TruckerDemoGameInstance::CreateVehicle()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine(); if (!physics || vehicle_) return;
    FNextVehicleSettings settings;
    settings.initialPosition = position_; settings.mass = 3500.0f; settings.maxEngineTorque = 1150.0f;
    const float xs[3] = {2.5f, -0.6f, -1.75f};
    for (int axle = 0; axle < 3; ++axle) for (int side = 0; side < 2; ++side)
        settings.wheels.push_back({{xs[axle], -0.2f, side ? -1.0f : 1.0f}, 0.5f,
            axle == 0 ? 0.36f : 0.5f, 0.12f, 0.48f, axle == 0, axle != 0});
    vehicle_ = physics->CreateWheeledVehicle(settings);
    if (bodyNode_ && vehicle_ != invalidNextVehicleId)
    {
        if (auto* component = bodyNode_->GetComponentPtr<Runtime::PhysicsComponent>())
        {
            component->BindPhysicsBody(physics->GetVehicleBodyID(vehicle_));
        }
    }
}

void TruckerDemoGameInstance::OnTick(double deltaSeconds)
{
    if (!vehicle_) return;
    const float dt = static_cast<float>(std::min(deltaSeconds, 0.1));
    throttle_ = glm::mix(throttle_, (forward_ ? 1.0f : 0.0f) - (reverse_ ? 1.0f : 0.0f), std::min(1.0f, dt * 3.5f));
    steer_ = glm::mix(steer_, (right_ ? 1.0f : 0.0f) - (left_ ? 1.0f : 0.0f), std::min(1.0f, dt * 5.0f));
    GetEngine().GetPhysicsEngine()->SetVehicleInput(vehicle_, {throttle_, steer_, 0.0f, handbrake_ ? 1.0f : 0.0f});
    GetEngine().GetPhysicsEngine()->GetVehicleBodyTransform(vehicle_, position_, rotation_);
    if (auto* body = GetEngine().GetPhysicsEngine()->GetBody(GetEngine().GetPhysicsEngine()->GetVehicleBodyID(vehicle_)))
        speed_ = glm::length(glm::vec2(body->velocity.x, body->velocity.z));
    elapsed_ += dt;
    UpdateSurface(); UpdateVisuals();
    const glm::vec2 p(position_.x, position_.z);
    const bool pickup = glm::distance(p, glm::vec2(27.0f, -11.0f)) < 5.0f;
    const bool dropoff = glm::distance(p, glm::vec2(-38.0f, -7.0f)) < 5.0f;
    if (mission_ == EMission::Driving && pickup) mission_ = EMission::AtPickup;
    if (mission_ == EMission::AtPickup && !pickup) mission_ = EMission::Driving;
    if (mission_ == EMission::Loaded && dropoff) mission_ = EMission::AtDropoff;
    if (mission_ == EMission::AtDropoff && !dropoff) mission_ = EMission::Loaded;
    if (interactRequested_ && speed_ < 0.75f)
    {
        if (mission_ == EMission::AtPickup) mission_ = EMission::Loaded;
        else if (mission_ == EMission::AtDropoff) mission_ = EMission::Complete;
    }
    interactRequested_ = false;
}

void TruckerDemoGameInstance::UpdateSurface()
{
    static constexpr float longitudinal[] = {1.0f, 0.9f, 0.8f, 0.65f, 0.45f, 0.55f};
    static constexpr float lateral[] = {1.0f, 0.9f, 0.75f, 0.6f, 0.4f, 0.5f};
    std::array<int, 6> counts{};
    for (int i = 0; i < 6; ++i)
    {
        NextBodyID contact = GetEngine().GetPhysicsEngine()->GetVehicleWheelContactBody(vehicle_, i);
        ESurface value = surfaces_.contains(contact) ? surfaces_[contact] : ESurface::Grass;
        ++counts[static_cast<int>(value)];
        GetEngine().GetPhysicsEngine()->SetVehicleWheelFrictionScale(vehicle_, i,
            longitudinal[static_cast<int>(value)], lateral[static_cast<int>(value)]);
    }
    surface_ = static_cast<ESurface>(static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin()));
    const float drag[] = {0, 0, 450, 900, 1800, 2600};
    if (auto* body = GetEngine().GetPhysicsEngine()->GetBody(GetEngine().GetPhysicsEngine()->GetVehicleBodyID(vehicle_)))
        GetEngine().GetPhysicsEngine()->AddForceToBody(body->bodyID, -glm::vec3(body->velocity.x, 0, body->velocity.z) * drag[static_cast<int>(surface_)]);
}

void TruckerDemoGameInstance::UpdateVisuals()
{
    if (bodyNode_) { bodyNode_->SetTranslation(position_); bodyNode_->SetRotation(rotation_); }
    for (int i = 0; i < 6; ++i) if (wheelNodes_[i])
    {
        glm::vec3 p; glm::quat q;
        if (GetEngine().GetPhysicsEngine()->GetVehicleWheelLocalTransform(vehicle_, i, p, q))
        { wheelNodes_[i]->SetTranslation(position_ + rotation_ * p); wheelNodes_[i]->SetRotation(rotation_ * q); }
    }
    if (cargoNode_)
    {
        const bool loaded = mission_ == EMission::Loaded || mission_ == EMission::AtDropoff || mission_ == EMission::Complete;
        cargoNode_->SetTranslation(loaded ? position_ + rotation_ * glm::vec3(-1.2f, 1.25f, 0.0f) : glm::vec3(0, -5, 0));
        cargoNode_->SetRotation(rotation_);
    }
    GetEngine().GetScene().MarkTransformDirty();
}

void TruckerDemoGameInstance::ResetVehicle()
{
    position_ = mission_ == EMission::Loaded || mission_ == EMission::AtDropoff ? glm::vec3(24, 1.5f, -8) : glm::vec3(-30, 1.5f, 0);
    rotation_ = glm::quat(1, 0, 0, 0); GetEngine().GetPhysicsEngine()->SetVehicleBodyTransform(vehicle_, position_, rotation_);
    if (mission_ == EMission::Complete) { mission_ = EMission::Driving; elapsed_ = 0; }
}

bool TruckerDemoGameInstance::OnKey(SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) return false;
    const bool down = e.type == SDL_EVENT_KEY_DOWN;
    switch (e.key.key) { case SDLK_W: forward_ = down; break; case SDLK_S: reverse_ = down; break;
    case SDLK_A: left_ = down; break; case SDLK_D: right_ = down; break; case SDLK_SPACE: handbrake_ = down; break;
    case SDLK_F: if (down) interactRequested_ = true; break; case SDLK_R: if (down) ResetVehicle(); break;
    case SDLK_C:
        if (down)
        {
            cameraYawOffset_ = 0.0f;
            cameraPitch_ = glm::radians(18.0f);
            cameraArmLength_ = 9.0f;
        }
        break;
    default: return false; }
    return true;
}

bool TruckerDemoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    const glm::dvec2 current(xpos, ypos);
    if (!hasMousePosition_)
    {
        lastMousePosition_ = current;
        hasMousePosition_ = true;
        return true;
    }

    const glm::dvec2 delta = current - lastMousePosition_;
    lastMousePosition_ = current;
    constexpr float sensitivity = 0.005f;
    cameraYawOffset_ -= static_cast<float>(delta.x) * sensitivity;
    cameraPitch_ = glm::clamp(cameraPitch_ - static_cast<float>(delta.y) * sensitivity,
                              glm::radians(-10.0f), glm::radians(75.0f));
    cameraYawOffset_ = std::remainder(cameraYawOffset_, glm::two_pi<float>());
    return true;
}

bool TruckerDemoGameInstance::OnScroll(double, double yoffset)
{
    cameraArmLength_ = glm::clamp(cameraArmLength_ - static_cast<float>(yoffset) * 0.8f, 4.5f, 18.0f);
    return true;
}

bool TruckerDemoGameInstance::OverrideRenderCamera(Assets::Camera& camera) const
{
    glm::vec3 vehicleForward = rotation_ * glm::vec3(1, 0, 0); vehicleForward.y = 0;
    if (glm::length(vehicleForward) < 0.1f) vehicleForward = {1,0,0};
    vehicleForward = glm::normalize(vehicleForward);
    const glm::quat orbitYaw = glm::angleAxis(cameraYawOffset_, glm::vec3(0, 1, 0));
    const glm::vec3 orbitForward = orbitYaw * vehicleForward;
    const glm::vec3 target = position_ + glm::vec3(0, 1.2f, 0) + vehicleForward * speed_ * 0.15f;
    const float horizontalArm = std::cos(cameraPitch_) * cameraArmLength_;
    const glm::vec3 cameraPosition = target - orbitForward * horizontalArm +
                                     glm::vec3(0, std::sin(cameraPitch_) * cameraArmLength_, 0);
    camera.ModelView = glm::lookAt(cameraPosition, target, glm::vec3(0,1,0));
    camera.FieldOfView = 62.0f; return true;
}

const char* TruckerDemoGameInstance::MissionName() const
{ static const char* names[] = {"drive-to-pickup", "at-pickup", "loaded", "at-dropoff", "complete"}; return names[static_cast<int>(mission_)]; }
const char* TruckerDemoGameInstance::SurfaceName() const
{ static const char* names[] = {"trail", "bridge", "grass", "sand", "mud", "water"}; return names[static_cast<int>(surface_)]; }

bool TruckerDemoGameInstance::OnRenderUI()
{
    ImGui::SetNextWindowPos({16, 16}); ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::Begin("Trucker MVP", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Speed  %4.1f km/h", speed_ * 3.6f); ImGui::Text("Surface  %s", SurfaceName());
    ImGui::Text("Mission  %s", MissionName()); ImGui::Text("Time  %.1fs", elapsed_);
    ImGui::Separator(); ImGui::Text("W/S throttle  A/D steer  Space brake"); ImGui::Text("F load/unload  R recover  C reset camera");
    ImGui::Text("Mouse orbit  Wheel zoom (%.1fm)", cameraArmLength_);
    if (mission_ == EMission::AtPickup) ImGui::TextColored({1, .8f, .2f, 1}, "STOP AND PRESS F TO LOAD");
    if (mission_ == EMission::AtDropoff) ImGui::TextColored({1, .8f, .2f, 1}, "STOP AND PRESS F TO DELIVER");
    if (mission_ == EMission::Complete) ImGui::TextColored({.2f, 1, .3f, 1}, "MISSION COMPLETE - %.1fs", elapsed_);
    ImGui::End(); return true;
}

void TruckerDemoGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("game.missionState", [this] { return std::string(MissionName()); });
    reg.Add("game.speed", [this] { return static_cast<double>(speed_); });
    reg.Add("game.surface", [this] { return std::string(SurfaceName()); });
    reg.Add("game.elapsed", [this] { return static_cast<double>(elapsed_); });
}

void TruckerDemoGameInstance::OnDestroy()
{ if (vehicle_ && GetEngine().GetPhysicsEngine()) GetEngine().GetPhysicsEngine()->RemoveVehicle(vehicle_); vehicle_ = invalidNextVehicleId; }
