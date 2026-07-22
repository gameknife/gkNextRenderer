#include "Engine/Common/CoreMinimal.hpp"

#include "TruckerDemoGameInstance.hpp"

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Options.hpp"
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

void TruckerDemoGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    using NextCVar::ECVarFlags;
    cvars.RegisterFloat("trucker.susp.frontFrequency", 1.3f, &suspensionFrontFrequency_, ECVarFlags::Archive,
                        "Front suspension natural frequency", nullptr, 0.5, 4.0);
    cvars.RegisterFloat("trucker.susp.rearFrequency", 1.6f, &suspensionRearFrequency_, ECVarFlags::Archive,
                        "Rear suspension natural frequency", nullptr, 0.5, 4.0);
    cvars.RegisterFloat("trucker.susp.frontDamping", 0.35f, &suspensionFrontDamping_, ECVarFlags::Archive,
                        "Front suspension damping ratio", nullptr, 0.1, 1.5);
    cvars.RegisterFloat("trucker.susp.rearDamping", 0.40f, &suspensionRearDamping_, ECVarFlags::Archive,
                        "Rear suspension damping ratio", nullptr, 0.1, 1.5);
    cvars.RegisterFloat("trucker.susp.preload", 0.04f, &suspensionPreload_, ECVarFlags::Archive,
                        "Suspension preload length", nullptr, 0.0, 0.25);
    cvars.RegisterFloat("trucker.susp.centerOfMassDrop", 0.35f, &centerOfMassDrop_, ECVarFlags::Archive,
                        "Chassis center of mass downward offset", nullptr, 0.0, 1.0);
    cvars.RegisterFloat("trucker.susp.frontAntiRoll", 10000.0f, &frontAntiRoll_, ECVarFlags::Archive,
                        "Front anti-roll stiffness", nullptr, 0.0, 50000.0);
    cvars.RegisterFloat("trucker.susp.rearAntiRoll", 14000.0f, &rearAntiRoll_, ECVarFlags::Archive,
                        "Rear anti-roll stiffness", nullptr, 0.0, 50000.0);
    cvars.RegisterFloat("trucker.drive.maxTorque", 1150.0f, &engineTorque_, ECVarFlags::Archive,
                        "Diesel engine peak torque", nullptr, 100.0, 5000.0);
    cvars.RegisterFloat("trucker.fuel.capacity", 120.0f, &fuelCapacity_, ECVarFlags::Archive,
                        "Fuel tank capacity", nullptr, 1.0, 1000.0);
    cvars.RegisterFloat("trucker.fuel.idleRate", 0.03f, &fuelIdleRate_, ECVarFlags::Archive,
                        "Idle fuel consumption per second", nullptr, 0.0, 2.0);
    cvars.RegisterFloat("trucker.fuel.throttleRate", 0.6f, &fuelThrottleRate_, ECVarFlags::Archive,
                        "Full throttle fuel consumption per second", nullptr, 0.0, 5.0);
    cvars.RegisterFloat("trucker.fuel.slipPenalty", 0.3f, &fuelSlipPenalty_, ECVarFlags::Archive,
                        "Wheel slip fuel penalty", nullptr, 0.0, 5.0);
    cvars.RegisterFloat("trucker.fuel.cargoRate", 0.08f, &fuelCargoRate_, ECVarFlags::Archive,
                        "Loaded cargo fuel penalty", nullptr, 0.0, 2.0);
    cvars.RegisterInt("trucker.validation.missionStage", 0, &agentMissionStage_, ECVarFlags::None,
                      "Agent-validation-only mission teleport stage", nullptr, 0, 2);
}

void TruckerDemoGameInstance::OnInit()
{
    Modules::Scad::Register();
    fuel_ = fuelCapacity_ * 0.75f;
    GetEngine().RequestLoadScene({.filename = "assets/scad/overhill_mission.scad"});
}

void TruckerDemoGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>&, std::vector<Assets::FMaterial>&, std::vector<Assets::LightObject>&,
    std::vector<Assets::AnimationTrack>&)
{
    for (const auto& node : nodes)
    {
        bool dynamicDecoration = false;
        for (Assets::Node* current = node.get(); current; current = current->GetParent())
        {
            const std::string& name = current->GetName();
            if (name.starts_with("player_") || name == "cargo_crate" || name.starts_with("mission_"))
            {
                dynamicDecoration = true;
                break;
            }
        }
        if (dynamicDecoration)
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
    std::vector<std::shared_ptr<Assets::Node>> wheels;
    surfaces_.clear();
    for (const auto& node : GetEngine().GetScene().Nodes())
    {
        const std::string& name = node->GetName();
        if (name == "player_truck_body") bodyNode_ = node;
        else if (name == "player_wheel") wheels.push_back(node);
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
    ResolveSceneAnchors();
    MapWheelVisuals(wheels);
    CreateVehicle();
}

void TruckerDemoGameInstance::ResolveSceneAnchors()
{
    for (const auto& node : GetEngine().GetScene().Nodes())
    {
        if (node->GetName() == "mission_pickup_marker") pickupPosition_ = node->WorldTranslation();
        else if (node->GetName() == "mission_dropoff_marker") dropoffPosition_ = node->WorldTranslation();
        else if (node->GetName() == "mission_fuel_marker") fuelPosition_ = node->WorldTranslation();
    }
}

void TruckerDemoGameInstance::MapWheelVisuals(const std::vector<std::shared_ptr<Assets::Node>>& wheels)
{
    std::vector<bool> used(wheels.size(), false);
    constexpr float axleX[3] = {2.5f, -0.6f, -1.75f};
    for (int axle = 0; axle < 3; ++axle)
    {
        for (int side = 0; side < 2; ++side)
        {
            const int wheelIndex = axle * 2 + side;
            const glm::vec3 expected = position_ + glm::vec3(axleX[axle], 0.5f, side ? -1.0f : 1.0f);
            float bestDistance = std::numeric_limits<float>::max();
            size_t best = wheels.size();
            for (size_t candidate = 0; candidate < wheels.size(); ++candidate)
            {
                if (used[candidate]) continue;
                const float distance = glm::length(wheels[candidate]->WorldTranslation() - expected);
                if (distance < bestDistance) { bestDistance = distance; best = candidate; }
            }
            if (best < wheels.size()) { wheelNodes_[wheelIndex] = wheels[best]; used[best] = true; }
        }
    }
}

void TruckerDemoGameInstance::CreateVehicle()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics || vehicle_ != invalidNextVehicleId) return;
    FNextVehicleSettings settings;
    settings.initialPosition = position_;
    settings.mass = 3500.0f;
    settings.centerOfMassOffset = {0.0f, -centerOfMassDrop_, 0.0f};
    settings.frontAntiRollStiffness = frontAntiRoll_;
    settings.rearAntiRollStiffness = rearAntiRoll_;
    settings.engine.maxTorque = engineTorque_;
    settings.engine.minRPM = 700.0f;
    settings.engine.maxRPM = 3000.0f;
    settings.engine.inertia = 1.2f;
    settings.engine.normalizedTorque = {{0.0f, 0.72f}, {0.18f, 0.90f}, {0.40f, 1.0f},
                                        {0.68f, 0.86f}, {1.0f, 0.32f}};
    settings.transmission.gearRatios = {3.80f, 2.35f, 1.55f, 1.05f, 0.74f};
    settings.transmission.reverseRatio = -3.50f;
    settings.transmission.shiftUpRPM = 2600.0f;
    settings.transmission.shiftDownRPM = 1300.0f;
    settings.transmission.clutchStrength = 18.0f;
    constexpr float axleX[3] = {2.5f, -0.6f, -1.75f};
    for (int axle = 0; axle < 3; ++axle)
    {
        for (int side = 0; side < 2; ++side)
        {
            FNextWheelSettings wheel;
            wheel.position = {axleX[axle], -0.2f, side ? -1.0f : 1.0f};
            wheel.radius = 0.5f;
            wheel.width = axle == 0 ? 0.36f : 0.5f;
            wheel.suspensionMin = 0.15f;
            wheel.suspensionMax = 0.55f;
            wheel.steered = axle == 0;
            wheel.driven = axle != 0;
            wheel.suspensionFrequency = axle == 0 ? suspensionFrontFrequency_ : suspensionRearFrequency_;
            wheel.suspensionDamping = axle == 0 ? suspensionFrontDamping_ : suspensionRearDamping_;
            wheel.suspensionPreload = suspensionPreload_;
            settings.wheels.push_back(wheel);
        }
    }
    vehicle_ = physics->CreateWheeledVehicle(settings);
    if (bodyNode_ && vehicle_ != invalidNextVehicleId)
    {
        if (auto* component = bodyNode_->GetComponentPtr<Runtime::PhysicsComponent>())
            component->BindPhysicsBody(physics->GetVehicleBodyID(vehicle_));
    }
}

