#pragma once

#include "StudioSimTypes.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace NextAI
{
    class FAIService;
}

namespace StudioSim
{
    struct FEmployee;

    // 每日目标系统。晨会异步向 LLM 要 3 个候选目标 →
    // 等玩家用 ChooseGoal/ChooseCustom 选择 → 异步分解到各职位 todayTask → Active。
    // GameInstance 在 State()==Active 时把阶段从 Briefing 切到 Working。
    // 所有 LLM 调用走 detached worker（GenerateTextAsync），回调只把文本入队，
    // 主线程 Tick 解析 + apply（与 DecisionScheduler 同一纪律）。目标系列调用发生在
    // Briefing 阶段（scheduler 未跑），不会和员工决策并发抢占单实例 llama-server。
    class GoalSystem
    {
    public:
        enum class EState
        {
            Idle,
            RequestingGoals,
            AwaitingChoice,
            Decomposing,
            Active
        };

        void BeginDay(NextAI::FAIService* ai);
        void Tick(NextAI::FAIService* ai, std::vector<FEmployee>& employees);
        void ChooseGoal(int index, NextAI::FAIService* ai, std::vector<FEmployee>& employees);
        void ChooseCustom(const std::string& title, NextAI::FAIService* ai, std::vector<FEmployee>& employees);
        void SetActiveGoal(const FDailyGoal& goal, std::vector<FEmployee>& employees);
        void Summarize(NextAI::FAIService* ai, const std::vector<FEmployee>& employees,
                       const FGameProject& gameProject);
        void Reset();

        EState State() const { return state_; }
        bool IsActive() const { return state_ == EState::Active; }
        const std::vector<FGoalOption>& Options() const { return options_; }
        const FDailyGoal& Goal() const { return goal_; }
        const std::string& Summary() const { return summary_; }

    private:
        enum class EMsgKind
        {
            Goals,
            Decompose,
            Summary
        };
        struct FMsg
        {
            EMsgKind    kind;
            std::string payload;
        };

        void StartDecompose(NextAI::FAIService* ai);
        void ApplyDecompose(const std::string& payload, std::vector<FEmployee>& employees);

        std::mutex mutex_;
        std::vector<FMsg> inbox_;
        EState state_ = EState::Idle;
        std::vector<FGoalOption> options_;
        FDailyGoal goal_;
        std::string summary_;
        bool summarizeRequested_ = false;
        int localScore_ = -1;
        std::string nextBriefingContext_;
        uint64_t generation_ = 0;
    };
}
