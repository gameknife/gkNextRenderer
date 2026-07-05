#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "DecisionScheduler.h"
#include "DayClock.h"
#include "EmployeeSystem.h"
#include "EventSystem.h"
#include "GatheringSystem.h"
#include "GoalSystem.h"
#include "OfficeMap.h"
#include "PerceptionSystem.h"
#include "ProductionSystem.h"
#include "StudioSimUI.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// StudioSim —— LLM 驱动的游戏工作室办公室模拟（见 docs/StudioSim-MVP-Plan.md）。
// M0：脚手架 + 空场景。 M1：OfficeMap 语义锚点。 M2：员工实体 + NavGrid 移动。
class StudioSimGameInstance : public NextGameInstanceBase
{
public:
    StudioSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~StudioSimGameInstance() override = default;

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
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    void ResetProjectPitchSelection();
    void StartProjectPitch(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                           StudioSim::EProjectSizeTier sizeTier);
    void SyncGameProjectProduction();
    void FinalizeProjectSettlement();
    void StartNextDay();
    void RaiseEventAndMaybeStartMeeting(const std::string& eventId);
    bool HasActiveGameProject() const;
    bool IsAwaitingPlayerDecision() const;
    bool IsPlayerDecisionFlowActive() const;
    glm::vec3 DesiredCameraTarget() const;
    glm::vec3 DesiredCameraEye() const;
    glm::mat4 ViewMatrix() const;
    void UpdateCamera(double deltaSeconds);
    int PickEmployeeAtScreen(const glm::vec2& screenPosition) const;

    StudioSim::OfficeMap officeMap_;
    StudioSim::DayClock dayClock_;
    StudioSim::EmployeeSystem employeeSystem_;
    StudioSim::DecisionScheduler scheduler_;
    StudioSim::GoalSystem goalSystem_;
    StudioSim::EventSystem eventSystem_;
    StudioSim::PerceptionSystem perceptionSystem_;
    StudioSim::GatheringSystem gatheringSystem_;
    StudioSim::ProductionSystem productionSystem_;
    StudioSim::StudioSimUI ui_;
    StudioSim::FGameProject gameProject_;
    StudioSim::FCompanyState companyState_;
    StudioSim::FWorldState worldState_;
    bool sceneReady_ = false;
    size_t sceneNodeCount_ = 0;
    bool goalMeetingStarted_ = false;
    int followEmployeeIndex_ = -1;
    float cameraZoom_ = 1.0f;
    glm::vec2 cameraPan_{0.0f};
    glm::vec3 cameraEye_{0.0f};
    glm::vec3 cameraTarget_{0.0f};
    bool cameraInitialized_ = false;
};