void TruckerDemoGameInstance::OnTick(double deltaSeconds)
{
    if (vehicle_ == invalidNextVehicleId) return;
    const float deltaTime = static_cast<float>(std::min(deltaSeconds, 0.1));
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    ApplyAgentValidationStage();
    physics->GetVehicleTelemetry(vehicle_, telemetry_);
    UpdateDriveInput(deltaTime);
    physics->SetVehicleInput(vehicle_, {throttle_, steer_, brake_, handbrake_ ? 1.0f : 0.0f});
    physics->GetVehicleBodyTransform(vehicle_, position_, rotation_);
    speed_ = std::abs(telemetry_.forwardSpeed);
    if (auto* body = physics->GetBody(physics->GetVehicleBodyID(vehicle_)))
        speed_ = glm::length(glm::vec2(body->velocity.x, body->velocity.z));
    if (mission_ != EMission::Available && mission_ != EMission::Complete) elapsed_ += deltaTime;
    UpdateSurface();
    UpdateFuel(deltaTime);
    UpdateMission(deltaTime);
    UpdateRecovery(deltaTime);
    UpdateVisuals();
    interactRequested_ = false;
}

void TruckerDemoGameInstance::ApplyAgentValidationStage()
{
    if (!GOption || !GOption->AgentValidation || agentMissionStage_ == appliedAgentMissionStage_) return;
    appliedAgentMissionStage_ = agentMissionStage_;
    const glm::vec3 target = agentMissionStage_ == 1 ? pickupPosition_ : dropoffPosition_;
    position_ = {target.x, target.y + 1.5f, target.z};
    rotation_ = glm::quat(1, 0, 0, 0);
    GetEngine().GetPhysicsEngine()->SetVehicleBodyTransform(vehicle_, position_, rotation_);
}

void TruckerDemoGameInstance::UpdateDriveInput(float deltaSeconds)
{
    float targetThrottle = 0.0f;
    float targetBrake = 0.0f;
    if (forward_)
    {
        if (telemetry_.forwardSpeed < -0.5f) targetBrake = 1.0f;
        else targetThrottle = 1.0f;
    }
    if (reverse_)
    {
        if (telemetry_.forwardSpeed > 0.5f) targetBrake = 1.0f;
        else targetThrottle = -1.0f;
    }
    if (fuel_ <= 0.0f) targetThrottle = 0.0f;
    throttle_ = glm::mix(throttle_, targetThrottle, std::min(1.0f, deltaSeconds * 3.5f));
    brake_ = glm::mix(brake_, targetBrake, std::min(1.0f, deltaSeconds * 8.0f));
    steer_ = glm::mix(steer_, (right_ ? 1.0f : 0.0f) - (left_ ? 1.0f : 0.0f),
                      std::min(1.0f, deltaSeconds * 5.0f));
}

