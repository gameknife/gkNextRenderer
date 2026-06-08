#pragma once

#include "StudioSimTypes.h"

#include <string>
#include <vector>

namespace StudioSim
{
    struct FEmployee;
    class OfficeMap;

    struct FProductionVisualEvent
    {
        glm::vec3 worldPos{0.0f};
        std::string text;
        std::string meter;
    };

    // 确定性的项目产出状态机：员工在工位 WORK 时按职位累积四仪表，
    // 推进 Production -> Polish -> Done，并在 Polish 阶段跑 bug 循环。
    class ProductionSystem
    {
    public:
        void StartProject(const FDailyGoal& goal, double gameMinutes, std::vector<FEmployee>& employees);
        void StartProject(const FGameProject& project, double gameMinutes, std::vector<FEmployee>& employees);
        void Tick(double gameMinutes, bool paused, std::vector<FEmployee>& employees, const OfficeMap& office);
        void Reset();
        void ForceShip(double gameMinutes, const std::string& reason);
        void SetFocusBoost(const std::string& meter, float boost);
        void ClearFocusBoost();

        bool Active() const { return active_; }
        const FProjectState& State() const { return state_; }
        std::vector<FProductionVisualEvent> ConsumeVisualEvents();

    private:
        void RecalculateProgress();
        void AdvanceStage(double gameMinutes);

        FProjectState state_;
        bool active_ = false;
        bool polishBugBatchGenerated_ = false;
        std::string activeGoalTitle_;
        int lastLoggedProgressBucket_ = -1;
        std::vector<FProductionVisualEvent> visualEvents_;
        std::string focusMeter_;
        float focusBoost_ = 1.0f;
    };
}
