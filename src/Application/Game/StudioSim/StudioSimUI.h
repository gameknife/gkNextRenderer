#pragma once

#include "DecisionScheduler.h"
#include "EmployeeSystem.h"
#include "EventSystem.h"
#include "GatheringSystem.h"
#include "GoalSystem.h"
#include "OfficeMap.h"
#include "ProductionSystem.h"

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace StudioSim
{
    struct FFloatingTextParticle
    {
        glm::vec3 worldPos{0.0f};
        glm::vec4 color{1.0f};
        std::string text;
        float ageSeconds = 0.0f;
        float durationSeconds = 1.6f;
    };

    class StudioSimUI
    {
    public:
        struct FHudContext
        {
            FWorldState& world;
            const FGameProject& gameProject;
            const FCompanyState& company;
            const GoalSystem& goalSystem;
            const ProductionSystem& productionSystem;
            const EmployeeSystem& employeeSystem;
            const DecisionScheduler& scheduler;
            const EventSystem& eventSystem;
            const GatheringSystem& gatheringSystem;
            const OfficeMap& officeMap;
            bool sceneReady = false;
            size_t sceneNodeCount = 0;
            bool meetingActive = false;
            const std::string& meetingTopic;
            bool awaitingPlayerDecision = false;
            bool playerDecisionFlowActive = false;
            std::function<void(const std::string&)> raiseEvent;
        };

        struct FModalContext
        {
            const FWorldState& world;
            const FGameProject& gameProject;
            const FCompanyState& company;
            const GoalSystem& goalSystem;
            const GatheringSystem& gatheringSystem;
            bool hasActiveGameProject = false;
            std::function<FGameProject(EGameGenre, EGameTheme, EProjectSizeTier)> buildProjectPreview;
            std::function<void(EGameGenre, EGameTheme, EProjectSizeTier)> startProject;
            std::function<void(int)> chooseGoal;
            std::function<void(const std::string&)> chooseCustomGoal;
            std::function<void(int)> acceptGathering;
            std::function<void(int)> rejectGathering;
            std::function<void()> startNextDay;
        };

        void DrawHud(const FHudContext& context);
        void DrawModals(const FModalContext& context);
        void DrawOverlay(const glm::mat4& viewProjection, const OfficeMap& officeMap,
                         const EmployeeSystem& employeeSystem,
                         const FWorldState& worldState) const;
        void CollectProductionVisualEvents(ProductionSystem& productionSystem);
        void Tick(double deltaSeconds);
        void Reset();
        void ResetProjectPitchSelection();
        void ResetGoalInput();

        bool ShowOverlay() const { return showOverlay_; }
        bool& ShowOverlayMutable() { return showOverlay_; }
        bool ShowPoiDebug() const { return showPoiDebug_; }
        bool& ShowPoiDebugMutable() { return showPoiDebug_; }
        const std::vector<FFloatingTextParticle>& FloatingText() const { return floatingText_; }
        EGameGenre SelectedGenre() const;
        EGameTheme SelectedTheme() const;
        EProjectSizeTier SelectedSize() const;

    private:
        void DrawStatusHud(const FHudContext& context, const glm::vec2& position, const glm::vec2& size);
        void DrawProgressHud(const FHudContext& context, const glm::vec2& position, const glm::vec2& size);
        void DrawEmployeeHud(const FHudContext& context, const glm::vec2& position, const glm::vec2& size);
        void DrawEventHud(const FHudContext& context, const glm::vec2& position, const glm::vec2& size);
        void DrawProjectPitchModal(const FModalContext& context);
        void DrawGoalChoiceModal(const FModalContext& context);
        void DrawGatheringDecisionModal(const FModalContext& context);
        void DrawReviewModal(const FModalContext& context);

        bool showOverlay_ = true;
        bool showPoiDebug_ = false;
        std::array<char, 128> customGoalBuffer_{};
        int pitchGenreIndex_ = 0;
        int pitchThemeIndex_ = 0;
        int pitchSizeIndex_ = 1;
        std::vector<FFloatingTextParticle> floatingText_;
    };
}