void TruckerDemoGameInstance::UpdateSurface()
{
    static constexpr float longitudinal[] = {1.0f, 0.9f, 0.8f, 0.65f, 0.45f, 0.55f};
    static constexpr float lateral[] = {1.0f, 0.9f, 0.75f, 0.6f, 0.4f, 0.5f};
    std::array<int, 6> counts{};
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    for (int index = 0; index < 6; ++index)
    {
        const NextBodyID contact = physics->GetVehicleWheelContactBody(vehicle_, index);
        const ESurface value = surfaces_.contains(contact) ? surfaces_[contact] : ESurface::Grass;
        ++counts[static_cast<int>(value)];
        physics->SetVehicleWheelFrictionScale(vehicle_, index, longitudinal[static_cast<int>(value)],
                                              lateral[static_cast<int>(value)]);
    }
    surface_ = static_cast<ESurface>(static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin()));
    static constexpr float drag[] = {0, 0, 450, 900, 1800, 2600};
    if (auto* body = physics->GetBody(physics->GetVehicleBodyID(vehicle_)))
        physics->AddForceToBody(body->bodyID, -glm::vec3(body->velocity.x, 0, body->velocity.z) * drag[static_cast<int>(surface_)]);
}

void TruckerDemoGameInstance::UpdateFuel(float deltaSeconds)
{
    float averageSlip = 0.0f;
    for (float slip : telemetry_.wheelSlip) averageSlip += std::abs(slip);
    if (!telemetry_.wheelSlip.empty()) averageSlip /= static_cast<float>(telemetry_.wheelSlip.size());
    const bool loaded = mission_ == EMission::Loaded || mission_ == EMission::AtDropoff || mission_ == EMission::Complete;
    const float rpmLoad = telemetry_.rpm / 3000.0f;
    const float consumption = fuelIdleRate_ + fuelThrottleRate_ * std::abs(throttle_) * glm::clamp(rpmLoad, 0.2f, 1.0f) +
                              fuelSlipPenalty_ * glm::clamp(averageSlip, 0.0f, 3.0f) + (loaded ? fuelCargoRate_ : 0.0f);
    fuel_ = std::max(0.0f, fuel_ - consumption * deltaSeconds);
    const float fuelDistance = glm::length(glm::vec2(position_.x - fuelPosition_.x, position_.z - fuelPosition_.z));
    if (fuelDistance < 5.0f && speed_ < 0.75f)
        fuel_ = std::min(fuelCapacity_, fuel_ + 30.0f * deltaSeconds);
}

void TruckerDemoGameInstance::UpdateMission(float)
{
    if (mission_ == EMission::Available && interactRequested_)
    {
        mission_ = EMission::Accepted;
        elapsed_ = 0.0f;
        showMissionPanel_ = false;
    }
    if (mission_ == EMission::Accepted) mission_ = EMission::Driving;

    const glm::vec2 truck(position_.x, position_.z);
    const bool pickup = glm::distance(truck, glm::vec2(pickupPosition_.x, pickupPosition_.z)) < 5.0f;
    const bool dropoff = glm::distance(truck, glm::vec2(dropoffPosition_.x, dropoffPosition_.z)) < 5.0f;
    if (mission_ == EMission::Driving && pickup && speed_ < 0.75f) mission_ = EMission::AtPickup;
    else if (mission_ == EMission::AtPickup && !pickup) mission_ = EMission::Driving;
    if (mission_ == EMission::Loaded && dropoff && speed_ < 0.75f) mission_ = EMission::AtDropoff;
    else if (mission_ == EMission::AtDropoff && !dropoff) mission_ = EMission::Loaded;
    if (interactRequested_ && speed_ < 0.75f)
    {
        if (mission_ == EMission::AtPickup) mission_ = EMission::Loaded;
        else if (mission_ == EMission::AtDropoff) { mission_ = EMission::Complete; showMissionPanel_ = true; }
    }
}

