#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "TruckHud.hpp"

namespace Assets { class Node; }

class TruckerDemoGameInstance final : public NextGameInstanceBase
{
public:
    TruckerDemoGameInstance(Vulkan::WindowConfig&, Runtime::Config::Options&, NextEngine*);
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool ShouldRenderUiDuringScreenshot() const override { return true; }
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool OverrideRenderCamera(Assets::Camera& camera) const override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>&, std::vector<Assets::FMaterial>&,
                            std::vector<Assets::LightObject>&, std::vector<Assets::AnimationTrack>&) override;
    void OnSceneLoaded() override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

private:
    enum class EMission { Available, Accepted, Driving, AtPickup, Loaded, AtDropoff, Complete };
    enum class ESurface { Trail, Bridge, Grass, Sand, Mud, Water };

    void CreateVehicle();
    void ResetVehicle();
    void ResolveSceneAnchors();
    void MapWheelVisuals(const std::vector<std::shared_ptr<Assets::Node>>& wheels);
    void UpdateDriveInput(float deltaSeconds);
    void UpdateFuel(float deltaSeconds);
    void UpdateMission(float deltaSeconds);
    void UpdateRecovery(float deltaSeconds);
    void ApplyAgentValidationStage();
    void UpdateVisuals();
    void UpdateSurface();
    glm::vec3 MissionTarget() const;
    const char* MissionName() const;
    const char* SurfaceName() const;

    NextVehicleID vehicle_ = invalidNextVehicleId;
    std::shared_ptr<Assets::Node> bodyNode_;
    std::array<std::shared_ptr<Assets::Node>, 6> wheelNodes_{};
    std::shared_ptr<Assets::Node> cargoNode_;
    std::unordered_map<NextBodyID, ESurface> surfaces_;
    FNextVehicleTelemetry telemetry_;
    TruckerDemo::FTruckHud hud_;

    glm::vec3 position_{-30.0f, 1.5f, 0.0f};
    glm::quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 pickupPosition_{27.0f, 0.04f, -4.0f};
    glm::vec3 dropoffPosition_{-38.0f, 0.04f, -1.0f};
    glm::vec3 fuelPosition_{16.0f, 0.04f, -4.0f};
    glm::vec3 recoveryPosition_{-30.0f, 1.5f, 0.0f};
    glm::quat recoveryRotation_{1.0f, 0.0f, 0.0f, 0.0f};

    float speed_ = 0.0f;
    float elapsed_ = 0.0f;
    float throttle_ = 0.0f;
    float brake_ = 0.0f;
    float steer_ = 0.0f;
    float fuel_ = 90.0f;
    float flippedSeconds_ = 0.0f;
    float stuckSeconds_ = 0.0f;
    bool forward_ = false;
    bool reverse_ = false;
    bool left_ = false;
    bool right_ = false;
    bool handbrake_ = false;
    bool diffLock_ = false;
    bool allWheelDrive_ = false;
    bool interactRequested_ = false;
    bool showMissionPanel_ = true;
    bool recoverySuggested_ = false;
    EMission mission_ = EMission::Available;
    ESurface surface_ = ESurface::Grass;

    float suspensionFrontFrequency_ = 1.3f;
    float suspensionRearFrequency_ = 1.6f;
    float suspensionFrontDamping_ = 0.35f;
    float suspensionRearDamping_ = 0.40f;
    float suspensionPreload_ = 0.04f;
    float centerOfMassDrop_ = 0.35f;
    float frontAntiRoll_ = 10000.0f;
    float rearAntiRoll_ = 14000.0f;
    float engineTorque_ = 1150.0f;
    float fuelCapacity_ = 120.0f;
    float fuelIdleRate_ = 0.03f;
    float fuelThrottleRate_ = 0.6f;
    float fuelSlipPenalty_ = 0.3f;
    float fuelCargoRate_ = 0.08f;
    int32_t agentMissionStage_ = 0;
    int32_t appliedAgentMissionStage_ = 0;

    glm::dvec2 lastMousePosition_{0.0};
    bool hasMousePosition_ = false;
    float cameraYawOffset_ = 0.0f;
    float cameraPitch_ = glm::radians(18.0f);
    float cameraArmLength_ = 9.0f;
};
