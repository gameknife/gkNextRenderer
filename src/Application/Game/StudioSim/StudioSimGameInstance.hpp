#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "DecisionScheduler.h"
#include "EmployeeSystem.h"
#include "EventSystem.h"
#include "GatheringSystem.h"
#include "GoalSystem.h"
#include "OfficeMap.h"
#include "ProductionSystem.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ImVec2;

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

private:
    void DrawStatusHud(const ImVec2& pos, const ImVec2& size);
    void DrawProgressHud(const ImVec2& pos, const ImVec2& size);
    void DrawEmployeeHud(const ImVec2& pos, const ImVec2& size);
    void DrawEventHud(const ImVec2& pos, const ImVec2& size);
    void DrawProjectPitchModal();
    void DrawGoalChoiceModal();
    void DrawGatheringDecisionModal();
    void DrawReviewModal();
    void DrawWorldOverlay() const;
    void CollectProductionVisualEvents();
    void TickFloatingText(double deltaSeconds);
    void ResetProjectPitchSelection();
    void StartProjectPitch(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                           StudioSim::EProjectSizeTier sizeTier);
    void SyncGameProjectProduction();
    void FinalizeProjectSettlement();
    void StartNextDay();
    void StartMeeting(const std::string& topic, double durationMinutes);
    void TickMeeting(double deltaSeconds);
    void RaiseEventAndMaybeStartMeeting(const std::string& eventId);
    bool HasActiveGameProject() const;
    bool IsAwaitingPlayerDecision() const;
    bool IsPlayerDecisionFlowActive() const;

    struct FMeetingRuntime
    {
        bool active = false;
        std::string topic;
        double endGameMinutes = 0.0;
        double elapsedRealSeconds = 0.0;
        double nextLineRealSeconds = 0.0;
        size_t nextLineIndex = 0;
        std::vector<StudioSim::FMeetingLine> lines;
    };

    struct FFloatingTextParticle
    {
        glm::vec3 worldPos{0.0f};
        glm::vec4 color{1.0f};
        std::string text;
        float ageSeconds = 0.0f;
        float durationSeconds = 1.6f;
    };

    StudioSim::OfficeMap officeMap_;
    StudioSim::EmployeeSystem employeeSystem_;
    StudioSim::DecisionScheduler scheduler_;
    StudioSim::GoalSystem goalSystem_;
    StudioSim::EventSystem eventSystem_;
    StudioSim::GatheringSystem gatheringSystem_;
    StudioSim::ProductionSystem productionSystem_;
    StudioSim::FGameProject gameProject_;
    StudioSim::FCompanyState companyState_;
    StudioSim::FWorldState worldState_;
    bool sceneReady_ = false;
    bool showOverlay_ = true;
    size_t sceneNodeCount_ = 0;
    char customGoalBuf_[128] = "";
    int pitchGenreIndex_ = 0;
    int pitchThemeIndex_ = 0;
    int pitchSizeIndex_ = 1;
    bool goalMeetingStarted_ = false;
    FMeetingRuntime meeting_;
    std::mutex meetingMutex_;
    std::vector<StudioSim::FMeetingLine> pendingMeetingLines_;
    uint64_t meetingGeneration_ = 0;
    std::vector<FFloatingTextParticle> floatingText_;
};