void TruckerDemoGameInstance::UpdateRecovery(float deltaSeconds)
{
    const glm::vec3 up = rotation_ * glm::vec3(0, 1, 0);
    flippedSeconds_ = glm::dot(up, glm::vec3(0, 1, 0)) < std::cos(glm::radians(80.0f))
        ? flippedSeconds_ + deltaSeconds : 0.0f;
    stuckSeconds_ = std::abs(throttle_) > 0.65f && speed_ < 0.25f ? stuckSeconds_ + deltaSeconds : 0.0f;
    recoverySuggested_ = flippedSeconds_ > 2.0f || stuckSeconds_ > 5.0f;

    const auto rememberPad = [this](const glm::vec3& pad)
    {
        if (glm::length(glm::vec2(position_.x - pad.x, position_.z - pad.z)) < 5.0f && speed_ < 0.5f)
        {
            recoveryPosition_ = {pad.x, 1.5f, pad.z};
            recoveryRotation_ = rotation_;
        }
    };
    rememberPad(pickupPosition_); rememberPad(dropoffPosition_); rememberPad(fuelPosition_);
}

void TruckerDemoGameInstance::UpdateVisuals()
{
    if (bodyNode_) { bodyNode_->SetTranslation(position_); bodyNode_->SetRotation(rotation_); }
    for (int index = 0; index < 6; ++index)
    {
        if (!wheelNodes_[index]) continue;
        glm::vec3 localPosition; glm::quat localRotation;
        if (GetEngine().GetPhysicsEngine()->GetVehicleWheelLocalTransform(vehicle_, index, localPosition, localRotation))
        {
            wheelNodes_[index]->SetTranslation(position_ + rotation_ * localPosition);
            wheelNodes_[index]->SetRotation(rotation_ * localRotation);
        }
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
    position_ = recoveryPosition_;
    rotation_ = recoveryRotation_;
    GetEngine().GetPhysicsEngine()->SetVehicleBodyTransform(vehicle_, position_, rotation_);
    flippedSeconds_ = stuckSeconds_ = 0.0f;
    recoverySuggested_ = false;
}

bool TruckerDemoGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) return false;
    const bool down = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_W: forward_ = down; break;
    case SDLK_S: reverse_ = down; break;
    case SDLK_A: left_ = down; break;
    case SDLK_D: right_ = down; break;
    case SDLK_SPACE: handbrake_ = down; break;
    case SDLK_F: if (down) interactRequested_ = true; break;
    case SDLK_R: if (down) ResetVehicle(); break;
    case SDLK_Q:
        if (down) { diffLock_ = !diffLock_; GetEngine().GetPhysicsEngine()->SetVehicleDiffLock(vehicle_, diffLock_); }
        break;
    case SDLK_E:
        if (down) { allWheelDrive_ = !allWheelDrive_; GetEngine().GetPhysicsEngine()->SetVehicleAllWheelDrive(vehicle_, allWheelDrive_); }
        break;
    case SDLK_M: if (down) showMissionPanel_ = !showMissionPanel_; break;
    case SDLK_C:
        if (down) { cameraYawOffset_ = 0.0f; cameraPitch_ = glm::radians(18.0f); cameraArmLength_ = 9.0f; }
        break;
    default: return false;
    }
    return true;
}

bool TruckerDemoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    const glm::dvec2 current(xpos, ypos);
    if (!hasMousePosition_) { lastMousePosition_ = current; hasMousePosition_ = true; return true; }
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
    if (glm::length(vehicleForward) < 0.1f) vehicleForward = {1, 0, 0};
    vehicleForward = glm::normalize(vehicleForward);
    const glm::quat orbitYaw = glm::angleAxis(cameraYawOffset_, glm::vec3(0, 1, 0));
    const glm::vec3 orbitForward = orbitYaw * vehicleForward;
    const glm::vec3 target = position_ + glm::vec3(0, 1.2f, 0) + vehicleForward * speed_ * 0.15f;
    const float horizontalArm = std::cos(cameraPitch_) * cameraArmLength_;
    const glm::vec3 cameraPosition = target - orbitForward * horizontalArm +
                                     glm::vec3(0, std::sin(cameraPitch_) * cameraArmLength_, 0);
    camera.ModelView = glm::lookAt(cameraPosition, target, glm::vec3(0, 1, 0));
    camera.FieldOfView = 62.0f;
    return true;
}

