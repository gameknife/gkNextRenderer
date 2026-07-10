#pragma once

#include "Engine/Runtime/GameInstance.hpp"

#include "CitizenSystem.h"
#include "CitySolSimUI.h"
#include "CityTimeSystem.h"
#include "TrafficSystem.h"

#include <glm/glm.hpp>

class CitySolSimGameInstance : public NextGameInstanceBase
{
public:
    CitySolSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~CitySolSimGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

private:
    glm::mat4 ViewMatrix() const;
    glm::vec3 DesiredCameraTarget() const;
    glm::vec3 DesiredCameraEye() const;
    void UpdateCamera(double deltaSeconds);
    void UpdateNightLights(Assets::Scene& scene);
    void BuildStreetLights(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                           std::vector<Assets::Model>& models,
                           std::vector<Assets::FMaterial>& materials);
    bool PickAtScreen(const glm::vec2& screenPosition);

    CitySolSim::CityTimeSystem time_;
    CitySolSim::TrafficSystem traffic_;
    CitySolSim::CitizenSystem citizens_;
    CitySolSim::CitySolSimUI ui_;
    std::vector<std::shared_ptr<Assets::Node>> streetLightNodes_;

    bool sceneReady_ = false;
    bool lightsVisible_ = false;
    float zoom_ = 1.0f;
    glm::vec2 panOffset_{0.0f};
    glm::vec3 cameraEye_{0.0f};
    glm::vec3 cameraTarget_{0.0f};
    bool cameraInitialized_ = false;
};
