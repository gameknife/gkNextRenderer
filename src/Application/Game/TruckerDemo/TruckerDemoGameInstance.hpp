#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"

namespace Assets { class Node; }

class TruckerDemoGameInstance final : public NextGameInstanceBase
{
public:
    TruckerDemoGameInstance(Vulkan::WindowConfig&, Runtime::Config::Options&, NextEngine*);
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool ShouldRenderUiDuringScreenshot() const override { return true; }
    bool OnKey(SDL_Event& event) override;
    bool OverrideRenderCamera(Assets::Camera& camera) const override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>&, std::vector<Assets::FMaterial>&,
                            std::vector<Assets::LightObject>&, std::vector<Assets::AnimationTrack>&) override;
    void OnSceneLoaded() override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

private:
    enum class EMission { Driving, AtPickup, Loaded, AtDropoff, Complete };
    enum class ESurface { Trail, Bridge, Grass, Sand, Mud, Water };
    void CreateVehicle();
    void ResetVehicle();
    void UpdateVisuals();
    void UpdateSurface();
    const char* MissionName() const;
    const char* SurfaceName() const;

    NextVehicleID vehicle_ = invalidNextVehicleId;
    std::shared_ptr<Assets::Node> bodyNode_;
    std::array<std::shared_ptr<Assets::Node>, 6> wheelNodes_{};
    std::shared_ptr<Assets::Node> cargoNode_;
    std::unordered_map<NextBodyID, ESurface> surfaces_;
    glm::vec3 position_{-30.0f, 1.5f, 0.0f};
    glm::quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};
    float speed_ = 0.0f;
    float elapsed_ = 0.0f;
    float throttle_ = 0.0f;
    float steer_ = 0.0f;
    bool forward_ = false, reverse_ = false, left_ = false, right_ = false, handbrake_ = false;
    bool interactRequested_ = false;
    EMission mission_ = EMission::Driving;
    ESurface surface_ = ESurface::Grass;
};