glm::vec3 TruckerDemoGameInstance::MissionTarget() const
{
    return mission_ == EMission::Loaded || mission_ == EMission::AtDropoff || mission_ == EMission::Complete
        ? dropoffPosition_ : pickupPosition_;
}

const char* TruckerDemoGameInstance::MissionName() const
{
    static constexpr const char* names[] = {"available", "accepted", "drive-to-pickup", "at-pickup",
                                             "loaded", "at-dropoff", "complete"};
    return names[static_cast<int>(mission_)];
}

const char* TruckerDemoGameInstance::SurfaceName() const
{
    static constexpr const char* names[] = {"trail", "bridge", "grass", "sand", "mud", "water"};
    return names[static_cast<int>(surface_)];
}

bool TruckerDemoGameInstance::OnRenderUI()
{
    const glm::vec3 forward = rotation_ * glm::vec3(1, 0, 0);
    const glm::vec3 target = MissionTarget();
    TruckerDemo::FTruckHudState state;
    state.speedKmh = speed_ * 3.6f;
    state.rpm = telemetry_.rpm;
    if (telemetry_.maxRPM > 0.0f) state.maxRPM = telemetry_.maxRPM;
    state.throttle = std::abs(throttle_);
    state.brake = brake_;
    state.fuel = fuel_;
    state.fuelCapacity = fuelCapacity_;
    state.slopeDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
    state.targetDistance = glm::length(glm::vec2(position_.x - target.x, position_.z - target.z));
    state.elapsed = elapsed_;
    state.gear = telemetry_.gear;
    state.handbrake = handbrake_;
    state.diffLock = diffLock_;
    state.allWheelDrive = allWheelDrive_;
    state.recoverySuggested = recoverySuggested_;
    state.missionPanel = showMissionPanel_;
    state.missionAvailable = mission_ == EMission::Available;
    state.missionComplete = mission_ == EMission::Complete;
    state.mission = MissionName();
    state.surface = SurfaceName();
    state.targetPosition = target;
    const TruckerDemo::FTruckHudActions actions = hud_.Draw(*this, state);
    if (actions.acceptMission && mission_ == EMission::Available) interactRequested_ = true;
    if (actions.restartMission)
    {
        mission_ = EMission::Available;
        elapsed_ = 0.0f;
        showMissionPanel_ = true;
    }
    return true;
}

void TruckerDemoGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("missionState", [this] { return std::string(MissionName()); });
    reg.Add("speed", [this] { return static_cast<double>(speed_); });
    reg.Add("surface", [this] { return std::string(SurfaceName()); });
    reg.Add("elapsed", [this] { return static_cast<double>(elapsed_); });
    reg.Add("fuel", [this] { return static_cast<double>(fuel_); });
    reg.Add("gear", [this] { return static_cast<int64_t>(telemetry_.gear); });
    reg.Add("rpm", [this] { return static_cast<double>(telemetry_.rpm); });
    reg.Add("diffLock", [this] { return diffLock_; });
    reg.Add("awd", [this] { return allWheelDrive_; });
}

void TruckerDemoGameInstance::OnDestroy()
{
    if (vehicle_ != invalidNextVehicleId && GetEngine().GetPhysicsEngine())
        GetEngine().GetPhysicsEngine()->RemoveVehicle(vehicle_);
    vehicle_ = invalidNextVehicleId;
}
