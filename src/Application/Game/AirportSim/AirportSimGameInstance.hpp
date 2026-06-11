#pragma once

#include "Engine/Runtime/GameInstance.hpp"

#include "AgentSystem.h"
#include "AirportMap.h"
#include "AirportSimUI.h"
#include "DecisionScheduler.h"
#include "FlightBoard.h"
#include "JourneySystem.h"
#include "PerceptionSystem.h"
#include "QueueSystem.h"
#include "TimeSystem.h"

#include <glm/glm.hpp>

// AirportSim —— Jumbo Airport Story 风格机场生态观察 Demo（见 docs/AirportSim-MVP-Plan.md）。
// 编排层：加载 airport.scad、tick 各系统、观察相机（俯视总览 + 锁定跟踪）。
class AirportSimGameInstance : public NextGameInstanceBase
{
public:
    AirportSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~AirportSimGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnKey(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

private:
    glm::mat4 ViewMatrix() const;
    glm::vec3 CameraEye() const;
    glm::vec3 CameraTarget() const;

    AirportSim::AirportMap map_;
    AirportSim::TimeSystem time_;
    AirportSim::FlightBoard flightBoard_;
    AirportSim::AgentSystem agents_;
    AirportSim::JourneySystem journey_;
    AirportSim::QueueSystem queues_;
    AirportSim::PerceptionSystem perception_;
    AirportSim::DecisionScheduler scheduler_;
    AirportSim::AirportSimUI ui_;

    bool sceneReady_ = false;
    float zoom_ = 1.0f;        // 0.55（近）~ 1.6（远）
    glm::vec2 panOffset_{0.0f}; // 方向键平移观察点
};
